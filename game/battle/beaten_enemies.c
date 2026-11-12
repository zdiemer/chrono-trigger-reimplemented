/* Battle: the enemies an action beat leave the fight.
 *
 * Once an action has been shown, the battle sees to the enemies it beat (its
 * step 6), from the last enemy place to the first. Each beaten enemy is down,
 * and its beaten mark and any drift it was making are cleared. A hidden enemy
 * simply leaves its place. One drawn on the battlefield is seen off and then
 * leaves its place, unless it is marked to stay on the field after the battle,
 * which keeps it where it is; to be seen off it takes the dying enemies'
 * palette, sprite palette 4, its own colors copied there.
 *
 * Seeing an enemy off waits first for the battlefield to be put back, where an
 * effect left that to do. Then, most often, it dissolves with a sound: over
 * 32 frames, two more of the 64 places of each 8x8 tile of its graphics go
 * clear a frame, scattered by a stride through the tile, and the graphics are
 * sent to VRAM after each pair, so that in about half a second nothing of it
 * is left; where no enemy's rule holds, its colors fade to white meanwhile. An
 * enemy whose family has it (battle_enemy_changes_the_battlefield) changes the battlefield
 * instead. And under an enemy's rule (battle_enemy_rule), but the second, an
 * enemy the rule binds dissolves without the sound, the battlefield then
 * changes to the rule's own and BG1 leaves the main screen; on a battlefield
 * the screen effects made, it dissolves with the sound as any other.
 *
 * The waits between frames and the transfers (battle/wait.c,
 * battle/uploads.c) are made where the original makes them. */
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/program.h"
#include "battle/sound.h"
#include "battle/wait.h"
#include "battle/frame.h"
#include "platform/video.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/frame_data.h"
#include "battle/screen.h"
#include "battle/setup_data.h"
#include "battle/sound.h"
#include "battle/wait.h"
#include "charge.h"
#include "vars.h"
#include "wram.h"

/* ---- A dissolve's step ------------------------------------------------------------------------ */

/* An 8x8 tile of four bit planes is 32 bytes: a row of its pixels is a byte
 * of each plane, the first two planes' bytes for the row side by side at the
 * row's place and the other two's half a tile on. A place in a tile is a row
 * and a column, the column in the place's low three bits and the row in the
 * next three. */
enum { TILE_BYTES = 32, SECOND_PLANES = 16, ROW_BYTES = 2, PLACE_COLUMN = 0x07, PLACE_ROW_SHIFT = 3, PLACE_ROW = 0x07 };
enum { TILE_BUFFER_BANK = 0x7E };

static uint16_t row_offset(uint8_t place) { return (uint16_t)(((place >> PLACE_ROW_SHIFT) & PLACE_ROW) * ROW_BYTES); }

/* The bits of a row's two plane bytes that stay as the place's column goes
 * clear: the leftmost pixel is each byte's top bit. */
static uint16_t pixel_kept(uint8_t place)
{
    unsigned bit = 0x80u >> (place & PLACE_COLUMN);
    return (uint16_t)~(bit << 8 | bit);
}

/* A pixel of a row of a tile goes clear in all four planes, so it shows
 * nothing. The row is reached from the bank's start in 24 bits, so a buffer
 * running past the bank's end reaches into the next, as the original's indexed
 * reads do. */
static void clear_pixel(GameState *g, uint16_t row_at, uint16_t kept)
{
    uint32_t at = ((uint32_t)TILE_BUFFER_BANK << 16) + row_at;
    wram_write16(g, at, wram_read16(g, at) & kept);
    wram_write16(g, at + SECOND_PLANES, wram_read16(g, at + SECOND_PLANES) & kept);
}

/* The enemy's tiles lie as its graphics kind lays them out, but for a wide
 * enemy in the fourth enemy place, whose rows the dissolve takes in the order
 * sprites.c lays a wide enemy's past the first place. */
enum { KIND_WIDE = 2, FOURTH_ENEMY_SLOT = BATTLE_FIRST_ENEMY + 3, LAYOUT_WIDE_ELSEWHERE = 5 };

static uint8_t dissolve_layout(const GameState *g, uint8_t slot)
{
    uint8_t kind = battle_object_kind(g, slot);
    return kind == KIND_WIDE && slot == FOURTH_ENEMY_SLOT ? (uint8_t)LAYOUT_WIDE_ELSEWHERE : kind;
}

