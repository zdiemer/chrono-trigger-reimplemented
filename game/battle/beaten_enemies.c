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
 * The work is C on the game's state: which way an enemy leaves, a dissolve's
 * step, the palette. The rest is the bridge around the waits between frames
 * and the transfers, which are the battle's (battle/wait.c, battle/uploads.c):
 * each is made where the original makes it, with what the original keeps on
 * the stack there, since shadow mode pairs a call with the original's by its
 * site and its stack. */
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
#include "original_effects.h"
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
enum { LEFT_LAYOUT = 0x53, LEFT_RUN_FIRST = 0x55, LEFT_TILES_LEFT = 0x57, LEFT_SLOT = 0x59, LEFT_KEPT = 0x73, LEFT_ROW = 0x75 };

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

/* The rest only the original's code and shadow mode read. */
static void leave_the_step(GameState *g, uint8_t slot, uint8_t place)
{
    screen_leave_word(g, LEFT_SLOT, slot);
    screen_leave_word(g, LEFT_KEPT, pixel_kept(place));
    screen_leave_word(g, LEFT_ROW, row_offset(place));
}

/* ---- The dissolve ------------------------------------------------------------------------------ */

/* The dissolve's pace, the frames it holds still first, and its frames of two
 * steps each. */
enum { DISSOLVE_PACE = 7, STILL_FRAMES = 10, DISSOLVE_FRAMES = 32, STEPS_A_FRAME = 2, FIRST_UPLOAD = 0 };

/* Where the original keeps a dissolve's state across its frames, for its own
 * step's routine and for shadow mode: the frames gone and the place come to. */
static void leave_the_frame(GameState *g, uint8_t slot, uint8_t frames, uint8_t place)
{
    set_battle_dissolve_frames(g, frames);
    set_battle_dissolve_place(g, place);
    leave_the_step(g, slot, place);
}

static uint32_t tiles_of(const GameState *g, uint8_t slot)
{
    return (uint32_t)TILE_BUFFER_BANK << 16 | battle_object_tile_buffer(g, slot);
}

/* Seeing an enemy off calls, each where the original makes it: the frame
 * work's waits (battle/wait.c) and its tail ($CD:0009), a sound (battle/
 * sound.c), the dissolve's steps sent to VRAM and waited for, the
 * battlefield loaded and its scripts run (battle/battlefield_change.c), the
 * dissolve and the battlefield's change as routines of their own under a
 * rule, and what the original keeps on the stack around the battlefield's
 * wait for the old scripts. */
typedef enum {
    BEATEN_STILL_FRAMES, BEATEN_STEPS_SENT, BEATEN_DISSOLVE_TAIL, BEATEN_SOUND,
    BEATEN_OLD_SCRIPTS_BEGIN, BEATEN_OLD_SCRIPTS_FRAME, BEATEN_OLD_SCRIPTS_GONE, BEATEN_LOAD_THE_BATTLEFIELD,
    BEATEN_NEW_SCRIPTS_FRAME, BEATEN_NEW_SCRIPTS_TAIL, BEATEN_RULE_DISSOLVE, BEATEN_RULE_CHANGE,
    BEATEN_SEE_OFF_FRAME, BEATEN_SEE_OFF_TAIL,
} BeatenCall;
typedef struct {
    uint8_t slot, pan, battlefield;
    uint16_t vram, bytes;
} BeatenArgs;
typedef bool (*BeatenCalls)(void *ctx, BeatenCall call, BeatenArgs args);

static bool beaten_call(BeatenCalls call, void *ctx, BeatenCall what) { return call(ctx, what, (BeatenArgs){ 0 }); }

// $CD:00A0
/* An enemy dissolves: where no rule holds, its colors start to fade to white;
 * it holds still for a sixth of a second, then dissolves, each frame's two
 * steps sent to the enemy's place in VRAM and waited for. */
