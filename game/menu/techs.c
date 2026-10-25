/* The techs as the menus list them.
 *
 * A tech belongs to a group whose members do it together: each character's
 * own eight, a double tech's group of three for each pair of characters, and
 * each triple tech alone in its group (menu_data.h). As the menus open they
 * note, for every group whose members the party has, which of its techs the
 * party has learned (menu_tech_groups_listed, a bit a tech), and mark the
 * techs that heal and so can be used from the menus, and each member's next
 * single tech, the one still being learned (menu_tech_marks); they note each
 * character's part in each double and triple tech (menu_tech_parts), and
 * which of the triple techs the rocks give the party could do
 * (menu_rock_techs_ready, worked out again whenever the menus' services
 * change the party).
 *
 * A member's list of one kind is then every group of that kind the member is
 * one of, in the groups' order, and each group's techs learned, in theirs.
 * What a tech costs is the member's part's cost (a single tech is all one
 * part), which two accessories halve and quarter. */
#include "menu/techs.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "menu/opening.h"
#include "menu/text.h"
#include "original_effects.h"
#include "port/machine.h"
#include "ram.h"
#include "vars.h"

/* ---- A member's techs of a kind ---------------------------------------------------------------- */

/* The list's places the listing clears before it fills them (the most a
 * member can have of one kind, with the end, fits). */
enum { CLEARED_PLACES = 0x18, NO_GROUP = 0xFF, FIRST_TECH_BIT = 0x80 };

/* What the original's search for the member's groups leaves in its cells:
 * the character's bit, and its count of groups left, run out. */
static void leave_groups_found(GameState *g, uint8_t character_bit)
{
    set_menu_scratch_tech_character_bit(g, character_bit);
    set_menu_scratch_tech_groups_left(g, 0);
}

// $C2:BCCD
/* The groups of the kind the character is one of, in their order; their
 * count. The list is left where the original keeps it, $FF after the last. */
static unsigned groups_of(GameState *g, uint8_t character, MenuTechKind kind, uint8_t *groups)
{
    uint8_t bit = menu_character_bit(g, character);
    uint8_t group = menu_tech_kind_first_group(g, kind), left = menu_tech_kind_groups(g, kind);
    unsigned found = 0;
    do {
        if (menu_tech_group_members(g, group) & bit)
            groups[found++] = group;
        group++;
    } while (--left != 0);
    for (unsigned i = 0; i < found; i++)
        ORIGINAL_EFFECT(set_menu_tech_member_groups(g, i, groups[i]));
    ORIGINAL_EFFECT(set_menu_tech_member_groups(g, found, NO_GROUP));
    ORIGINAL_EFFECT(leave_groups_found(g, bit));
    return found;
}

/* What the original's walk through the groups' techs leaves in its cells:
 * where in the groups' list it stopped, the last group's learned bits as far
 * as it shifted them, the last group's end and the tech it got to. */
typedef struct {
    uint8_t groups_walked, bits_left, group_end, next_tech;
} TechWalk;

static void leave_techs_walked(GameState *g, TechWalk w, bool any)
{
    set_menu_scratch_tech_group_at(g, w.groups_walked);
    if (!any)
        return;
    set_menu_scratch_tech_learned_bits(g, w.bits_left);
    set_menu_scratch_tech_group_end(g, w.group_end);
    set_menu_scratch_tech_next(g, w.next_tech);
}

// $C2:BCAE, $C2:BD00
/* Each group's techs the party has learned, from the group's first up to the
 * next group's (at least the first, looked at whatever the end says). */
