/* Battle: an action's animation, chosen and loaded.
 *
 * As the battle starts showing an action (battle/action_showing.c) it loads
 * the action's animation (battle/action_animation_data.h says what one is
 * made of): the scripts its channels run, the graphics its effects draw
 * with, their colours' animation and the battlefield it may change to.
 * Which animation depends on the action: a party member's attack is its
 * weapon's, shown from where the member stands, up close where it gets to its
 * target, or as a critical hit; an enemy's attack is one of the enemy's two;
 * a tech or an item has its own.
 *
 * Choosing the animation and starting its scripts is C on the game's state.
 * Loading the rest spans frames: the effects' graphics sets go to VRAM and
 * the background may change (battle/battlefield_change.c), the sprite tiles are unpacked (the
 * decompressor) and the bitmap shapes built a few a frame (battle/sprites.c),
 * between waits for the next frame with the frame step between them
 * (battle/frame_step.c). The loading makes those calls (LoadCall) where the
 * original makes them, and each kind of action is loaded by a routine of its
 * own, as the original's table of loaders has them. */
#include "battle/action_animation_data.h"
#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/frame.h"
#include "battle/program.h"
#include "battle/steps.h"
#include "battle/wait.h"
#include "data/decompress.h"
#include "battle/positions.h"
#include "charge.h"
#include "port/vars_ram.h"
#include "vars.h"

/* What the action shows (battle_action_type): nothing, an attack, a tech or
 * an item. */
typedef enum { SHOWS_NOTHING, SHOWS_AN_ATTACK, SHOWS_A_TECH, SHOWS_AN_ITEM, SHOWING_KINDS } ShowingKind;

/* Where battle_script_subjects lists the action's first target. */
enum { FIRST_TARGET_PLACE = 3 };

/* An animation and who keeps its parts. */
typedef struct {
    ActionAnimation parts;
    AnimationKeeper keeper;
} ChosenAnimation;

/* ---- A party member's attack: up close or not -------------------------------------------- */

// $C1:49FF
/* A party member's attack is shown up close where the member gets to its
 * first target: where it finds the way there across the battlefield's grid,
 * which a member who shoots looks for only if the target is within its long
 * reach. The member turns to face the target either way. */
static bool attacks_up_close(GameState *g, uint8_t member)
{
    uint8_t target = battle_script_subjects(g, FIRST_TARGET_PLACE);
    set_battle_target_searcher(g, member);
    set_battle_search_other(g, target);
    BattlePoint from = { combatant_x(g, member), combatant_y(g, member) };
    BattlePoint to = { combatant_x(g, target), combatant_y(g, target) };
    bool within;
    TIMED_AS(g, 0xC12AE3, within = battle_within_reach(g, member, target, REACH_LONG));
    uint8_t direction;
    TIMED_AS(g, 0xC10222, direction = battle_direction(g, from, to));
    set_battle_object_facing(g, member, battle_facing_of_direction(g, direction));
    bool close = within;
    if (battle_character_walks_up(g, party_members(g, member)) || within) {
        TIMED_AS(g, 0xC14A71, close = battle_find_way(g, from, to));
    }
    return close;
}

/* ---- Choosing the animation ----------------------------------------------------------------- */

// $C1:432C, $C1:4497
/* An enemy's attack is the first of its two, or the second where the action
 * names it. A party member's is its weapon's, whose view the attack picks: a
 * critical hit, up close, or from where the member stands; the battlefield's
 * grid is marked with the combatants first, for the way to its target. */
