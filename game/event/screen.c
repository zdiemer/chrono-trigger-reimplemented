/* Scripts directing the screen: fading it in and out, tinting it with a
 * colour (a flash, a red alert, a dream's haze), changing the map's tiles
 * (a door opening, a bridge appearing), drifting the layers, scrolling the
 * camera, shaking the screen, and handing the party to the player or taking
 * it away; the screen effects the windows cut into the picture (a hole, an
 * oval of light, a curtain, a searchlight: field/screen_effects.c); the
 * credits the endings write into BG3, and BG2's halves cleared
 * (field/script_layers.c); and the field's end, into a scene.
 *
 * The fades and tints are gradual when a rate is given: the field's
 * interrupt steps them a frame at a time toward their target, and marks them
 * running (field/field.h, field_effect_status) until they arrive. */
#include "event/bridge.h"
#include "charge.h"
#include "core/display.h"
#include "core/palette.h"
#include "core/program.h"
#include "event/engine.h"
#include "field/field.h"
#include "field/layers.h"
#include "field/main_loop.h"
#include "field/screen_effects.h"
#include "field/script_layers.h"
#include "field/walk.h"

enum { ALL_MAPS = 0x07 };   /* a tile copy's maps: one bit each */

// $C0:3C05
/* Give the player control of the party (nonzero), or take it away for a
 * cutscene. */
static EventFlow set_player_control(GameState *g, uint8_t control)
{
    set_field_player_control(g, control);
    return next_command();
}
EVENT_OPERATION(set_player_control, control)

// $C0:3CD0
/* Fade the screen to a brightness (0 black to 15 full): at once, or at a
 * rate. Ends the turn. */
static EventFlow fade(GameState *g, uint8_t brightness, uint8_t rate)
{
    if (rate != 0) {
        set_field_brightness_rate(g, (uint16_t)(rate << 8 | rate));
        set_field_brightness_target(g, brightness);
        set_field_effect_status(g, field_effect_status(g) | FIELD_FADING);
    } else {
        set_field_brightness(g, brightness);
    }
    return end_turn();
}
EVENT_OPERATION(fade, brightness, rate)

// $C0:3CFD
/* Wait for a fade, or a tint, to arrive. */
static EventFlow wait_for_screen(GameState *g, uint8_t changes)
{
    return (field_effect_status(g) & changes) ? stay() : end_turn();
}
EVENT_OPERATION(wait_for_screen, changes)

/* A tint: a colour mixed into the whole picture, added to it or taken away.
 * The field keeps it as the hardware has it -- the fixed colour's shadow
 * (which colour channels in bits 5-7, the strength in bits 0-4) and the
 * colour-math setting it applies with -- and its interrupt hands both on each
 * frame; so for now the tint is written in those terms here, the field's
 * vocabulary for it still to come (docs/TODO.md). */
enum { CHANNELS = VID_LINE_FIXED_COLOR_ALL_CHANNELS, STRENGTH = 0x1F };

/* The colour-math setting a tint applies with: over every layer and the
 * sprites, added or taken away. */
static uint8_t tint_math(TintMix mix) { return mix == TINT_ADDED ? 0x17 : 0x97; }

/* The tint's colour channels become the target's, its strength stays. The
 * original keeps the channels in its scratch cell. */
static void take_target_channels(GameState *g)
{
    uint8_t channels = field_fixed_color_target(g) & CHANNELS;
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, channels));
    set_field_fixed_color(g, (uint8_t)((field_fixed_color(g) & STRENGTH) | channels));
}

// $C0:3D07
/* Tint the screen with a colour, added to the picture or taken away from it:
 * at once, or at a rate, which ends the turn. No colour takes the tint away
 * and gives the picture back its own colour layers; that form of the command
 * has no rate, and is a byte shorter. */
