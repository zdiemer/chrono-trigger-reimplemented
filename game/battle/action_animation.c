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
 * the background may change (the frame work's $CD:0015, $CD:002A and
 * $CD:0018, still the original's), the sprite tiles are unpacked (the
 * decompressor) and the bitmap shapes built a few a frame (battle/sprites.c),
 * between waits for the next frame with the frame step between them
 * (battle/frame_step.c). So the loading is a bridge that makes those calls
 * where the original makes them, and each kind of action is an entry of its
 * own, as the original's table of loaders has them. */
#include "battle/action_animation_data.h"
#include "battle/animation.h"
#include "battle/animation_data.h"
#include "battle/battle.h"
#include "battle/bridge.h"
#include "battle/entries.h"
#include "battle/positions.h"
#include "charge.h"
#include "original_effects.h"
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

/* The attack's search for the way leaves its two points in the first work
 * cells, as the original asks with them there. */
static void leave_the_way_asked(GameState *g, BattlePoint from, BattlePoint to)
{
    set_battle_work(g, 0, from.x);
    set_battle_work(g, 1, from.y);
    set_battle_work(g, 2, to.x);
    set_battle_work(g, 3, to.y);
}

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
        ORIGINAL_EFFECT(leave_the_way_asked(g, from, to));
        TIMED_AS(g, 0xC14A71, close = battle_find_way(g, from, to));
    }
    ORIGINAL_EFFECT(set_battle_attack_up_close(g, close));
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

/* The original walks a half's channels out of the first work cells, the
 * second half's left in the next two, and clears the battle's note of the
 * second half as it passes over the first's scripts. */
static void leave_the_channels_walked(GameState *g, uint16_t second_half_channels, bool second_half)
{
    set_battle_work(g, 0, 0);
    set_battle_work(g, 1, 0);
    set_battle_work(g, 2, (uint8_t)second_half_channels);
    set_battle_work(g, 3, (uint8_t)(second_half_channels >> 8));
    if (second_half)
        set_battle_animation_second_set(g, battle_animation_second_set(g) & 0xFF00);
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
    ORIGINAL_EFFECT(leave_the_channels_walked(g, second_channels, second_half));
}

/* ---- The animation's cells -------------------------------------------------------------- */

/* The palette animation and the effects' records are what the rest of the
 * battle reads of the animation (battle/sprites.c); the rest the original
 * keeps in cells only the loading reads. */
static void leave_the_animation(GameState *g, ActionAnimation parts, uint16_t scripts_at, uint16_t tiles_at)
{
    set_battle_animation_scripts(g, parts.scripts);
    set_battle_animation_scripts_at(g, scripts_at);
    set_battle_animation_graphics(g, parts.graphics);
    set_battle_animation_second_graphics(g, parts.second_graphics);
    set_battle_animation_background(g, parts.background);
    set_battle_animation_tiles(g, parts.tiles);
    set_battle_animation_tiles_at(g, tiles_at);
    set_battle_animation_palette(g, parts.palette);
    set_battle_animation_records(g, parts.records);
}

static void leave_no_animation(GameState *g)
{
    leave_the_animation(g, (ActionAnimation){ .background = ANIMATION_NO_BACKGROUND }, 0, 0);
    set_battle_animation_cleared(g, 0);
}

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
    ORIGINAL_EFFECT(leave_the_animation(g, parts, scripts_at, tiles_at));
    start_the_scripts(g, battle_script_set_bank(chosen.keeper), scripts_at, second_half);
    return (LoadedAnimation){ parts, tiles_at };
}

/* ---- The one-sprite effects ----------------------------------------------------------------- */

/* Effect sprite sets 32-47 hold a sprite each, at their anchor: the effects'
 * one-sprite shapes, in the battle's sprite priority. */
enum { FIRST_ONE_SPRITE_SET = 32, ONE_SPRITE_SETS = 16, ONE_SPRITE_SETS_START = 0x1800, ONE_SPRITE_SET_BYTES = 8 };

static void leave_the_sets_counted(GameState *g)
{
    set_battle_work(g, 0, ONE_SPRITE_SETS);
    set_battle_work(g, 1, 0);
}

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
    ORIGINAL_EFFECT(leave_the_sets_counted(g));
}

