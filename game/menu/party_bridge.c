/* The party's services as the original's code calls them: by number in A,
 * the character they work on in Y and the item in X, through the dispatcher
 * at $C2:8C36 that the long call at $C2:8004 reaches. The field's scripts,
 * the power-on and a New Game+ call them that way, and their ports run them
 * as C here (party_run_service); the original's own callers that are still
 * its code reach the registered entry.
 *
 * Each service is a row of a C table: its operation (menu/party.c,
 * menu/items.c, menu/saves.c) and the answer the dispatcher hands back,
 * which is the service's game answer where it has one (0 for a character
 * that is there, an item's count) and otherwise what the original's routine
 * leaves. Every service ends with what the party wears shown
 * (party_wear_it_shows), which leaves the index registers as the dispatcher
 * returns them. */
#include "menu/party.h"

#include "charge.h"
#include "menu/bridge.h"
#include "menu/items.h"
#include "menu/menu_data.h"
#include "menu/saves.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The answers ---------------------------------------------------------------------------- */

/* A search's answer: 0 where the character is there, $FF where it isn't. */
enum { THERE = 0x00, NOT_THERE = 0xFF, FINISHED = 0xFF };

/* For the original's code and shadow mode: how many places the search
 * looked through. */
static void leave_places_searched(GameState *g, unsigned places) { set_menu_service_places_searched(g, (uint8_t)places); }

static uint8_t search_answer(RecordPlace place) { return place == NOT_IN_THE_RECORD ? NOT_THERE : THERE; }

/* What the original's searches leave as the answer where they change the
 * record: the byte at the place the search ended, a character's own where
 * it took a place. */
static uint8_t reserve_answer(ReserveTaking r) { return r.out; }

static uint8_t last_place_byte(const GameState *g) { return party_waiting(g, PARTY_WAITING_COUNT - 1); }

/* ---- The services ------------------------------------------------------------------------------ */

typedef uint16_t (*Service)(GameState *g, Platform *pf, uint8_t character, uint8_t item);

// $C2:8C75
static uint16_t is_active(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    ORIGINAL_EFFECT(leave_places_searched(g, PARTY_ACTIVE_PLACES));
    return search_answer(party_place_of(g, character, PARTY_ACTIVE_PLACES));
}

// $C2:8CCF
static uint16_t is_recruited(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    ORIGINAL_EFFECT(leave_places_searched(g, PARTY_RECORD_PLACES));
    return search_answer(party_place_of(g, character, PARTY_RECORD_PLACES));
}

// $C2:8C79
static uint16_t bring_in(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    ORIGINAL_EFFECT(leave_places_searched(g, PARTY_RECORD_PLACES));
    PartyBringingIn b = party_bring_in(g, character);
    return b.joined ? character : reserve_answer(b.reserve);
}

// $C2:8CA2
static uint16_t send_to_the_reserve(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    ORIGINAL_EFFECT(leave_places_searched(g, PARTY_ACTIVE_PLACES));
    PartySending s = party_send_to_the_reserve(g, character);
    return s.was_active ? reserve_answer(s.reserve) : NOT_THERE;
}

// $C2:8CE5
static uint16_t recruit(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    ORIGINAL_EFFECT(leave_places_searched(g, PARTY_RECORD_PLACES));
    PartyRecruit r = party_recruit(g, character);
    return r.outcome == PARTY_ALREADY_RECRUITED ? THERE : r.outcome == PARTY_RECRUITED ? character : last_place_byte(g);
}

// $C2:8CF7
static uint16_t dismiss(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    ORIGINAL_EFFECT(leave_places_searched(g, PARTY_RECORD_PLACES));
    RecordPlace place = party_dismiss(g, character);
    return place == NOT_IN_THE_RECORD ? NOT_THERE : (uint8_t)(character | PARTY_PLACE_EMPTIED);
}

// $C2:8D05
static uint16_t fill_hp(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)character, (void)item;
    party_fill_hp(g);
    return 0;
}

// $C2:8D45
static uint16_t fill_mp(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)character, (void)item;
    party_fill_mp(g);
    return 0;
}

// $C2:8D63
static uint16_t game_finished(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)g, (void)character, (void)item;
    party_note_the_game_finished(pf);
    return FINISHED;
}

