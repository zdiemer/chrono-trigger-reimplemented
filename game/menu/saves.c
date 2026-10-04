/* The save code: checking a slot on the cartridge, saving the game into it,
 * loading it back, and the load a New Game+ starts with.
 *
 * A saved game is a copy of the runs of work RAM the ROM's table lists (the
 * inventory, the characters' records and what they have learned, their names,
 * the time played, where the party is, the story's memory and the rest), one
 * after the other from the slot's start (menu/saves.h has where the slots and
 * the record of them are). Saving a game marks its slot and keeps beside the
 * mark a sum of the slot's bytes; a slot holds a game while its mark is there
 * and its bytes still add up to the sum. The layout and the sum are the
 * format the cartridge's saves were written in, so they are kept to the bit:
 * a save made by the original loads here, and the other way round.
 *
 * The menus check every slot as they open (menu/save_screen.c), and the
 * services check them at power-on to find one to go on from; the save point's
 * screen saves; the load screen (menu/load_screen.c) loads.
 *
 * A New Game+ starts from a finished game: it takes over the inventory, the
 * characters' records and their names, and which characters' locked techs
 * have been unlocked, then puts each character's starting equipment back on,
 * fills everyone's HP and MP, and leaves the story's key items behind,
 * through the menus' services (menu/party.h).
 *
 * At power on the services look at the cartridge (saves_power_on): a game on
 * it has the game cleared for the load screen to load it into, and none has
 * a new game's state set up (saves_start_a_new_game, which the load screen
 * also sets up for the new game it starts). */
#include "menu/saves.h"

#include "charge.h"
#include "cpu_io.h"
#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "menu/party.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- A slot's mark and sum ------------------------------------------------------------------ */

/* The word a save marks its slot with. */
enum { SAVED_GAME_MARK = 0xE41B };

static uint16_t saved_word(Platform *pf, PfOrigin o, uint16_t at)
{
    return (uint16_t)(sv_read8(pf, o, at) | sv_read8(pf, o, (uint16_t)(at + 1)) << 8);
}

static void put_saved_word(Platform *pf, PfOrigin o, uint16_t at, uint16_t v)
{
    sv_write8(pf, o, at, (uint8_t)v);
    sv_write8(pf, o, (uint16_t)(at + 1), (uint8_t)(v >> 8));
}

static uint16_t slot_mark(Platform *pf, uint8_t slot) { return saved_word(pf, PF_AT(0xFFF9D8), (uint16_t)(SAVES_SLOT_MARKS + 2 * slot)); }
static uint16_t slot_sum(Platform *pf, uint8_t slot) { return saved_word(pf, PF_AT(0xFFF9E3), (uint16_t)(SAVES_SLOT_SUMS + 2 * slot)); }

// $FF:FBE7
/* A saved game's sum: its words added from the last to the first, in sixteen
 * bits, where an addition that overflows carries one into the next (the last
 * one's is lost). This is the sum the cartridge's saves were written with. */
static uint16_t game_sum(Platform *pf, uint16_t game)
{
    uint16_t sum = 0, carried = 0;
    for (int at = SAVED_GAME_BYTES - 2; at >= 0; at -= 2) {
        uint32_t total = (uint32_t)sum + saved_word(pf, PF_AT(0xFFFBFB), (uint16_t)(game + at)) + carried;
        sum = (uint16_t)total;
        carried = (uint16_t)(total >> 16);
    }
    return sum;
}

/* Where a slot's game is: as the original's code addresses it, and in save
 * memory. */
uint16_t saves_slot_on_the_bus(const GameState *g, uint8_t slot) { return menu_saved_game_on_the_bus(g, slot); }

static uint16_t game_of(const GameState *g, uint8_t slot) { return saves_game_in_memory(saves_slot_on_the_bus(g, slot)); }

// $FF:F9C4
SavedGameCheck saves_check_slot(GameState *g, Platform *pf, uint8_t slot)
{
    if (slot_mark(pf, slot) != SAVED_GAME_MARK)
        return SAVED_GAME_NONE;
    return game_sum(pf, game_of(g, slot)) == slot_sum(pf, slot) ? SAVED_GAME_HELD : SAVED_GAME_SPOILT;
}

/* ---- The runs of work RAM a game is --------------------------------------------------------- */

