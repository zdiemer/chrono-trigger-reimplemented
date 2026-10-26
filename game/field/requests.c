/* Field mode: the main loop's turn at the screen's changes and the requests.
 *
 * Every frame the field's main loop moves a brightness change and a tint
 * change that are under way a step toward their targets, and then answers
 * what the event scripts and the leader's walk have asked of it
 * (field_requests, field/field.h): a location change alone, or the menus
 * alone, or else a step event, a chest, and then a tile copy or a battle,
 * in this order:
 * - a location change: the picture fades out a step a frame, then the party's
 *   place is kept as the one it came from, the next place becomes the
 *   party's, and the field starts again there;
 * - the menus a script asked for (field/menus.c);
 * - a switch stepped onto pressed, and the step remembered, and a chest the
 *   leader opened shown open (field/switches.c);
 * - a tile copy a script queued: the screen scrolls back onto the grid, the
 *   tiles are copied into the maps (field/tile_copy.c), and the layers they
 *   changed drawn again and sent in the vblank;
 * - a battle: the layers scrolled into place, the battle fought, and the
 *   field taken up again after it.
 * The field starts again on a fresh stack, which is the original's to do; so
 * are the routines the answers call out to. */
#include "field/field.h"
#include "field/battle_return.h"
#include "field/battle_start.h"
#include "field/main_loop.h"
#include "field/menus.h"
#include "field/party.h"
#include "field/requests.h"
#include "field/sounds.h"
#include "field/switches.h"
#include "field/walk.h"

#include "battle/program.h"
#include "charge.h"
#include "core/program.h"
#include "core/display.h"
#include "core/palette.h"
#include "field/layers.h"
#include "field/sprites.h"
#include "field/streaming.h"
#include "field/layers.h"
#include "field/bridge.h"
#include "field/display.h"
#include "field/parts.h"
#include "field/scratch.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* ---- The screen's changes -------------------------------------------------------------------- */

/* A change under way waits its rate's frames between steps: the rate's low
 * byte is the wait, its high byte the frames still to wait. */
enum { BRIGHTNESS_BITS = 0x0F };

static bool change_waits(uint16_t *rate)
{
    uint8_t wait = (uint8_t)*rate, left = (uint8_t)(*rate >> 8);
    if (left != 0) {
        *rate = (uint16_t)((left - 1) << 8 | wait);
        return true;
    }
    *rate = (uint16_t)(wait << 8 | wait);
    return false;
}

// $C0:1F24
/* A step of the brightness toward its target, which ends the change there.
 * Down to nothing, the change ends whatever its target. */
static void step_brightness(GameState *g)
{
    uint8_t brightness = field_brightness(g);
    uint8_t now = (uint8_t)(brightness & BRIGHTNESS_BITS), target = field_brightness_target(g);
    if (now == target) {
        set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_FADING));
        return;
    }
    uint16_t rate = field_brightness_rate(g);
    bool waits = change_waits(&rate);
    set_field_brightness_rate(g, rate);
    if (waits)
        return;
    if (now < target) {
        set_field_brightness(g, (uint8_t)(brightness + 1));
        return;
    }
    set_field_brightness(g, (uint8_t)(brightness - 1));
    if ((uint8_t)(brightness - 1) == 0)
        set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_FADING));
}

// $C0:1F5A
/* A step of the tint (the fixed color) toward its target. */
static void step_tint(GameState *g)
{
    uint8_t tint = field_fixed_color(g), target = field_fixed_color_target(g);
    if (tint == target) {
        set_field_effect_status(g, (uint8_t)(field_effect_status(g) & ~FIELD_TINTING));
        return;
    }
    uint16_t rate = field_fixed_color_rate(g);
    bool waits = change_waits(&rate);
    set_field_fixed_color_rate(g, rate);
    if (!waits)
        set_field_fixed_color(g, (uint8_t)(tint < target ? tint + 1 : tint - 1));
}

void field_step_screen_changes(GameState *g)
{
    if (field_effect_status(g) & FIELD_FADING)
        step_brightness(g);
    if (field_effect_status(g) & FIELD_TINTING)
        step_tint(g);
}

/* ---- The requests' answers ------------------------------------------------------------------ */

static bool asked(const GameState *g, uint8_t request) { return (field_requests(g) & request) != 0; }
static void answered(GameState *g, uint8_t request) { set_field_requests(g, (uint8_t)(field_requests(g) & ~request)); }

/* The switch's sound (field/sounds.c), run from each switch's own place as
 * the original calls it there. It reads A's high byte (the sound driver
 * keeps it and hands it back) and nothing else of the registers. Two of the
 * five switches note their shape before the sound and three after; nothing
 * reads it in between, but the call is compared, so the shape is noted first
 * where the original does. */
