/* The party as the menus keep it (game/menu/party.c): who is in it, what
 * they wear, and the services the rest of the game calls to change it.
 *
 * The party's record is nine places: the three of the active party, who walk
 * the map and fight, and the six of the reserve. Each holds a character, or
 * is empty; an empty place keeps the byte of whoever left it, marked
 * (PARTY_PLACE_EMPTIED). A character joins the active party in its first
 * empty place, or else takes the third place and sends whoever had it to
 * the front of the reserve; a member sent to the reserve goes to its front,
 * and the members behind close up. The reserve takes someone at its front by
 * moving everyone a place back as far as its first empty place; a full
 * reserve loses its last.
 *
 * The services (the field's scripts call them through $C2:8004, a New
 * Game+ and the menus' screens as C) look a character up, recruit one,
 * dismiss one, bring one into the active party or send one to the reserve;
 * equip a character or strip it; fill every character's HP or MP; count an
 * item the party holds. Every one of them ends with the party's looks and
 * its rocks brought up to date with who is in it and what they wear
 * (party_wear_it_shows): each active member's sprite colours take its
 * weapon's, and a rock some character wears teaches the party its triple
 * tech where the party could do it. */
#ifndef CT_GAME_MENU_PARTY_H
#define CT_GAME_MENU_PARTY_H

#include <stdbool.h>
#include <stdint.h>

#include "platform/save.h"
#include "snes/system.h"
#include "state.h"

/* The record's places: the active party's, then the reserve's. */
enum { PARTY_ACTIVE_PLACES = 3, PARTY_RECORD_PLACES = 9 };
typedef uint8_t RecordPlace;
enum { NOT_IN_THE_RECORD = 0xFF };

/* An emptied place keeps the character that was there with this bit set;
 * a new game's are all character 0's. */
enum { PARTY_PLACE_EMPTIED = 0x80 };

/* Whether the active party is characters (or empty places), as the
 * services that show what it wears read it: the game's always is. For the
 * guards of their callers. */
bool party_record_shows_characters(const GameState *g);

/* Where a character is among the record's first `places` (the active
 * party's, or all nine), or NOT_IN_THE_RECORD. */
RecordPlace party_place_of(const GameState *g, uint8_t character, unsigned places);

/* A character recruited: into the reserve's first empty place, unless it
 * has been recruited already; a full reserve takes nobody. */
typedef enum { PARTY_ALREADY_RECRUITED, PARTY_RECRUITED, PARTY_NO_ROOM } PartyRecruiting;
typedef struct {
    PartyRecruiting outcome;
    RecordPlace place;   /* where the character is; NOT_IN_THE_RECORD for no room */
} PartyRecruit;
PartyRecruit party_recruit(GameState *g, uint8_t character);

/* A character dismissed: its place emptied. The place, or
 * NOT_IN_THE_RECORD where it wasn't recruited. */
RecordPlace party_dismiss(GameState *g, uint8_t character);

/* The reserve taking someone at its front: where its members stopped
 * moving back (the empty place that closed, or none, PARTY_RECORD_PLACES)
 * and the byte that came out of the record there -- that place's, or with
 * no empty place the last member's, who leaves the record. */
typedef struct {
    RecordPlace closed;
    uint8_t out;
} ReserveTaking;

/* A character brought into the active party, from wherever it was: into an
 * empty active place (`joined`, which then holds its place), or the third
 * member sent to the reserve's front for it (`joined` false: `reserve` says
 * how the reserve took them). */
typedef struct {
    bool joined;
    RecordPlace place;
    ReserveTaking reserve;
} PartyBringingIn;
PartyBringingIn party_bring_in(GameState *g, uint8_t character);

/* A member of the active party sent to the reserve's front, the members
 * behind it closing up; `was_active` false (and nothing done) for a
 * character not in the active party. */
typedef struct {
    bool was_active;
    ReserveTaking reserve;
} PartySending;
PartySending party_send_to_the_reserve(GameState *g, uint8_t character);

/* Every character's HP or MP filled to its greatest: HP to the greatest an
 * earring it wears raises it to (party_greatest_hp). */
void party_fill_hp(GameState *g);
void party_fill_mp(GameState *g);

/* The HP a character with `max_hp` reaches with `accessory` on: a silver
 * earring raises it a quarter, a gold one a half, to 999 at most. */
uint16_t party_greatest_hp(uint16_t max_hp, uint8_t accessory);

/* A character equipped with an item: what it had in the item's slot goes
 * back to the party's items, and the item is taken from them. An item that
 * is no equipment changes nothing. Item 0 strips the character instead,
 * every slot back to its nothing but Ayla's fists, without giving anything
 * back. Either way its HP is filled. */
void party_equip(GameState *g, uint8_t character, uint8_t item);

/* What the party wears, shown: each active member's sprite colours take its
 * weapon's, and the rocks' triple techs follow who wears a rock. Every
 * service ends with it. */
void party_wear_it_shows(GameState *g);

/* A rock's triple tech is the party's while a character wears the rock and
 * the party could do it (menu_rock_techs_ready, worked out first). */
void party_rocks_teach(GameState *g);

/* The game finished: the cartridge notes it, so that its load screen offers
 * a New Game+. */
void party_note_the_game_finished(Platform *pf);

/* ---- The services as the original's code calls them ($C2:8004) ------------------------------ */

/* The services by the number their callers give in A. */
typedef enum {
    SERVICE_IS_ACTIVE = 0,
    SERVICE_BRING_IN = 1,
    SERVICE_SEND_TO_THE_RESERVE = 2,
    SERVICE_IS_RECRUITED = 3,
    SERVICE_RECRUIT = 4,
    SERVICE_DISMISS = 5,
    SERVICE_FILL_HP = 6,
    SERVICE_FILL_MP = 7,
    SERVICE_GAME_FINISHED = 8,
    SERVICE_POWER_ON = 9,
    SERVICE_EQUIP = 0x0A,
    SERVICE_SHOW_WHAT_IT_WEARS = 0x0B,
    SERVICE_COUNT_ITEM = 0x0C,
    PARTY_SERVICES
} PartyService;

/* Run a service as the original's dispatcher does (menu/party_bridge.c),
 * for a port that calls it where the original calls $C2:8004: its work, what
 * every service ends with, and its answer -- a byte, or the word of a service
 * that works 16 bits wide. For the original's code and shadow mode, the index
 * registers the dispatcher leaves (scaffolding: ORIGINAL_EFFECT). */
uint16_t party_run_service(GameState *g, Platform *pf, PartyService service, uint8_t character, uint8_t item);

/* The same for a caller that hands a service on what the one before it left
 * in the registers it takes its arguments from (a New Game+'s fillings). */
uint16_t party_run_service_as_left(GameState *g, Platform *pf, PartyService service, PartyService before);
uint16_t party_service_x(PartyService service);
uint16_t party_service_y(const GameState *g, PartyService service);

void menu_party_services(Snes *s);
void menu_show_what_the_party_wears(Snes *s);
void menu_rocks_teach(Snes *s);
bool menu_party_service_runs(Snes *s);
bool menu_party_wear_is_showable(Snes *s);

#endif