static bool dissolve(GameState *g, uint8_t slot, BeatenCalls call, void *ctx)
{
    ORIGINAL_EFFECT(set_battle_dissolve_pace(g, DISSOLVE_PACE));
    if (!battle_enemy_rule(g))
        battle_fade_through_the_preset(g, battle_dissolve_fade_kind(g), battle_dissolve_fade(g));
    if (!beaten_call(call, ctx, BEATEN_STILL_FRAMES))
        return false;
    uint8_t place = 0;
    uint16_t vram = setup_upload_vram(g, FIRST_UPLOAD, slot);
    for (uint8_t frame = 0; frame < DISSOLVE_FRAMES; frame++) {
        for (unsigned step = 0; step < STEPS_A_FRAME; step++)
            TIMED_AS(g, 0xCD010A, place = dissolve_a_step(g, slot, DISSOLVE_PACE, place));
        leave_the_step_over_the_effect_settings(g, slot);
        ORIGINAL_EFFECT(leave_the_frame(g, slot, frame, place));
        BeatenArgs sent = { .slot = slot, .vram = vram, .bytes = battle_dissolve_bytes(g, battle_object_kind(g, slot)) };
        if (!call(ctx, BEATEN_STEPS_SENT, sent) || !beaten_call(call, ctx, BEATEN_DISSOLVE_TAIL))
            return false;
    }
    ORIGINAL_EFFECT(set_battle_dissolve_frames(g, DISSOLVE_FRAMES));
    return true;
}

/* The sound an enemy dissolves with, from where it stands. */
enum { DISSOLVE_SOUND = 0x95 };

// $CD:0093
static bool dissolve_with_a_sound(GameState *g, uint8_t slot, BeatenCalls call, void *ctx)
{
    if (!call(ctx, BEATEN_SOUND, (BeatenArgs){ .pan = combatant_x(g, slot) }))
        return false;
    return dissolve(g, slot, call, ctx);
}

/* ---- An enemy's battlefield ------------------------------------------------------------------- */

// $CD:0E05, with $CD:0DFA
/* An enemy leaving changes the battlefield, once the screen scripts running
 * have ended, and waits until the new battlefield's own have. */
