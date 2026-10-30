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
 * caller's mode), and the menus' tasks start ($C2:8190).
 *
 * Several of those are still the original's; the opening is a bridge that
 * calls them, and the ones in C are run as C. */
#include "menu/opening.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/graphics.h"
#include "menu/items.h"
#include "menu/members.h"
#include "menu/save_screen.h"
#include "menu/sprites.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "port/native.h"
#include "sound/sound.h"

/* ---- The party, as the menus list it ------------------------------------------------------------ */

enum { ACTIVE_PLACES = 3 };

uint8_t menu_member_in_place(const GameState *g, unsigned place)
{
    return place < ACTIVE_PLACES ? party_members(g, place) : party_waiting(g, place - ACTIVE_PLACES);
}

// $C2:816A
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

// $C2:92F4
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
    ORIGINAL_EFFECT(set_menu_scratch_settings_packed(g, second));
}

/* ---- The opening ------------------------------------------------------------------------------- */

/* The menus' interrupts: their vblank handler, and a line interrupt handler
 * that returns at once. */
enum { LONG_JUMP = 0x5C };

static void take_the_interrupts(GameState *g)
{
    set_nmi_vector(g, LONG_JUMP | (uint32_t)MENU_VBLANK_HANDLER << 8);
    set_irq_vector(g, LONG_JUMP | (uint32_t)MENU_LINE_HANDLER << 8);
}

/* The sound effects quietened and the driver's first set of effects taken:
 * silence played on both pairs of effect voices, their volume put back to
 * full at once, each command from its own call. */
enum { CHOOSE_EFFECT_SET = 0x70, FIRST_EFFECT_SET = 0, SILENCE = 0xFF, AT_ONCE = 1 };
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

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
    au_command(pf, PF_CALL_DC(QUIETING[i].site, 0xC70004, DRIVER_IGNORES), QUIETING[i].command);
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

/* What the opening does that is a routine of the menus' own, run as C or on
 * the machine (below): a quietening command sent, the screen set up, the
 * save slots read, the upload queue sent, the records' sprites loaded, the
 * item tables built, and the low-HP marks worked out where the port takes
 * them; and where the graphics' building leaves what the original's code
 * reads (scaffolding). Each false where it went no further. */
typedef enum { OPENING_SCREEN, OPENING_SAVE_SLOTS, OPENING_UPLOADS, OPENING_RECORDS, OPENING_ITEMS } OpeningCall;
typedef struct {
    bool (*quieten)(void *ctx, unsigned command);
    bool (*call)(void *ctx, OpeningCall call);
    void (*graphics_built)(void *ctx);
    bool (*low_hp_marks)(void *ctx);
} OpeningCalls;

enum { LOW_HP_MARKS = 0xC2813C };

// $C2:8096
/* The menus opened: their interrupts' handlers taken, the effects quietened,
 * the screen set up, their cells cleared, their graphics built, the save
 * slots read, the sprites cleared and the queued uploads sent, the party
 * listed, the settings unpacked, the records' sprites loaded, the item
 * tables built, and the characters' low-HP marks worked out (the routine it
 * runs into, $C2:813C). */
static bool open_the_menus(GameState *g, Platform *pf, const OpeningCalls *c, void *ctx)
{
    take_the_interrupts(g);
    for (unsigned i = 0; i < QUIETING_COMMANDS; i++)
        if (!c->quieten(ctx, i))
            return false;
    if (!c->call(ctx, OPENING_SCREEN))
        return false;
    clear_the_menus_cells(g);
    TIMED_AS(g, 0xC2968D, menu_build_graphics(g, pf));
    c->graphics_built(ctx);
    if (!c->call(ctx, OPENING_SAVE_SLOTS))
        return false;
    TIMED_AS(g, 0xC2984A, menu_clear_sprites(g));
    if (!c->call(ctx, OPENING_UPLOADS))
        return false;
    TIMED_AS(g, 0xC2816A, menu_list_party(g));
    TIMED_AS(g, 0xC292F4, menu_unpack_settings(g));
    if (!c->call(ctx, OPENING_RECORDS) || !c->call(ctx, OPENING_ITEMS) || !c->low_hp_marks(ctx))
        return false;
    TIMED_AS(g, LOW_HP_MARKS, menu_work_out_low_hp_marks(g));
    return true;
}