static EventFlow tint(GameState *g, uint8_t colour, TintMix mix, uint8_t rate)
{
    if (colour == 0) {
        set_field_fixed_color_target(g, FIELD_NO_TINT);
        set_field_fixed_color(g, FIELD_NO_TINT);
        set_field_fixed_color_math_layers(g, field_color_math_layers(g));
        return next_command_after(2);
    }
    set_field_fixed_color_target(g, colour);
    take_target_channels(g);
    if (rate == 0) {
        set_field_fixed_color(g, field_fixed_color_target(g));
        set_field_fixed_color_math_layers(g, tint_math(mix));
        return next_command();
    }
    set_field_fixed_color_rate(g, (uint16_t)(rate << 8 | rate));
    set_field_fixed_color_math_layers(g, tint_math(mix));
    set_field_effect_status(g, field_effect_status(g) | FIELD_TINT_CHANGES);
    return end_turn();
}
EVENT_OPERATION(tint, colour, mix, rate)

// $C0:3D84
/* Shake the screen, or stop shaking it: a stop puts the layers' scroll back
 * where it was. (The shake is any nonzero value; the field keeps the
 * script's byte.) */
static EventFlow shake_screen(GameState *g, uint8_t shaking)
{
    set_field_screen_shake(g, shaking);
    if (shaking == 0) {
        set_field_scroll_offset_x(g, 0);
        set_field_scroll_offset_y(g, 0);
    }
    return next_command();
}
EVENT_OPERATION(shake_screen, shaking)

/* A change to the map's tiles: a rectangle of tiles (its top left and bottom
 * right) copied to another place on the maps chosen. */
static void describe_tile_copy(GameState *g, uint16_t from, uint16_t to, uint16_t destination, uint8_t maps)
{
    set_field_tile_copy_from(g, from);
    set_field_tile_copy_to(g, to);
    set_field_tile_copy_destination(g, destination);
    set_field_tile_copy_maps(g, maps);
}

// $C0:3DC5
/* Change the map's tiles on the field's next pass: a door opens, a bridge
 * appears. Ends the turn. */
static EventFlow queue_tile_copy(GameState *g, uint16_t from, uint16_t to, uint16_t destination, uint8_t maps)
{
    describe_tile_copy(g, from, to, destination, maps);
    set_field_tile_copy_queued_maps(g, field_tile_copy_maps(g) & ALL_MAPS);
    set_field_requests(g, field_requests(g) | FIELD_REQUEST_TILE_COPY);
    return end_turn();
}
EVENT_OPERATION(queue_tile_copy, from, to, destination, maps)

// $C0:3DF4
/* Drift the layers chosen (bits 0-2) at a speed (x in the low byte, y in the
 * high) for a number of frames: clouds, water, a quake. */
static EventFlow drift_layers(GameState *g, uint16_t drift, uint8_t layers, uint8_t frames)
{
    set_field_layer_extra_speed_time(g, frames);
    for (unsigned layer = 0; layer < FIELD_LAYER_EXTRA_SPEED_COUNT; layer++) {
        if (layers & 1u << layer) {
            set_field_layer_extra_speed_x(g, layer, (uint8_t)drift);
            set_field_layer_extra_speed_y(g, layer, (uint8_t)(drift >> 8));
        }
    }
    return next_command();
}
EVENT_OPERATION(drift_layers, drift, layers, frames)

enum { SCROLL_IDLE = 0, SCROLL_RUNNING = 1 };

// $C0:3E30
/* Scroll the camera to a tile (in 16-pixel tiles), with the party out of the
 * player's hands, and wait until it gets there. */
static EventFlow scroll_camera(GameState *g, uint8_t scroll_x, uint8_t scroll_y)
{
    uint8_t state = field_screen_scroll_state(g);
    if (state == SCROLL_RUNNING)
        return stay();
    if (state != SCROLL_IDLE) {
        set_field_screen_scroll_state(g, SCROLL_IDLE);
        return next_command();
    }
    set_field_screen_scroll_target_x(g, (uint8_t)(scroll_x << 1));
    set_field_screen_scroll_target_y(g, (uint8_t)(scroll_y << 1));
    set_field_screen_scroll_state(g, SCROLL_RUNNING);
    set_field_player_control(g, 0);
    return stay();
}
EVENT_OPERATION(scroll_camera, scroll_x, scroll_y)

// $C0:6788
/* How far down the screen the objects' animations may be updated each frame
 * (the scanline the update stops at): a busy map trades animation for time. */