/* A run's bytes stay in its bank, as the block move's do. */
static uint32_t in_bank(uint32_t at, uint16_t i) { return (at & 0xFF0000u) | (uint16_t)(at + i); }

/* Copy a run into save memory, `to` on, and back. The original moves each
 * with the game's block move (core/memory.c), whose time is charged for it. */
static void save_run(GameState *g, Platform *pf, uint32_t from, uint16_t to, uint16_t bytes)
{
    uint32_t uncounted = charge_uncounted_begin(g);   /* the block move's charge stands for it */
    for (uint16_t i = 0; i < bytes; i++)
        sv_write8(pf, PF_AT(0xFFFA44), (uint16_t)(to + i), wram_read8(g, in_bank(from, i)));
    charge_uncounted_end(g, uncounted);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
}

static void load_run(GameState *g, Platform *pf, uint16_t from, uint32_t to, uint16_t bytes)
{
    uint32_t uncounted = charge_uncounted_begin(g);
    for (uint16_t i = 0; i < bytes; i++)
        wram_write8(g, in_bank(to, i), sv_read8(pf, PF_AT(0xFFFADB), (uint16_t)(from + i)));
    charge_uncounted_end(g, uncounted);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
}

/* ---- What the save code leaves where the original works ------------------------------------ */

/* The original copies its table of runs out of the ROM before it starts, and
 * moves each run through the block move stub, whose banks it writes for each:
 * the table, and the stub as the last move left it. */
enum { BLOCK_MOVE_OPCODE = 0x54, RETURN_OPCODE = 0x6B };

static void leave_block_move(GameState *g, uint8_t to_bank, uint8_t from_bank)
{
    set_menu_block_move_stub(g, (uint32_t)BLOCK_MOVE_OPCODE | (uint32_t)to_bank << 8 | (uint32_t)from_bank << 16
                                    | (uint32_t)RETURN_OPCODE << 24);
}

static void leave_saved_runs_table(GameState *g)
{
    for (unsigned i = 0; i < MENU_SAVED_RUNS * MENU_SAVED_RUN_BYTES; i++)
        set_menu_save_region_table_byte(g, i, menu_saved_runs_byte(g, i));
}

static void leave_carried_runs_table(GameState *g)
{
    for (unsigned i = 0; i < MENU_CARRIED_RUNS * MENU_CARRIED_RUN_BYTES; i++)
        set_menu_save_region_table_byte(g, i, menu_carried_runs_byte(g, i));
}

/* Save memory's bank, as the block move stub names it. */
enum { SAVE_MEMORY_BANK = 0x30 };

static uint8_t bank_of(uint32_t at) { return (uint8_t)(at >> 16); }

/* ---- Saving --------------------------------------------------------------------------------- */

/* Each save counts one, up to the most the count holds. */
static void count_a_save(GameState *g)
{
    uint16_t saved = times_saved(g);
    if (saved != UINT16_MAX)
        set_times_saved(g, (uint16_t)(saved + 1));
}

/* The cartridge's count of finishes moves on by one when a game whose count
 * is the cartridge's is saved. */
static void count_on_the_cartridge(GameState *g, Platform *pf)
{
    uint8_t counted = saves_times_finished(pf, PF_AT(0xFFFA6B));
    if (counted == times_finished(g))
        sv_write8(pf, PF_AT(0xFFFA75), SAVES_TIMES_FINISHED, (uint8_t)(counted + 1));
}

static void leave_saved(GameState *g, uint16_t on_the_bus)
{
    leave_saved_runs_table(g);
    leave_block_move(g, SAVE_MEMORY_BANK, bank_of(menu_saved_run(g, MENU_SAVED_RUNS - 1).at));
    set_menu_scratch_sum_from(g, on_the_bus);
}

// $FF:F9FB
/* Save the game into the slot: the game goes on in it from now on, counts a
 * save, and is copied in; the slot is marked with its sum, and noted as the
 * one saved last. */
