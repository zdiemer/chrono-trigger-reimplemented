#include "core/program.h"
/* Opening the menus: what they set up each time the game enters them,
 * whichever screen they open on.
 *
 * The menus take the interrupts over (their vblank handler, display.c, and a
 * line interrupt that does nothing), quiet the sound effects and switch the
 * sound driver to the first set of effects, set the screen up blank
 * (display.c), and then build what every screen draws from: the fonts' and
 * windows' graphics, the three save slots' summaries, the party as the menus
 * list it, the player's settings unpacked into a cell each, the characters'
 * sprites, the items and the characters' figures, and last each character's
 * low-HP mark (menu/members.c). The screen itself is picked after (the
 * caller's mode), and the menus' tasks start ($C2:8190). */
#include "menu/opening.h"

#include "charge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/graphics.h"
#include "menu/items.h"
#include "menu/members.h"
#include "menu/save_screen.h"
#include "menu/sprites.h"
#include "platform/audio.h"
#include "sound/sound.h"
#include "vars.h"

/* ---- The party, as the menus list it ------------------------------------------------------------ */

enum { ACTIVE_PLACES = 3 };

uint8_t menu_member_in_place(const GameState *g, unsigned place)
{
    return place < ACTIVE_PLACES ? party_members(g, place) : party_waiting(g, place - ACTIVE_PLACES);
}

/* The party's members in order, each with its place, and how many are
 * active and how many there are. */
void menu_list_party(GameState *g)
{
    uint8_t listed = 0, active = 0;
    for (unsigned place = 0; place < MENU_PARTY_PLACES; place++) {
        uint8_t member = menu_member_in_place(g, place);
        if (member & MENU_NO_MEMBER)
            continue;
        set_menu_members(g, listed, member);
        set_menu_member_places(g, listed, (uint8_t)place);
        listed++;
        if (place < ACTIVE_PLACES)
            active++;
    }
    set_menu_active_members(g, active);
    set_menu_member_count(g, listed);
}

/* ---- The settings ------------------------------------------------------------------------------ */

/* Where each setting sits in the player's three bytes of settings
 * (game_settings). */
enum {
    SOUND_OUTPUT_BIT = 0x08, OWN_BUTTONS_BIT = 0x10, KEEP_MENU_CURSORS_BIT = 0x20, ACTIVE_TIME_BIT = 0x40,
    DESCRIPTIONS_BIT = 0x80, BATTLE_SPEED_BITS = 0x07,
    WINDOW_DESIGN_BITS = 0x07, MESSAGE_SPEED_SHIFT = 3, MESSAGE_SPEED_BITS = 0x07, KEEP_BATTLE_COMMAND_BIT = 0x40,
    KEEP_BATTLE_LISTS_BIT = 0x80,
    BATTLE_GAUGE_BITS = 0x03,
};

static uint8_t bit_set(uint8_t byte, uint8_t bit) { return (byte & bit) != 0; }

/* The player's settings, a cell each for the settings screen. */
void menu_unpack_settings(GameState *g)
{
    set_menu_setting_spare(g, 0);   /* cleared with the settings' cells */
    uint8_t first = game_settings(g, 0), second = game_settings(g, 1), third = game_settings(g, 2);
    set_menu_setting_descriptions(g, bit_set(first, DESCRIPTIONS_BIT));
    set_menu_setting_active_time(g, bit_set(first, ACTIVE_TIME_BIT));
    set_menu_setting_keep_menu_cursors(g, bit_set(first, KEEP_MENU_CURSORS_BIT));
    set_menu_setting_own_buttons(g, bit_set(first, OWN_BUTTONS_BIT));
    set_menu_setting_sound_output(g, bit_set(first, SOUND_OUTPUT_BIT));
    set_menu_setting_battle_speed(g, (uint8_t)(first & BATTLE_SPEED_BITS));
    set_menu_window_design(g, (uint8_t)(second & WINDOW_DESIGN_BITS));
    set_menu_setting_message_speed(g, (uint8_t)((second >> MESSAGE_SPEED_SHIFT) & MESSAGE_SPEED_BITS));
    set_menu_setting_keep_battle_lists(g, bit_set(second, KEEP_BATTLE_LISTS_BIT));
    set_menu_setting_keep_battle_command(g, bit_set(second, KEEP_BATTLE_COMMAND_BIT));
    set_menu_setting_battle_gauge(g, (uint8_t)(third & BATTLE_GAUGE_BITS));
}

// $C2:934F
/* The settings screen's cells packed back into the player's settings as it
 * leaves for the main menu: an on-off setting is its cell's lowest bit. */
static uint8_t bit_if(uint8_t cell, uint8_t bit) { return (cell & 1) ? bit : 0; }