// $CD:010A, $CD:0154
/* One step of an enemy's dissolve: the dissolve moves on through the places
 * of a tile by its pace's stride, and the place it comes to goes clear in
 * every tile of both runs of the enemy's graphics. A stride that is odd visits
 * all 64 places in 64 steps. The place it came to is the result. */
static uint8_t dissolve_a_step(GameState *g, uint8_t slot, uint8_t pace, uint8_t place)
{
    place = (uint8_t)(place + battle_dissolve_stride(g, pace));
    uint16_t kept = pixel_kept(place);
    uint8_t layout = dissolve_layout(g, slot);
    for (unsigned r = 0; r < BATTLE_DISSOLVE_RUNS; r++) {
        BattleTileRun run = battle_dissolve_run(g, layout, r);
        uint16_t row_at = (uint16_t)(battle_object_tile_buffer(g, slot) + run.first + row_offset(place));
        for (uint16_t tile = 0; tile < run.tiles; tile++, row_at = (uint16_t)(row_at + TILE_BYTES))
            clear_pixel(g, row_at, kept);
    }
    return place;
}

/* What the original leaves in the battle's shared work cells after a step:
 * the layout, the last run's first tile and its count of tiles run out, the
 * enemy's slot, the bits kept and the row's place in a tile, each a word. */
enum { LEFT_LAYOUT = 0x53, LEFT_RUN_FIRST = 0x55, LEFT_TILES_LEFT = 0x57, LEFT_ROW = 0x75 };

/* The first three lie over the screen effect's kept settings
 * (battle_screen_effect_settings), and a screen effect whose command gives
 * fewer than five settings takes the rest from there, so they are the game's:
 * an enemy dissolved changes the next such effect. */
static void leave_the_step_over_the_effect_settings(GameState *g, uint8_t slot)
{
    uint8_t layout = dissolve_layout(g, slot);
    screen_set_cell_word(g, LEFT_LAYOUT, layout);
    screen_set_cell_word(g, LEFT_RUN_FIRST, battle_dissolve_run(g, layout, BATTLE_DISSOLVE_RUNS - 1).first);
    screen_set_cell_word(g, LEFT_TILES_LEFT, 0);
}

/* ---- The dissolve ------------------------------------------------------------------------------ */

/* The dissolve's pace, the frames it holds still first, and its frames of two
 * steps each. */
enum { DISSOLVE_PACE = 7, STILL_FRAMES = 10, DISSOLVE_FRAMES = 32, STEPS_A_FRAME = 2, FIRST_UPLOAD = 0 };

static uint32_t tiles_of(const GameState *g, uint8_t slot)
{
    return (uint32_t)TILE_BUFFER_BANK << 16 | battle_object_tile_buffer(g, slot);
}

/* The frame work's tail ($CD:0009) as seeing an enemy off takes it. */
static bool finish_the_frame(GameState *g, Platform *pf)
{
    bool done = false;
    TIMED_AS(g, 0xCD04AA, done = battle_frame_wait_finished(g, pf));
    return done;
}

// $CD:00A0
/* An enemy dissolves: where no rule holds, its colors start to fade to white;
 * it holds still for a sixth of a second, then dissolves, each frame's two
 * steps sent to the enemy's place in VRAM and waited for. */
static bool dissolve(GameState *g, Platform *pf, uint8_t slot)
{
    if (!battle_enemy_rule(g))
        battle_fade_through_the_preset(g, battle_dissolve_fade_kind(g), battle_dissolve_fade(g));
    if (!battle_wait_some_frames(g, pf, STILL_FRAMES))
        return false;
    uint8_t place = 0;
    uint16_t vram = setup_upload_vram(g, FIRST_UPLOAD, slot);
    for (uint8_t frame = 0; frame < DISSOLVE_FRAMES; frame++) {
        for (unsigned step = 0; step < STEPS_A_FRAME; step++)
            TIMED_AS(g, 0xCD010A, place = dissolve_a_step(g, slot, DISSOLVE_PACE, place));
        leave_the_step_over_the_effect_settings(g, slot);
        vid_send_vram_and_wait(pf, PF_CALL(0xCD00F8, BATTLE_SEND), vram, PF_GAME(tiles_of(g, slot)),
                               battle_dissolve_bytes(g, battle_object_kind(g, slot)));
        if (!finish_the_frame(g, pf))
            return false;
    }
    return true;
}

/* The sound an enemy dissolves with, from where it stands. */
enum { DISSOLVE_SOUND = 0x95 };

/* ---- An enemy's battlefield ------------------------------------------------------------------- */

// $CD:0E05, with $CD:0DFA
/* An enemy leaving changes the battlefield, once the screen scripts running
 * have ended, and waits until the new battlefield's own have. */