void saves_save_game(GameState *g, Platform *pf, uint8_t slot)
{
    set_save_slot_chosen(g, slot);
    count_a_save(g);
    uint16_t on_the_bus = saves_slot_on_the_bus(g, slot), game = saves_game_in_memory(on_the_bus);
    set_menu_slot_game_at(g, on_the_bus);
    uint16_t to = game;
    for (unsigned i = 0; i < MENU_SAVED_RUNS; i++) {
        MenuSavedRun run = menu_saved_run(g, i);
        save_run(g, pf, run.at, to, run.bytes);
        to = (uint16_t)(to + run.bytes);
    }
    put_saved_word(pf, PF_AT(0xFFFA58), (uint16_t)(SAVES_SLOT_SUMS + 2 * slot), game_sum(pf, game));
    put_saved_word(pf, PF_AT(0xFFFA5F), (uint16_t)(SAVES_SLOT_MARKS + 2 * slot), SAVED_GAME_MARK);
    sv_write8(pf, PF_AT(0xFFFA67), SAVES_LAST_SLOT, slot);
    count_on_the_cartridge(g, pf);
    ORIGINAL_EFFECT(leave_saved(g, on_the_bus));
}

/* ---- Loading -------------------------------------------------------------------------------- */

static void leave_loaded(GameState *g)
{
    uint32_t last = menu_saved_run(g, MENU_SAVED_RUNS - 1).at;
    leave_saved_runs_table(g);
    leave_block_move(g, bank_of(last), SAVE_MEMORY_BANK);
    set_menu_scratch_run_at(g, (uint16_t)last);
}

// $FF:FAA3
/* Load the slot's game: every run of work RAM it holds, back where it was. */
void saves_load_game(GameState *g, Platform *pf, uint8_t slot)
{
    uint16_t on_the_bus = saves_slot_on_the_bus(g, slot);
    set_menu_slot_game_at(g, on_the_bus);
    uint16_t from = saves_game_in_memory(on_the_bus);
    for (unsigned i = 0; i < MENU_SAVED_RUNS; i++) {
        MenuSavedRun run = menu_saved_run(g, i);
        load_run(g, pf, from, run.at, run.bytes);
        from = (uint16_t)(from + run.bytes);
    }
    ORIGINAL_EFFECT(leave_loaded(g));
}

/* ---- A New Game+ ---------------------------------------------------------------------------- */

/* Where in a saved game the characters whose locked techs have been unlocked
 * are (party_techs_unlocked, in the story's memory's run). */
enum { SAVED_TECHS_UNLOCKED = 0x7E3 };

static void leave_carried_over(GameState *g)
{
    uint32_t last = menu_carried_run(g, MENU_CARRIED_RUNS - 1).at;
    leave_carried_runs_table(g);
    leave_block_move(g, bank_of(last), SAVE_MEMORY_BANK);
    set_menu_scratch_run_at(g, (uint16_t)last);
}

/* What a New Game+ takes over from the game it carries on from. */
static void take_over_from(GameState *g, Platform *pf, uint8_t slot)
{
    uint16_t on_the_bus = saves_slot_on_the_bus(g, slot), game = saves_game_in_memory(on_the_bus);
    set_menu_slot_game_at(g, on_the_bus);
    for (unsigned i = 0; i < MENU_CARRIED_RUNS; i++) {
        MenuCarriedRun run = menu_carried_run(g, i);
        load_run(g, pf, (uint16_t)(game + run.from), run.at, run.bytes);
    }
    ORIGINAL_EFFECT(leave_carried_over(g));
    set_party_techs_unlocked(g, sv_read8(pf, PF_AT(0xFFFB3F), (uint16_t)(game + SAVED_TECHS_UNLOCKED)));
}

/* The story's key items, which a New Game+ leaves behind: an item kind's
 * bit. */
enum { KEY_ITEM = 0x10, NO_ITEM = 0 };

static void leave_key_items_behind(GameState *g)
{
    for (unsigned place = 0; place < INVENTORY_ITEMS_COUNT; place++) {
        uint8_t item = inventory_items(g, place);
        if (item != NO_ITEM && (menu_item_kinds(g, item) & KEY_ITEM)) {
            set_inventory_items(g, place, NO_ITEM);
            set_inventory_counts(g, place, 0);
        }
    }
}

/* The story's note that this game carries on from a finished one. */
enum { CARRIED_OVER = 0x10 };

static void note_carried_over(GameState *g) { set_story_game_notes(g, CARRIED_OVER); }

/* Ayla fights with her fists, which are no item, so her weapon isn't put
 * back on. */
enum { AYLA = 5 };

static unsigned equipment_put_back(uint8_t character) { return character == AYLA ? MENU_STARTING_EQUIPMENT - 1 : MENU_STARTING_EQUIPMENT; }