void menu_pack_settings(GameState *g)
{
    set_game_settings(g, 0, (uint8_t)(bit_if(menu_setting_sound_output(g), SOUND_OUTPUT_BIT) | bit_if(menu_setting_own_buttons(g), OWN_BUTTONS_BIT)
                                      | bit_if(menu_setting_keep_menu_cursors(g), KEEP_MENU_CURSORS_BIT)
                                      | bit_if(menu_setting_active_time(g), ACTIVE_TIME_BIT)
                                      | bit_if(menu_setting_descriptions(g), DESCRIPTIONS_BIT)
                                      | (menu_setting_battle_speed(g) & BATTLE_SPEED_BITS)));
    uint8_t second = (uint8_t)(bit_if(menu_setting_keep_battle_command(g), KEEP_BATTLE_COMMAND_BIT)
                               | bit_if(menu_setting_keep_battle_lists(g), KEEP_BATTLE_LISTS_BIT)
                               | (menu_setting_message_speed(g) & MESSAGE_SPEED_BITS) << MESSAGE_SPEED_SHIFT);
    set_game_settings(g, 1, (uint8_t)(second | (menu_window_design(g) & WINDOW_DESIGN_BITS)));
    set_game_settings(g, 2, (uint8_t)(menu_setting_battle_gauge(g) & BATTLE_GAUGE_BITS));
}

/* ---- The opening ------------------------------------------------------------------------------- */

static void take_the_interrupts(GameState *g)
{
    program_install_vblank_handler(g, menu_vblank);
    charge_accesses(g, 4);
    program_install_line_handler(g, menu_line);
    charge_accesses(g, 4);
}

/* The sound effects quietened and the driver's first set of effects taken:
 * silence played on both pairs of effect voices, their volume put back to
 * full at once, each command from its own call. */
enum { CHOOSE_EFFECT_SET = 0x70, FIRST_EFFECT_SET = 0, SILENCE = 0xFF, AT_ONCE = 1 };

static const struct {
    uint32_t site;
    AudioCommand command;
} QUIETING[] = {
    { 0xC280B2, { .command = CHOOSE_EFFECT_SET, .p1 = FIRST_EFFECT_SET, .count = 1 } },
    { 0xC280C5, { .command = AU_START_EFFECT, .p1 = SILENCE, .p2 = AU_CENTRE, .count = 2 } },
    { 0xC280CE, { .command = SOUND_EFFECT_ON_VOICES_4_5, .count = 0 } },
    { 0xC280E1, { .command = AU_FADE_EFFECTS, .p1 = AT_ONCE, .p2 = AU_FULL_VOLUME, .count = 2 } },
    { 0xC280EA, { .command = AU_FADE_MORE_EFFECTS, .count = 0 } },
};
enum { QUIETING_COMMANDS = sizeof QUIETING / sizeof QUIETING[0] };

static void quieten_the_channel(Platform *pf, unsigned i)
{
    au_command(pf, PF_CALL(QUIETING[i].site, 0xC70004), QUIETING[i].command);
}

/* The layers' vertical scroll a line up, the zero word the menus read, the
 * cursor facing its own way, the blank string, and the member list empty. */
enum { SCROLLED_LAYERS = 3, A_LINE_UP = 0xFFFF, BLANK = 0xFF, LISTED_MEMBERS = 9 };

static void clear_the_menus_cells(GameState *g)
{
    for (unsigned bg = 0; bg < SCROLLED_LAYERS; bg++)
        set_menu_scroll_y(g, bg, A_LINE_UP);
    set_menu_zero(g, 0);
    set_menu_cursor_flip(g, 0);
    set_menu_list_scroll_most(g, 0);
    for (unsigned i = 0; i < MENU_BLANK_STRING_COUNT; i++)
        set_menu_blank_string(g, i, BLANK);
    for (unsigned i = 0; i < LISTED_MEMBERS; i++)
        set_menu_members(g, i, BLANK);
}

enum { LOW_HP_MARKS = 0xC2813C };

// $C2:8096
/* The menus opened: their interrupts' handlers taken, the effects quietened,
 * the screen set up, their cells cleared, their graphics built, the save
 * slots read, the sprites cleared and the queued uploads sent, the party
 * listed, the settings unpacked, the records' sprites loaded, the item
 * tables built, and the characters' low-HP marks worked out (the routine it
 * runs into, $C2:813C). The save slots, the uploads, the records' sprites
 * and the item tables are each refused where the state is one they don't
 * handle, and the program goes no further. */
bool menu_opens(GameState *g, Platform *pf)
{
    take_the_interrupts(g);
    for (unsigned i = 0; i < QUIETING_COMMANDS; i++)
        quieten_the_channel(pf, i);
    TIMED_AS(g, 0xC2940D, menu_video_set_up(g, pf));
    clear_the_menus_cells(g);
    TIMED_AS(g, 0xC2968D, menu_build_graphics(g, pf));
    bool back = false;
    TIMED_AS(g, 0xC2D156, back = menu_save_slots_read(g, pf));
    if (!back)
        return false;
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    if (!menu_upload_queue_sent(g, pf))
        return false;
    TIMED_AS(g, 0xC2816A, menu_list_party(g));
    TIMED_AS(g, 0xC292F4, menu_unpack_settings(g));
    TIMED_AS(g, 0xC2F3CA, back = menu_record_sprites_loaded(g));
    if (!back)
        return false;
    TIMED_AS(g, 0xC29875, back = menu_items_built(g));
    if (!back)
        return false;
    TIMED_AS(g, LOW_HP_MARKS, menu_work_out_low_hp_marks(g));
    return true;
}