/* ---- As C ------------------------------------------------------------------------------------- */

typedef struct {
    GameState *game;
    Platform *pf;
} MenuOpeningContext;

static bool quieten(void *ctx, unsigned command)
{
    MenuOpeningContext *c = ctx;
    quieten_the_channel(c->pf, command);
    return true;
}

/* Each where its guard takes it; false where it doesn't, the program going
 * no further there. */
static bool call(void *ctx, OpeningCall call)
{
    MenuOpeningContext *c = ctx;
    GameState *g = c->game;
    bool back = true;
    switch (call) {
    case OPENING_SCREEN: TIMED_AS(g, 0xC2940D, menu_video_set_up(g, c->pf)); break;
    case OPENING_SAVE_SLOTS: TIMED_AS(g, 0xC2D156, back = menu_save_slots_read(g, c->pf)); break;
    case OPENING_UPLOADS: back = menu_upload_queue_sent(g, c->pf); break;
    case OPENING_RECORDS: TIMED_AS(g, 0xC2F3CA, back = menu_record_sprites_loaded(g)); break;
    case OPENING_ITEMS: TIMED_AS(g, 0xC29875, back = menu_items_built(g)); break;
    }
    return back;
}

static void graphics_built(void *ctx) { (void)ctx; }

/* The opening clears the zero word the marks' walk starts from. */
static bool low_hp_marks(void *ctx)
{
    (void)ctx;
    return true;
}

bool menu_opens(GameState *g, Platform *pf)
{
    static const OpeningCalls in_the_program = { quieten, call, graphics_built, low_hp_marks };
    MenuOpeningContext c = { g, pf };
    return open_the_menus(g, pf, &in_the_program, &c);
}

/* ---- On the machine --------------------------------------------------------------------------- */

/* Each quietening command is a call-out to the driver, whose return the
 * bridge checks. */
static bool quieten_on_the_machine(void *ctx, unsigned command)
{
    Snes *s = ctx;
    MenuMachine m = menu_machine_now(s);
    quieten_the_channel(m_platform(s), command);
    return menu_back(s, QUIETING[command].site + MENU_JSL_SIZE, m);
}

/* Registers the routines the opening calls don't read (tools/dontcare_check.py
 * checks each). */
enum { SETUP_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };
enum { OPENING_ENDS = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* The screen's setup is the original's, called; the rest are siblings run as
 * the original's calls reach them. */
static bool call_on_the_machine(void *ctx, OpeningCall call)
{
    Snes *s = ctx;
    switch (call) {
    case OPENING_SCREEN: menu_set_widths(s, false, false); return menu_call(s, 0xC280F0, 0xC2940D, SETUP_IGNORES);
    case OPENING_SAVE_SLOTS: return menu_run(s, 0xC28127, 0xC2D156, menu_read_save_slots);
    case OPENING_UPLOADS: return menu_run(s, 0xC2812D, 0xC28663, menu_upload_vram_queue);
    case OPENING_RECORDS: return menu_run(s, 0xC28136, 0xC2F3CA, menu_load_record_sprites);
    case OPENING_ITEMS: return menu_run(s, 0xC28139, 0xC29875, menu_build_items);
    }
    return false;
}

static void graphics_built_on_the_machine(void *ctx) { menu_leave_graphics_built(ctx); }

static bool low_hp_marks_on_the_machine(void *ctx)
{
    Snes *s = ctx;
    if (menu_low_hp_marks_are_workable(s))
        return true;
    m_stop_at(s, LOW_HP_MARKS, OPENING_ENDS);
    return false;
}

/* What the stubs' block move leaves before the sound calls: its count run
 * out, $FFFF, of which the driver's call reads the high byte. */
static void leave_stubs_moved(Snes *s) { s->cpu.a |= 0xFF00; }

void menu_open(Snes *s)
{
    static const OpeningCalls on_the_machine = { quieten_on_the_machine, call_on_the_machine,
                                                 graphics_built_on_the_machine, low_hp_marks_on_the_machine };
    menu_set_widths(s, false, false);
    s->cpu.d = MENU_PAGE;
    ORIGINAL_EFFECT(leave_stubs_moved(s));
    menu_set_widths(s, true, false);
    if (open_the_menus(state_of(s), m_platform(s), &on_the_machine, s))
        m_rts(s);
}
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