void menu_list_techs(GameState *g, uint8_t character, MenuTechKind kind)
{
    for (unsigned i = 0; i < CLEARED_PLACES; i++)
        set_menu_tech_list(g, i, MENU_NO_TECH);
    ORIGINAL_EFFECT(charge_block_move(g, CLEARED_PLACES));
    uint8_t groups[MENU_TECH_MEMBER_GROUPS_COUNT];
    unsigned found = groups_of(g, character, kind, groups);
    uint8_t listed = 0;
    TechWalk w = { 0, 0, 0, 0 };
    for (unsigned i = 0; i < found; i++) {
        uint8_t group = groups[i], learned = menu_tech_groups_listed(g, group);
        uint8_t tech = menu_tech_group_first(g, group), end = menu_tech_group_first(g, (uint8_t)(group + 1));
        do {
            if (learned & FIRST_TECH_BIT) {
                set_menu_tech_list(g, listed, tech);
                set_menu_tech_list_groups(g, listed, group);
                listed++;
            }
            learned = (uint8_t)(learned << 1);
            tech++;
        } while (tech < end);
        w = (TechWalk){ (uint8_t)(i + 1), learned, end, tech };
    }
    ORIGINAL_EFFECT(leave_techs_walked(g, w, found != 0));
    set_menu_techs_listed(g, listed);
    set_menu_tech_list(g, listed, MENU_NO_TECH);
}

/* ---- What a tech costs ------------------------------------------------------------------------- */

/* The status record's accessory, and the two that cut a tech's cost; the
 * character in the record's first byte's low four bits. */
enum { ACCESSORY_AT = 0x2A, MP_HALVED = 0xA2, MP_QUARTERED = 0xA3, CHARACTER_IN_RECORD = 0x0F };

static uint8_t half_rounded_up(uint8_t mp) { return (uint8_t)((mp >> 1) + (mp & 1)); }

// $C2:BC59
static uint8_t cut_by_the_accessory(const GameState *g, uint8_t mp)
{
    uint8_t accessory = menu_status_record_bytes_byte(g, ACCESSORY_AT);
    if (accessory == MP_QUARTERED)
        mp = half_rounded_up(mp);
    if (accessory == MP_QUARTERED || accessory == MP_HALVED)
        mp = half_rounded_up(mp);
    return mp;
}

// $C2:BC22
/* A double or triple tech's cost is read a word wide, and the word kept: its
 * high byte is the next part's cost, uncut. A single tech's is a byte. */
MenuTechCost menu_tech_cost_for(GameState *g, uint8_t tech, MenuTechKind kind)
{
    uint16_t part = tech;
    if (kind != MENU_SINGLE_TECHS) {
        uint8_t character = (uint8_t)(menu_status_record_character(g) & CHARACTER_IN_RECORD);
        part = menu_tech_parts(g, character, (unsigned)(tech - MENU_FIRST_DOUBLE_TECH));
    }
    uint8_t mp = cut_by_the_accessory(g, menu_tech_part_mp(g, part));
    set_menu_tech_cost_needed(g, mp);
    if (kind != MENU_SINGLE_TECHS)
        ORIGINAL_EFFECT(set_menu_tech_cost_beside(g, menu_tech_part_mp(g, (uint16_t)(part + 1))));
    return (MenuTechCost){ mp, part };
}

/* ---- What a tech does from the menus ----------------------------------------------------------- */

enum { HEALS = 0x80, BEING_LEARNED = 0x40 };

bool menu_tech_heals(const GameState *g, uint8_t tech) { return (menu_tech_marks(g, tech) & HEALS) != 0; }
bool menu_tech_being_learned(const GameState *g, uint8_t tech) { return (menu_tech_marks(g, tech) & BEING_LEARNED) != 0; }

/* The effect's bits (as menu_use_effect has them): what it restores, from
 * the effect record's third byte's top two; one member (4) or every member
 * (8), from the targets' bit. The factor is the second byte's low five. */
enum { RESTORES_BITS = 0xC0, FACTOR_BITS = 0x1F, EVERY_MEMBER_BIT = 0x01, ONE_MEMBER = 0x04, EVERY_MEMBER = 0x08 };
enum { TECH_RECORD_BYTES = 12 };

