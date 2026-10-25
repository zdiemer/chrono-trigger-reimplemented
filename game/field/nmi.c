/* Field mode: the vertical blank interrupt.
 *
 * Every vblank the field's handler sets the display up for the frame: the per
 * frame routine of bank C2 (the menus' and the controller's), the window and
 * color math settings from their shadows, the per-line effects' enable and
 * the brightness. When the main loop has finished a frame (field_frame_pending)
 * it also does that frame's vblank work: one screen transition step asked for
 * (field_screen_transition), or one step of a map transition (moving the
 * screen to a new map, field_map_transition); otherwise a palette fade, the
 * palette, the sprite table, the queued transfers of sprite tiles and the
 * layers' deferred edge uploads; and then the per-line effects for the
 * frame, and the frame is marked done.
 *
 * Most of that work is other routines, some still the original's code, so the
 * handler is a bridge: its decisions are plain C over the game's state, and it
 * calls the routines as the original does, with the registers it has saved on
 * the interrupted code's stack. */
#include "field/field.h"
#include "core/input.h"
#include "core/palette.h"
#include "core/program.h"
#include "charge.h"

#include "field/bridge.h"
#include "field/dialog_window.h"
#include "field/display.h"
#include "field/line_effects.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/native.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The frame's work, as decisions ---------------------------------------------------- */

/* The screen transition steps, one a vblank, the lowest asked for first. */
enum { SCREEN_TRANSITION_STEPS = 4 };

static int screen_transition_step(const GameState *g)
{
    uint8_t asked = field_screen_transition(g);
    for (int step = 0; step < SCREEN_TRANSITION_STEPS; step++)
        if (asked & (1u << step))
            return step;
    return -1;
}

/* A map transition (field/field.h, FieldMapMove): a move to a destination,
 * each with its tile map base, or a send of a tile map staged in bank $7E:
 * BG2's left screen, its right screen, a piece of BG3's map
 * (field/display.c). */

/* The first two moves also set BG1's tile map in the line effects' values,
 * two screens wide, from the destination's. */

/* The destinations: BG1's and BG2's tile maps for the first two, the one the
 * map loader chose for the third, BG3's for the fourth. */
static void start_map_move(GameState *g, FieldMapMove move)
{
    uint16_t destination = move == FIELD_MOVE_TO_BG1                 ? field_layer_map_base(g, FIELD_BG1)
                           : move == FIELD_MOVE_TO_BG2               ? field_layer_map_base(g, FIELD_BG2)
                           : move == FIELD_MOVE_TO_THE_LOADERS_PLACE ? field_transition_map_base(g)
                                                                     : field_layer_map_base(g, FIELD_BG3);
    set_field_transfer_vram(g, destination);
}

static void finish_map_move(GameState *g, FieldMapMove move)
{
    set_field_map_transition(g, 0);
    if (move <= FIELD_MOVE_TO_BG2) {
        uint32_t maps = field_line_bg_maps(g, 0);
        uint8_t bg1 = (uint8_t)((field_layer_map_base(g, FIELD_BG1) >> 8) | FIELD_MAP_TWO_SCREENS_WIDE);
        set_field_line_bg_maps(g, 0, (maps & 0xFFFFFF00u) | bg1);
    }
}

/* Whether the dialog's text row is to be sent this frame (field/dialog_window.c):
 * a request to send it once is taken. */
enum { SEND_ONCE = 0x80 };   /* dialog_text_sending */

static bool dialog_text_asked(GameState *g)
{
    uint8_t asked = dialog_text_sending(g);
    if (asked & SEND_ONCE)
        set_dialog_text_sending(g, 0);
    return asked != 0;
}

/* ---- The display's settings, every vblank ---------------------------------------------- */

static void start_vblank(Platform *pf)
{
    tm_ack_vblank(pf, PF_AT(0xC0EA70));
    vid_enable_line_effects(pf, PF_AT(0xC0EA75), 0x00);
    tm_enable_interrupts(pf, PF_AT(0xC0EA7A), TM_INT_VBLANK | TM_INT_LINE | TM_INT_AUTO_JOYPAD);
    vid_set_mode(pf, PF_AT(0xC0EA7F), &(VidMode){ .mode = 1, .bg3_priority = true });
}

