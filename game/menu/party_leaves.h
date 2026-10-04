/* What the party's services leave behind for the original's sake
 * (menu/party_bridge.c): scaffolding, written only through ORIGINAL_EFFECT
 * and never read by the services' logic.
 *
 * The original's code reaches the services by number through a dispatcher
 * ($C2:8004, $C2:8C36), the character in Y and the item in X. The
 * dispatcher keeps the call's Y and X in the menus' cells, and the answer
 * after: a search's yes or no, what a change to the party's record moved,
 * an item's count, a character's HP after it was equipped. The ports call
 * the services' operations (menu/party.h) and none of this; a port standing
 * where the original's call was writes the cells from its own registers --
 * what its original held there, whatever that was -- around the operation:
 *
 *     PartyServiceCall call = { 0 };
 *     ORIGINAL_EFFECT(call = party_service_called(g, y, x));
 *     ... the operation, then party_wear_it_shows ...
 *     ORIGINAL_EFFECT(party_service_answered(g, SERVICE_RECRUIT, &call));
 *
 * The answer is worked out from the call and the party's record as it was
 * before, so the operations hand back only what the game asks of them. */
#ifndef CT_GAME_MENU_PARTY_LEAVES_H
#define CT_GAME_MENU_PARTY_LEAVES_H

#include <stdint.h>

#include "menu/party.h"
#include "state.h"

/* The services by the number the original's callers give in A. */
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

/* A call as the dispatcher took it: Y, X and the party's record before. */
typedef struct {
    uint8_t y, x;
    uint8_t record[PARTY_RECORD_PLACES];
} PartyServiceCall;

/* The call's cells written, before the operation. */
PartyServiceCall party_service_called(GameState *g, uint8_t y, uint8_t x);

/* The answer the dispatcher hands back in A (a byte, or a word for the
 * services that work 16 bits wide), worked out after the operation and
 * kept in the menus' cells with what the service's searches leave. */
uint16_t party_service_answered(GameState *g, PartyService service, const PartyServiceCall *call);

/* The index registers the dispatcher hands back, as the party's looks leave
 * them; a byte for a service that works 8 bits wide. */
uint16_t party_service_x(PartyService service);
uint16_t party_service_y(const GameState *g, PartyService service);

#endif