enum { SOUND_READS_NOTHING = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
/* Where each switch calls the sound from, by its shape and the column
 * stepped into: its own place, which is also where the port stops if the
 * call comes back other than it went. */
static const struct {
    uint32_t left, right;   /* stepped into the switch's left column, or its right */
} sound_sites[] = {
    [SWITCH_ONE_BY_TWO] = { 0xC00D92, 0xC00D92 },
    [SWITCH_TWO_BY_TWO] = { 0xC00E8F, 0xC01040 },
    [SWITCH_TWO_BY_THREE] = { 0xC01212, 0xC0149D },
};

static void leave_shape_before_the_sound(GameState *g, SwitchPress p)
{
    if (p.shape == SWITCH_ONE_BY_TWO || (p.shape == SWITCH_TWO_BY_TWO && p.step_column == 0))
        set_field_switch_shape(g, (uint8_t)p.shape);
}

/* A chest asked for with the step event is opened after it, and its sound
 * reads A's high byte as the step event's code leaves it (field/leaves.c). */
static void leave_a_for_the_chest(Snes *s, uint16_t step, bool pressed)
{
    if (asked(state_of(s), FIELD_REQUEST_OPEN_CHEST))
        s->cpu.a = (uint16_t)(field_step_event_high_a(state_of(s), step, pressed) << 8 | (s->cpu.a & 0xFF));
}

// $C0:0D45-$C0:16DB
/* A step event: the step remembered, and a switch stepped onto pressed with
 * its sound. False where the sound's call came back other than it went, and
 * the port has stopped. */
static bool answer_step_event(Snes *s)
{
    GameState *g = state_of(s);
    answered(g, FIELD_REQUEST_STEP_EVENT);
    uint16_t step = field_step_tile(g);
    field_remember_step(g, step);
    SwitchPress p = field_press_switch(g, step);
    if (!p.pressed) {
        leave_a_for_the_chest(s, step, false);
        return true;
    }
    ORIGINAL_EFFECT(leave_shape_before_the_sound(g, p));
    s->cpu.a = (uint16_t)(field_switch_sound_high_a(g, p, step) << 8 | (s->cpu.a & 0xFF));
    unsigned shape = p.shape;
    uint32_t site = p.step_column == 0 ? sound_sites[shape].left : sound_sites[shape].right;
    if (!field_run(s, site, FIELD_SWITCH_SOUND_ENTRY, SOUND_READS_NOTHING, field_play_the_switch_sound_entry,
                   on_the_field_page(s, false)))
        return false;
    field_queue_switch_redraw(g, p, step);
    m_set_flag(s, CPU_FLAG_V, field_switch_redraw_overflow(g));
    leave_a_for_the_chest(s, step, true);
    return true;
}

/* ---- Calling on the machine ----------------------------------------------------------------- */

/* The original's routines the answers call. */
enum {
    FRAME_WAIT = 0xC0EC60,          /* waits for the next frame */
    PALETTE_ANIMATION = 0xFDFFF7,   /* the original's jump to it */
    BATTLE = 0xC10000,
    /* the field taken up after the battle (field/battle_return.c): a port
     * that runs frames and ends by going on into another, which only a call
     * can run */
    FIELD_AFTER_BATTLE = FIELD_AFTER_BATTLE_ENTRY,
    FIELD_TURN = FIELD_WALK_A_FRAME_ENTRY,   /* a port, which waits for the next frame: called */
};
/* The ports the answers run as C. */
enum { RECENTRE = 0xC0885A, COPY_TILES = 0xC0AF4E, SCROLL_LAYERS = 0xC02C41, FREE_SPRITE_AREAS = 0xC0E935,
       DRAW_BG1 = 0xC075E9, DRAW_BG2 = 0xC078EC, DRAW_BG3 = 0xC07CB5 };
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
       READS_THE_OVERFLOW = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

static bool call(Snes *s, uint32_t site, uint32_t routine, uint16_t dont_care)
{
    return bridge_call(s, site, routine, dont_care, on_the_field_page(s, false));
}
/* The columns right of the view brought in once the screen is put back where
 * the party is (field/streaming.h). */
static bool bring_in_the_right_columns(Snes *s, uint32_t site)
{
    return field_run(s, site, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, READS_NOTHING, field_bring_in_the_right_columns,
                     on_the_field_page(s, false));
}

static bool wait_frame(Snes *s, uint32_t site)
{
    return field_wait_frame(s, site, FRAME_WAIT, READS_NOTHING, on_the_field_page(s, false));
}

/* The field's own steps (field/main_loop.c), run as C. */
static bool refresh(Snes *s, uint32_t site)
{
    return field_run(s, site, FIELD_REFRESH_EFFECTS_ENTRY, READS_NOTHING, field_refresh_effects,
                     on_the_field_page(s, false));
}

/* ---- A location change ($C0:0C8F) --------------------------------------------------------- */

/* Where the party is kept as the place it came from: the location, the
 * leader's tile, and the way it will face coming back (the opposite of its
 * facing: up and down, left and right pair off in the facing's low bit). */
enum { FACING_OPPOSITE = 0x01 };

static void keep_where_the_party_was(GameState *g)
{
    LeaderPlace at = field_leaders_place(g);
    set_location_previous_id(g, location_id(g));
    set_location_previous_x(g, at.x);
    set_location_previous_y(g, at.y);
    set_location_previous_flags(g, (uint8_t)(at.facing ^ FACING_OPPOSITE));
}

/* The field starts again on a fresh stack at its start ($C0:0000), which the
 * port leaves to the original, from where it sets the stack. */
enum { FIELD_STARTS_AGAIN = 0xC00CC0, AS_THE_FIELD_STARTS = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:0C8F
/* A step of the fade a frame; dark, the change made. False where the port
 * has handed the field's new start to the original. */
static bool change_location_on_the_machine(Snes *s)
{
    GameState *g = state_of(s);
    if (field_dims_a_step(g))
        return true;
    display_go_dark(s);
    keep_where_the_party_was(g);
    field_move_to_the_next_place(g);
    m_stop_at_own(s, FIELD_STARTS_AGAIN, AS_THE_FIELD_STARTS);
    return false;
}

/* ---- A chest ($C0:16DC) --------------------------------------------------------------------- */

/* The chest's sound (field/sounds.c), run from the original's own call. It
 * reads A's high byte (the sound driver keeps it and hands it back): the
 * step event's where one came before (leave_a_for_the_chest), else as the
 * main loop called the requests. The chest's kind is forgotten before the
 * sound, which only shadow mode's comparison at the call sees: opening the
 * chest sets it again. */
enum { CHEST_SOUND_SITE = 0xC016EB };

static bool answer_chest(Snes *s)
{
    GameState *g = state_of(s);
    ORIGINAL_EFFECT(field_chest_opening_starts(g));
    answered(g, FIELD_REQUEST_OPEN_CHEST);
    if (!field_run(s, CHEST_SOUND_SITE, FIELD_CHEST_SOUND_ENTRY, SOUND_READS_NOTHING, field_play_the_chest_sound_entry,
                   on_the_field_page(s, false)))
        return false;
    if (field_open_chest(g))
        m_set_flag(s, CPU_FLAG_V, field_chest_redraw_overflow(g));
    return true;
}

/* ---- A tile copy ($C0:1779) ----------------------------------------------------------------- */

/* The routines a copy calls, each a port: the screen's step back, the frame's
 * work, the tiles' copy, the palettes' animation, a layer's drawing at the
 * map's load, the effects' refresh, the columns right of the view brought
 * in, and the wait for the next frame. */
typedef enum {
    TC_RECENTRE, TC_FRAME_WORK, TC_COPY, TC_PALETTES, TC_DRAW_BG1, TC_DRAW_BG2, TC_DRAW_BG3, TC_REFRESH, TC_COLUMNS,
    TC_WAIT,
} TileCopyRoutine;

/* Every call the copy makes, by where the original makes it. A layer sent
 * alone has its own copy of the calls; BG3's ends with a frame wait of its
 * own, the others by jumping to the main loop's (TC_LAST_WAIT). */
typedef enum {
    RECENTRING, RECENTRING_FRAME_WORK, RECENTRING_WAIT, COPYING, COPYING_PALETTES, COPYING_WAIT,
    BG1_DRAWN, BG1_REFRESHED, BG1_WAITED, BG1_REFRESHED_AFTER, BG1_COLUMNS,
    BG2_DRAWN, BG2_REFRESHED, BG2_WAITED, BG2_REFRESHED_AFTER, BG2_COLUMNS,
    BG3_DRAWN, BG3_REFRESHED, BG3_WAITED, BG3_REFRESHED_AFTER, BG3_COLUMNS, BG3_LAST_WAIT,
    BOTH_BG1_DRAWN, BOTH_BG1_REFRESHED, BOTH_BG1_WAITED, BOTH_BG2_DRAWN, BOTH_BG2_REFRESHED, BOTH_BG2_WAITED,
    BOTH_BG1_DRAWN_AGAIN, BOTH_BG1_REFRESHED_AGAIN, BOTH_BG1_WAITED_AGAIN, BOTH_REFRESHED_AFTER, BOTH_COLUMNS,
    TILE_COPY_CALLS,
    TC_LAST_WAIT = TILE_COPY_CALLS,
} TileCopyCall;

static const struct {
    TileCopyRoutine routine;
    uint32_t site;
} tile_copy_calls[TILE_COPY_CALLS] = {
    [RECENTRING] = { TC_RECENTRE, 0xC01781 },          [RECENTRING_FRAME_WORK] = { TC_FRAME_WORK, 0xC01784 },
    [RECENTRING_WAIT] = { TC_WAIT, 0xC0178B },         [COPYING] = { TC_COPY, 0xC01790 },
    [COPYING_PALETTES] = { TC_PALETTES, 0xC01793 },    [COPYING_WAIT] = { TC_WAIT, 0xC01797 },
    [BG1_DRAWN] = { TC_DRAW_BG1, 0xC017A9 },           [BG1_REFRESHED] = { TC_REFRESH, 0xC017B0 },
    [BG1_WAITED] = { TC_WAIT, 0xC017B9 },              [BG1_REFRESHED_AFTER] = { TC_REFRESH, 0xC017BC },
    [BG1_COLUMNS] = { TC_COLUMNS, 0xC017BF },
    [BG2_DRAWN] = { TC_DRAW_BG2, 0xC017D2 },           [BG2_REFRESHED] = { TC_REFRESH, 0xC017D9 },
    [BG2_WAITED] = { TC_WAIT, 0xC017E2 },              [BG2_REFRESHED_AFTER] = { TC_REFRESH, 0xC017E5 },
    [BG2_COLUMNS] = { TC_COLUMNS, 0xC017E8 },
    [BG3_DRAWN] = { TC_DRAW_BG3, 0xC0186F },           [BG3_REFRESHED] = { TC_REFRESH, 0xC01876 },
    [BG3_WAITED] = { TC_WAIT, 0xC0187F },              [BG3_REFRESHED_AFTER] = { TC_REFRESH, 0xC01882 },
    [BG3_COLUMNS] = { TC_COLUMNS, 0xC01885 },          [BG3_LAST_WAIT] = { TC_WAIT, 0xC01888 },
    [BOTH_BG1_DRAWN] = { TC_DRAW_BG1, 0xC017FE },      [BOTH_BG1_REFRESHED] = { TC_REFRESH, 0xC01802 },
    [BOTH_BG1_WAITED] = { TC_WAIT, 0xC0180B },         [BOTH_BG2_DRAWN] = { TC_DRAW_BG2, 0xC01817 },
    [BOTH_BG2_REFRESHED] = { TC_REFRESH, 0xC0182B },   [BOTH_BG2_WAITED] = { TC_WAIT, 0xC01832 },
    [BOTH_BG1_DRAWN_AGAIN] = { TC_DRAW_BG1, 0xC0183E }, [BOTH_BG1_REFRESHED_AGAIN] = { TC_REFRESH, 0xC0184F },
    [BOTH_BG1_WAITED_AGAIN] = { TC_WAIT, 0xC01856 },   [BOTH_REFRESHED_AFTER] = { TC_REFRESH, 0xC01859 },
    [BOTH_COLUMNS] = { TC_COLUMNS, 0xC0185C },
};

/* A call of the copy's, as C or on the machine (below); false where it went
 * no further. */
typedef bool (*TileCopyRun)(void *ctx, TileCopyCall call);

/* The layers a copy changed (field_tile_copy_queued_maps), drawn again and
 * sent: BG1, BG2 or BG3 alone, or BG1 and BG2 (below). A layer is drawn on the
 * layers' page and put back at its map's start, and the vblank told to move it
 * to the layer's place (field_map_transition); after it the frame's effects
 * are refreshed and the layers' scroll put where the screen is. */
typedef enum { QUEUED_BG1 = 1, QUEUED_BG2 = 2, QUEUED_BG1_AND_BG2 = 3, QUEUED_BG3 = 4 } QueuedMaps;

static const struct {
    FieldMapMove move;
    TileCopyCall drawn, refreshed, waited, refreshed_after, columns, last_wait;
} alone[FIELD_LAYERS] = {
    [FIELD_BG1] = { FIELD_MOVE_TO_BG1, BG1_DRAWN, BG1_REFRESHED, BG1_WAITED, BG1_REFRESHED_AFTER, BG1_COLUMNS,
                    TC_LAST_WAIT },
    [FIELD_BG2] = { FIELD_MOVE_TO_BG2, BG2_DRAWN, BG2_REFRESHED, BG2_WAITED, BG2_REFRESHED_AFTER, BG2_COLUMNS,
                    TC_LAST_WAIT },
    [FIELD_BG3] = { FIELD_MOVE_TO_BG3, BG3_DRAWN, BG3_REFRESHED, BG3_WAITED, BG3_REFRESHED_AFTER, BG3_COLUMNS,
                    BG3_LAST_WAIT },
};

/* BG1 and the transition base trade places: the copy's second pass sends
 * BG1's map to where the first sent BG2's. */
static void trade_bg1_base(GameState *g)
{
    uint16_t bg1 = field_layer_map_base(g, 0);
    set_field_layer_map_base(g, 0, field_transition_map_base(g));
    set_field_transition_map_base(g, bg1);
}

// $C0:179A-$C0:17EB, $C0:1867-$C0:188B
/* One layer alone. */
static bool send_one_layer(GameState *g, FieldLayer layer, TileCopyRun run, void *ctx)
{
    if (!run(ctx, alone[layer].drawn))
        return false;
    field_put_the_layer_at_its_start(g, layer);
    if (!run(ctx, alone[layer].refreshed))
        return false;
    set_field_map_transition(g, alone[layer].move);
    set_field_tile_copy_queued_maps(g, 0);
    return run(ctx, alone[layer].waited) && run(ctx, alone[layer].refreshed_after) && run(ctx, alone[layer].columns)
           && run(ctx, alone[layer].last_wait);
}

// $C0:17EC-$C0:1866
/* BG1 and BG2: BG1 drawn and sent; then BG2 drawn and sent to BG1's place
 * while BG1 goes again to the transition's; then BG1 drawn and sent back, a
 * frame each. */
static bool send_bg1_and_bg2(GameState *g, TileCopyRun run, void *ctx)
{
    if (!run(ctx, BOTH_BG1_DRAWN) || !run(ctx, BOTH_BG1_REFRESHED))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_THE_LOADERS_PLACE);
    set_field_tile_copy_queued_maps(g, 0);
    if (!run(ctx, BOTH_BG1_WAITED) || !run(ctx, BOTH_BG2_DRAWN))
        return false;
    trade_bg1_base(g);
    field_put_the_layer_at_its_start(g, FIELD_BG1);
    field_put_the_layer_at_its_start(g, FIELD_BG2);
    if (!run(ctx, BOTH_BG2_REFRESHED))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG2);
    if (!run(ctx, BOTH_BG2_WAITED) || !run(ctx, BOTH_BG1_DRAWN_AGAIN))
        return false;
    trade_bg1_base(g);
    field_put_the_layer_at_its_start(g, FIELD_BG1);
    if (!run(ctx, BOTH_BG1_REFRESHED_AGAIN))
        return false;
    set_field_map_transition(g, FIELD_MOVE_TO_BG1);
    return run(ctx, BOTH_BG1_WAITED_AGAIN) && run(ctx, BOTH_REFRESHED_AFTER) && run(ctx, BOTH_COLUMNS)
           && run(ctx, TC_LAST_WAIT);
}