/* The original finds the record with the multiplier, and keeps its offset. */
static void leave_record_found(GameState *g, uint8_t tech)
{
    hw_set_multiplicand(g, TECH_RECORD_BYTES);
    set_menu_scratch_tech_record_offset(g, hw_multiply_by(g, tech));
}

// $C2:B3F2, $C2:B43A
MenuTechHealing menu_tech_healing(GameState *g, uint8_t tech)
{
    ORIGINAL_EFFECT(leave_record_found(g, tech));
    uint8_t restores = (uint8_t)(menu_tech_effect_byte(g, tech, MENU_TECH_EFFECT_RESTORES) & RESTORES_BITS);
    uint8_t who = (menu_tech_targets(g, tech) & EVERY_MEMBER_BIT) ? EVERY_MEMBER : ONE_MEMBER;
    return (MenuTechHealing){ (uint8_t)(restores | who),
                              (uint8_t)(menu_tech_effect_byte(g, tech, MENU_TECH_EFFECT_FACTOR) & FACTOR_BITS) };
}

/* ---- A tech's row ------------------------------------------------------------------------------ */

/* The name's eleven glyphs (its icon first); in the colours of a tech still
 * being learned, of one usable here, or the text's own. */
enum { NAME_SETTINGS = 0x0B, BEING_LEARNED_COLOURS = 0x0C, USABLE_COLOURS = 0x08, PLAIN = 0, NO_TECHS_PAST = 0x80 };

static uint8_t colours_of(const GameState *g, uint8_t tech, bool waiting)
{
    if (menu_tech_being_learned(g, tech))
        return BEING_LEARNED_COLOURS;
    return menu_tech_heals(g, tech) && !waiting ? USABLE_COLOURS : PLAIN;
}

static void leave_name_found(GameState *g, uint8_t tech)
{
    hw_set_multiplicand(g, tech);
    (void)hw_multiply_by(g, MENU_NAME_TABLE_BYTES);
}

// $C2:BD99, $C2:BDB2
void menu_draw_tech_row(GameState *g, uint16_t at, uint8_t tech, bool waiting)
{
    if (tech >= NO_TECHS_PAST) {
        (void)menu_draw_blanks(g, at, NAME_SETTINGS);
        return;
    }
    ORIGINAL_EFFECT(leave_name_found(g, tech));
    set_menu_text_attribute(g, colours_of(g, tech, waiting));
    (void)menu_draw_string(g, menu_tech_name(g, tech), at, NAME_SETTINGS);
}

bool menu_tech_is_the_games(uint8_t tech, MenuTechKind kind)
{
    return tech < MENU_TECH_MARKS_COUNT && (kind == MENU_SINGLE_TECHS || tech >= MENU_FIRST_DOUBLE_TECH);
}

/* ---- As the menus open: the party's techs noted ------------------------------------------------- */

/* Which characters the party doesn't have: a bit each, Crono's bit 7 to
 * Magus's bit 1, as the groups' characters are kept. */
enum { NO_CHARACTERS = 0xFE };

static uint8_t characters_missing(const GameState *g)
{
    uint8_t missing = NO_CHARACTERS;
    for (unsigned place = 0; place < MENU_PARTY_PLACES; place++) {
        uint8_t member = menu_member_in_place(g, place);
        if (!(member & MENU_NO_MEMBER))
            missing &= (uint8_t)~menu_character_bit(g, member);
    }
    return missing;
}

static bool party_has_all_of(const GameState *g, uint8_t group, uint8_t missing)
{
    return (menu_tech_group_members(g, group) & missing) == 0;
}

/* A single tech, numbered from 1, eight a character from Crono's; the seven
 * characters. */
enum { SINGLE_TECHS_A_CHARACTER = 8, CHARACTERS = 7 };

static bool single_tech_learned(const GameState *g, uint8_t tech)
{
    uint8_t index = (uint8_t)(tech - 1);
    return (party_single_techs_learned(g, index / SINGLE_TECHS_A_CHARACTER)
            & menu_character_bit(g, index % SINGLE_TECHS_A_CHARACTER)) != 0;
}

