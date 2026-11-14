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
 * Most of that work is other routines, which the handler calls as the
 * original does; its decisions are plain C over the game's state. */
#include "field/field.h"
#include "field/streaming.h"
#include "core/input.h"
#include "core/palette.h"
#include "core/program.h"
#include "charge.h"

#include "field/bridge.h"
#include "menu/menu.h"
#include "field/dialog_window.h"
#include "field/display.h"
#include "field/line_effects.h"
#include "field/margin_objects.h"
#include "platform/timing.h"
#include "platform/video.h"
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
    const uint8_t shadows[] = {
        field_window_shadows_bg1_bg2(g), field_window_shadows_bg3_bg4(g), field_window_shadows_obj_colour(g),
        field_window_shadows_window2_left(g), field_window_shadows_window2_right(g),
    };
    VidWindows w = { 0 };
    for (unsigned i = 0; i < sizeof windows / sizeof windows[0]; i++) {
        vid_windows_decode(&w, windows[i].field, shadows[i]);
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
 * marked done. The field's own sends are timed as the routines they are. */
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
    field_margins_due(g);
    field_forget_the_objects_past_the_window(g);
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
    TIMED_AS(g, SEND_SPRITE_TABLE, field_upload_sprite_table(g, pf));
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

/* Whether any edge came in. */
static bool upload_the_edges(GameState *g, Platform *pf)
{
    bool any = false;
    for (unsigned k = 0; k < FIELD_EDGE_UPLOADS; k++)
        if (edge_upload_asked(g, k)) {
            TIMED_AS(g, edge_uploads[k], field_upload_layer_edge(g, pf, k));
            any = true;
        }
    no_more_edges_asked(g);
    return any;
}

/* ---- The handler ------------------------------------------------------------------------------ */

enum { MENUS_FRAME = 0xC28002 };   /* the menus' and the controller's per-frame input work */

// $C0:EA63
void field_vblank(GameState *g, Platform *pf)
{
    start_vblank(pf);
    if (field_frame_pending(g) != 0 && !work_before_the_queue(g, pf)) {
        upload_the_queue(g, pf);
        if (!PORT_CHECK(edges(g))) {
            program_breaks_a_contract(g, FIELD_VBLANK_HANDLER, "layers' edges the port doesn't upload");
            return;
        }
        bool moved = upload_the_edges(g, pf);
        field_fill_the_views_margins(g, pf, moved);
        field_list_the_objects_past_the_window(g, pf);
        finish_frame(g, pf);
    }
    field_show_the_layers_across(g, pf);
    field_show_the_objects_past_the_window(g, pf);
    bool read = false;
    TIMED_AS(g, MENUS_FRAME, read = input_take_frame(g, pf));
    if (!read) {
        program_fail(g, MENUS_FRAME, "the controller not read, or the reset buttons held");
        return;
    }
    apply_display_shadows(g, pf);
}