// $C0:1779
/* The screen scrolls back onto the grid a frame at a time, the frame's work
 * going on, then the tiles are copied and the layers they changed sent; the
 * main loop's pass goes on after the last frame's wait. */
static bool copy_the_tiles(GameState *g, TileCopyRun run, void *ctx)
{
    answered(g, FIELD_REQUEST_TILE_COPY);
    screen_starts_recentring(g);
    for (;;) {
        if (!run(ctx, RECENTRING) || !run(ctx, RECENTRING_FRAME_WORK))
            return false;
        if (screen_is_recentred(g))
            break;
        if (!run(ctx, RECENTRING_WAIT))
            return false;
    }
    if (!run(ctx, COPYING) || !run(ctx, COPYING_PALETTES) || !run(ctx, COPYING_WAIT))
        return false;
    switch ((QueuedMaps)field_tile_copy_queued_maps(g)) {
    case QUEUED_BG1: return send_one_layer(g, FIELD_BG1, run, ctx);
    case QUEUED_BG2: return send_one_layer(g, FIELD_BG2, run, ctx);
    case QUEUED_BG3: return send_one_layer(g, FIELD_BG3, run, ctx);
    case QUEUED_BG1_AND_BG2: return send_bg1_and_bg2(g, run, ctx);
    default: return true;
    }
}