/* The single techs a rock's triple tech needs. */
enum { TECHS_A_ROCK_NEEDS = 3 };

/* How many of the rock's techs, from its first, are learned before the
 * first that isn't. */
static unsigned rock_techs_learned(const GameState *g, unsigned rock)
{
    unsigned learned = 0;
    while (learned < TECHS_A_ROCK_NEEDS && single_tech_learned(g, menu_rock_tech_needs(g, rock, learned)))
        learned++;
    return learned;
}

/* What the original's search leaves in its cells: past the last rock, the
 * characters missing, and from the last rock whose characters are all there,
 * how many of its techs it still had to find learned and the last it looked
 * at (less one). */
static void leave_rock_search(GameState *g, uint8_t missing)
{
    set_menu_rock_tech_search_rock(g, MENU_ROCKS);
    set_menu_rock_tech_search_missing(g, missing);
    for (int rock = MENU_ROCKS - 1; rock >= 0; rock--) {
        if (!party_has_all_of(g, (uint8_t)(MENU_FIRST_ROCK_GROUP + rock), missing))
            continue;
        unsigned learned = rock_techs_learned(g, (unsigned)rock);
        unsigned last = learned < TECHS_A_ROCK_NEEDS ? learned : TECHS_A_ROCK_NEEDS - 1;
        set_menu_rock_tech_search_techs_left(g, (uint8_t)(TECHS_A_ROCK_NEEDS - learned));
        set_menu_rock_tech_search_tech(g, (uint8_t)(menu_rock_tech_needs(g, (unsigned)rock, last) - 1));
        return;
    }
}

// $FF:F958
/* Which of the five triple techs the rocks give the party could do now: the
 * tech's three characters in the party, each with the single tech it takes
 * learned. */
void menu_note_rock_techs(GameState *g)
{
    uint8_t missing = characters_missing(g);
    for (unsigned rock = 0; rock < MENU_ROCKS; rock++) {
        bool ready = party_has_all_of(g, (uint8_t)(MENU_FIRST_ROCK_GROUP + rock), missing)
                     && rock_techs_learned(g, rock) == TECHS_A_ROCK_NEEDS;
        set_menu_rock_techs_ready(g, rock, ready ? 1 : 0);
    }
    ORIGINAL_EFFECT(leave_rock_search(g, missing));
}

/* How many members the screens let the player choose among: the active
 * ones, or all of them once the story lets the party be switched and
 * nothing holds it as it is (story_party_rules). */
enum { PARTY_SWITCHED_FROM = 0x49, PARTY_HELD = 0x01 };

static uint8_t members_reachable(const GameState *g)
{
    bool switchable = event_memory_byte(g, 0) >= PARTY_SWITCHED_FROM && !(story_party_rules(g) & PARTY_HELD);
    return switchable ? menu_member_count(g) : menu_active_members(g);
}

/* The techs that heal, which can be used from the menus: Marle's Aura, Cure
 * and Cure 2, Robo's Cure Beam and Heal Beam, Frog's Slurp, Heal and Cure 2,
 * and Ayla's Kiss. */
static const uint8_t HEALING_TECHS[] = { 0x09, 0x0C, 0x0F, 0x1A, 0x1D, 0x21, 0x24, 0x27, 0x29 };

static void mark_healing_techs(GameState *g)
{
    for (unsigned i = 0; i < sizeof HEALING_TECHS; i++)
        set_menu_tech_marks(g, HEALING_TECHS[i], (uint8_t)(menu_tech_marks(g, HEALING_TECHS[i]) | HEALS));
}

/* The party's techs learned of a group: the characters' own, the doubles'
 * and the triples' are kept apart. */
enum { FIRST_DOUBLE_GROUP = 7, FIRST_TRIPLE_GROUP = 22 };