/* The windows, the color math, the fixed color, the line effects and the
 * brightness from the field's shadows (0 brightness blanks the screen). */
static void apply_display_shadows(const GameState *g, Platform *pf)
{
    static const struct { uint32_t field; uint32_t site; } windows[] = {
        { VID_WINDOWS_BG1_BG2, 0xC0EC1C }, { VID_WINDOWS_BG3_BG4, 0xC0EC22 }, { VID_WINDOWS_OBJ_BACK, 0xC0EC28 },
        { VID_WINDOWS_W2_LEFT, 0xC0EC2E }, { VID_WINDOWS_W2_RIGHT, 0xC0EC34 },
    };
    VidWindows w = { 0 };
    for (unsigned i = 0; i < sizeof windows / sizeof windows[0]; i++) {
        vid_windows_decode(&w, windows[i].field, field_window_shadows_byte(g, i));
        vid_set_windows(pf, PF_AT(windows[i].site), &w, windows[i].field);
    }
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_CONTROL, field_color_math_control(g));
    vid_set_color_math(pf, PF_AT(0xC0EC3A), &math, VID_MATH_CONTROL);
    vid_set_fixed_color(pf, PF_AT(0xC0EC3F), VID_RED | VID_GREEN | VID_BLUE, 0);
    uint8_t color = field_fixed_color(g);
    vid_set_fixed_color(pf, PF_AT(0xC0EC45), color >> 5, color & 0x1F);
    vid_enable_line_effects(pf, PF_AT(0xC0EC4B), field_line_effect_enable(g));
    uint8_t brightness = field_brightness(g);
    VidDisplay display = brightness != 0 ? vid_display_decode(brightness) : (VidDisplay){ .blank = true };
    vid_set_display(pf, PF_AT(0xC0EC55), &display);
}

/* ---- The frame's work ------------------------------------------------------------------------ */

/* The frame is done: the per-line effects are set up for it, and it is
 * marked done. The field's own sends run as C, timed as the routines they
 * are. */
enum { PROGRAM_LINE_EFFECTS = 0xFDC1EE, SEND_SPRITE_TABLE = 0xC0ECA3, SEND_QUEUED_TRANSFERS = 0xC0B1B2 };

static void finish_frame(GameState *g, Platform *pf)
{
    TIMED_AS(g, PROGRAM_LINE_EFFECTS, field_set_up_line_effects(g, pf));
    set_field_frame_pending(g, 0);
}

/* The screen transition's jobs, one for each bit of field_screen_transition:
 * the dialog window's looks sent, a vblank each, after a battle
 * (field/dialog_window.c). */
static void run_screen_transition(GameState *g, Platform *pf, int step)
{
    set_field_screen_transition(g, (uint8_t)(field_screen_transition(g) & ~(1u << step)));
    switch (step) {
    case 0: TIMED_AS(g, FIELD_SEND_WINDOW_FRAME_ENTRY, field_send_the_window_frame(g, pf)); break;
    case 1: TIMED_AS(g, FIELD_SEND_TEXT_AREA_ENTRY, field_send_the_text_area(g, pf)); break;
    case 2: TIMED_AS(g, FIELD_SEND_WINDOW_DESIGN_ENTRY, field_send_the_window_design(g, pf)); break;
    default: TIMED_AS(g, FIELD_SEND_CHOICE_POINTER_ENTRY, field_send_the_choice_pointer(g, pf)); break;
    }
}

/* A move to a map, or a step of the transition; true where the frame's work
 * ended with it (and is finished). */