/* ---- As C ---- */

typedef struct {
    GameState *game;
    Platform *pf;
} TileCopyAsC;

static bool tile_copy_call(void *ctx, TileCopyCall call)
{
    TileCopyAsC *c = ctx;
    GameState *g = c->game;
    bool done = true;
    switch (call == TC_LAST_WAIT ? TC_WAIT : tile_copy_calls[call].routine) {
    case TC_RECENTRE:
        TIMED_AS(g, RECENTRE, done = field_the_screen_recentres(g));
        return done;
    case TC_FRAME_WORK:
        TIMED_AS(g, FIELD_FRAME_WORK_ENTRY, done = field_frame_work_done(g, c->pf));
        return done;
    case TC_COPY:
        if (!field_tile_copy_is_on_the_map(g)) {
            program_needs_the_machine(g, COPY_TILES, "a tile copy off the map");
            return false;
        }
        TIMED_AS(g, COPY_TILES, map_copy_tiles(g));
        return true;
    case TC_PALETTES: TIMED_AS(g, PALETTE_ANIMATION, done = palette_animated(g)); return done;
    case TC_DRAW_BG1: return field_layer_drawn_at_the_load(g, FIELD_BG1);
    case TC_DRAW_BG2: return field_layer_drawn_at_the_load(g, FIELD_BG2);
    case TC_DRAW_BG3: return field_layer_drawn_at_the_load(g, FIELD_BG3);
    case TC_REFRESH: TIMED_AS(g, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(g)); return done;
    case TC_COLUMNS:
        if (!layers_repeat_within_the_maps(g)) {
            program_needs_the_machine(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, "layers the port doesn't stream");
            return false;
        }
        TIMED_AS(g, FIELD_BRING_IN_THE_RIGHT_COLUMNS_ENTRY, stream_the_right_columns(g));
        return true;
    case TC_WAIT: TIMED_AS(g, FRAME_WAIT, field_finish_the_frame(g, c->pf)); return true;
    default: return false;
    }
}