static EventFlow set_animation_line_limit(GameState *g, uint8_t scanline)
{
    set_field_animation_line_limit(g, (uint16_t)((field_animation_line_limit(g) & 0xFF00) | scanline));
    return next_command();
}
EVENT_OPERATION(set_animation_line_limit, scanline)

// $C0:3D97
/* Change the map's tiles now (field/layers.h): a door opens, a bridge
 * appears. Ends the turn. */
static EventFlow copy_tiles(GameState *g, uint16_t from, uint16_t to, uint16_t destination, uint8_t maps)
{
    describe_tile_copy(g, from, to, destination, maps);
    map_copy_tiles(g);
    return end_turn();
}
EVENT_OPERATION(copy_tiles, from, to, destination, maps)

/* Its guard: the rectangle and where it goes are on the map, as the game's
 * copies are; past its edges the original's block moves run on into the
 * next table, and on into the cell the command keeps its offset in. */
static bool copy_stays_on_the_map(const GameState *g, uint16_t from, uint16_t to, uint16_t destination)
{
    (void)g;
    return map_tile_copy_fits(from, to, destination);
}
EVENT_CHECK(copy_stays_on_the_map, from, to, destination)

/* ---- The credits, and BG2's halves ------------------------------------------------------------ */

// $C0:3ED6, $C0:3EDE, $C0:3EE2
/* Leave the field's main loop work on the layers, a piece a frame
 * (field/script_layers.c): BG2's left or right half cleared, or the credits
 * written into BG3 a line at a time. Ends the turn. */
static EventFlow layer_work(GameState *g, uint8_t layer_work)
{
    script_asks_for_layer_work(g, layer_work);
    return end_turn();
}
EVENT_OPERATION(layer_work, layer_work)

// $C0:3EE6
/* Put BG3, the credits' layer, at a place in 8-pixel tiles, with nothing
 * left of a drift. Ends the turn. */
static EventFlow place_bg3(GameState *g, uint8_t tile_x, uint8_t tile_y)
{
    set_field_layer_scroll_accumulator_x(g, FIELD_BG3, 0);
    set_field_layer_scroll_accumulator_y(g, FIELD_BG3, 0);
    set_field_layer_scroll_h(g, FIELD_BG3, (uint16_t)(tile_x * FIELD_ENTRY_PIXELS));
    set_field_layer_scroll_v(g, FIELD_BG3, (uint16_t)(tile_y * FIELD_ENTRY_PIXELS));
    return end_turn();
}
EVENT_OPERATION(place_bg3, tile_x, tile_y)

// $C0:3F12
/* Where in BG3's tile map the credits' lines start. */
static EventFlow place_the_credits(GameState *g, uint8_t text_column, uint8_t text_row)
{
    credits_start_at(g, text_column, text_row);
    return next_command();
}
EVENT_OPERATION(place_the_credits, text_column, text_row)

/* BG3 held for the credits: it stays where it is while the screen scrolls,
 * is drawn whole, and doesn't drift. */
enum { BG3_LAYER_BIT = 1u << FIELD_BG3, BG3_SPEED = FIELD_BG3 - 1 };

static void hold_bg3_for_the_credits(GameState *g)
{
    set_field_layer_still(g, (uint8_t)(field_layer_still(g) | BG3_LAYER_BIT));
    set_field_layer_drawn_whole(g, (uint8_t)(field_layer_drawn_whole(g) | BG3_LAYER_BIT));
    set_field_layer_speed_x(g, BG3_SPEED, 0);
    set_field_layer_speed_y(g, BG3_SPEED, 0);
}

/* BG3's tile map two screens wide, or two screens tall (field_bg3_map_size). */
enum { BG3_TWO_SCREENS_WIDE = 1, BG3_TWO_SCREENS_TALL = 2 };

/* What the credits' choice calls (field/script_layers.c): the credits'
 * letters loaded into BG3's tiles, through the unpacker, and the layers'
 * staging blanked. */
