/* The party's services as the original's code calls them: by number in A,
 * the character they work on in Y and the item in X, through the dispatcher
 * at $C2:8C36 that the long call at $C2:8004 reaches. The ports call the
 * operations themselves (menu/party.h); this is the registered entry, for
 * the original's callers that are still its code, and what the dispatcher
 * leaves (menu/party_leaves.h).
 *
 * Each service is a row of a C table: its operation (menu/party.c,
 * menu/items.c, menu/saves.c) and whether it works 16 bits wide. Every
 * service ends with what the party wears shown (party_wear_it_shows), which
 * leaves the index registers as the dispatcher returns them. */
#include "menu/party.h"

#include <string.h>

#include "charge.h"
#include "menu/bridge.h"
#include "menu/items.h"
#include "menu/menu_data.h"
#include "menu/party_leaves.h"
#include "menu/saves.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* ---- The services ------------------------------------------------------------------------------ */

typedef void (*Service)(GameState *g, Platform *pf, uint8_t character, uint8_t item);

// $C2:8C75, $C2:8CCF, $C2:8C5A, $C2:8E1E
/* The searches, the count of an item and the service that is only what
 * every service ends with change nothing; their answers are worked out
 * with the leaves. */
static void nothing(GameState *g, Platform *pf, uint8_t character, uint8_t item) { (void)g, (void)pf, (void)character, (void)item; }

// $C2:8C79
static void bring_in(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    (void)party_bring_in(g, character);
}

// $C2:8CA2
static void send_to_the_reserve(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    (void)party_send_to_the_reserve(g, character);
}

// $C2:8CE5
static void recruit(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    (void)party_recruit(g, character);
}

// $C2:8CF7
static void dismiss(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)item;
    (void)party_dismiss(g, character);
}

// $C2:8D05
static void fill_hp(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)character, (void)item;
    party_fill_hp(g);
}

// $C2:8D45
static void fill_mp(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf, (void)character, (void)item;
    party_fill_mp(g);
}

// $C2:8D63
static void game_finished(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)g, (void)character, (void)item;
    party_note_the_game_finished(pf);
}

// $C2:8D6A
static void power_on(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)character, (void)item;
    TIMED_AS(g, 0xC28D6A, (void)saves_power_on(g, pf));
}

// $C2:8D8F
static void equip(GameState *g, Platform *pf, uint8_t character, uint8_t item)
{
    (void)pf;
    party_equip(g, character, item);
}

/* The services by number, and whether each works 16 bits wide (its answer a
 * word, and the index registers left wide). */
static const struct {
    Service run;
    bool wide;
} services[PARTY_SERVICES] = {
    [SERVICE_IS_ACTIVE] = { nothing, false },
    [SERVICE_BRING_IN] = { bring_in, false },
    [SERVICE_SEND_TO_THE_RESERVE] = { send_to_the_reserve, false },
    [SERVICE_IS_RECRUITED] = { nothing, false },
    [SERVICE_RECRUIT] = { recruit, false },
    [SERVICE_DISMISS] = { dismiss, false },
    [SERVICE_FILL_HP] = { fill_hp, true },
    [SERVICE_FILL_MP] = { fill_mp, true },
    [SERVICE_GAME_FINISHED] = { game_finished, false },
    [SERVICE_POWER_ON] = { power_on, false },
    [SERVICE_EQUIP] = { equip, true },
    [SERVICE_SHOW_WHAT_IT_WEARS] = { nothing, false },
    [SERVICE_COUNT_ITEM] = { nothing, false },
};

/* ---- What the dispatcher leaves ------------------------------------------------------------- */

PartyServiceCall party_service_called(GameState *g, uint8_t y, uint8_t x)
{
    PartyServiceCall call = { y, x, { 0 } };
    for (RecordPlace place = 0; place < PARTY_ACTIVE_PLACES; place++)
        call.record[place] = party_members(g, place);
    for (RecordPlace place = PARTY_ACTIVE_PLACES; place < PARTY_RECORD_PLACES; place++)
        call.record[place] = party_waiting(g, place - PARTY_ACTIVE_PLACES);
    set_menu_service_character(g, y);
    set_menu_item_operand(g, x);
    return call;
}

/* The answers, from the record as it was: a search's 0 where the character
 * is there and $FF where it isn't; a change's, the byte the original's
 * routine ends on. */
enum { THERE = 0x00, NOT_THERE = 0xFF, FINISHED = 0xFF };

static bool empty(uint8_t at) { return (at & PARTY_PLACE_EMPTIED) != 0; }

static RecordPlace search(const uint8_t *record, uint8_t character, unsigned places)
{
    for (RecordPlace place = 0; place < places; place++)
        if (record[place] == character)
            return place;
    return NOT_IN_THE_RECORD;
}

/* The byte the reserve's taking someone at its front ends on: its first
 * empty place's, which closes, or with none the last member's. */
static uint8_t reserve_end(const uint8_t *record)
{
    for (RecordPlace place = PARTY_ACTIVE_PLACES; place < PARTY_RECORD_PLACES; place++)
        if (empty(record[place]))
            return record[place];
    return record[PARTY_RECORD_PLACES - 1];
}

static uint8_t search_answer(const uint8_t *record, uint8_t character, unsigned places)
{
    return search(record, character, places) == NOT_IN_THE_RECORD ? NOT_THERE : THERE;
}

/* Bringing a character in empties its place first, then takes an empty
 * active place (its own number), or the third and the reserve's front. */