/* ---- On the machine ---- */

/* The original ends most sends by jumping to the frame wait, which returns
 * to the main loop itself; the port stops there. What the main loop reads
 * after it is what the calls before left. */
enum { THE_LAST_WAIT = DC_A | DC_N | DC_Z };

static bool tile_copy_call_on_the_machine(void *ctx, TileCopyCall call)
{
    Snes *s = ctx;
    if (call == TC_LAST_WAIT) {
        m_stop_at_own(s, FRAME_WAIT, THE_LAST_WAIT);
        return false;
    }
    uint32_t site = tile_copy_calls[call].site;
    switch (tile_copy_calls[call].routine) {
    case TC_RECENTRE: return field_run(s, site, RECENTRE, READS_NOTHING, field_recentre_the_screen, on_the_field_page(s, false));
    case TC_FRAME_WORK:
        return field_run(s, site, FIELD_FRAME_WORK_ENTRY, READS_THE_OVERFLOW, field_frame_work, on_the_field_page(s, false));
    case TC_COPY: return field_run(s, site, COPY_TILES, READS_NOTHING, field_copy_tiles, on_the_field_page(s, false));
    case TC_PALETTES: return bridge_call_long(s, site, PALETTE_ANIMATION, READS_NOTHING, on_the_field_page(s, false));
    case TC_DRAW_BG1: return field_run_on_page(s, site, DRAW_BG1, READS_NOTHING, field_draw_bg1_at_the_load, FIELD_LAYERS_PAGE);
    case TC_DRAW_BG2: return field_run_on_page(s, site, DRAW_BG2, READS_NOTHING, field_draw_bg2_at_the_load, FIELD_LAYERS_PAGE);
    case TC_DRAW_BG3: return field_run_on_page(s, site, DRAW_BG3, READS_NOTHING, field_draw_bg3_at_the_load, FIELD_LAYERS_PAGE);
    case TC_REFRESH: return refresh(s, site);
    case TC_COLUMNS: return bring_in_the_right_columns(s, site);
    case TC_WAIT: return wait_frame(s, site);
    default: return false;
    }
}