enum { READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C0:3E84
/* Choose which of the credits' texts BG3 shows next, and how BG3's tile map
 * is laid out for it: BG3 held for them, its letters loaded and the staging
 * blanked. The original keeps the command's offset on the stack over the
 * letters' loading, and reads its operand after it (the letters are unpacked
 * into bank $7F, beside the script). */
void event_cmd_choose_credits(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    BridgeMachine as = event_machine(s, false);
    m_push16(s, s->cpu.y);
    hold_bg3_for_the_credits(g);
    if (!field_run(s, 0xC03E93, FIELD_LOAD_THE_CREDITS_LETTERS_ENTRY, READS_NOTHING, field_load_the_credits_letters, as))
        return;
    TIMED_AS(g, FIELD_BLANK_THE_STAGING_ENTRY, field_blank_the_staging(g));
    s->cpu.y = m_pull16(s);
    EventCommand c = event_command_of(s, 0x29);
    set_field_bg3_map_size(g, c.two_screens_tall ? BG3_TWO_SCREENS_TALL : BG3_TWO_SCREENS_WIDE);
    set_field_credits_cursor(g, credits_text_start(g, c.credits_text));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FIRST, c.credits_text));
    ORIGINAL_EFFECT(keep_byte(g, KEPT_THIRD, c.credits_text));
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + 1)));
    event_return(s, (EventStep){ (uint16_t)(at + 2), true });
}

/* ---- The searchlight ----------------------------------------------------------------------------- */

// $C0:3F26
/* Move the searchlight's corners from where they start to where they end
 * over a number of frames (field/screen_effects.c). Ends the turn. */
static EventFlow move_searchlight(GameState *g, uint8_t frames, SearchlightMove searchlight)
{
    screen_searchlight_moves(g, &searchlight, frames);
    return end_turn();
}
EVENT_OPERATION(move_searchlight, frames, searchlight)

/* Its guard: a move that takes frames (none would divide by nothing). */
static bool searchlight_moves_over_frames(const GameState *g, uint8_t frames)
{
    (void)g;
    return frames != 0;
}
EVENT_CHECK(searchlight_moves_over_frames, frames)

/* ---- Event $FF: the screen effects, and the field's end into a scene ----------------------------- */

/* The command's number: below $90 a scene the field ends into; from $90 a
 * screen effect, started or waited on, or one of three things more. */
enum { FIRST_SCREEN_COMMAND = 0x90, SCREEN_COMMANDS = 16 };

typedef enum {
    STARTS_AN_EFFECT,       /* a hole or an oval closing, BG3 in its circle, the field's close, the oval gone */
    OPENS_A_CIRCLE,         /* a hole or an oval opening at a place, to a radius */
    DRAWS_A_CURTAIN,        /* the curtain, waited on until it has swept across */
    PLAYS_THE_RIPPLE,       /* the ripple, waited on until it has played */
    ENDS_THE_PUSH,          /* the object being pushed is done with */
    RESTARTS_THE_FIELD,     /* the field starts again from its loader ($C0:4181, the original's) */
    CALLS_PARTY_ROUTINE,    /* the menus' party routine 8 ($C0:3E7C) */
} ScreenCommandKind;

static const struct {
    ScreenCommandKind kind;
    ScreenEffect effect;    /* STARTS_AN_EFFECT, OPENS_A_CIRCLE */
    Curtain curtain;        /* DRAWS_A_CURTAIN */
    uint8_t length;
} screen_commands[SCREEN_COMMANDS] = {
    [0x0] = { OPENS_A_CIRCLE, HOLE_OPENING, 0, 5 },
    [0x1] = { STARTS_AN_EFFECT, HOLE_CLOSING, 0, 2 },
    [0x2] = { DRAWS_A_CURTAIN, 0, CURTAIN_UNCOVERS_RIGHTWARD, 2 },
    [0x3] = { DRAWS_A_CURTAIN, 0, CURTAIN_UNCOVERS_LEFTWARD, 2 },
    [0x4] = { DRAWS_A_CURTAIN, 0, CURTAIN_COVERS_RIGHTWARD, 2 },
    [0x5] = { DRAWS_A_CURTAIN, 0, CURTAIN_COVERS_LEFTWARD, 2 },
    [0x6] = { RESTARTS_THE_FIELD, 0, 0, 2 },
    [0x7] = { OPENS_A_CIRCLE, OVAL_OPENING, 0, 5 },
    [0x8] = { STARTS_AN_EFFECT, OVAL_CLOSING, 0, 2 },
    [0x9] = { STARTS_AN_EFFECT, BG3_IN_A_CIRCLE, 0, 2 },
    [0xA] = { STARTS_AN_EFFECT, BG3_BLENDED_ROUND_A_CIRCLE, 0, 2 },
    [0xB] = { STARTS_AN_EFFECT, ON_TO_THE_SCENE, 0, 2 },
    [0xC] = { STARTS_AN_EFFECT, OVAL_GONE, 0, 2 },
    [0xD] = { ENDS_THE_PUSH, 0, 0, 2 },
    [0xE] = { PLAYS_THE_RIPPLE, 0, 0, 2 },
    [0xF] = { CALLS_PARTY_ROUTINE, 0, 0, 2 },
};

