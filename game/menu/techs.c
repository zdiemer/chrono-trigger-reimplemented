/* The techs as the menus list them.
 *
 * A tech belongs to a group whose members do it together: each character's
 * own eight, a double tech's group of three for each pair of characters, and
 * each triple tech alone in its group (menu_data.h). As the menus open they
 * note, for every group whose members the party has, which of its techs the
 * party has learned (menu_tech_groups_listed, a bit a tech), and mark the
 * techs that heal and so can be used from the menus, and each member's next
 * single tech, the one still being learned (menu_tech_marks; both $FF:F813,
 * still the original's).
 *
 * A member's list of one kind is then every group of that kind the member is
 * one of, in the groups' order, and each group's techs learned, in theirs.
 * What a tech costs is the member's part's cost (a single tech is all one
 * part), which two accessories halve and quarter. */
#include "menu/techs.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/menu_data.h"
#include "menu/text.h"
#include "original_effects.h"
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
 * character in the record's first byte's low four bits; each character's
 * row of parts. */
enum { ACCESSORY_AT = 0x2A, MP_HALVED = 0xA2, MP_QUARTERED = 0xA3, CHARACTER_IN_RECORD = 0x0F, PARTS_A_CHARACTER = 0x40 };

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
        part = menu_tech_parts(g, (unsigned)(character * PARTS_A_CHARACTER + tech));
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

bool menu_tech_is_the_games(uint8_t tech) { return tech < MENU_TECH_MARKS_COUNT; }