static ChosenAnimation attack_animation(GameState *g)
{
    uint8_t actor = battle_action_actor(g);
    if (!battle_slot_is_party(actor)) {
        uint16_t enemy = battle_enemy_numbers(g, actor - BATTLE_FIRST_ENEMY);
        return (ChosenAnimation){ battle_enemy_attack_animation(g, enemy, battle_action_what(g) != 0),
                                  ANIMATIONS_OF_ENEMY_ATTACKS };
    }
    TIMED_AS(g, 0xCCF110, battle_mark_combatants_on_grid(g));
    bool close;
    TIMED_AS(g, 0xC149FF, close = attacks_up_close(g, actor));
    uint8_t weapon = battle_member_carried_weapon(g, actor);
    set_battle_acting_weapon(g, weapon);
    AttackView view = (battle_action_flags(g) & ACTION_CRITICAL_HIT) ? ATTACK_CRITICAL
                      : close                                         ? ATTACK_UP_CLOSE
                                                                      : ATTACK_FROM_AFAR;
    return (ChosenAnimation){ battle_weapon_animation(g, weapon, view, party_members(g, actor)),
                              ANIMATIONS_OF_PARTY_ATTACKS };
}

// $C1:45A0, $C1:4682
static ChosenAnimation tech_animation(GameState *g)
{
    bool enemy = !battle_slot_is_party(battle_action_actor(g));
    return (ChosenAnimation){ battle_tech_animation(g, enemy, battle_action_what(g)),
                              enemy ? ANIMATIONS_OF_ENEMY_TECHS : ANIMATIONS_OF_PARTY_TECHS };
}

// $C1:475B
static ChosenAnimation item_animation(GameState *g)
{
    uint8_t item = (uint8_t)(battle_action_what(g) - BATTLE_FIRST_ITEM_NUMBER);
    return (ChosenAnimation){ battle_item_animation(g, item), ANIMATIONS_OF_ITEMS };
}

/* ---- Starting the scripts ----------------------------------------------------------------- */

/* A channel set running its script. The original sets the running flag and
 * the pointer a word each, which clears the next channel's flag (set again if
 * it runs too; past the last, the pad before the first pointer) and the pad
 * after the pointer. */
static void start_channel(GameState *g, unsigned channel, uint8_t bank, uint16_t script)
{
    set_battle_script_running(g, channel, 1);
    if (channel + 1 < BATTLE_SCRIPT_RUNNING_COUNT)
        set_battle_script_running(g, channel + 1, 0);
    else
        set_battle_script_pointer_pads(g, 0, 0);
    set_battle_script_pointers(g, channel, (uint32_t)bank << 16 | script);
    set_battle_script_pointer_pads(g, channel + 1, 0);
}

static bool runs_channel(uint16_t channels, unsigned channel) { return channels & (0x8000u >> channel); }

static uint16_t channels_counted(uint16_t channels)
{
    uint16_t count = 0;
    for (unsigned channel = 0; channel < SCRIPT_SET_CHANNELS; channel++)
        count = (uint16_t)(count + runs_channel(channels, channel));
    return count;
}

// $C1:4455
/* The script set's channels start: those its first half marks, each with
 * its script in turn; or for the second half, those it marks, with the
 * scripts after the first half's. */
static void start_the_scripts(GameState *g, uint8_t bank, uint16_t set, bool second_half)
{
    uint16_t channels = battle_script_set_channels(g, bank, set, false);
    uint16_t second_channels = battle_script_set_channels(g, bank, set, true);
    uint16_t script = (uint16_t)(set + SCRIPT_SET_HEADER_BYTES);
    if (second_half) {
        script = (uint16_t)(script + 2 * channels_counted(channels));
        channels = second_channels;
    }
    for (unsigned channel = 0; channel < SCRIPT_SET_CHANNELS; channel++) {
        if (!runs_channel(channels, channel))
            continue;
        start_channel(g, channel, bank, battle_script_set_script(g, bank, script));
        script = (uint16_t)(script + 2);
    }
}

/* ---- The animation's cells -------------------------------------------------------------- */

/* What the loading goes on with over the frames: the parts and where the
 * sprite tiles are. */
typedef struct {
    ActionAnimation parts;
    uint16_t tiles_at;
} LoadedAnimation;