/* False where the port stopped. */
static bool answer_tile_copy(Snes *s) { return copy_the_tiles(state_of(s), tile_copy_call_on_the_machine, s); }

/* ---- A battle ($C0:188C) ------------------------------------------------------------------- */

/* The battle's over, for the script that asked for it (event $D8). */
static void battle_over(GameState *g)
{
    answered(g, FIELD_REQUEST_BATTLE);
    set_field_effect_status(g, (uint8_t)(field_effect_status(g) | FIELD_BATTLE_OVER));
}

/* The battle's steps that are other routines: the battle's start on the
 * field (field/battle_start.c), which finds none to fight or puts the field
 * aside; the field's turn where it found none; the layers scrolled into
 * place and the effects built with the field's setup's values, a frame; the
 * battle (bank $C1's); the field taken up again (field/battle_return.c); the
 * switches the party stood on pressed again and the sprites' areas freed;
 * and the field's turn after it. The program makes them as C, and the
 * machine's entry from the original's calls. */
typedef struct {
    FieldBattleStart (*begins)(void *ctx);
    bool (*no_battle_turn)(void *ctx);
    bool (*scroll_the_layers)(void *ctx);
    bool (*effects_refreshed)(void *ctx);
    bool (*wait_a_frame)(void *ctx);
    bool (*fight)(void *ctx);
    bool (*take_up_the_field)(void *ctx);
    bool (*press_the_switches)(void *ctx);
    bool (*free_the_sprite_areas)(void *ctx);
    bool (*field_turn)(void *ctx);
} BattleSteps;

// $C0:188C
/* The battle fought, and the field taken up again: its interrupts, its
 * objects and its turn. False where a step went no further. */
static bool fight_a_battle(GameState *g, const BattleSteps *steps, void *ctx)
{
    FieldBattleStart start = steps->begins(ctx);
    if (start == FIELD_BATTLE_START_STOPPED)
        return false;
    if (start == FIELD_NO_BATTLE) {
        battle_over(g);
        return steps->no_battle_turn(ctx);
    }
    if (!steps->scroll_the_layers(ctx))
        return false;
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) | FIELD_DISPLAY_SETUP));
    if (!steps->effects_refreshed(ctx))
        return false;
    set_field_display_flags(g, (uint8_t)(field_display_flags(g) & ~FIELD_DISPLAY_SETUP));
    if (!steps->wait_a_frame(ctx) || !steps->fight(ctx))
        return false;
    TIMED_AS(g, FIELD_INSTALL_NMI_ENTRY, field_take_the_vblank_interrupt(g));
    TIMED_AS(g, FIELD_INSTALL_IRQ_ENTRY, field_take_the_line_interrupt(g));
    battle_over(g);
    return steps->take_up_the_field(ctx) && steps->press_the_switches(ctx) && steps->free_the_sprite_areas(ctx) &&
           steps->field_turn(ctx);
}

/* On the machine, each from the original's call of it. Where the battle's
 * setup on the field finds none to fight, the field's turn goes on by the
 * original's jump to it, which returns to the main loop. */