// $C2:8D6A
static uint16_t power_on(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)character, (void)item;
    SavesAtPowerOn start = SAVES_NOTHING_SAVED;
    TIMED_AS(g, 0xC28D6A, start = saves_power_on(g, pf));
    return start;
}

// $C2:8D8F
/* The answer is the character's HP after; for an item that is no equipment
 * the original leaves the lookup's category. */
static uint16_t equip(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf;
    party_equip(g, character, item);
    if (item != MENU_NO_WEAPON && menu_item_category(g) >= MENU_EQUIPMENT_SLOTS)
        return (uint8_t)menu_item_category(g);
    return character_records_hp(g, character);
}

// $C2:8C5A
/* Nothing but what every service ends with; the answer is what the
 * dispatch left in A, the number doubled. */
static uint16_t show_what_it_wears(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)g, (void)pf, (void)character, (void)item;
    return SERVICE_SHOW_WHAT_IT_WEARS * 2;
}

// $C2:8E1E
static uint16_t count_item(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)character;
    return menu_items_held(g, item);
}

/* The services by number, and whether each works 16 bits wide (its answer a
 * word, and the index registers left wide). */
static const struct {
    Service run;
    bool wide;
} services[PARTY_SERVICES] = {
    [SERVICE_IS_ACTIVE] = { is_active, false },
    [SERVICE_BRING_IN] = { bring_in, false },
    [SERVICE_SEND_TO_THE_RESERVE] = { send_to_the_reserve, false },
    [SERVICE_IS_RECRUITED] = { is_recruited, false },
    [SERVICE_RECRUIT] = { recruit, false },
    [SERVICE_DISMISS] = { dismiss, false },
    [SERVICE_FILL_HP] = { fill_hp, true },
    [SERVICE_FILL_MP] = { fill_mp, true },
    [SERVICE_GAME_FINISHED] = { game_finished, false },
    [SERVICE_POWER_ON] = { power_on, false },
    [SERVICE_EQUIP] = { equip, true },
    [SERVICE_SHOW_WHAT_IT_WEARS] = { show_what_it_wears, false },
    [SERVICE_COUNT_ITEM] = { count_item, false },
};

/* For the original's code and shadow mode: the dispatcher keeps the call's
 * Y and X, and the answer, a byte or a word. */
static void leave_call(GameState *g, uint8_t y, uint8_t x)
{
    set_menu_service_character(g, y);
    set_menu_item_operand(g, x);
}

static void leave_answer(GameState *g, PartyService service, uint16_t answer)
{
    set_menu_service_answer(g, (uint8_t)answer);
    if (services[service].wide)
        set_menu_service_places_searched(g, (uint8_t)(answer >> 8));
}

// $C2:8C36
uint16_t party_run_service(GameState *g, Platform *pf, PartyService service, uint8_t character, uint8_t item)
{
    ORIGINAL_EFFECT(leave_call(g, character, item));
    uint16_t answer = services[service].run(g, pf, character, item);
    ORIGINAL_EFFECT(leave_answer(g, service, answer));
    party_wear_it_shows(g);
    return services[service].wide ? answer : (uint8_t)answer;
}

uint16_t party_run_service_as_left(GameState *g, Platform *pf, PartyService service, PartyService before)
{
    return party_run_service(g, pf, service, (uint8_t)party_service_y(g, before), (uint8_t)party_service_x(before));
}

/* ---- What the dispatcher leaves ------------------------------------------------------------- */

/* The index registers as the party's looks leave them (the rocks'
 * teaching's, below), a byte where the service worked 8 bits wide. */
enum { FIRST_ROCK = 0xAE, ROCKS = 5 };

static bool wide(PartyService service) { return services[service].wide; }

static uint16_t records_end(void);
static uint16_t last_rock_worn(const GameState *g);

uint16_t party_service_x(PartyService service) { return wide(service) ? records_end() : (uint8_t)records_end(); }

uint16_t party_service_y(const GameState *g, PartyService service)
{
    uint16_t y = last_rock_worn(g);
    return wide(service) ? y : (uint8_t)y;
}