/* The check's answer as the original hands it over, in A and in its
 * caller's first cell, beside the slot's place: on the menus' page or the
 * clock's. Nothing reads the answer from the cell. */
enum { ANSWER_HELD = 0x0000, ANSWER_NONE = 0x8080, ANSWER_SPOILT = 0x4040 };

static uint16_t check_answer(SavedGameCheck check)
{
    return check == SAVED_GAME_HELD ? ANSWER_HELD : check == SAVED_GAME_NONE ? ANSWER_NONE : ANSWER_SPOILT;
}

static void leave_check_answer(GameState *g, SaveCheckPage page, SavedGameCheck check)
{
    set_save_check_answer(g, page, check_answer(check));
}

SavedGameCheck saves_check_slot_noting(GameState *g, Platform *pf, SaveCheckPage page, uint8_t slot)
{
    SavedGameCheck check = saves_check_slot(g, pf, slot);
    set_save_check_game_at(g, page, saves_slot_on_the_bus(g, slot));
    ORIGINAL_EFFECT(leave_check_answer(g, page, check));
    return check;
}

/* ---- A game's start --------------------------------------------------------------------------- */

/* What a game starts from before it is loaded or begun: the story's memory,
 * the clock's page (the time played, the player's own button assignments,
 * the menus' services' cells), the settings and who has joined, all clear;
 * then the settings and the button assignments the game starts with. */
enum { STORY_BYTES = 0x200, CLOCK_PAGE_BYTES = 0x100, SETTINGS_TO_JOINED_BYTES = 0x20 };

// $C2:95BB
void saves_clear_the_game(GameState *g)
{
    wram_fill8(g, RAM_EVENT_MEMORY, STORY_BYTES, 0);
    wram_fill8(g, RAM_PLAY_TIME, CLOCK_PAGE_BYTES, 0);
    wram_fill8(g, RAM_GAME_SETTINGS, SETTINGS_TO_JOINED_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, STORY_BYTES + CLOCK_PAGE_BYTES + SETTINGS_TO_JOINED_BYTES));
    for (unsigned i = 0; i < GAME_SETTINGS_COUNT; i++)
        set_game_settings(g, i, menu_new_game_setting(g, i));
    for (unsigned i = 0; i < BUTTON_ASSIGNMENTS_COUNT; i++)
        set_button_assignments(g, i, menu_default_button_assignment(g, i));
    set_button_assignment_spare(g, menu_default_button_assignment(g, BUTTON_ASSIGNMENTS_COUNT));
    for (unsigned i = 0; i < OWN_BUTTON_ASSIGNMENTS_COUNT; i++)
        set_own_button_assignments(g, i, menu_default_button_assignment(g, i));
}

/* A new game: the inventory empty, every character's record and what it
 * has learned as the game starts them, no gold, nobody in the party but the
 * party's places all character 0's, emptied, and Crono joined; the battle's
 * remembered cursors at their starts. Who has joined is written as a word,
 * over the first member's place on the battlefield, which every battle's
 * start sets again. */
enum { INVENTORY_BYTES = 0x200, REMEMBERED_CURSORS_BYTES = 0x1E, CRONO = 0 };

// $C2:956E
void saves_start_a_new_game(GameState *g)
{
    saves_clear_the_game(g);
    wram_fill8(g, RAM_INVENTORY_ITEMS, INVENTORY_BYTES, 0);
    for (unsigned i = 0; i < MENU_NEW_GAME_RECORDS_BYTES; i++)
        wram_write8(g, RAM_CHARACTER_RECORDS + i, menu_new_game_record_byte(g, i));
    set_party_gold_low(g, 0);
    set_party_gold_high(g, 0);
    for (unsigned place = 0; place < PARTY_ACTIVE_PLACES; place++)
        set_party_members(g, place, PARTY_PLACE_EMPTIED);
    for (unsigned place = 0; place < PARTY_WAITING_COUNT; place++)
        set_party_waiting(g, place, PARTY_PLACE_EMPTIED);
    set_party_characters_joined(g, menu_character_bit(g, CRONO));
    set_battle_party_placement_tile_x(g, 0, 0);
    wram_fill8(g, RAM_BATTLE_REMEMBERED_CURSORS, REMEMBERED_CURSORS_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, INVENTORY_BYTES + MENU_NEW_GAME_RECORDS_BYTES + PARTY_RECORD_PLACES
                                             + REMEMBERED_CURSORS_BYTES));
}