static unsigned screen_command_index(uint8_t command) { return (unsigned)(command - FIRST_SCREEN_COMMAND); }

// $C0:4036-$C0:41DC
/* A screen command the field's C runs: true once it is done, false while
 * the script waits on it. */
static bool run_screen_command(GameState *g, uint8_t command, uint8_t x, uint8_t y, uint8_t radius)
{
    unsigned i = screen_command_index(command);
    switch (screen_commands[i].kind) {
    case OPENS_A_CIRCLE: screen_circle_opens(g, screen_commands[i].effect, x, y, radius); return true;
    case DRAWS_A_CURTAIN: return screen_curtain_drawn(g, screen_commands[i].curtain);
    case PLAYS_THE_RIPPLE: return screen_ripple_played(g);
    case ENDS_THE_PUSH: field_mode_flag(g, FIELD_OBJECT_PUSHED, false); return true;
    default: screen_effect_start(g, screen_commands[i].effect); return true;
    }
}

/* The original's routines the field's end calls, and what is dead where it
 * starts the scene on a fresh stack ($C0:3FD6). */
enum { WALK_THE_LEADER = 0xC0881E, PALETTE_ANIMATION = 0xFDFFF7, FRAME_WAIT = 0xC0EC60 };
enum { THE_SCENE_STARTS = 0xC03FD6, AS_THE_SCENE_STARTS = DC_A | DC_X | DC_Y | DC_N | DC_Z | DC_C };

// $C0:3FBB
/* The field ends for a scene: the picture fades out a step a frame, the
 * leader still walking and the palettes animating, and the screen goes dark.
 * The original then starts the scene on a fresh stack, reading its number
 * from where the command keeps it: the scene engine's, or from $80 the time
 * gate's tunnels (bank $FD's). */
static void end_the_field_for_a_scene(Snes *s, uint8_t scene)
{
    GameState *g = state_of(s);
    BridgeMachine as = event_machine(s, false);
    set_field_scene_to_start(g, scene);
    while (field_dims_a_step(g)) {
        if (!field_run(s, 0xC03FC3, WALK_THE_LEADER, READS_NOTHING, field_walk_the_leader, as) ||
            !field_run_long(s, 0xC03FC6, PALETTE_ANIMATION, READS_NOTHING, palette_animate, as) ||
            !field_wait_frame(s, 0xC03FCA, FRAME_WAIT, READS_NOTHING, as))
            return;
    }
    display_go_dark(s);
    m_stop_at_own(s, THE_SCENE_STARTS, AS_THE_SCENE_STARTS);
}

/* The original's dispatch of the screen commands, a JSR through its table
 * ($C0:4009), with the registers it has there: the command's index doubled
 * in A and X, the carry and the overflow its working out left clear. The
 * field's restart is the original's, from there. */
enum { SCREEN_DISPATCH = 0xC04009, AT_THE_DISPATCH = DC_AL | DC_N | DC_Z | DC_C };

static void as_the_dispatch_has_it(Snes *s, uint8_t command)
{
    uint16_t index = (uint16_t)(screen_command_index(command) * 2);
    s->cpu.a = index;
    s->cpu.x = index;
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_flag(s, CPU_FLAG_V, false);
}

