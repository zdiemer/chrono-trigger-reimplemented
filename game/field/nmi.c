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

#include "field/bridge.h"
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

/* A map transition: moving to a destination (1-4, each with its tile map
 * base), or sending a tile map staged in bank $7E to VRAM: the staged half
 * screen to BG2's left screen, the same to its right screen, or a piece of
 * BG3's map (bits 3-5, each the original's routine). */
enum {
    MAP_MOVES = 4,
    MAP_SEND_BG2_LEFT = 0x08,     /* $C0:7F5D */
    MAP_SEND_BG2_RIGHT = 0x10,    /* $C0:7F58 */
    MAP_SEND_BG3_PIECE = 0x20,    /* $C0:EC77 */
};

/* The first two moves also set BG1's tile map in the line effects' values,
 * two screens wide, from the destination's. */

/* The destinations: BG1's and BG2's tile maps for the first two, the one the
 * map loader chose for the third, BG3's for the fourth. */
static void start_map_move(GameState *g, uint8_t move)
{
    uint16_t destination = move == 1   ? field_layer_map_base(g, FIELD_BG1)
                           : move == 2 ? field_layer_map_base(g, FIELD_BG2)
                           : move == 3 ? field_transition_map_base(g)
                                       : field_layer_map_base(g, FIELD_BG3);
    set_field_transfer_vram(g, destination);
}

static void finish_map_move(GameState *g, uint8_t move)
{
    set_field_map_transition(g, 0);
    if (move <= 2) {
        uint32_t maps = field_line_bg_maps(g, 0);
        uint8_t bg1 = (uint8_t)((field_layer_map_base(g, FIELD_BG1) >> 8) | FIELD_MAP_TWO_SCREENS_WIDE);
        set_field_line_bg_maps(g, 0, (maps & 0xFFFFFF00u) | bg1);
    }
}

/* A fade the frame asked for: a request with FADE_ONCE is taken once. */
enum { FADE_ONCE = 0x80 };   /* field_fade_request */