// $C2:8D6A
/* At power on: the slots looked at, first to last, for a game held. With
 * one the game is cleared for it to load into; with none a new game starts.
 * The check runs on the clock's page, which keeps the slot it checks. */
SavesAtPowerOn saves_power_on(GameState *g, Platform *pf)
{
    for (uint8_t slot = 0; slot < MENU_SAVE_SLOTS; slot++) {
        set_save_check_slot(g, SAVE_CHECK_CLOCK, slot);
        if (saves_check_slot_noting(g, pf, SAVE_CHECK_CLOCK, slot) == SAVED_GAME_HELD) {
            saves_clear_the_game(g);
            saves_note_how_it_starts(g, SAVES_GAME_TO_LOAD);
            return SAVES_GAME_TO_LOAD;
        }
    }
    saves_start_a_new_game(g);
    saves_note_how_it_starts(g, SAVES_NOTHING_SAVED);
    return SAVES_NOTHING_SAVED;
}

/* ---- The registered entries ----------------------------------------------------------------- */

// $FF:F9C4
/* The check runs on its caller's page: the slot is read from there, and the
 * slot's place and the answer left there. */
void menu_check_save_slot(Snes *s)
{
    GameState *g = state_of(s);
    SaveCheckPage page;
    (void)save_check_page_of(m_direct_page(s), &page);
    SavedGameCheck check = saves_check_slot_noting(g, m_platform(s), page, save_check_slot(g, page));
    s->cpu.a = check_answer(check);
    m_rtl(s);
}

// $FF:F9FB
void menu_save_game(Snes *s)
{
    GameState *g = state_of(s);
    saves_save_game(g, m_platform(s), menu_save_slot_shown(g));
    m_rtl(s);
}

// $FF:FAA3
/* The load comes back with A's high byte clear, which the menus' exit
 * carries on. */

void menu_load_game(Snes *s)
{
    GameState *g = state_of(s);
    saves_load_game(g, m_platform(s), menu_save_slot_shown(g));
    ORIGINAL_EFFECT(menu_leave_a_high_byte_clear(s));
    m_rtl(s);
}

/* What the original leaves as it starts on a character: the multiplier with
 * the character's starting record's place worked out, and the place in its
 * cells. */
static void leave_record_place(GameState *g, uint8_t character)
{
    uint16_t place = (uint16_t)(character * MENU_STARTING_RECORD_BYTES);
    hw_set_multiplicand(g, character);
    (void)hw_multiply_by(g, MENU_STARTING_RECORD_BYTES);
    set_menu_starting_record_at(g, MENU_ITEM_TABLES_BANK | place);
}

/* ... and as each piece of equipment is put on: the character and the place
 * in its equipment. */
static void leave_equipping(GameState *g, uint8_t character, uint16_t which)
{
    set_menu_scratch_carried_character(g, character);
    set_menu_scratch_carried_slot(g, which);
}

/* ... and once every character has its equipment: both counts run out (the
 * place's count a word, run out in its low byte). */
enum { RUN_OUT = 0xFF };

static void leave_everyone_equipped(GameState *g)
{
    set_menu_scratch_carried_character(g, RUN_OUT);
    set_menu_scratch_carried_slot(g, RUN_OUT);
}

/* The menus' services, whose time the equipping's and the filling's is. */
enum { MENUS_SERVICES = 0xC28004 };

// $FF:FAED
/* A New Game+ carries on from the finished game in a slot: what it takes
 * over, each character's starting equipment put back on (the service
 * equipping it), everyone's HP and MP filled, and the story's key items left
 * behind. The fillings take the arguments the service before left, as the
 * original's calls hand them on. */