static LoadedAnimation start_the_animation(GameState *g, ChosenAnimation chosen, bool second_half)
{
    ActionAnimation parts = chosen.parts;
    uint16_t tiles_at = battle_animation_tiles_place(g, chosen.keeper, parts.tiles);
    uint16_t scripts_at = battle_script_set_at(g, chosen.keeper, parts.scripts);
    set_battle_palette_anim_frames(g, battle_animation_palette_frames(g, chosen.keeper, parts.palette));
    set_battle_effect_records(g, battle_animation_records_place(g, chosen.keeper, parts.records));
    start_the_scripts(g, battle_script_set_bank(chosen.keeper), scripts_at, second_half);
    return (LoadedAnimation){ parts, tiles_at };
}

/* ---- The one-sprite effects ----------------------------------------------------------------- */

/* Effect sprite sets 32-47 hold a sprite each, at their anchor: the effects'
 * one-sprite shapes, in the battle's sprite priority. */
enum { FIRST_ONE_SPRITE_SET = 32, ONE_SPRITE_SETS = 16, ONE_SPRITE_SETS_START = 0x1800, ONE_SPRITE_SET_BYTES = 8 };

static void set_up_one_sprite_effects(GameState *g)
{
    for (unsigned shape = 0; shape < ONE_SPRITE_SETS; shape++) {
        uint16_t at = (uint16_t)(ONE_SPRITE_SETS_START + shape * ONE_SPRITE_SET_BYTES);
        set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_X, 0);
        set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_Y, 0);
        set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_TILE, battle_one_sprite_tile(g, shape));
        set_battle_effect_sprite_data(g, at + EFFECT_SPRITE_ATTRIBUTES,
                                      battle_one_sprite_attributes(g, shape) | battle_sprite_attributes(g));
        set_battle_bitmap_counts(g, FIRST_ONE_SPRITE_SET + shape, 1);
    }
}

/* ---- Loading over the frames ---------------------------------------------------------------- */

/* One of the effects' two graphics sets, the frame work's loads
 * (battle/battlefield_change.c). Between its steps the loading waits for the
 * next frame ($C1:007E) and takes the frame step ($C1:10FA,
 * battle/frame_step.c). */
static bool load_graphics_set(GameState *g, Platform *pf, uint8_t set, bool second)
{
    bool done = false;
    TIMED_AS(g, second ? 0xCD002A : 0xCD0015, done = battle_load_effect_tile_rows(g, pf, set, second));
    return done;
}

/* The battlefield changes where the animation has one to change to: always
 * for the one action that has it, otherwise only where the action's extra
 * byte is clear. */
enum { ACTION_THAT_ALWAYS_CHANGES_THE_BACKGROUND = 0x37 };

static bool changes_the_background(const GameState *g, ActionAnimation parts)
{
    if (parts.background == ANIMATION_NO_BACKGROUND)
        return false;
    return battle_action_what(g) == ACTION_THAT_ALWAYS_CHANGES_THE_BACKGROUND || battle_action_extra(g) == 0;
}

/* The sprite tiles unpacked into the decode buffer, two zero bytes after
 * them ending the bitmap shapes' stream; the decompressor takes where from
 * and where to in its cells. */
enum { DECODE_BUFFER = 0x7E2D00 };

static void unpack_the_tiles(GameState *g, uint16_t tiles_at)
{
    set_decompress_source(g, battle_animation_tiles_stream(tiles_at));
    set_decompress_dest(g, DECODE_BUFFER);
    (void)unpack_stream(g);
    uint16_t end = decompress_length(g);
    set_battle_decoded_tiles(g, end, 0);
    set_battle_decoded_tiles(g, (uint16_t)(end + 1), 0);
}

/* The bitmap shapes are built from the stream, up to four a frame, in eight
 * calls ($C1:4943, battle/sprites.c). */
enum { SHAPE_BUILDS = 8 };

static bool build_the_shapes(GameState *g, Platform *pf)
{
    set_battle_bitmap_set(g, 0);
    set_battle_bitmap_stream(g, 0);
    for (unsigned build = 0; build < SHAPE_BUILDS; build++) {
        bool done = false;
        TIMED_AS(g, 0xC14943, done = battle_build_bitmap_shapes(g, pf));
        if (!done)
            return false;
    }
    return true;
}

