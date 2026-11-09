/* The party's appearance at the remaining checked menu handoff. */
#include "menu/party.h"
#include "menu/bridge.h"
#include "port/machine.h"
#include "port/vars_ram.h"

static bool party_members_are_characters(Snes *s)
{
    vars_peek(true);
    bool ok = party_record_shows_characters(state_of(s));
    vars_peek(false);
    return ok;
}

// $C2:834D
void menu_show_what_the_party_wears(Snes *s)
{
    GameState *g = state_of(s);
    party_wear_it_shows(g);
    m_rts(s);
}

/* They reach the records and the palette through the data bank, on work
 * RAM's; the active members are characters. */
bool menu_party_wear_is_showable(Snes *s) { return s->cpu.db == MENU_DATA_BANK && party_members_are_characters(s); }