static bool change_the_battlefield(GameState *g, uint8_t battlefield, BeatenCalls call, void *ctx)
{
    BeatenArgs args = { .battlefield = battlefield };
    if (!call(ctx, BEATEN_OLD_SCRIPTS_BEGIN, args))
        return false;
    while (battle_screen_scripts_running(g))
        if (!beaten_call(call, ctx, BEATEN_OLD_SCRIPTS_FRAME))
            return false;
    if (!call(ctx, BEATEN_OLD_SCRIPTS_GONE, args) || !call(ctx, BEATEN_LOAD_THE_BATTLEFIELD, args))
        return false;
    while (battle_screen_scripts_running(g))
        if (!beaten_call(call, ctx, BEATEN_NEW_SCRIPTS_FRAME) || !beaten_call(call, ctx, BEATEN_NEW_SCRIPTS_TAIL))
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

// $CD:006B
static bool dissolve_under_the_rule(GameState *g, BeatenCalls call, void *ctx)
{
    if (!beaten_call(call, ctx, BEATEN_RULE_DISSOLVE)
        || !call(ctx, BEATEN_RULE_CHANGE, (BeatenArgs){ .battlefield = RULE_BATTLEFIELD }))
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
static bool see_off(GameState *g, uint8_t slot, BeatenCalls call, void *ctx)
{
    set_battle_ended_effect_object(g, slot);
    do
        if (!beaten_call(call, ctx, BEATEN_SEE_OFF_FRAME) || !beaten_call(call, ctx, BEATEN_SEE_OFF_TAIL))
            return false;
    while (battle_background_restore_request(g));
    switch (way_out(g, slot)) {
    case DISSOLVES_UNDER_THE_RULE: return dissolve_under_the_rule(g, call, ctx);
    case CHANGES_THE_BATTLEFIELD: return change_the_battlefield(g, FAMILY_BATTLEFIELD, call, ctx);
    case DISSOLVES: break;
    }
    return dissolve_with_a_sound(g, slot, call, ctx);
}

/* On the machine: the waits for the old scripts are a subroutine of the
 * change's own, called with the battlefield kept on the stack; the
 * battlefield is loaded from A as its TAX takes it, high byte and all. What
 * the original's calls of the dissolve and of the battlefield's change don't
 * read: the index registers and the flags, and of the dissolve's A the low
 * byte. */
enum { DISSOLVE_READS = DC_AL | DC_X | DC_Y | DC_FLAGS, CHANGE_READS = DC_X | DC_Y | DC_FLAGS };

/* The frame work's tail's calls, by where the original makes them. */
static uint32_t tail_site(BeatenCall call)
{
    return call == BEATEN_DISSOLVE_TAIL ? 0xCD00FB : call == BEATEN_NEW_SCRIPTS_TAIL ? 0xCD0E1C : 0xCD0049;
}

static bool beaten_on_the_machine(void *ctx, BeatenCall call, BeatenArgs a)
{
    Snes *s = ctx;
    GameState *g = state_of(s);
    switch (call) {
    case BEATEN_STILL_FRAMES:
        return battle_wait_frames(s, 0xCD00C1, BATTLE_SOME_FRAMES_WAIT, BATTLE_COUNTING_WAIT_READS, STILL_FRAMES);
    case BEATEN_STEPS_SENT: return battle_send_and_wait(s, 0xCD00F8, tiles_of(g, a.slot), a.vram, a.bytes);
    case BEATEN_DISSOLVE_TAIL:
    case BEATEN_NEW_SCRIPTS_TAIL:
    case BEATEN_SEE_OFF_TAIL: return battle_run_long(s, tail_site(call), 0xCD04AA, battle_frame_wait_tail);
    case BEATEN_SOUND: return battle_play_a_sound_from(s, 0xCD009D, DISSOLVE_SOUND, a.pan, battle_carrying(BATTLE_CARRY_AS_IT_IS));
    case BEATEN_OLD_SCRIPTS_BEGIN:
        battle_keep(s, battle_keeping_byte(a.battlefield, BATTLE_CARRY_AS_IT_IS));
        battle_push_the_return(s, 0xCD0E06);
        return true;
    case BEATEN_OLD_SCRIPTS_GONE:
        battle_pull_the_return(s);
        battle_stop_keeping(s, battle_keeping_byte(a.battlefield, BATTLE_CARRY_AS_IT_IS));
        return true;
    case BEATEN_LOAD_THE_BATTLEFIELD:
        return battle_load_and_run_battlefield_on_the_machine(s, 0xCD0E0B, (uint16_t)((s->cpu.a & 0xFF00) | a.battlefield));
    case BEATEN_RULE_DISSOLVE: return battle_run_long_dc(s, 0xCD006F, 0xCD00A0, DISSOLVE_READS, battle_dissolve_an_enemy);
    case BEATEN_RULE_CHANGE:
        m_set_a8(s, a.battlefield);
        return battle_run_long_dc(s, 0xCD0075, 0xCD0E05, CHANGE_READS, battle_change_battlefield_as_an_enemy_leaves);
    case BEATEN_OLD_SCRIPTS_FRAME: return battle_wait_for_the_next_frame_from(s, 0xCD0DFF);
    case BEATEN_NEW_SCRIPTS_FRAME: return battle_wait_for_the_next_frame_from(s, 0xCD0E19);
    default: return battle_wait_for_the_next_frame_from(s, 0xCD0046);
    }
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

/* What the original leaves in its cells as it looks at an enemy drawn on the
 * battlefield: where its placement's record is, in quarters of a record's
 * bytes; and once it has copied its colors, as it calls to see the enemy
 * off, the copy's count run out and the enemy's slot. */
enum { LEFT_RECORD_QUARTERS = 0, LEFT_COPY_COUNT = 0, LEFT_ENEMY = 2, RECORD_QUARTERS = 4 };

static void leave_the_copy(GameState *g, uint8_t slot)
{
    set_battle_work(g, LEFT_COPY_COUNT, 0);
    set_battle_work(g, LEFT_ENEMY, slot);
    set_battle_work(g, LEFT_ENEMY + 1, 0);
}

/* Seeing an enemy off is a routine of its own ($CD:0024, above), called
 * with the enemy. */
typedef bool (*SeeOff)(void *ctx, uint8_t slot);

/* After the search, where the last enemy it found drawn stayed on the field
 * (none seen off after it), the record's place it worked out stays in its
 * cell, and its slot in A, which leaves A's high byte clear for the step's
 * asker. Otherwise the last call to see an enemy off left them. */
typedef struct {
    bool any;
    uint8_t slot;
} KeptEnemy;

static void leave_the_search(GameState *g, KeptEnemy last_kept)
{
    if (last_kept.any)
        ORIGINAL_EFFECT(set_battle_work(g, LEFT_RECORD_QUARTERS, (uint8_t)((last_kept.slot - BATTLE_FIRST_ENEMY) * RECORD_QUARTERS)));
}

// $CF:FE20
static bool see_the_beaten_enemies_off(GameState *g, SeeOff see_off_an_enemy, void *ctx, KeptEnemy *kept)
{
    KeptEnemy last_kept = { false, 0 };
    for (uint8_t slot = BATTLE_COMBATANTS - 1; slot >= BATTLE_FIRST_ENEMY; slot--) {
        unsigned enemy = (unsigned)(slot - BATTLE_FIRST_ENEMY);
        if (!battle_enemy_beaten(g, enemy))
            continue;
        if (!battle_object_hidden(g, slot)) {
            fall(g, slot);
            if (battle_object_drawn_on_the_battlefield(g, slot)) {
                if (battle_enemy_stays_on_the_field(g, enemy)) {
                    last_kept = (KeptEnemy){ true, slot };
                    continue;
                }
                last_kept.any = false;
                take_the_dying_palette(g, slot);
                ORIGINAL_EFFECT(leave_the_copy(g, slot));
                if (!see_off_an_enemy(ctx, slot))
                    return false;
            }
        }
        set_battle_slot_present(g, slot, 0);
    }
    leave_the_search(g, last_kept);
    *kept = last_kept;
    return true;
}

/* On the machine: what the original's call to see an enemy off doesn't
 * read: the index registers and the flags. It keeps the enemy's slot on the
 * stack across it, and calls with the slot in A. */
enum { SEEING_OFF_READS = DC_X | DC_Y | DC_FLAGS };

static bool see_off_on_the_machine(void *ctx, uint8_t slot)
{
    Snes *s = ctx;
    s->cpu.a = slot;
    return battle_run_long_dc_under(s, 0xCFFE76, 0xCD0024, SEEING_OFF_READS, battle_see_off_an_enemy,
                                    battle_keeping_word(slot, BATTLE_CARRY_AS_IT_IS));
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* The search ends past the first enemy place, with the carry its last
 * compare leaves set, which the step's asker reads. */
/* Where the last enemy found drawn stayed on the field (none seen off after
 * it), its slot is left in A, which leaves A's high byte clear for the step's
 * asker; otherwise the last call to see an enemy off left it. */
void battle_see_off_the_beaten_enemies(Snes *s)
{
    KeptEnemy last_kept;
    if (!see_the_beaten_enemies_off(state_of(s), see_off_on_the_machine, s, &last_kept))
        return;
    if (last_kept.any)
        s->cpu.a = last_kept.slot;
    m_set_flag(s, CPU_FLAG_C, true);
    m_rtl(s);
}

// $C1:006D
/* The battle's step 6: the work, from its long call. */
void battle_see_to_the_beaten_enemies_entry(Snes *s)
{
    if (!battle_run_long(s, 0xC1006D, 0xCFFE20, battle_see_off_the_beaten_enemies))
        return;
    m_rts(s);
}

/* The enemy comes in A. */
void battle_see_off_an_enemy(Snes *s)
{
    if (see_off(state_of(s), (uint8_t)s->cpu.a, beaten_on_the_machine, s))
        m_rtl(s);
}

/* The enemy is the one being seen off. */
void battle_dissolve_an_enemy(Snes *s)
{
    if (dissolve(state_of(s), battle_ended_effect_object(state_of(s)), beaten_on_the_machine, s))
        m_rtl(s);
}

/* The step takes its pace and the place the dissolve came to from the cells
 * the original's dissolve keeps them in, and puts the place it comes to back
 * there; it leaves A clear, high byte and all, for its caller. */
void battle_dissolve_a_step(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t slot = battle_ended_effect_object(g);
    uint8_t place = dissolve_a_step(g, slot, battle_dissolve_pace(g), battle_dissolve_place(g));
    set_battle_dissolve_place(g, place);
    leave_the_step_over_the_effect_settings(g, slot);
    ORIGINAL_EFFECT(leave_the_step(g, slot, place));
    s->cpu.a = 0;
    m_rts(s);
}

/* The battlefield comes in A. */
void battle_change_battlefield_as_an_enemy_leaves(Snes *s)
{
    if (change_the_battlefield(state_of(s), (uint8_t)s->cpu.a, beaten_on_the_machine, s))
        m_rtl(s);
}

/* ---- The guards ------------------------------------------------------------------------------- */

static bool an_enemy(uint8_t slot) { return slot >= BATTLE_FIRST_ENEMY && slot < BATTLE_COMBATANTS; }

/* An enemy whose tiles' layout the dissolve's runs have. */
static bool dissolves(const GameState *g, uint8_t slot)
{
    return an_enemy(slot) && dissolve_layout(g, slot) < BATTLE_DISSOLVE_LAYOUTS;
}

static bool dissolving_on_page(Snes *s)
{
    vars_peek(true);
    bool ok = dissolves(state_of(s), battle_ended_effect_object(state_of(s)));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ok;
}

/* The dissolve and its step: on the battle's page with A's high byte clear
 * (the original indexes by the enemy with it), for an enemy the dissolve has
 * a layout for. */
bool battle_dissolving(Snes *s) { return dissolving_on_page(s); }

/* Seeing an enemy off: the same, for the enemy in A. */
bool battle_seeing_off(Snes *s)
{
    vars_peek(true);
    bool ok = dissolves(state_of(s), (uint8_t)s->cpu.a);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && ok;
}

/* The beaten enemies: on the battle's page, every beaten enemy's palette one
 * of the eight sprite palettes, as the game keeps it (doubled); the original
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

bool battle_seeing_off_the_beaten(Snes *s)
{
    vars_peek(true);
    bool ok = palettes_as_kept(state_of(s));
    vars_peek(false);
    return battle_on_page(s) && ok;
}

/* ---- As C ------------------------------------------------------------------------------------ */

typedef struct {
    GameState *game;
    Platform *pf;
} BeatenEnemiesContext;

/* Each with its guard's state half: the dissolve's, an enemy whose tiles'
 * layout the dissolve's runs have. */
static bool see_to_a_part(void *ctx, BeatenCall call, BeatenArgs a)
{
    BeatenEnemiesContext *c = ctx;
    GameState *g = c->game;
    bool done = false;
    switch (call) {
    case BEATEN_STILL_FRAMES: TIMED_AS(g, BATTLE_SOME_FRAMES_WAIT, done = battle_wait_some_frames(g, c->pf, STILL_FRAMES)); return done;
    case BEATEN_STEPS_SENT:
        vid_send_vram_and_wait(c->pf, PF_CALL_DC(0xCD00F8, BATTLE_SEND, BATTLE_SEND_READS), a.vram,
                               PF_GAME(tiles_of(g, a.slot)), a.bytes);
        return true;
    case BEATEN_DISSOLVE_TAIL:
    case BEATEN_NEW_SCRIPTS_TAIL:
    case BEATEN_SEE_OFF_TAIL: TIMED_AS(g, 0xCD04AA, done = battle_frame_wait_finished(g, c->pf)); return done;
    case BEATEN_SOUND: TIMED_AS(g, BATTLE_PLAY_A_SOUND_ENTRY, battle_play_a_sound(c->pf, DISSOLVE_SOUND, a.pan)); return true;
    case BEATEN_OLD_SCRIPTS_BEGIN:
    case BEATEN_OLD_SCRIPTS_GONE: return true;
    case BEATEN_LOAD_THE_BATTLEFIELD: return battle_load_and_run_battlefield(g, c->pf, a.battlefield);
    case BEATEN_RULE_DISSOLVE:
        if (!dissolves(g, battle_ended_effect_object(g)))
            return battle_needs_the_machine(g, 0xCD00A0, "an enemy whose dissolve the port doesn't run");
        TIMED_AS(g, 0xCD00A0, done = dissolve(g, battle_ended_effect_object(g), see_to_a_part, ctx));
        return done;
    case BEATEN_RULE_CHANGE: TIMED_AS(g, 0xCD0E05, done = change_the_battlefield(g, a.battlefield, see_to_a_part, ctx)); return done;
    default: TIMED_AS(g, BATTLE_NEXT_FRAME_WAIT, done = battle_wait_for_the_next_frame(g, c->pf)); return done;
    }
}

static bool see_off_one(void *ctx, uint8_t slot)
{
    BeatenEnemiesContext *c = ctx;
    if (!dissolves(c->game, slot))
        return battle_needs_the_machine(c->game, 0xCD0024, "an enemy whose dissolve the port doesn't run");
    bool done = false;
    TIMED_AS(c->game, 0xCD0024, done = see_off(c->game, slot, see_to_a_part, ctx));
    return done;
}

// $C1:006D
/* The battle's step 6: the beaten enemies seen off. */
bool battle_see_to_the_beaten_enemies(GameState *g, Platform *pf)
{
    if (!palettes_as_kept(g))
        return battle_needs_the_machine(g, 0xCFFE20, "a beaten enemy's palette past the sprites'");
    BeatenEnemiesContext c = { g, pf };
    KeptEnemy last_kept;
    bool done = false;
    TIMED_AS(g, 0xCFFE20, done = see_the_beaten_enemies_off(g, see_off_one, &c, &last_kept));
    return done;
}