static bool dissolve_with_a_sound(GameState *g, Platform *pf, uint8_t slot)
{
    uint8_t from = combatant_x(g, slot);
    TIMED_AS(g, BATTLE_PLAY_A_SOUND_ENTRY, battle_play_a_sound(pf, DISSOLVE_SOUND, from));
    return dissolve(g, pf, slot);
}

static bool change_the_battlefield(GameState *g, Platform *pf, uint8_t battlefield)
{
    while (battle_screen_scripts_running(g))
        if (!battle_wait_for_the_next_frame(g, pf))
            return false;
    if (!battle_load_and_run_battlefield(g, pf, battlefield))
        return false;
    while (battle_screen_scripts_running(g))
        if (!battle_wait_for_the_next_frame(g, pf) || !finish_the_frame(g, pf))
            return false;
    return true;
}

/* The battlefield an enemy's family leaves on, and a rule's. */
enum { FAMILY_BATTLEFIELD = 0x38, RULE_BATTLEFIELD = 0x40 };

/* BG1 off the main screen of the screens' line effect, from the screen's
 * settings, below the split as above it. */
enum { MAIN_SCREEN = 0, OUTSIDE_THE_WINDOW = 0, BG1 = 0x01 };

static void take_bg1_off_the_main_screen(GameState *g)
{
    set_field_line_screens_main_layers(g, OUTSIDE_THE_WINDOW, (uint8_t)(battle_screen_settings(g, MAIN_SCREEN) & ~BG1));
}

static bool dissolves(const GameState *g, uint8_t slot);

// $CD:006B
/* The enemy the rule binds dissolves (refused where its tiles' layout is
 * one the dissolve's runs don't have), and the battlefield changes to the
 * rule's own. */
static bool dissolve_under_the_rule(GameState *g, Platform *pf)
{
    uint8_t slot = battle_ended_effect_object(g);
    if (!dissolves(g, slot))
        return battle_contract_broken(g, 0xCD00A0, "an enemy whose dissolve the port doesn't run");
    bool done = false;
    TIMED_AS(g, 0xCD00A0, done = dissolve(g, pf, slot));
    if (!done)
        return false;
    TIMED_AS(g, 0xCD0E05, done = change_the_battlefield(g, pf, RULE_BATTLEFIELD));
    if (!done)
        return false;
    take_bg1_off_the_main_screen(g);
    return true;
}

/* ---- Seeing an enemy off --------------------------------------------------------------------- */

typedef enum { DISSOLVES, DISSOLVES_UNDER_THE_RULE, CHANGES_THE_BATTLEFIELD } WayOut;

/* An enemy's rule that leaves its enemies to leave as any do. */
enum { THE_SECOND_RULE = 2 };

static WayOut way_out(const GameState *g, uint8_t slot)
{
    uint8_t rule = battle_enemy_rule(g);
    if (rule != 0 && rule != THE_SECOND_RULE) {
        if (battle_background_flags(g) & BATTLE_BACKGROUND_EFFECTS_BATTLEFIELD)
            return DISSOLVES;
        if (battle_rule_applies(g, slot))
            return DISSOLVES_UNDER_THE_RULE;
    }
    if (battle_enemy_changes_the_battlefield(g, (unsigned)(slot - BATTLE_FIRST_ENEMY)))
        return CHANGES_THE_BATTLEFIELD;
    return DISSOLVES;
}

// $CD:0043 (through $CD:0024)
static bool see_off(GameState *g, Platform *pf, uint8_t slot)
{
    set_battle_ended_effect_object(g, slot);
    do
        if (!battle_wait_for_the_next_frame(g, pf) || !finish_the_frame(g, pf))
            return false;
    while (battle_background_restore_request(g));
    switch (way_out(g, slot)) {
    case DISSOLVES_UNDER_THE_RULE: return dissolve_under_the_rule(g, pf);
    case CHANGES_THE_BATTLEFIELD: return change_the_battlefield(g, pf, FAMILY_BATTLEFIELD);
    case DISSOLVES: break;
    }
    return dissolve_with_a_sound(g, pf, slot);
}

/* Seeing an enemy off is a routine of its own ($CD:0024, above), refused
 * for an enemy whose tiles' layout the dissolve's runs don't have. */
static bool see_off_an_enemy(GameState *g, Platform *pf, uint8_t slot)
{
    if (!dissolves(g, slot))
        return battle_contract_broken(g, 0xCD0024, "an enemy whose dissolve the port doesn't run");
    bool done = false;
    TIMED_AS(g, 0xCD0024, done = see_off(g, pf, slot));
    return done;
}

