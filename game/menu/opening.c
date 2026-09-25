/* Opening the menus: what they set up each time the game enters them,
 * whichever screen they open on.
 *
 * The menus take the interrupts over (their vblank handler, display.c, and a
 * line interrupt that does nothing), quiet the sound effects and switch the
 * sound driver to the first set of effects, set the screen up blank
 * (display.c), and then build what every screen draws from: the fonts' and
 * windows' graphics, the three save slots' summaries, the party as the menus
 * list it, the player's settings unpacked into a cell each, the characters'
 * sprites, the items and the characters' figures. The screen itself is picked
 * after (the caller's mode), and the menus' tasks start ($C2:8190).
 *
 * Several of those are still the original's; the opening is a bridge that
 * calls them, and the ones in C are run as C. */
#include "menu/opening.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/graphics.h"
#include "menu/save_screen.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/native.h"
#include "sound/sound.h"

/* ---- The party, as the menus list it ------------------------------------------------------------ */

/* The party's nine places: three active, six waiting; a place without a
 * member holds a negative byte. */
enum { ACTIVE_PLACES = 3, PARTY_PLACES = 9, NO_MEMBER = 0x80 };

static uint8_t member_in_place(const GameState *g, unsigned place)
{
    return place < ACTIVE_PLACES ? party_members(g, place) : party_waiting(g, place - ACTIVE_PLACES);
}

// $C2:816A
/* The party's members in order, each with its place, and how many are
 * active and how many there are. */
void menu_list_party(GameState *g)
{
    uint8_t listed = 0, active = 0;
    for (unsigned place = 0; place < PARTY_PLACES; place++) {
        uint8_t member = member_in_place(g, place);
        if (member & NO_MEMBER)
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

// $C2:92F4
/* The player's settings, a cell each for the settings screen. */
void menu_unpack_settings(GameState *g)
{
    ORIGINAL_EFFECT(set_menu_setting_spare(g, 0));   /* cleared with the settings' cells */
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

/* ---- The opening ------------------------------------------------------------------------------- */

/* The menus' interrupts: their vblank handler, and a line interrupt handler
 * that returns at once. */
enum { LONG_JUMP = 0x5C, MENU_VBLANK_HANDLER = 0xC2840E, MENU_LINE_HANDLER = 0xC28472 };

static void take_the_interrupts(GameState *g)
{
    set_nmi_vector(g, LONG_JUMP | (uint32_t)MENU_VBLANK_HANDLER << 8);
    set_irq_vector(g, LONG_JUMP | (uint32_t)MENU_LINE_HANDLER << 8);
}

/* The sound effects quietened and the driver's first set of effects taken:
 * silence played on both pairs of effect voices, their volume put back to
 * full at once. */
enum { CHOOSE_EFFECT_SET = 0x70, FIRST_EFFECT_SET = 0, SILENCE = 0xFF, AT_ONCE = 1 };
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static bool quiet_the_effects(Snes *s)
{
    static const struct {
        uint32_t site;
        AudioCommand command;
    } COMMANDS[] = {
        { 0xC280B2, { .command = CHOOSE_EFFECT_SET, .p1 = FIRST_EFFECT_SET, .count = 1 } },
        { 0xC280C5, { .command = AU_START_EFFECT, .p1 = SILENCE, .p2 = AU_CENTRE, .count = 2 } },
        { 0xC280CE, { .command = SOUND_EFFECT_ON_VOICES_4_5, .count = 0 } },
        { 0xC280E1, { .command = AU_FADE_EFFECTS, .p1 = AT_ONCE, .p2 = AU_FULL_VOLUME, .count = 2 } },
        { 0xC280EA, { .command = AU_FADE_MORE_EFFECTS, .count = 0 } },
    };
    for (unsigned i = 0; i < sizeof COMMANDS / sizeof COMMANDS[0]; i++) {
        MenuMachine m = menu_machine_now(s);
        au_command(m_platform(s), PF_CALL_DC(COMMANDS[i].site, 0xC70004, DRIVER_IGNORES), COMMANDS[i].command);
        if (!menu_back(s, COMMANDS[i].site + MENU_JSL_SIZE, m))
            return false;
    }
    return true;
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
    set_menu_list_top(g, 0);
    for (unsigned i = 0; i < MENU_BLANK_STRING_COUNT; i++)
        set_menu_blank_string(g, i, BLANK);
    for (unsigned i = 0; i < LISTED_MEMBERS; i++)
        set_menu_members(g, i, BLANK);
}

/* What the stubs' block move leaves before the sound calls: its count run
 * out, $FFFF, of which the driver's call reads the high byte. */
static void leave_stubs_moved(Snes *s) { s->cpu.a |= 0xFF00; }

/* Registers the routines the opening calls don't read (tools/dontcare_check.py
 * checks each). */
enum { SETUP_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { OPENING_ENDS = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

// $C2:8096
/* The opening, up to the characters' figures ($C2:813C), which it runs into
 * and the port stops at. */
static bool open_the_menus(Snes *s)
{
    GameState *g = state_of(s);
    menu_set_widths(s, false, false);
    s->cpu.d = MENU_PAGE;
    take_the_interrupts(g);
    ORIGINAL_EFFECT(leave_stubs_moved(s));
    menu_set_widths(s, true, false);
    if (!quiet_the_effects(s))
        return false;
    menu_set_widths(s, false, false);
    if (!menu_call(s, 0xC280F0, 0xC2940D, SETUP_IGNORES))
        return false;
    clear_the_menus_cells(g);
    TIMED_AS(g, 0xC2968D, menu_build_graphics(g, m_platform(s)));
    menu_leave_graphics_built(s);
    if (!menu_run(s, 0xC28127, 0xC2D156, menu_read_save_slots))
        return false;
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    if (!menu_run(s, 0xC2812D, 0xC28663, menu_upload_vram_queue))
        return false;
    TIMED_AS(g, 0xC2816A, menu_list_party(g));
    TIMED_AS(g, 0xC292F4, menu_unpack_settings(g));
    if (!menu_call(s, 0xC28136, 0xC2F3CA, SETUP_IGNORES) || !menu_call(s, 0xC28139, 0xC29875, SETUP_IGNORES))
        return false;
    m_stop_at(s, 0xC2813C, OPENING_ENDS);
    return true;
}

void menu_open(Snes *s) { (void)open_the_menus(s); }

// $C2:816A
void menu_list_party_entry(Snes *s)
{
    menu_list_party(state_of(s));
    m_rts(s);
}

// $C2:92F4
void menu_unpack_settings_entry(Snes *s)
{
    menu_unpack_settings(state_of(s));
    m_rts(s);
}

/* The opening sets its own page; it runs with the data bank on work RAM's,
 * as the menus' entry leaves it. */
bool menu_opening_runs(Snes *s) { return s->cpu.db == MENU_DATA_BANK; }