void saves_carry_over(GameState *g, Platform *pf, uint8_t slot)
{
    take_over_from(g, pf, slot);
    for (int character = CHARACTER_RECORDS_COUNT - 1; character >= 0; character--) {
        ORIGINAL_EFFECT(leave_record_place(g, (uint8_t)character));
        for (int which = (int)equipment_put_back((uint8_t)character) - 1; which >= 0; which--) {
            uint8_t item = menu_starting_record_byte(g, (uint8_t)character, menu_starting_equipment_place(g, (unsigned)which));
            ORIGINAL_EFFECT(leave_equipping(g, (uint8_t)character, (uint16_t)which));
            TIMED_AS(g, MENUS_SERVICES, (void)party_run_service(g, pf, SERVICE_EQUIP, (uint8_t)character, item));
        }
    }
    ORIGINAL_EFFECT(leave_everyone_equipped(g));
    TIMED_AS(g, MENUS_SERVICES, (void)party_run_service_as_left(g, pf, SERVICE_FILL_HP, SERVICE_EQUIP));
    TIMED_AS(g, MENUS_SERVICES, (void)party_run_service_as_left(g, pf, SERVICE_FILL_MP, SERVICE_FILL_HP));
    leave_key_items_behind(g);
    note_carried_over(g);
}

/* The load comes back with A as the last service leaves it: its answer, 0,
 * a word. */
static void leave_a_clear(Snes *s) { s->cpu.a = 0; }

void menu_load_carried_over(Snes *s)
{
    GameState *g = state_of(s);
    saves_carry_over(g, m_platform(s), menu_save_slot_shown(g));
    ORIGINAL_EFFECT(leave_a_clear(s));
    m_rtl(s);
}

// $C2:956E
/* A new game's state, as the menus' opening and the load screen start one:
 * the block moves that set it up leave A, X and Y past their ends. */
static void leave_new_game_set_up(Snes *s)
{
    enum { MOVED = 0xFFFF, CURSORS_END = 0x2C98, CURSORS_END_DESTINATION = 0x2C9A };
    s->cpu.a = MOVED;
    s->cpu.x = m_x8(s) ? (uint8_t)CURSORS_END : CURSORS_END;
    s->cpu.y = m_x8(s) ? (uint8_t)CURSORS_END_DESTINATION : CURSORS_END_DESTINATION;
}

void menu_start_a_new_game(Snes *s)
{
    saves_start_a_new_game(state_of(s));
    ORIGINAL_EFFECT(leave_new_game_set_up(s));
    m_rts(s);
}

// $C2:8D6A
/* The party's power-on service, which hands back how the game starts in A,
 * with byte-wide registers; A's high byte is as the block moves that clear
 * or start the game leave it, which the dispatcher hands on. */
enum { BLOCK_MOVED_HIGH = 0xFF00 };

void menu_power_on(Snes *s)
{
    SavesAtPowerOn start = saves_power_on(state_of(s), m_platform(s));
    menu_set_widths(s, true, true);
    s->cpu.a = (uint16_t)(BLOCK_MOVED_HIGH | (uint8_t)start);
    m_rts(s);
}

/* ---- The guards ----------------------------------------------------------------------------- */

/* The check runs on one of the two pages it is called on, for one of the
 * three slots. */
bool menu_save_slot_is_checkable(Snes *s)
{
    SaveCheckPage page;
    if (!save_check_page_of(m_direct_page(s), &page))
        return false;
    vars_peek(true);
    bool ok = save_check_slot(state_of(s), page) < MENU_SAVE_SLOTS;
    vars_peek(false);
    return ok;
}

/* The power-on looks at the slots on the clock's page, and starts a new
 * game through the data bank on work RAM's. */
bool menu_power_on_runs(Snes *s)
{
    SaveCheckPage page;
    return save_check_page_of(m_direct_page(s), &page) && page == SAVE_CHECK_CLOCK && menu_new_game_can_start(s);
}

/* The save and the loads run on the menus' page with the data bank on work
 * RAM's, for one of the three slots; they start their table of runs from the
 * menus' zero word, which is 0 whenever the menus run. */
bool menu_save_code_runs(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_save_slot_shown(state_of(s)) < MENU_SAVE_SLOTS && menu_zero(state_of(s)) == 0;
    vars_peek(false);
    return ok;
}

/* A New Game+ runs as the save code does, with the active party's members
 * characters, as the services that put their equipment back read them. */
bool menu_carry_over_runs(Snes *s)
{
    if (!menu_save_code_runs(s))
        return false;
    vars_peek(true);
    bool ok = party_record_shows_characters(state_of(s));
    vars_peek(false);
    return ok;
}

/* A new game's state is set up through the data bank, on work RAM's. */
bool menu_new_game_can_start(Snes *s) { return s->cpu.db == MENU_DATA_BANK; }