static uint8_t group_techs_learned(const GameState *g, uint8_t group)
{
    if (group < FIRST_DOUBLE_GROUP)
        return party_single_techs_learned(g, group);
    if (group < FIRST_TRIPLE_GROUP)
        return party_double_techs_learned(g, (unsigned)(group - FIRST_DOUBLE_GROUP));
    return party_triple_techs_learned(g, (unsigned)(group - FIRST_TRIPLE_GROUP));
}

/* Every group whose characters are all in the party lists the techs the
 * party has learned of it; the others keep what they listed before. */
static void list_learned_techs(GameState *g, uint8_t missing)
{
    for (int group = MENU_TECH_GROUPS - 1; group >= 0; group--)
        if (party_has_all_of(g, (uint8_t)group, missing))
            set_menu_tech_groups_listed(g, (unsigned)group, group_techs_learned(g, (uint8_t)group));
}

/* A character learns its techs in order, its first in bit 7: how many it
 * knows is how far its last one learned is from the first. */
static uint8_t techs_known(uint8_t learned)
{
    uint8_t known = SINGLE_TECHS_A_CHARACTER;
    while (known != 0 && !(learned & 1)) {
        learned >>= 1;
        known--;
    }
    return known;
}

/* The bits of a tech's marks the table's other use, the items' kinds,
 * keeps. */
enum { ITEM_KIND_BITS = 0x1E };

/* Each character's next single tech, the one it is learning, is listed too,
 * marked as such, and can't be used from the menus until it is learned; one
 * whose locked techs aren't unlocked yet shows no more than its first few,
 * and one who knows all eight has none to show. */
static void list_techs_being_learned(GameState *g)
{
    uint8_t unlocked = party_techs_unlocked(g);
    for (uint8_t character = 0; character < CHARACTERS; character++, unlocked >>= 1) {
        uint8_t listed = menu_tech_groups_listed(g, character), known = techs_known(listed);
        if (!(unlocked & 1) && known >= menu_techs_before_the_lock(g, character))
            continue;
        uint8_t next = menu_character_bit(g, known);
        if (next == 0)
            continue;
        set_menu_tech_groups_listed(g, character, (uint8_t)(listed | next));
        uint8_t tech = (uint8_t)(character * SINGLE_TECHS_A_CHARACTER + known + 1);
        set_menu_tech_marks(g, tech, (uint8_t)((menu_tech_marks(g, tech) & ITEM_KIND_BITS) | BEING_LEARNED));
    }
}

/* The parts table's columns: the double groups' techs, three a group, then
 * the triple techs. */
enum { DOUBLE_GROUPS = 15, TECHS_A_DOUBLE_GROUP = 3, TRIPLE_TECHS = 15, FIRST_TRIPLE_COLUMN = DOUBLE_GROUPS * TECHS_A_DOUBLE_GROUP };

/* Each double tech's two characters take their parts in it. */
static void note_double_tech_parts(GameState *g)
{
    for (uint8_t group = 0; group < DOUBLE_GROUPS; group++) {
        uint8_t first = menu_double_tech_character(g, group, 0), second = menu_double_tech_character(g, group, 1);
        for (unsigned tech = 0; tech < TECHS_A_DOUBLE_GROUP; tech++) {
            unsigned column = group * TECHS_A_DOUBLE_GROUP + tech;
            set_menu_tech_parts(g, first, column, menu_double_tech_part(g, group, tech, 0));
            set_menu_tech_parts(g, second, column, menu_double_tech_part(g, group, tech, 1));
        }
    }
}

/* Each triple tech's three characters take the next three parts, in the
 * order of their bits. */
enum { CHARACTER_BITS = 8, FIRST_CHARACTER_BIT = 0x80 };

static void note_triple_tech_parts(GameState *g)
{
    unsigned part = 0;
    for (uint8_t tech = 0; tech < TRIPLE_TECHS; tech++) {
        uint8_t characters = menu_tech_group_members(g, (uint8_t)(FIRST_TRIPLE_GROUP + tech));
        for (uint8_t character = 0; character < CHARACTER_BITS; character++)
            if (characters & (FIRST_CHARACTER_BIT >> character))
                set_menu_tech_parts(g, character, FIRST_TRIPLE_COLUMN + tech, menu_triple_tech_part(g, part++));
    }
}