// $C0:3E7C
/* The game finished: the cartridge notes it, for the load screen's New
 * Game+ (menu/party.h), through the menus' services, which the command's
 * dispatch reaches through its table; the script goes on after the
 * command. For the original's code and shadow mode, the services keep the
 * command's offset and the dispatch's X as their call's. */
enum { SCREEN_COMMAND_LENGTH = 2, MENUS_SERVICES = 0xC28004 };

static void note_the_game_finished(Snes *s)
{
    party_note_the_game_finished(m_platform(s));
    party_wear_it_shows(state_of(s));
}

void event_screen_party_routine(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t resume = (uint16_t)(event_command_offset(s) + SCREEN_COMMAND_LENGTH);
    ORIGINAL_EFFECT(event_keep_offset(g, resume));
    PartyServiceCall call = { 0 };
    ORIGINAL_EFFECT(call = party_service_called(g, (uint8_t)resume, (uint8_t)s->cpu.x));
    NATIVE_TIME_AS(s, MENUS_SERVICES, note_the_game_finished(s));
    ORIGINAL_EFFECT(event_menus_answered(s, SERVICE_GAME_FINISHED, &call));
    s->cpu.x = resume;
    m_rts(s);
}

/* ---- As C, with no machine ------------------------------------------------------------------ */

// $C0:3FBB
/* The field ends for a scene, as C: the fade out a frame at a time, the
 * leader walking and the palettes animating, and the screen dark; the scene
 * the field ends into is the program's next step (field/main_loop.h), which
 * only the machine runs yet. False where the walk is the machine's. */
static bool end_the_field_for_a_scene_as_c(GameState *g, Platform *pf, uint8_t scene)
{
    set_field_scene_to_start(g, scene);
    while (field_dims_a_step(g)) {
        bool walked = false;
        TIMED_AS(g, WALK_THE_LEADER, walked = field_the_leader_walks(g));
        if (!walked)
            return false;
        bool animated = false;
        TIMED_AS(g, PALETTE_ANIMATION, animated = palette_animated_as_c(g));
        if (!animated)
            return false;
        TIMED_AS(g, FRAME_WAIT, field_finish_the_frame(g, pf));
    }
    TIMED_AS(g, DISPLAY_BLANK_AND_STOP_ENTRY, display_blank_and_stop(pf));
    field_scene_starts(g, scene);
    return false;
}

bool event_screen_command_as_c(GameState *g, Platform *pf, uint16_t at, EventStep *step)
{
    EventCommand c = event_decode(g, pf, event_opcode(0xFF), at);
    if (c.screen_command < FIRST_SCREEN_COMMAND)
        return end_the_field_for_a_scene_as_c(g, pf, c.screen_command);
    unsigned i = screen_command_index(c.screen_command);
    if (screen_commands[i].kind == RESTARTS_THE_FIELD) {
        program_needs_the_machine(g, SCREEN_DISPATCH, "the field's restart a script asks for");
        return false;
    }
    if (screen_commands[i].kind == CALLS_PARTY_ROUTINE) {
        TIMED_AS(g, MENUS_SERVICES, (party_note_the_game_finished(pf), party_wear_it_shows(g)));
        *step = (EventStep){ (uint16_t)(at + SCREEN_COMMAND_LENGTH), false };
        return true;
    }
    bool done = run_screen_command(g, c.screen_command, (uint8_t)c.x, (uint8_t)c.y, c.radius);
    *step = (EventStep){ done ? (uint16_t)(at + screen_commands[i].length) : at, false };
    return true;
}