// $C1:4833
/* The effects' two graphics sets loaded, and the background changed; a
 * frame on, the effects' animations found in their records; a frame on, the
 * sprite tiles unpacked; a frame on, the bitmap shapes built, and the
 * one-sprite effects set up. */
static bool load_the_animation(GameState *g, Platform *pf, LoadedAnimation a)
{
    if (!load_graphics_set(g, pf, a.parts.graphics, false) || !load_graphics_set(g, pf, a.parts.second_graphics, true))
        return false;
    battle_wait_for_frame(g, pf);
    if (changes_the_background(g, a.parts)) {
        bool changed = false;
        TIMED_AS(g, 0xCD0018, changed = battle_change_battlefield(g, pf, a.parts.background));
        if (!changed)
            return false;
    }
    battle_wait_for_frame(g, pf);
    if (!battle_frame_stepped(g, pf))
        return false;
    TIMED_AS(g, 0xC148EC, battle_find_effect_animations(g));
    battle_wait_for_frame(g, pf);
    if (!battle_frame_stepped(g, pf))
        return false;
    unpack_the_tiles(g, a.tiles_at);
    battle_wait_for_frame(g, pf);
    if (!battle_frame_stepped(g, pf) || !build_the_shapes(g, pf))
        return false;
    set_up_one_sprite_effects(g);
    return true;
}

/* A missed action shows its animation's second half. */
static bool second_half_asked(const GameState *g) { return battle_action_extra(g) & ACTION_MISSED; }

// $C1:4310
/* The action's animation: none yet, and then its kind's, where it shows
 * one (each kind's loading a routine of its own). */
static void forget_the_animation(GameState *g)
{
    set_battle_palette_anim_frames(g, 0);
    set_battle_effect_records(g, 0);
}

static ShowingKind showing_kind(const GameState *g)
{
    uint8_t type = battle_action_type(g);
    return type < SHOWING_KINDS ? (ShowingKind)type : SHOWS_NOTHING;
}

static ChosenAnimation chosen_for(GameState *g, ShowingKind kind)
{
    switch (kind) {
    case SHOWS_AN_ATTACK: return attack_animation(g);
    case SHOWS_A_TECH: return tech_animation(g);
    default: return item_animation(g);
    }
}

/* Each kind's loading, as the original's table of loaders has them. */

static const struct { uint32_t routine; } loaders[SHOWING_KINDS] = {
    [SHOWS_AN_ATTACK] = { 0xC1432C },
    [SHOWS_A_TECH] = { 0xC145A0 },
    [SHOWS_AN_ITEM] = { 0xC1475B },
};

/* What the loading takes: the actor is a combatant, and a party member's
 * attack has a target to face. */
static bool actor_fits(const GameState *g) { return battle_names_combatant(battle_action_actor(g)); }

static bool target_fits(const GameState *g)
{
    return !battle_slot_is_party(battle_action_actor(g))
           || battle_names_combatant(battle_script_subjects(g, FIRST_TARGET_PLACE));
}

/* Each kind's loading, refused where the actor isn't a combatant or a party
 * member's attack has no target to face. */
static bool load_by_kind(GameState *g, Platform *pf, ShowingKind kind)
{
    if (!PORT_CHECK(actor_fits(g)) || (kind == SHOWS_AN_ATTACK && !PORT_CHECK(target_fits(g))))
        return battle_contract_broken(g, loaders[kind].routine, "an animation the port doesn't load");
    bool done = false;
    TIMED_AS(g, loaders[kind].routine,
             done = load_the_animation(g, pf, start_the_animation(g, chosen_for(g, kind), second_half_asked(g))));
    return done;
}

bool battle_load_action_animation(GameState *g, Platform *pf)
{
    forget_the_animation(g);
    ShowingKind showing = showing_kind(g);
    return showing == SHOWS_NOTHING || load_by_kind(g, pf, showing);
}