static uint8_t bring_in_answer(const PartyServiceCall *call)
{
    uint8_t record[PARTY_RECORD_PLACES];
    memcpy(record, call->record, sizeof record);
    RecordPlace was = search(record, call->y, PARTY_RECORD_PLACES);
    if (was != NOT_IN_THE_RECORD)
        record[was] = (uint8_t)(record[was] | PARTY_PLACE_EMPTIED);
    for (RecordPlace place = 0; place < PARTY_ACTIVE_PLACES; place++)
        if (empty(record[place]))
            return call->y;
    return reserve_end(record);
}

static uint8_t recruit_answer(const PartyServiceCall *call)
{
    if (search(call->record, call->y, PARTY_RECORD_PLACES) != NOT_IN_THE_RECORD)
        return THERE;
    for (RecordPlace place = PARTY_ACTIVE_PLACES; place < PARTY_RECORD_PLACES; place++)
        if (empty(call->record[place]))
            return call->y;
    return call->record[PARTY_RECORD_PLACES - 1];
}

static uint16_t party_service_answer(GameState *g, PartyService service, const PartyServiceCall *call)
{
    const uint8_t *record = call->record;
    switch (service) {
    case SERVICE_IS_ACTIVE: return search_answer(record, call->y, PARTY_ACTIVE_PLACES);
    case SERVICE_IS_RECRUITED: return search_answer(record, call->y, PARTY_RECORD_PLACES);
    case SERVICE_BRING_IN: return bring_in_answer(call);
    case SERVICE_SEND_TO_THE_RESERVE:
        return search(record, call->y, PARTY_ACTIVE_PLACES) == NOT_IN_THE_RECORD ? NOT_THERE : reserve_end(record);
    case SERVICE_RECRUIT: return recruit_answer(call);
    case SERVICE_DISMISS:
        return search(record, call->y, PARTY_RECORD_PLACES) == NOT_IN_THE_RECORD ? NOT_THERE : (uint8_t)(call->y | PARTY_PLACE_EMPTIED);
    case SERVICE_FILL_HP:
    case SERVICE_FILL_MP: return 0;
    case SERVICE_GAME_FINISHED: return FINISHED;
    case SERVICE_POWER_ON: return event_memory_byte(g, SAVES_HOW_IT_STARTS);
    case SERVICE_EQUIP: return character_records_hp(g, call->y);
    /* the dispatch's own A, the number doubled */
    case SERVICE_SHOW_WHAT_IT_WEARS: return SERVICE_SHOW_WHAT_IT_WEARS * 2;
    case SERVICE_COUNT_ITEM: return menu_items_held(g, call->x);
    case PARTY_SERVICES: break;
    }
    return 0;
}

/* How many places a service's search went through: the active party's, or
 * the whole record. */
static unsigned places_searched(PartyService service)
{
    switch (service) {
    case SERVICE_IS_ACTIVE:
    case SERVICE_SEND_TO_THE_RESERVE: return PARTY_ACTIVE_PLACES;
    case SERVICE_IS_RECRUITED:
    case SERVICE_BRING_IN:
    case SERVICE_RECRUIT:
    case SERVICE_DISMISS: return PARTY_RECORD_PLACES;
    default: return 0;
    }
}

uint16_t party_service_answered(GameState *g, PartyService service, const PartyServiceCall *call)
{
    uint16_t answer = party_service_answer(g, service, call);
    if (places_searched(service))
        set_menu_service_places_searched(g, (uint8_t)places_searched(service));
    set_menu_service_answer(g, (uint8_t)answer);
    if (services[service].wide)
        set_menu_service_places_searched(g, (uint8_t)(answer >> 8));
    return services[service].wide ? answer : (uint8_t)answer;
}

/* The index registers as the rocks' teaching leaves them: X past the last
 * record; Y the last rock a character wears, or past the rocks' techs' marks
 * its clearing moved over. */
enum { FIRST_ROCK = 0xAE, ROCKS = 5 };

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

uint16_t party_service_x(PartyService service) { return services[service].wide ? records_end() : (uint8_t)records_end(); }

uint16_t party_service_y(const GameState *g, PartyService service)
{
    uint16_t y = last_rock_worn(g);
    return services[service].wide ? y : (uint8_t)y;
}

/* ---- The entries ---------------------------------------------------------------------------------- */

// $C2:8004
/* The services' long entry: the service in A, the character in Y and the
 * item in X, each a byte. The answer comes back in A, a word where the
 * service works 16 bits wide and with a clear high byte otherwise; the
 * flags as they were. X and Y come back as the service leaves them, but
 * only a byte of each where the caller's index registers are a byte (the
 * dispatcher returns to the caller's width): a caller that runs the
 * services 8 bits wide, as a New Game+'s carrying the party over does
 * ($FF:FB84-$FB98), hands that X straight on to its next call, whose
 * dispatcher keeps it as the item (party_service_called). */
void menu_party_services(Snes *s)
{
    GameState *g = state_of(s);
    PartyService service = (PartyService)(uint8_t)s->cpu.a;
    PartyServiceCall call = party_service_called(g, (uint8_t)s->cpu.y, (uint8_t)s->cpu.x);
    services[service].run(g, m_platform(s), call.y, call.x);
    party_wear_it_shows(g);
    s->cpu.a = party_service_answered(g, service, &call);
    uint16_t x = party_service_x(service), y = party_service_y(g, service);
    s->cpu.x = m_x8(s) ? (uint8_t)x : x;
    s->cpu.y = m_x8(s) ? (uint8_t)y : y;
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
 * one of the characters' records and with equipment (the game's always
 * is). */
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
    bool equipment = party_can_equip(state_of(s), item);
    vars_peek(false);
    return equipment;
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