// $C0:3FAF
/* Event $FF. Every form ends the turn. */
void event_cmd_screen_command(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, 0xFF);
    if (c.screen_command < FIRST_SCREEN_COMMAND) {
        end_the_field_for_a_scene(s, c.screen_command);
        return;
    }
    unsigned i = screen_command_index(c.screen_command);
    ORIGINAL_EFFECT(keep_byte(g, KEPT_FOURTH, c.screen_command));
    as_the_dispatch_has_it(s, c.screen_command);
    if (screen_commands[i].kind == RESTARTS_THE_FIELD) {
        m_stop_at_own(s, SCREEN_DISPATCH, AT_THE_DISPATCH);
        return;
    }
    if (screen_commands[i].kind == CALLS_PARTY_ROUTINE) {
        if (event_call_bridge(s, SCREEN_DISPATCH, 0xC03E7C, event_screen_party_routine))
            event_return(s, (EventStep){ s->cpu.x, false });
        return;
    }
    bool done = run_screen_command(g, c.screen_command, (uint8_t)c.x, (uint8_t)c.y, c.radius);
    event_return(s, (EventStep){ done ? (uint16_t)(at + screen_commands[i].length) : at, false });
}

/* Its guard: a command the original's table has (past it the table runs on
 * into code). */
static bool a_screen_command(const GameState *g, uint8_t screen_command)
{
    (void)g;
    return screen_command < FIRST_SCREEN_COMMAND + SCREEN_COMMANDS;
}
EVENT_CHECK(a_screen_command, screen_command)

EVENT_OPCODES(event_screen_opcodes,
    { 0xE3, OPERATION(set_player_control), .control = OPERAND(1), .length = 2 },
    { 0xF0, OPERATION(fade), .brightness = OPERAND_BITS(1, 4, 0x0F), .rate = OPERAND_BITS(1, 0, 0x0F), .length = 2 },
    { 0xF2, OPERATION(wait_for_screen), .changes = CONSTANT(FIELD_FADING), .length = 1 },
    { 0xF3, OPERATION(wait_for_screen), .changes = CONSTANT(FIELD_TINTING), .length = 1 },
    { 0xF1, OPERATION(tint), .colour = OPERAND(1), .mix = OPERAND_BITS(2, 7, 0x01), .rate = OPERAND_BITS(2, 0, 0x7F),
      .length = 3 },
    { 0xF4, OPERATION(shake_screen), .shaking = OPERAND(1), .length = 2 },
    { 0xE5, OPERATION(queue_tile_copy), .from = OPERAND_WORD(1), .to = OPERAND_WORD(3),
      .destination = OPERAND_WORD(5), .maps = OPERAND(7), .length = 8 },
    { 0xE6, OPERATION(drift_layers), .drift = OPERAND_WORD(1), .layers = OPERAND(3), .frames = OPERAND(4),
      .length = 5 },
    { 0xE7, OPERATION(scroll_camera), .scroll_x = OPERAND(1), .scroll_y = OPERAND(2), .length = 3 },
    { 0x47, OPERATION(set_animation_line_limit), .scanline = OPERAND(1), .length = 2 },
    { 0xE4, OPERATION(copy_tiles), .from = OPERAND_WORD(1), .to = OPERAND_WORD(3), .destination = OPERAND_WORD(5),
      .maps = OPERAND(7), .length = 8, CHECK(copy_stays_on_the_map) },
    { 0x2A, OPERATION(layer_work), .layer_work = CONSTANT(FIELD_CLEAR_BG2_LEFT), .length = 1 },
    { 0x2B, OPERATION(layer_work), .layer_work = CONSTANT(FIELD_CLEAR_BG2_RIGHT), .length = 1 },
    { 0x32, OPERATION(layer_work), .layer_work = CONSTANT(FIELD_WRITING_CREDITS), .length = 1 },
    { 0x2C, OPERATION(place_bg3), .tile_x = OPERAND(1), .tile_y = OPERAND(2), .length = 3 },
    { 0x2F, OPERATION(place_the_credits), .text_column = OPERAND(1), .text_row = OPERAND(2), .length = 3 },
    { 0x29, .credits_text = OPERAND_BITS(1, 0, 0x7F), .two_screens_tall = OPERAND_BITS(1, 7, 0x01), .length = 2 },
    { 0xFE, OPERATION(move_searchlight), .frames = OPERAND(1), .searchlight = SEARCHLIGHT_MOVE(2), .length = 18,
      CHECK(searchlight_moves_over_frames) },
    { 0xFF, .screen_command = OPERAND(1), .x = OPERAND(2), .y = OPERAND(3), .radius = OPERAND(4), .length = 2,
      CHECK(a_screen_command) });