static bool run_map_transition(GameState *g, Platform *pf, uint8_t asked)
{
    if (asked >= 1 && asked <= FIELD_MAP_MOVES) {
        start_map_move(g, (FieldMapMove)asked);
        TIMED_AS(g, FIELD_SEND_STAGED_SCREEN_ENTRY, field_send_staged_layer(g, pf, false));
        finish_map_move(g, (FieldMapMove)asked);
        finish_frame(g, pf);
        return true;
    }
    /* The transition's sends, in the order the handler looks at them: each
     * where its bit is asked for, and a half screen of BG2 ends the frame's
     * work with it. */
    static const struct {
        uint8_t asked;
        uint32_t routine;
        bool ends_the_frame;
    } sends[] = {
        { FIELD_SEND_BG2_LEFT, FIELD_SEND_STAGED_BG2_LEFT_ENTRY, true },
        { FIELD_SEND_BG2_RIGHT, FIELD_SEND_STAGED_BG2_RIGHT_ENTRY, true },
        { FIELD_SEND_BG3_PIECE, FIELD_SEND_BG3_PIECE_ENTRY, false },
    };
    for (unsigned k = 0; k < sizeof sends / sizeof sends[0]; k++) {
        if (!(asked & sends[k].asked))
            continue;
        if (sends[k].asked == FIELD_SEND_BG3_PIECE)
            TIMED_AS(g, sends[k].routine, field_send_bg3_piece(g, pf));
        else
            TIMED_AS(g, sends[k].routine, field_send_staged_bg2_screen(g, pf, sends[k].asked == FIELD_SEND_BG2_RIGHT));
        set_field_map_transition(g, (uint8_t)(field_map_transition(g) & ~sends[k].asked));
        if (sends[k].ends_the_frame) {
            finish_frame(g, pf);
            return true;
        }
    }
    return false;
}

/* The frame's vblank work up to the queued sprite tiles: a screen
 * transition step, or a map transition's (a move to a new map ends the
 * frame's work with it) and the dialog's text; then the palette and the
 * sprite table. True where the frame's work ended early, and is finished. */
static bool work_before_the_queue(GameState *g, Platform *pf)
{
    if (field_screen_transition(g) != 0) {
        int step = screen_transition_step(g);
        if (step >= 0)
            run_screen_transition(g, pf, step);
    } else {
        uint8_t asked = field_map_transition(g);
        if (asked != 0 && run_map_transition(g, pf, asked))
            return true;
        if (dialog_text_asked(g))
            TIMED_AS(g, FIELD_SEND_DIALOG_TEXT_ENTRY, field_send_dialog_text(g, pf));
    }
    TIMED_AS(g, PALETTE_UPLOAD_ENTRY, palette_send_working(pf));
    TIMED_AS(g, SEND_SPRITE_TABLE, field_upload_sprite_table(pf));
    return false;
}

static void upload_the_queue(GameState *g, Platform *pf)
{
    TIMED_AS(g, SEND_QUEUED_TRANSFERS, field_upload_queued_transfers(g, pf));
}

/* The layers' deferred edge uploads their flags ask for, a bit for each
 * layer in each of four groups, run on the layers' page, then no more are
 * asked for: the field's own (field/display.c), timed as the routines they
 * are, where an edge's parts are no larger than they take. */
static const uint32_t edge_uploads[FIELD_EDGE_UPLOADS] = { 0xC08445, 0xC084C1, 0xC08523, 0xC08585,
                                                           0xC085E7, 0xC08649, 0xC086AB, 0xC086F7,
                                                           0xC08729, 0xC0875B, 0xC0878D, 0xC087BF };

static bool edge_upload_asked(const GameState *g, unsigned k)
{
    return field_layer_upload_flags(g, k / 3) & (1u << (k % 3));
}

static bool edges(const GameState *g)
{
    for (unsigned k = 0; k < FIELD_EDGE_UPLOADS; k++)
        if (edge_upload_asked(g, k) && !field_layer_edges_fit(g))
            return false;
    return true;
}

static void no_more_edges_asked(GameState *g)
{
    for (unsigned group = 0; group < 4; group++)
        set_field_layer_upload_flags(g, group, 0);
}

static void upload_the_edges(GameState *g, Platform *pf)
{
    for (unsigned k = 0; k < FIELD_EDGE_UPLOADS; k++)
        if (edge_upload_asked(g, k))
            TIMED_AS(g, edge_uploads[k], field_upload_layer_edge(g, pf, k));
    no_more_edges_asked(g);
}

/* ---- The handler, as C ----------------------------------------------------------------------- */

enum { MENUS_FRAME = 0xC28002 };   /* the menus' and the controller's per-frame input work */