/* ---- Loading over the frames ---------------------------------------------------------------- */

/* Wait for the next frame ($C1:007E), from the call at `site`: the wait reads
 * none of the registers but A's high byte, which it takes along. */
enum { FRAME_WAIT_READS = DC_AL | DC_X | DC_Y | DC_FLAGS };

static bool wait_for_the_next_frame(Snes *s, uint32_t site) { return battle_call(s, site, 0xC1007E, FRAME_WAIT_READS); }

static bool frame_step(Snes *s, uint32_t site) { return battle_run(s, site, 0xC110FA, battle_frame_step); }

/* The frame work's loads take what they load in A, a byte. */
enum { FRAME_WORK_LOAD_READS = DC_X | DC_Y | DC_FLAGS };

static bool frame_work_load(Snes *s, uint32_t site, uint32_t routine, uint8_t what)
{
    s->cpu.a = what;
    return battle_call_long(s, site, routine, FRAME_WORK_LOAD_READS);
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
 * them ending the bitmap shapes' stream. The decompressor takes where from
 * and where to in its cells, and none of the registers. */
enum { DECODE_BUFFER = 0x7E2D00 };

static bool unpack_the_tiles(Snes *s, uint16_t tiles_at)
{
    GameState *g = state_of(s);
    set_decompress_source(g, (uint32_t)ANIMATION_TILES_BANK << 16 | tiles_at);
    set_decompress_dest(g, DECODE_BUFFER);
    if (!battle_call_long(s, 0xC14880, 0xC30002, DC_A | DC_X | DC_Y | DC_FLAGS | DC_DB | DC_D))
        return false;
    uint16_t end = decompress_length(g);
    set_battle_decoded_tiles(g, end, 0);
    set_battle_decoded_tiles(g, (uint16_t)(end + 1), 0);
    return true;
}

/* The bitmap shapes are built from the stream, up to four a frame, in eight
 * calls. */
static const uint32_t shape_calls[] = { 0xC1489B, 0xC1489E, 0xC148A1, 0xC148A4, 0xC148A7, 0xC148AA, 0xC148AD, 0xC148B0 };

static bool build_the_shapes(Snes *s)
{
    GameState *g = state_of(s);
    set_battle_bitmap_set(g, 0);
    set_battle_bitmap_stream(g, 0);
    for (unsigned call = 0; call < sizeof shape_calls / sizeof shape_calls[0]; call++)
        if (!battle_run(s, shape_calls[call], 0xC14943, battle_build_bitmap_shapes))
            return false;
    return true;
}

// $C1:4833
/* The effects' two graphics sets loaded, and the background changed; a
 * frame on, the effects' animations found in their records; a frame on, the
 * sprite tiles unpacked; a frame on, the bitmap shapes built, and the
 * one-sprite effects set up. */
static bool load_the_animation(Snes *s, LoadedAnimation a)
{
    GameState *g = state_of(s);
    if (!frame_work_load(s, 0xC14836, 0xCD0015, a.parts.graphics)
        || !frame_work_load(s, 0xC1483D, 0xCD002A, a.parts.second_graphics) || !wait_for_the_next_frame(s, 0xC14841))
        return false;
    if (changes_the_background(g, a.parts) && !frame_work_load(s, 0xC14857, 0xCD0018, a.parts.background))
        return false;
    if (!wait_for_the_next_frame(s, 0xC1485B) || !frame_step(s, 0xC1485E))
        return false;
    TIMED_AS(g, 0xC148EC, battle_find_effect_animations(g));
    if (!wait_for_the_next_frame(s, 0xC14864) || !frame_step(s, 0xC14867) || !unpack_the_tiles(s, a.tiles_at)
        || !wait_for_the_next_frame(s, 0xC1488D) || !frame_step(s, 0xC14890) || !build_the_shapes(s))
        return false;
    set_up_one_sprite_effects(g);
    return true;
}

/* ---- The entries ------------------------------------------------------------------------------ */

/* A missed action shows its animation's second half. */
static bool second_half_asked(const GameState *g) { return battle_action_extra(g) & ACTION_MISSED; }

/* A kind's loading, from its choice; it leaves A's high byte clear, as the
 * original's last count does. */
static void load(Snes *s, ChosenAnimation chosen)
{
    GameState *g = state_of(s);
    LoadedAnimation loaded = start_the_animation(g, chosen, second_half_asked(g));
    if (!load_the_animation(s, loaded))
        return;
    s->cpu.a = 0;
    m_rts(s);
}

void battle_load_attack_animation(Snes *s) { load(s, attack_animation(state_of(s))); }
void battle_load_tech_animation(Snes *s) { load(s, tech_animation(state_of(s))); }
void battle_load_item_animation(Snes *s) { load(s, item_animation(state_of(s))); }

// $C1:4310
/* The action's animation: none yet, and then its kind's, where it shows
 * one. */
typedef struct {
    uint32_t routine;
    void (*entry)(Snes *);
} AnimationLoader;

static const AnimationLoader loaders[SHOWING_KINDS] = {
    [SHOWS_AN_ATTACK] = { 0xC1432C, battle_load_attack_animation },
    [SHOWS_A_TECH] = { 0xC145A0, battle_load_tech_animation },
    [SHOWS_AN_ITEM] = { 0xC1475B, battle_load_item_animation },
};

static void forget_the_animation(GameState *g)
{
    set_battle_palette_anim_frames(g, 0);
    set_battle_effect_records(g, 0);
    ORIGINAL_EFFECT(leave_no_animation(g));
}

/* The original picks the loader with the kind doubled in A and X, its
 * shift's flags left (no carry: the kinds are small). */
static void leave_the_kind_doubled(Snes *s, ShowingKind kind)
{
    uint8_t doubled = (uint8_t)(kind * 2);
    m_set_a8(s, doubled);
    s->cpu.x = (uint16_t)((s->cpu.a & 0xFF00) | doubled);
    m_set_nz8(s, doubled);
    m_set_flag(s, CPU_FLAG_C, false);
}

void battle_load_action_animation(Snes *s)
{
    GameState *g = state_of(s);
    forget_the_animation(g);
    uint8_t type = battle_action_type(g);
    ShowingKind kind = type < SHOWING_KINDS ? (ShowingKind)type : SHOWS_NOTHING;
    leave_the_kind_doubled(s, kind);
    if (kind != SHOWS_NOTHING && !battle_run(s, 0xC14328, loaders[kind].routine, loaders[kind].entry))
        return;
    m_rts(s);
}

/* Whether a party member's attack is shown up close, for the original's
 * call; it leaves A's high byte clear, as the way's search does. */
void battle_attack_up_close_entry(Snes *s)
{
    GameState *g = state_of(s);
    (void)attacks_up_close(g, battle_action_actor(g));
    s->cpu.a = 0;
    m_rts(s);
}

/* The guards. The loaders index the ROM's tables with A's high byte along,
 * clear as the showing has it; the actor is a combatant, and a party member's
 * attack has a target to face. What else the original's loaders read -- the
 * showing's note of the second half, the animation's cells as the loading's
 * start leaves them -- is scaffolding here, which a native build doesn't
 * write; their fuzz runs give it as the showing does (tools/fuzz_routines.toml). */
static bool actor_fits(const GameState *g) { return battle_names_combatant(battle_action_actor(g)); }

static bool target_fits(const GameState *g)
{
    return !battle_slot_is_party(battle_action_actor(g))
           || battle_names_combatant(battle_script_subjects(g, FIRST_TARGET_PLACE));
}

bool battle_animation_loading_ready(Snes *s)
{
    vars_peek(true);
    bool fits = actor_fits(state_of(s));
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

bool battle_attack_animation_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = actor_fits(g) && target_fits(g);
    vars_peek(false);
    return battle_on_page_a_high_clear(s) && fits;
}

bool battle_attack_up_close_ready(Snes *s)
{
    vars_peek(true);
    const GameState *g = state_of(s);
    bool fits = battle_slot_is_party(battle_action_actor(g)) && target_fits(g);
    vars_peek(false);
    return battle_on_page(s) && fits;
}