enum { NO_BATTLE_TURN = DC_A | DC_N | DC_Z };

static FieldBattleStart begins_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    if (!field_run(s, 0xC01890, FIELD_BATTLE_BEGINS_ENTRY, READS_NOTHING, field_battle_begins, on_the_field_page(s, false)))
        return FIELD_BATTLE_START_STOPPED;
    return m_flag(s, CPU_FLAG_C) ? FIELD_NO_BATTLE : FIELD_BATTLE_SET;
}

static bool no_battle_turn_on_the_machine(void *ctx)
{
    m_stop_at_own(ctx, FIELD_TURN, NO_BATTLE_TURN);
    return false;
}

static bool scroll_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run_long(s, 0xC01895, SCROLL_LAYERS, READS_NOTHING, field_scroll_layers, on_the_field_page(s, false));
}

static bool refresh_before_the_battle_on_the_machine(void *ctx) { return refresh(ctx, 0xC0189D); }
static bool wait_before_the_battle_on_the_machine(void *ctx) { return wait_frame(ctx, 0xC018A4); }

static bool fight_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    s->cpu.a = field_frame_wait_leaves_a(state_of(s));
    return bridge_call_long(s, 0xC018A7, BATTLE, 0, on_the_field_page(s, false));
}

static bool take_up_on_the_machine(void *ctx) { return call(ctx, 0xC018C1, FIELD_AFTER_BATTLE, READS_NOTHING); }

static bool press_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run(s, 0xC018C4, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, READS_NOTHING,
                     field_press_the_remembered_switches_entry, on_the_field_page(s, false));
}

static bool free_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    return field_run(s, 0xC018C7, FREE_SPRITE_AREAS, READS_NOTHING, field_free_every_sprite_area,
                     on_the_field_page(s, false));
}

static bool field_turn_on_the_machine(void *ctx) { return call(ctx, 0xC018CA, FIELD_TURN, READS_NOTHING); }

/* False where the port stopped. */
static bool answer_battle(Snes *s)
{
    static const BattleSteps on_the_machine = {
        begins_on_the_machine, no_battle_turn_on_the_machine, scroll_on_the_machine,
        refresh_before_the_battle_on_the_machine, wait_before_the_battle_on_the_machine, fight_on_the_machine,
        take_up_on_the_machine, press_on_the_machine, free_on_the_machine, field_turn_on_the_machine,
    };
    return fight_a_battle(state_of(s), &on_the_machine, s);
}

/* As C, each with its guard's state half first. */
typedef struct {
    GameState *game;
    Platform *pf;
} BattleAsC;

static FieldBattleStart battle_begins(void *ctx)
{
    BattleAsC *c = ctx;
    FieldBattleStart start = FIELD_BATTLE_START_STOPPED;
    TIMED_AS(c->game, FIELD_BATTLE_BEGINS_ENTRY, start = field_battle_set_up(c->game, c->pf));
    return start;
}

static bool walk_a_frame(void *ctx)
{
    BattleAsC *c = ctx;
    return field_walked_a_frame(c->game, c->pf);
}

static bool scroll_the_layers(void *ctx)
{
    TIMED_AS(((BattleAsC *)ctx)->game, SCROLL_LAYERS, field_scroll_layers_by_themselves(((BattleAsC *)ctx)->game));
    return true;
}

static bool refresh_the_effects(void *ctx)
{
    BattleAsC *c = ctx;
    bool done = false;
    TIMED_AS(c->game, FIELD_REFRESH_EFFECTS_ENTRY, done = field_effects_refreshed(c->game));
    return done;
}

static bool wait_for_the_frame(void *ctx)
{
    BattleAsC *c = ctx;
    TIMED_AS(c->game, FRAME_WAIT, field_finish_the_frame(c->game, c->pf));
    return true;
}

static bool fight(void *ctx)
{
    BattleAsC *c = ctx;
    return battle_fought(c->game, c->pf);
}

static bool take_up_the_field(void *ctx)
{
    BattleAsC *c = ctx;
    return field_taken_up_after_battle(c->game, c->pf);
}

static bool press_the_switches(void *ctx)
{
    GameState *g = ((BattleAsC *)ctx)->game;
    if (!field_remembered_switches_on_the_map(g)) {
        program_needs_the_machine(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, "remembered switches off the map");
        return false;
    }
    TIMED_AS(g, FIELD_PRESS_REMEMBERED_SWITCHES_ENTRY, field_press_the_remembered_switches(g));
    return true;
}

static bool free_the_sprite_areas(void *ctx)
{
    GameState *g = ((BattleAsC *)ctx)->game;
    TIMED_AS(g, FREE_SPRITE_AREAS, sprites_free_every_frame_area(g));
    return true;
}

static FieldPass a_battle_fought(GameState *g, Platform *pf)
{
    static const BattleSteps as_c = {
        battle_begins, walk_a_frame, scroll_the_layers, refresh_the_effects, wait_for_the_frame, fight,
        take_up_the_field, press_the_switches, free_the_sprite_areas, walk_a_frame,
    };
    BattleAsC c = { g, pf };
    return fight_a_battle(g, &as_c, &c) ? FIELD_PASS_GOES_ON : FIELD_PASS_STOPS;
}