// $C0:EA63
void field_vblank(GameState *g, Platform *pf)
{
    start_vblank(pf);
    if (field_frame_pending(g) != 0 && !work_before_the_queue(g, pf)) {
        upload_the_queue(g, pf);
        if (!edges(g)) {
            program_needs_the_machine(g, FIELD_VBLANK_HANDLER, "layers' edges the port doesn't upload");
            return;
        }
        upload_the_edges(g, pf);
        finish_frame(g, pf);
    }
    bool read = false;
    TIMED_AS(g, MENUS_FRAME, read = input_take_frame(g, pf));
    if (!read) {
        program_needs_the_machine(g, MENUS_FRAME, "the controller not read, or the reset buttons held");
        return;
    }
    apply_display_shadows(g, pf);
}

/* ---- Where it meets the original ------------------------------------------------------- */

enum { NMI_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* A call of the frame's work, from its call site, as the original makes it:
 * on the field's page (or the layers', for the deferred uploads; the bank
 * C2 routine on whichever page the handler has), with the data bank $00 and
 * 8-bit A; the registers are the interrupted code's, saved,
 * and none is read (but Y by the bank C2 routine, which is what the frame's
 * work left). It goes on where the call came back as it went, and
 * stops at the call's return otherwise. */
static bool call(Snes *s, uint32_t site, uint32_t routine, uint16_t page)
{
    return bridge_call(s, site, routine, NMI_DONT_CARE, (BridgeMachine){ false, page, 0 });
}

static bool call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    return bridge_call_long(s, site, routine, dont_care, (BridgeMachine){ false, m_direct_page(s), 0 });
}

/* Edges larger than the port's take are the original's, each called from its
 * site on the layers' page. */
static bool run_edge_uploads(Snes *s)
{
    static const uint32_t upload_sites[FIELD_EDGE_UPLOADS] = { 0xC0EB9D, 0xC0EBA6, 0xC0EBAF, 0xC0EBB8,
                                                               0xC0EBC1, 0xC0EBCA, 0xC0EBD3, 0xC0EBDC,
                                                               0xC0EBE5, 0xC0EBEE, 0xC0EBF7, 0xC0EC00 };
    GameState *g = state_of(s);
    for (unsigned k = 0; k < FIELD_EDGE_UPLOADS; k++) {
        if (!edge_upload_asked(g, k))
            continue;
        if (field_layer_edges_fit(g)) {
            TIMED_AS(g, edge_uploads[k], field_upload_layer_edge(g, m_platform(s), k));
            continue;
        }
        s->cpu.d = FIELD_LAYERS_PAGE;
        if (!call(s, upload_sites[k], edge_uploads[k], FIELD_LAYERS_PAGE))
            return false;
        s->cpu.d = FIELD_PAGE;
    }
    no_more_edges_asked(g);
    return true;
}

/* The menus' per-frame routine reads Y, which the original's upload of the
 * sprite tile queue leaves its last entry's second size (and which the edge
 * uploads the original runs may move on): given to it here, where the
 * original sets it. */
static void hand_on_queue_y(Snes *s)
{
    s->cpu.y = field_queue_last_second_size(state_of(s), s->cpu.y);
}

/* The frame's vblank work on the machine. False where the port stopped. */
static bool frame_work(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.d = FIELD_PAGE;
    if (work_before_the_queue(g, m_platform(s)))
        return true;
    hand_on_queue_y(s);
    upload_the_queue(g, m_platform(s));
    if (!run_edge_uploads(s))
        return false;
    finish_frame(g, m_platform(s));
    return true;
}

// $C0:EA63 (installed at the $0500 NMI stub by field_install_nmi)
void field_nmi(Snes *s)
{
    m_save_interrupted_registers(s);
    s->cpu.db = 0x00;
    Platform *pf = m_platform(s);
    GameState *g = state_of(s);
    start_vblank(pf);
    if (field_frame_pending(g) != 0 && !frame_work(s))
        return;
    if (!call_long(s, 0xC0EC15, MENUS_FRAME, NMI_DONT_CARE & ~DC_Y))
        return;
    apply_display_shadows(g, pf);
    m_return_to_interrupted(s);
}