/* ---- The beaten enemies ---------------------------------------------------------------------- */

/* The sprite palettes follow the background's 128 colors, sixteen colors
 * each; an object's palette is kept doubled, as its sprites' attribute bits
 * have it. A dying enemy's colors but the first, the clear one, are its
 * palette's next twelve. */
enum { FIRST_SPRITE_COLOR = 128, PALETTE_COLORS = 16, DYING_PALETTE = 4, ENEMY_COLORS = 12 };

static uint8_t as_attribute(unsigned palette) { return (uint8_t)(palette * 2); }

static unsigned first_color_of(uint8_t attribute) { return FIRST_SPRITE_COLOR + 1 + attribute / 2 * PALETTE_COLORS; }

// $CF:FE4F
static void take_the_dying_palette(GameState *g, uint8_t slot)
{
    unsigned from = first_color_of(battle_object_palette(g, slot));
    unsigned to = first_color_of(as_attribute(DYING_PALETTE));
    for (unsigned i = 0; i < ENEMY_COLORS; i++) {
        uint16_t color = palette_working(g, from + i);
        set_palette_working(g, to + i, color);
        set_palette_base(g, to + i, color);
    }
    set_battle_object_palette(g, slot, as_attribute(DYING_PALETTE));
}

/* A beaten enemy is down, no longer counted beaten by the action, and stops
 * drifting. The original counts the down mark up, as a fall's pose may
 * already have set it. */
static void fall(GameState *g, uint8_t slot)
{
    unsigned enemy = (unsigned)(slot - BATTLE_FIRST_ENEMY);
    set_battle_combatant_down(g, slot, (uint8_t)(battle_combatant_down(g, slot) + 1));
    set_battle_enemy_beaten(g, enemy, 0);
    set_battle_enemy_drifting(g, enemy, 0);
}

// $CF:FE20
static bool see_the_beaten_enemies_off(GameState *g, Platform *pf)
{
    for (uint8_t slot = BATTLE_COMBATANTS - 1; slot >= BATTLE_FIRST_ENEMY; slot--) {
        unsigned enemy = (unsigned)(slot - BATTLE_FIRST_ENEMY);
        if (!battle_enemy_beaten(g, enemy))
            continue;
        if (!battle_object_hidden(g, slot)) {
            fall(g, slot);
            if (battle_object_drawn_on_the_battlefield(g, slot)) {
                if (battle_enemy_stays_on_the_field(g, enemy))
                    continue;
                take_the_dying_palette(g, slot);
                if (!see_off_an_enemy(g, pf, slot))
                    return false;
            }
        }
        set_battle_slot_present(g, slot, 0);
    }
    return true;
}

/* ---- What the port sees off ------------------------------------------------------------------- */

static bool an_enemy(uint8_t slot) { return slot >= BATTLE_FIRST_ENEMY && slot < BATTLE_COMBATANTS; }

/* An enemy whose tiles' layout the dissolve's runs have. */
static bool dissolves(const GameState *g, uint8_t slot)
{
    return an_enemy(slot) && dissolve_layout(g, slot) < BATTLE_DISSOLVE_LAYOUTS;
}

/* The beaten enemies: every beaten enemy's palette one of the eight sprite
 * palettes, as the game keeps it (doubled); the original
 * finds the colors of any other in a byte's reach, past the palettes. */
enum { SPRITE_PALETTES = 8 };

static bool palettes_as_kept(const GameState *g)
{
    for (uint8_t slot = BATTLE_FIRST_ENEMY; slot < BATTLE_COMBATANTS; slot++) {
        uint8_t attribute = battle_object_palette(g, slot);
        if (battle_enemy_beaten(g, (unsigned)(slot - BATTLE_FIRST_ENEMY))
            && (attribute % 2 || attribute >= as_attribute(SPRITE_PALETTES)))
            return false;
    }
    return true;
}

/* ---- Seeing them off ---------------------------------------------------------------------------- */

// $C1:006D
/* The battle's step 6: the beaten enemies seen off. */
bool battle_see_to_the_beaten_enemies(GameState *g, Platform *pf)
{
    if (!palettes_as_kept(g))
        return battle_contract_broken(g, 0xCFFE20, "a beaten enemy's palette past the sprites'");
    bool done = false;
    TIMED_AS(g, 0xCFFE20, done = see_the_beaten_enemies_off(g, pf));
    return done;
}