/* What the original's noting of the parts leaves in its cells: the last
 * double group's first character's row (its low byte taken by the triples'
 * shifting, run out), the last triple tech's last character counted, the
 * triples' parts placed, the groups and the last double's parts' start, and
 * where the next column would be. */
enum { PARTS_ROW_SHIFT = 6, DOUBLE_PARTS_A_GROUP = 6 };

static uint8_t last_character_counted(uint8_t characters)
{
    uint8_t counted = 0;
    while (characters != 0) {
        characters = (uint8_t)(characters << 1);
        counted++;
    }
    return counted;
}

static void leave_parts_noted(GameState *g)
{
    unsigned parts = 0;
    for (uint8_t tech = 0; tech < TRIPLE_TECHS; tech++)
        for (uint8_t characters = menu_tech_group_members(g, (uint8_t)(FIRST_TRIPLE_GROUP + tech)); characters != 0;
             characters &= (uint8_t)(characters - 1))
            parts++;
    uint8_t last_triple = menu_tech_group_members(g, FIRST_TRIPLE_GROUP + TRIPLE_TECHS - 1);
    set_menu_scratch_tech_parts_first_row(g, (uint16_t)((menu_double_tech_character(g, DOUBLE_GROUPS - 1, 0) << PARTS_ROW_SHIFT) & 0xFF00));
    set_menu_scratch_tech_parts_second_row(g, (uint16_t)(last_character_counted(last_triple) << 8));
    set_menu_scratch_tech_parts_placed(g, (uint16_t)parts);
    set_menu_scratch_tech_parts_group(g, TRIPLE_TECHS);
    set_menu_scratch_tech_parts_from(g, (DOUBLE_GROUPS - 1) * DOUBLE_PARTS_A_GROUP);
    set_menu_tech_parts_column(g, RAM_MENU_TECH_PARTS + FIRST_TRIPLE_COLUMN + TRIPLE_TECHS);
}

// $FF:F813
/* As the menus open: how many members the screens let the player choose
 * among, and what of the party's techs they list -- the healing techs marked
 * usable, the rocks' triple techs the party could do, the techs learned of
 * every group whose characters are all in the party, each character's next
 * single tech, and each character's part in each double and triple tech. */
void menu_note_party_techs(GameState *g)
{
    set_menu_members_reachable(g, members_reachable(g));
    mark_healing_techs(g);
    menu_note_rock_techs(g);
    list_learned_techs(g, characters_missing(g));
    list_techs_being_learned(g);
    note_double_tech_parts(g);
    note_triple_tech_parts(g);
    ORIGINAL_EFFECT(leave_parts_noted(g));
}

/* ---- The registered entries ---------------------------------------------------------------------- */

// $FF:F813
void menu_note_party_techs_entry(Snes *s)
{
    menu_note_party_techs(state_of(s));
    m_rtl(s);
}

// $FF:F958
/* Entered with any widths, which it puts back; it works on a page of its own
 * and puts the caller's back. */
void menu_note_rock_techs_entry(Snes *s)
{
    menu_note_rock_techs(state_of(s));
    m_rtl(s);
}

/* The noting reads the party and its techs through a data bank on work
 * RAM's, and the noting of the techs works on the menus' page, where it
 * counts the triple techs' parts in a word whose high byte it never sets:
 * 0 whenever the menus open. */
bool menu_rock_techs_are_notable(Snes *s) { return s->cpu.db == MENU_DATA_BANK; }

bool menu_party_techs_can_be_noted(const GameState *g) { return menu_scratch_tech_parts_placed(g) >> 8 == 0; }

bool menu_party_techs_are_notable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_party_techs_can_be_noted(state_of(s));
    vars_peek(false);
    return ok;
}