/* ---- The entry ------------------------------------------------------------------------------ */

// $C0:0C76
/* The field's page, 8-bit A and 16-bit index registers. A's high byte and the
 * overflow are read after it, as the routines it calls leave them or as it
 * came. */
/* The requests asked for, in their order. False where the port stopped. */
static bool answer_requests(Snes *s)
{
    GameState *g = state_of(s);
    if (asked(g, FIELD_REQUEST_LOCATION_CHANGE))
        return change_location_on_the_machine(s);
    if (asked(g, FIELD_REQUEST_MENUS))
        return field_open_script_menu(s);
    if (asked(g, FIELD_REQUEST_STEP_EVENT) && !answer_step_event(s))
        return false;
    if (asked(g, FIELD_REQUEST_OPEN_CHEST) && !answer_chest(s))
        return false;
    if (asked(g, FIELD_REQUEST_TILE_COPY))
        return answer_tile_copy(s);
    if (asked(g, FIELD_REQUEST_BATTLE))
        return answer_battle(s);
    return true;
}

/* ---- As C, with no machine ------------------------------------------------------------------ */

/* The requests a program with no machine answers as C so far: a location
 * change, a script's menu and a tile copy. The rest stop it where they are
 * asked for (core/program.h). */
static const struct {
    uint8_t request;
    uint32_t routine;
    const char *what;
} only_the_machines[] = {
    { FIELD_REQUEST_STEP_EVENT, 0xC00C76, "a step on an event's tile" },
    { FIELD_REQUEST_OPEN_CHEST, 0xC016DC, "a chest opened" },
};

// $C0:0C8F
static FieldPass change_location(GameState *g, Platform *pf)
{
    if (field_dims_a_step(g))
        return FIELD_PASS_GOES_ON;
    TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
    keep_where_the_party_was(g);
    field_move_to_the_next_place(g);
    return FIELD_STARTS_ANEW;
}

FieldPass field_requests_handled(GameState *g, Platform *pf)
{
    field_step_screen_changes(g);
    if (field_requests(g) == 0)
        return FIELD_PASS_GOES_ON;
    if (asked(g, FIELD_REQUEST_LOCATION_CHANGE))
        return change_location(g, pf);
    if (asked(g, FIELD_REQUEST_MENUS))
        return field_script_menu_opened(g, pf);
    for (unsigned i = 0; i < sizeof only_the_machines / sizeof only_the_machines[0]; i++)
        if (asked(g, only_the_machines[i].request)) {
            program_needs_the_machine(g, only_the_machines[i].routine, only_the_machines[i].what);
            return FIELD_PASS_STOPS;
        }
    if (asked(g, FIELD_REQUEST_TILE_COPY)) {
        TileCopyAsC c = { g, pf };
        return copy_the_tiles(g, tile_copy_call, &c) ? FIELD_PASS_GOES_ON : FIELD_PASS_STOPS;
    }
    if (asked(g, FIELD_REQUEST_BATTLE))
        return a_battle_fought(g, pf);
    return FIELD_PASS_GOES_ON;
}

void field_handle_requests(Snes *s)
{
    GameState *g = state_of(s);
    field_step_screen_changes(g);
    if (field_requests(g) == 0 || answer_requests(s))
        m_rts(s);
}

/* The guard: on the field's page with the data bank $00, and the switch a
 * step event names, and each of the switch's tiles, inside the map's rows
 * (a switch reaches up to two rows above the tile stepped on, and a column
 * left of it, which from a row's first column is the row above's last), and
 * not a one-by-two switch whose upper tile
 * is the tile set's last (moving it on would carry into the next tile, as
 * the original moves it, a word at a time); and a chest opened inside the
 * map's rows; for a location change made (the fade done), the leader an
 * object or none, its link a word (the original reads the leader's place
 * with it as a 16-bit index). */
enum { LAST_TILE = 0xFF };

bool field_requests_can_run(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t requests = field_requests(g);
    bool ok = true;
    if ((requests & FIELD_REQUEST_STEP_EVENT) &&
        !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS))) {
        uint16_t step = field_step_tile(g);
        ok = (step >> 8) < FIELD_MAP_ROWS && field_switch_inside_the_map(g, step) &&
             !(field_is_one_by_two_switch(g, step) && field_map_byte(g, field_switch_upper_tile(step)) == LAST_TILE);
    }
    if ((requests & FIELD_REQUEST_LOCATION_CHANGE) && field_is_dark(g))
        ok = ok && field_leader_readable(g);
    if ((requests & FIELD_REQUEST_OPEN_CHEST) && !(requests & (FIELD_REQUEST_LOCATION_CHANGE | FIELD_REQUEST_MENUS)))
        ok = ok && (field_opened_chest_tile(g) >> 8) < FIELD_MAP_ROWS;
    vars_peek(false);
    return ok;
}