/* The index registers as the rocks' teaching leaves them: X past the last
 * record; Y the last rock a character wears, or past the rocks' techs' marks
 * its clearing moved over. */
static uint16_t records_end(void) { return (uint16_t)(RAM_ABS(RAM_CHARACTER_RECORDS) + CHARACTER_RECORDS_COUNT * RAM_CHARACTER_RECORDS_STRIDE); }

static uint16_t last_rock_worn(const GameState *g)
{
    uint16_t y = (uint16_t)(RAM_ABS(RAM_PARTY_TRIPLE_TECHS_LEARNED) + PARTY_TRIPLE_TECHS_LEARNED_COUNT);
    for (uint8_t character = 0; character < CHARACTER_RECORDS_COUNT; character++) {
        uint8_t accessory = character_records_record_byte(g, character, menu_category_slot_place(g, MENU_ACCESSORIES));
        if (accessory >= FIRST_ROCK && accessory < FIRST_ROCK + ROCKS)
            y = accessory;
    }
    return y;
}

/* ---- The entries ---------------------------------------------------------------------------------- */

// $C2:8004
/* The services' long entry: the service in A, the character in Y and the
 * item in X, each a byte. The answer comes back in A, a word where the
 * service works 16 bits wide and with a clear high byte otherwise; the
 * flags as they were. */
void menu_party_services(Snes *s)
{
    GameState *g = state_of(s);
    PartyService service = (PartyService)(uint8_t)s->cpu.a;
    uint16_t answer = party_run_service(g, m_platform(s), service, (uint8_t)s->cpu.y, (uint8_t)s->cpu.x);
    s->cpu.a = answer;
    s->cpu.x = party_service_x(service);
    s->cpu.y = party_service_y(g, service);
    m_rtl(s);
}

static bool party_members_are_characters(Snes *s)
{
    vars_peek(true);
    bool ok = party_record_shows_characters(state_of(s));
    vars_peek(false);
    return ok;
}

/* The services the table has, with the active party's members characters
 * (or empty places) and only a character brought into it; the equipping for
 * one of the characters' records, and for an item that is no equipment
 * only with A's high byte clear, which its answer takes (the game equips
 * equipment only). */
bool menu_party_service_runs(Snes *s)
{
    PartyService service = (PartyService)(uint8_t)s->cpu.a;
    if (service >= PARTY_SERVICES || !party_members_are_characters(s))
        return false;
    uint8_t character = (uint8_t)s->cpu.y, item = (uint8_t)s->cpu.x;
    if (service == SERVICE_BRING_IN)
        return character < CHARACTER_RECORDS_COUNT;
    if (service != SERVICE_EQUIP)
        return true;
    if (character >= CHARACTER_RECORDS_COUNT)
        return false;
    vars_peek(true);
    bool equipment = item == MENU_NO_WEAPON || (unsigned)menu_item_category_of(state_of(s), item) < MENU_EQUIPMENT_SLOTS;
    vars_peek(false);
    return equipment || (s->cpu.a & 0xFF00) == 0;
}

/* The entries of the party's looks and the rocks' teaching, which the
 * original's code also calls alone: A as the teaching's loop leaves it,
 * past the last record, and the index registers as above, a byte where the
 * caller's are. */
static void leave_wear_shown(Snes *s, uint16_t y)
{
    s->cpu.a = records_end();
    s->cpu.x = m_x8(s) ? (uint8_t)records_end() : records_end();
    s->cpu.y = m_x8(s) ? (uint8_t)y : y;
}

// $C2:834D
void menu_show_what_the_party_wears(Snes *s)
{
    GameState *g = state_of(s);
    party_wear_it_shows(g);
    ORIGINAL_EFFECT(leave_wear_shown(s, last_rock_worn(g)));
    m_rts(s);
}

// $C2:82E1
void menu_rocks_teach(Snes *s)
{
    GameState *g = state_of(s);
    party_rocks_teach(g);
    ORIGINAL_EFFECT(leave_wear_shown(s, last_rock_worn(g)));
    m_rts(s);
}

/* They reach the records and the palette through the data bank, on work
 * RAM's; the active members are characters. */
bool menu_party_wear_is_showable(Snes *s) { return s->cpu.db == MENU_DATA_BANK && party_members_are_characters(s); }