static bool fade_asked(GameState *g)
{
    uint8_t fade = field_fade_request(g);
    if (fade & FADE_ONCE)
        set_field_fade_request(g, 0);
    return fade != 0;
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

/* ---- Where it meets the original ------------------------------------------------------- */

enum { NMI_DONT_CARE = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { JSR_SIZE = 3, JSL_SIZE = 4 };   /* a call's return is the instruction after it */

/* The routines the frame's work calls that are still the original's. */
enum {
    SEND_STAGED_MAP = 0xC07F62,       /* a map move: the staged tile map to field_transfer_vram */
    SEND_BG2_LEFT = 0xC07F5D,         /* the staged half screen to BG2's left screen */
    SEND_BG2_RIGHT = 0xC07F58,        /* the same to its right screen */
    SEND_BG3_PIECE = 0xC0EC77,
    FADE_STEP = 0xC06ECB,
    UPLOAD_PALETTE = 0xC0712E,        /* core/palette.c's */
    MENUS_FRAME = 0xC28002,           /* the menus' and the controller's per-frame routine */
};

/* A call of the frame's work, from its call site, as the original makes it:
 * on the field's page (or the layers', for the deferred uploads; the bank
 * C2 routine on whichever page the handler has), with the data bank $00 and
 * 8-bit A; the registers are the interrupted code's, saved,
 * and none is read (but Y by the bank C2 routine, which is what the frame's
 * work left). It goes on where the call came back as it went, and
 * stops at the call's return otherwise. */
static bool call(Snes *s, uint32_t site, uint32_t routine, uint16_t page)
{
    m_jsr_dc(s, site, routine, NMI_DONT_CARE);
    if (field_machine_back(s, false, page, 0))
        return true;
    m_stop_at_own(s, site + JSR_SIZE, 0);
    return false;
}

static bool call_long(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    uint16_t page = m_direct_page(s);
    m_jsl_dc(s, site, routine, dont_care);
    if (field_machine_back(s, false, page, 0))
        return true;
    m_stop_at_own(s, site + JSL_SIZE, 0);
    return false;
}

/* The frame is done: the per-line effects are set up for it, and it is
 * marked done. The field's own sends run as C, timed as the routines they
 * are. */
enum { PROGRAM_LINE_EFFECTS = 0xFDC1EE, SEND_SPRITE_TABLE = 0xC0ECA3, SEND_QUEUED_TRANSFERS = 0xC0B1B2 };

static void finish_frame(Snes *s)
{
    GameState *g = state_of(s);
    NATIVE_TIME_AS(s, PROGRAM_LINE_EFFECTS, field_set_up_line_effects(g, m_platform(s)));
    set_field_frame_pending(g, 0);
}

/* The screen transition's jobs, one for each bit of field_screen_transition,
 * all the original's, and where the handler calls each. */
static bool run_screen_transition(Snes *s, int step)
{
    static const uint32_t transition_sites[SCREEN_TRANSITION_STEPS] = { 0xC0EA9E, 0xC0EAAC, 0xC0EABA, 0xC0EAC8 };
    static const uint32_t routines[SCREEN_TRANSITION_STEPS] = { 0xC06EB0, 0xC06EF1, 0xC06E5C, 0xC06F5E };
    GameState *g = state_of(s);
    set_field_screen_transition(g, (uint8_t)(field_screen_transition(g) & ~(1u << step)));
    return call(s, transition_sites[step], routines[step], FIELD_PAGE);
}

/* A move to a map (the frame then ends with it), or a step of the transition.
 * Returns false where the port stopped; `finished` says the frame's work
 * ended with it. */
static bool run_map_transition(Snes *s, uint8_t asked, bool *finished)
{
    static const uint32_t move_sites[MAP_MOVES] = { 0xC0EAE1, 0xC0EB01, 0xC0EB21, 0xC0EB38 };
    GameState *g = state_of(s);
    *finished = true;
    if (asked >= 1 && asked <= MAP_MOVES) {
        start_map_move(g, asked);
        if (!call(s, move_sites[asked - 1], SEND_STAGED_MAP, FIELD_PAGE))
            return false;
        finish_map_move(g, asked);
        finish_frame(s);
        return true;
    }
    if (asked & MAP_SEND_BG2_LEFT) {
        if (!call(s, 0xC0EB4A, SEND_BG2_LEFT, FIELD_PAGE))
            return false;
        set_field_map_transition(g, (uint8_t)(field_map_transition(g) & ~MAP_SEND_BG2_LEFT));
        finish_frame(s);
        return true;
    }
    if (asked & MAP_SEND_BG2_RIGHT) {
        if (!call(s, 0xC0EB5E, SEND_BG2_RIGHT, FIELD_PAGE))
            return false;
        set_field_map_transition(g, (uint8_t)(field_map_transition(g) & ~MAP_SEND_BG2_RIGHT));
        finish_frame(s);
        return true;
    }
    *finished = false;
    if (asked & MAP_SEND_BG3_PIECE) {
        if (!call(s, 0xC0EB72, SEND_BG3_PIECE, FIELD_PAGE))
            return false;
        set_field_map_transition(g, (uint8_t)(field_map_transition(g) & ~MAP_SEND_BG3_PIECE));
    }
    return true;
}

/* The layers' deferred edge uploads their flags ask for, a bit for each
 * layer in each of four groups, run on the layers' page; then no more are
 * asked for. They are the field's own (field/display.c), run as C and timed
 * as the routines they are; where an edge's parts are larger than their
 * entries' guard takes, the entries are called, and the original runs them. */
static bool run_edge_uploads(Snes *s)
{
    static const uint32_t upload_sites[FIELD_EDGE_UPLOADS] = { 0xC0EB9D, 0xC0EBA6, 0xC0EBAF, 0xC0EBB8,
                                                               0xC0EBC1, 0xC0EBCA, 0xC0EBD3, 0xC0EBDC,
                                                               0xC0EBE5, 0xC0EBEE, 0xC0EBF7, 0xC0EC00 };
    static const uint32_t uploads[FIELD_EDGE_UPLOADS] = { 0xC08445, 0xC084C1, 0xC08523, 0xC08585,
                                                          0xC085E7, 0xC08649, 0xC086AB, 0xC086F7,
                                                          0xC08729, 0xC0875B, 0xC0878D, 0xC087BF };
    GameState *g = state_of(s);
    for (unsigned k = 0; k < FIELD_EDGE_UPLOADS; k++) {
        if (!(field_layer_upload_flags(g, k / 3) & (1u << (k % 3))))
            continue;
        if (field_layer_edges_fit(g)) {
            NATIVE_TIME_AS(s, uploads[k], field_upload_layer_edge(g, m_platform(s), k));
            continue;
        }
        s->cpu.d = FIELD_LAYERS_PAGE;
        if (!call(s, upload_sites[k], uploads[k], FIELD_LAYERS_PAGE))
            return false;
        s->cpu.d = FIELD_PAGE;
    }
    for (unsigned group = 0; group < 4; group++)
        set_field_layer_upload_flags(g, group, 0);
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

/* The frame's vblank work: a screen transition step, or a map transition's
 * (a move to a new map ends the frame's work with it) and a fade; then the
 * palette, the sprites and the uploads. False where the port stopped. */
static bool frame_work(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.d = FIELD_PAGE;
    if (field_screen_transition(g) != 0) {
        int step = screen_transition_step(g);
        if (step >= 0 && !run_screen_transition(s, step))
            return false;
    } else {
        uint8_t asked = field_map_transition(g);
        if (asked != 0) {
            bool finished = false;
            if (!run_map_transition(s, asked, &finished))
                return false;
            if (finished)
                return true;
        }
        if (fade_asked(g) && !call(s, 0xC0EB81, FADE_STEP, FIELD_PAGE))
            return false;
    }
    if (!call(s, 0xC0EB86, UPLOAD_PALETTE, FIELD_PAGE))
        return false;
    NATIVE_TIME_AS(s, SEND_SPRITE_TABLE, field_upload_sprite_table(m_platform(s)));
    hand_on_queue_y(s);
    NATIVE_TIME_AS(s, SEND_QUEUED_TRANSFERS, field_upload_queued_transfers(g, m_platform(s)));
    if (!run_edge_uploads(s))
        return false;
    finish_frame(s);
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
