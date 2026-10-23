/* The sound driver's CPU half (bank $C7): what the main processor does so that
 * the sound processor plays.
 *
 * The sound processor runs a driver of its own (docs/SOUND.md; game/sound/ is
 * that driver in C, which the player plays in its place). This half, on the
 * main processor, has three jobs:
 *
 * - Booting the driver: its program goes up to the sound processor's boot ROM
 *   a byte at a time, each byte's index echoed back, then the boot ROM is told
 *   to run it, and the CPU half gives it a moment to start.
 * - Carrying out the game's commands. A caller puts a command and its three
 *   parameters in sound_command and calls the driver ($C7:0004, which the
 *   audio service's au_command does). Most commands go to the driver as they
 *   are, and are done once it echoes them. A song has its samples, tables,
 *   sequence and percussion sent first. Two commands are the CPU half's own:
 *   $70 switches the set of sound effects the driver has, $71 loads one more
 *   sample for the song playing.
 * - Keeping a record of the driver's sound memory. The samples the songs'
 *   instruments are made of (blocks, numbered 1-62) lie in sixteen slots, end
 *   to end after the boot's own samples, below the echo's buffer at the top.
 *   A song keeps the samples it shares with the last one, moved down to close
 *   the gaps the others leave, and loads the ones it lacks after them; a
 *   change of the echo's delay forgets the slots its buffer now covers.
 *
 * Talking to the driver. The processors share four ports. A message is up to
 * three bytes on ports 1-3 and a count on port 0, which the driver echoes once
 * it has the bytes (its last two it reads after it echoes, so the next
 * message must not reach the ports before it has). Data for sound memory goes
 * in a transfer: a header with the address on ports 2-3 and a kind on port 1
 * (how many bytes a message carries, or moves within sound memory), counted
 * from $E0, which the driver confirms by showing the kind on port 1 (else the
 * count goes again, and the header with it); then the data's messages, the
 * count running on in seven bits. The next transfer starts at $E0 again. A
 * song's upload ends with $E0 on ports 1 and 0.
 *
 * How it is built. Every wait for the driver's answer is made through the
 * audio service (platform/audio.h): an echo, port 0 moving on, an echo that
 * the original stops waiting for after a number of polls, and the exchange
 * that ends a message, which the original makes by calling its routine at
 * $C7:09DA. In the shipping mode the port waits where it stands; shadow mode
 * meets the original at each wait, so each names the original's place: the
 * call of the exchange routine that ends a message (a Site), or the loop a
 * wait stands for. The routines the original calls that wait inside
 * themselves -- a song's command ($C7:0A39), a block's upload ($C7:0655), the
 * effects' upload ($C7:0922) -- are called as C, inside the original's frame
 * for them (original_frame.h), so the waits in them meet the original's with
 * its stack as it is there. Everything is C over the game's state: what the
 * driver keeps lives in its work RAM page at $1E00 (re/ram.toml), and the
 * cells the original also leaves there along the way, which only shadow mode
 * looks at, are written as scaffolding. The machine is in the entries'
 * wrappers, at the end of the file. */
#include "core/sound_driver.h"

#include "charge.h"
#include "core/sound_data.h"
#include "original_effects.h"
#include "original_frame.h"
#include "platform/audio.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The driver's state ---------------------------------------------------------------- */

/* While a song is going up the driver turns every command away: the cell
 * holds the song, a number below $80; between songs, SONG_READY. */
enum { SONG_READY = 0xE0, NOT_LOADING = 0x80 };

static bool loading_a_song(const GameState *g) { return !(sound_driver_song(g) & NOT_LOADING); }

/* The slots. Each holds a block (none, 0), its length, and where it starts in
 * sound memory; the next slot starts where it ends. */
typedef uint8_t Block;
enum { SLOTS = 16, NO_BLOCK = 0 };

static Block slot_block(const GameState *g, unsigned slot) { return sound_driver_slot_ids(g, slot); }
static void hold(GameState *g, unsigned slot, Block block) { set_sound_driver_slot_ids(g, slot, block); }
static uint16_t slot_length(const GameState *g, unsigned slot) { return sound_driver_slot_lengths(g, slot); }
static void set_slot_length(GameState *g, unsigned slot, uint16_t length)
{
    set_sound_driver_slot_lengths(g, slot, length);
}
static uint16_t slot_start(const GameState *g, unsigned slot) { return sound_driver_slot_addresses(g, slot); }
static uint16_t slot_end(const GameState *g, unsigned slot) { return sound_driver_slot_addresses(g, slot + 1); }
static void set_slot_end(GameState *g, unsigned slot, uint16_t end)
{
    set_sound_driver_slot_addresses(g, slot + 1, end);
}

static bool slot_free(const GameState *g, unsigned slot) { return slot_block(g, slot) == NO_BLOCK; }

/* Forget a slot and those after it: what each holds, how long it is and where
 * it ends. */
static void forget_slots_from(GameState *g, unsigned first)
{
    for (unsigned slot = first; slot < SLOTS; slot++) {
        hold(g, slot, NO_BLOCK);
        set_slot_length(g, slot, 0);
        set_slot_end(g, slot, 0);
    }
}

/* The echo's buffer takes the top of sound memory, 2 KiB a unit of its delay
 * down from $F0FF: it starts at page $F1 less 8 a unit. */
static uint8_t echo_page_for(uint8_t delay) { return (uint8_t)(0xF1 - (delay << 3)); }

// $C7:0A12
/* Forget the slots from the first that ends at or past where the echo's
 * buffer starts. The slots in use end where one ends at page 0. */
static void forget_slots_under_the_echo(GameState *g)
{
    for (unsigned slot = 0; slot < SLOTS; slot++) {
        uint8_t end_page = (uint8_t)(slot_end(g, slot) >> 8);
        if (end_page == 0)
            return;
        if (end_page >= sound_driver_echo_page(g)) {
            forget_slots_from(g, slot);
            return;
        }
    }
}

// $C7:09FD
/* The echo plays with a new delay: its buffer moves, and what samples it now
 * covers are forgotten. */
static void set_echo_delay(GameState *g, uint8_t delay)
{
    set_sound_driver_echo_delay(g, delay);
    set_sound_driver_echo_page(g, echo_page_for(delay));
    forget_slots_under_the_echo(g);
}

static void keep_echo_delay(GameState *g, uint8_t delay)
{
    if (delay != sound_driver_echo_delay(g))
        set_echo_delay(g, delay);
}

/* The set of sound effects at $2F00, the field's or the battle's: the one the
 * game asks for (which the field sets before some songs), the one the driver
 * was last sent, and whether it is due to go up again before the next song
 * (the set asked changed, or the last song's sequence ran into it). */
static bool battle_effects(uint8_t asked) { return asked != 0; }

static void send_the_effects_asked(GameState *g, bool battle)
{
    set_sound_driver_effects_asked(g, battle);
    set_sound_driver_effects_sent(g, battle);
}

/* The cell counts the reasons the effects are due; any is enough. */
static void effects_due(GameState *g) { set_sound_driver_effects_due(g, (uint8_t)(sound_driver_effects_due(g) + 1)); }

/* Command $71's slot: the one that holds the song's first sample, kept as the
 * original's index (the slot times 2). */
static unsigned first_sample_slot(const GameState *g) { return sound_driver_first_sample_slot(g) / 2u; }
static void set_first_sample_slot(GameState *g, unsigned slot)
{
    set_sound_driver_first_sample_slot(g, (uint16_t)(2 * slot));
}

/* ---- What only shadow mode reads ------------------------------------------------------
 *
 * The original works through cells of its page that nothing reads but its
 * own next instructions: where in the cartridge it is reading (a long
 * pointer, with the streams below), a work word, an index it multiplies,
 * lists it sorts a song's blocks into, and where each has got to. They are
 * left as it leaves them. */

static void leave_work(GameState *g, uint16_t v) { set_sound_driver_work(g, v); }
static void leave_index(GameState *g, uint16_t v) { set_sound_driver_index_scratch(g, v); }

/* ---- Messages ------------------------------------------------------------------------------ */

/* The original's place for a message: its call of the exchange routine that
 * ends it, where shadow mode meets the port (and where the message's writes
 * to the ports are reported from). */
typedef uint32_t Site;

enum { EXCHANGE = 0xC709DA, EXCHANGE_AGAIN = 0xC709EA };
/* $C7:09DA takes the count in A's low byte, $C7:09EA from the count's cell;
 * neither reads anything else of the caller's. */
enum { EXCHANGE_IGNORES = DC_X | DC_Y | DC_FLAGS | DC_AH, EXCHANGE_AGAIN_IGNORES = DC_A | DC_X | DC_Y | DC_FLAGS };

/* A transfer's counts start here, and run on in seven bits. */
enum { FIRST_COUNT = 0xE0, COUNT_BITS = 0x7F };
static uint8_t count_after(uint8_t count) { return (count + 1) & COUNT_BITS; }

typedef struct {
    GameState *g;
    Platform *pf;
} Link;

static uint8_t count(const Link *l) { return sound_driver_upload_counter(l->g); }
static void set_count(Link *l, uint8_t c) { set_sound_driver_upload_counter(l->g, c); }

static void send(Link *l, Site at, uint8_t port, uint8_t v) { au_driver_write(l->pf, PF_AT(at), port, v); }
static uint8_t shown(Link *l, Site at, uint8_t port) { return au_driver_read(l->pf, PF_AT(at), port); }

/* Two bytes of a message, a word low byte first: on ports 1-2 (a table's
 * word), or 2-3 (an address in sound memory). */
static void send_word(Link *l, Site at, uint16_t v)
{
    send(l, at, 1, v & 0xFF);
    send(l, at, 2, v >> 8);
}
static void send_address(Link *l, Site at, uint16_t to)
{
    send(l, at, 2, to & 0xFF);
    send(l, at, 3, to >> 8);
}

/* End a message with `c` and wait for its echo; the count after it. */
static uint8_t exchange(Link *l, Site at, uint8_t c)
{
    au_driver_exchange(l->pf, PF_CALL_DC(at, EXCHANGE, EXCHANGE_IGNORES), c);
    return count_after(c);
}

/* End a message with the transfer's count, and count on. */
static void counted(Link *l, Site at) { set_count(l, exchange(l, at, count(l))); }

/* A transfer is over: the next starts from the first count. */
static void end_transfer(Link *l) { set_count(l, FIRST_COUNT); }

/* Header kinds: words of a table; sound memory, three bytes a message (or the
 * same announced as one of the CPU half's own commands', for $70's effects
 * and $71's sample); and moves within sound memory. */
enum { KIND_WORDS = 2, KIND_DATA = 3, KIND_COMMANDS_DATA = 5, KIND_MOVE = 7 };

/* A transfer's header, sent until the driver confirms its kind: the count of
 * one it didn't goes again first (the original's call of $C7:09EA at
 * `again`), which starts its count over. */
static void send_header(Link *l, Site at, Site again, uint16_t to, uint8_t kind)
{
    for (;;) {
        send_address(l, at, to);
        send(l, at, 1, kind);
        counted(l, at);
        if (shown(l, at, 1) == kind)
            return;
        au_driver_exchange(l->pf, PF_CALL_DC(again, EXCHANGE_AGAIN, EXCHANGE_AGAIN_IGNORES), count(l));
        end_transfer(l);
    }
}

/* A table's header, which the driver doesn't confirm, counted from the first
 * count. */
static void send_table_header(Link *l, Site at, uint16_t to)
{
    send_address(l, at, to);
    send(l, at, 1, KIND_WORDS);
    set_count(l, exchange(l, at, FIRST_COUNT));
}

/* A table of one word: command $71's tuning and envelope. */
static void send_table_word(Link *l, Site header, uint16_t to, Site at, uint16_t word)
{
    send_table_header(l, header, to);
    send_word(l, at, word);
    (void)exchange(l, at, count(l));
}

/* The end of a song's upload, or of a command's own transfer: $E0 on ports 1
 * and 0, which the driver echoes in its own time (the original stops waiting
 * in its loop from `loop` to `after` after ECHO_TRIES polls, and goes on the
 * same either way). */
enum { ECHO_TRIES = 0x0400 };

static void send_the_end(Link *l, Site at, Site loop, Site after)
{
    send(l, at, 1, FIRST_COUNT);
    send(l, at, 0, FIRST_COUNT);
    au_driver_wait_echo_or_give_up(l->pf, PF_LOOP(loop, after), after, FIRST_COUNT, ECHO_TRIES);
}

/* ---- Data from the cartridge ------------------------------------------------------------
 *
 * A resource goes up three bytes a message, read in order from where the
 * cartridge keeps it (core/sound_data.h): a block, a song's sequence, a set
 * of effects, a song's percussion. The original reads it through a long
 * pointer in its page, which shadow mode compares: a resource from one of the
 * tables (a block, a sequence) by its bank alone, the offset in a register,
 * the bank moved on where the offset wraps in the loop it keeps for data that
 * runs into the next bank; the others by where they are. */
typedef enum { A_BLOCK, A_SEQUENCE, AN_EFFECT_SET, A_SONGS_PERCUSSION } Resource;

typedef struct {
    Resource what;
    SoundBytes bytes;
    uint16_t next;       /* the next byte's index */
    bool across_banks;   /* sent from the original's loop for data that runs into the next bank */
} Stream;

/* Where the original's long pointer stands for the stream. */
static void leave_data_pointer(GameState *g, const Stream *d)
{
    uint32_t place = sound_bytes_place(d->bytes);
    if (d->what == A_BLOCK || d->what == A_SEQUENCE)
        place = (d->across_banks ? place + d->next : place) & 0xFF0000u;
    else if (d->what == A_SONGS_PERCUSSION)
        place = sound_bytes_place(sound_song_percussion_bytes(0));   /* the table's, the song's an offset into it */
    set_sound_driver_data_pointer(g, place);
}

static void leave_the_next_bank(GameState *g, const Stream *d)
{
    if (d->across_banks && sound_bytes_start_a_bank(d->bytes, d->next))
        leave_data_pointer(g, d);
}

static Stream stream_of(GameState *g, Resource what, SoundBytes bytes)
{
    Stream d = { what, bytes, 0, false };
    ORIGINAL_EFFECT(leave_data_pointer(g, &d));
    return d;
}

static uint8_t next_byte(GameState *g, Stream *d)
{
    uint8_t v = sound_bytes_byte(g, d->bytes, d->next++);
    ORIGINAL_EFFECT(leave_the_next_bank(g, d));
    return v;
}

/* Whether the original sends `length` bytes from here in its loop for data
 * that runs into the next bank, which has an exchange of its own. */
static bool sent_across_banks(Stream *d, uint16_t length)
{
    d->across_banks = sound_bytes_reach_a_banks_end(d->bytes, d->next, length);
    return d->across_banks;
}

/* The command's routine, whose rate the time charged here goes at. */
enum { SOUND_COMMAND = 0xC70004 };

/* The original loads each byte through its long pointer and steps its index
 * before it stores the byte to its port; the port has the byte in hand. The
 * load is charged before the byte goes, so that a message's bytes reach the
 * ports as far apart as the original's: the SPC700 reads a message's last
 * two bytes after it echoes, and the next message's last byte, sent sooner,
 * came within a few of its cycles of overwriting the one it hadn't read
 * (docs/PLATFORM.md, "The CPU half's waits, as built"). */
enum { ACCESSES_A_LOAD = 2 };

/* `messages` messages of three bytes on ports 1-3, counted. Every length in
 * the cartridge sent this way is a multiple of three. */
static void send_messages(Link *l, Stream *d, Site at, unsigned messages)
{
    for (unsigned n = 0; n < messages; n++) {
        for (uint8_t port = 1; port <= 3; port++) {
            ORIGINAL_EFFECT(charge_as(l->g, SOUND_COMMAND, ACCESSES_A_LOAD));
            send(l, at, port, next_byte(l->g, d));
        }
        counted(l, at);
    }
}

/* ---- A song's samples ------------------------------------------------------------------------ */

/* The blocks a song uses, in the order of its instruments (its sixteen
 * entries as the lists hold them, zeros after the last), and those of them no
 * slot held. */
typedef struct {
    Block blocks[SLOTS], missing[SLOTS];
    unsigned count, count_missing;
} SongBlocks;

static void leave_lists(GameState *g, const SongBlocks *b)
{
    for (unsigned i = 0; i < SLOTS; i++) {
        set_sound_driver_song_ids(g, i, b->blocks[i]);
        set_sound_driver_missing_ids(g, i, b->missing[i]);
    }
}

// $C7:01D2
/* Where the lists have got to, the slots in use and the end of the song's
 * entries in its table. */
static void leave_list_ends(GameState *g, uint8_t song, const SongBlocks *b, unsigned used_end)
{
    set_sound_driver_list_ends(g, 0, (uint16_t)(RAM_SOUND_DRIVER_SONG_IDS + 2 * b->count));
    set_sound_driver_list_ends(g, 1, (uint16_t)(RAM_SOUND_DRIVER_MISSING_IDS + 2 * b->count_missing));
    leave_work(g, (uint16_t)(2 * used_end));
    set_sound_driver_song_ids_end(g, (uint16_t)(2 * SLOTS * song + 2 * SLOTS));
}

/* How far the slots in use reach: past the last that holds a block. */
static unsigned slots_in_use(const GameState *g)
{
    unsigned end = SLOTS;
    while (end > 0 && slot_free(g, end - 1))
        end--;
    return end;
}

/* The last of the slots below `end` that holds `block`, or SLOTS for none. */
static unsigned slot_holding_below(const GameState *g, unsigned end, Block block)
{
    for (unsigned slot = end; slot-- > 0;)
        if (slot_block(g, slot) == block)
            return slot;
    return SLOTS;
}

/* The original's searches compare a word a slot or a list's entry, and step
 * their loop; the port asks its slots and lists. Charged for each compared. */
enum { ACCESSES_A_COMPARE = 4 };

/* The original looks for each of the song's blocks in the slots in use, from
 * the last down. */
static void charge_the_slot_search(GameState *g, unsigned end, unsigned held)
{
    charge_as(g, SOUND_COMMAND, ACCESSES_A_COMPARE * (held == SLOTS ? end : end - held));
}

// $C7:01B5
/* The song's blocks, and those no slot holds. */
static SongBlocks blocks_of(GameState *g, uint8_t song)
{
    SongBlocks b = { .count = 0, .count_missing = 0 };
    unsigned used_end = slots_in_use(g);
    for (unsigned i = 0; i < SLOTS; i++) {
        Block block = sound_song_instrument_block(g, song, (uint8_t)i);
        if (block == NO_BLOCK)
            break;
        b.blocks[b.count++] = block;
        unsigned held = slot_holding_below(g, used_end, block);
        ORIGINAL_EFFECT(charge_the_slot_search(g, used_end, held));
        if (held == SLOTS)
            b.missing[b.count_missing++] = block;
    }
    ORIGINAL_EFFECT(leave_lists(g, &b));
    ORIGINAL_EFFECT(leave_list_ends(g, song, &b, used_end));
    return b;
}

static bool song_uses(const SongBlocks *b, Block block)
{
    for (unsigned i = 0; i < b->count; i++)
        if (b->blocks[i] == block)
            return true;
    return false;
}

/* The original looks for a slot's block in the song's list in its page,
 * through the whole list for one the song doesn't use; the port has the list
 * in hand. */
static void charge_the_list_search(GameState *g, const SongBlocks *b, Block block)
{
    unsigned compared = SLOTS;
    for (unsigned i = 0; i < SLOTS; i++)
        if (b->blocks[i] == block) {
            compared = i + 1;
            break;
        }
    charge_as(g, SOUND_COMMAND, ACCESSES_A_COMPARE * compared);
}

// $C7:0217
/* The song keeps the slots that hold its blocks; the others hold nothing now
 * (up to the first that held nothing). */
static void free_what_the_song_doesnt_use(GameState *g, const SongBlocks *b)
{
    for (unsigned slot = 0; slot < SLOTS && !slot_free(g, slot); slot++) {
        Block block = slot_block(g, slot);
        ORIGINAL_EFFECT(charge_the_list_search(g, b, block));
        if (!song_uses(b, block))
            hold(g, slot, NO_BLOCK);
    }
}

static unsigned first_free_slot(const GameState *g)
{
    unsigned slot = 0;
    while (slot < SLOTS && !slot_free(g, slot))
        slot++;
    return slot;
}

// $C7:025A
/* Close up the gaps: each block kept goes down into the first free slot
 * before it, which the driver is told as a move within sound memory (where
 * the block is, where it goes, how long it is), in a transfer whose kind the
 * driver confirms after the first move. False where it doesn't. */
static bool move_blocks_down(Link *l)
{
    GameState *g = l->g;
    send(l, 0xC7025E, 1, KIND_MOVE);
    ORIGINAL_EFFECT(leave_work(g, 0));
    bool confirmed = false;
    for (unsigned free = 0; free < SLOTS; free++) {
        if (!slot_free(g, free))
            continue;
        unsigned kept = free + 1;
        while (kept < SLOTS && slot_free(g, kept))
            kept++;
        if (kept == SLOTS)
            break;
        send_address(l, 0xC7028B, slot_start(g, kept));
        counted(l, 0xC7028B);
        if (!confirmed) {
            confirmed = true;
            ORIGINAL_EFFECT(leave_work(g, 1));
            if (shown(l, 0xC70298, 1) != KIND_MOVE)
                return false;
        }
        uint16_t length = slot_length(g, kept), to = slot_start(g, free);
        hold(g, free, slot_block(g, kept));
        hold(g, kept, NO_BLOCK);
        send_address(l, 0xC702BF, to);
        set_slot_end(g, free, (uint16_t)(to + length));
        counted(l, 0xC702BF);
        set_slot_length(g, free, length);
        send_address(l, 0xC702D6, length);
        counted(l, 0xC702D6);
    }
    end_transfer(l);
    return true;
}

// $C7:02E4
/* The driver turned the moves down: it is sent zeros for good measure, and
 * every slot is forgotten, so that every block of the song is missing. */
static void forget_every_slot(Link *l, uint8_t song, SongBlocks *b)
{
    GameState *g = l->g;
    send_address(l, 0xC702F0, 0);
    (void)exchange(l, 0xC702F0, count(l));
    for (unsigned i = 0; i < SLOTS; i++) {
        b->blocks[i] = sound_song_instrument_block(g, song, (uint8_t)i);
        b->missing[i] = b->blocks[i];
    }
    forget_slots_from(g, 0);
    b->count_missing = b->count;
    ORIGINAL_EFFECT(leave_lists(g, b));
    end_transfer(l);
}

/* ---- A song's tables, sequence and percussion ----------------------------------------------- */

/* Where in sound memory the driver keeps what a song brings: the directory's
 * entries for its samples ($20 on), its instruments' tunings and envelopes,
 * its sequence, the set of effects after it, and its percussion. */
enum {
    DIRECTORY_TABLE = 0x1E80,
    TUNINGS_TABLE = 0x1F40,
    ENVELOPES_TABLE = 0x1FC0,
    SEQUENCE_TABLE = 0x2000,
    EFFECTS_TABLE = 0x2F00,
    PERCUSSION_TABLE = 0xF120,
};
/* A sequence this long reaches the effects. */
enum { ROOM_BEFORE_THE_EFFECTS = EFFECTS_TABLE - SEQUENCE_TABLE };

// $C7:037B
/* Each block of the song's directory entry: where the slot holding it starts
 * and where its sample loops from, or zeros for one no slot holds. The CPU
 * half keeps the table it sends; the entries past the song's last are what an
 * earlier song left. (The original's search runs on past the slots into
 * their lengths, which the entry's guard keeps from matching a block.) */
static void set_directory_entry(GameState *g, unsigned i, Block block)
{
    uint16_t start = 0, loop = 0;
    for (unsigned slot = 0; slot < SLOTS; slot++)
        if (slot_block(g, slot) == block) {
            start = slot_start(g, slot);
            loop = (uint16_t)(start + sound_block_loop(g, block));
            break;
        }
    set_sound_driver_address_table_address(g, i, start);
    set_sound_driver_address_table_second_address(g, i, loop);
}

static void leave_directory_end(GameState *g, unsigned entries)
{
    uint32_t pointer = sound_driver_data_pointer(g);
    set_sound_driver_data_pointer(g, (pointer & 0xFF0000u) | (uint16_t)(RAM_SOUND_DRIVER_ADDRESS_TABLE + 4 * entries));
}

static void send_directory(Link *l, const SongBlocks *b)
{
    GameState *g = l->g;
    for (unsigned i = 0; i < b->count; i++)
        set_directory_entry(g, i, b->blocks[i]);
    ORIGINAL_EFFECT(leave_directory_end(g, b->count));
    send_header(l, 0xC703DB, 0xC703E7, DIRECTORY_TABLE, KIND_WORDS);
    for (unsigned i = 0; i < SLOTS; i++) {
        send_word(l, 0xC703FD, sound_driver_address_table_address(g, i));
        counted(l, 0xC703FD);
        send_word(l, 0xC703FD, sound_driver_address_table_second_address(g, i));
        counted(l, 0xC703FD);
    }
    end_transfer(l);
}

/* A word for each of the song's sixteen entries (an entry without a block
 * reads the word before the table): the instruments' tunings, then their
 * envelopes. */
static void send_instruments(Link *l, const SongBlocks *b)
{
    GameState *g = l->g;
    send_header(l, 0xC70420, 0xC7042C, TUNINGS_TABLE, KIND_WORDS);
    for (unsigned i = 0; i < SLOTS; i++) {
        send_word(l, 0xC7044B, sound_instrument_tuning(g, b->blocks[i]));
        counted(l, 0xC7044B);
    }
    end_transfer(l);
    send_header(l, 0xC7046E, 0xC7047A, ENVELOPES_TABLE, KIND_WORDS);
    for (unsigned i = 0; i < SLOTS; i++) {
        send_word(l, 0xC70499, sound_instrument_envelope(g, b->blocks[i]));
        counted(l, 0xC70499);
    }
    end_transfer(l);
}

// $C7:04DB
/* The song's sequence: it goes up a message more than its bytes fill. */
static void send_sequence(Link *l, uint8_t song)
{
    GameState *g = l->g;
    send_header(l, 0xC704EE, 0xC704FA, SEQUENCE_TABLE, KIND_DATA);
    ORIGINAL_EFFECT(leave_index(g, song));
    Stream d = stream_of(g, A_SEQUENCE, sound_song_sequence_bytes(g, song));
    uint16_t length = sound_song_length(g, song);
    ORIGINAL_EFFECT(leave_work(g, length));
    if (length >= ROOM_BEFORE_THE_EFFECTS)
        effects_due(g);
    unsigned messages = length / 3u + 1;
    if (sent_across_banks(&d, length))
        send_messages(l, &d, 0xC7056C, messages);
    else
        send_messages(l, &d, 0xC7058C, messages);
    end_transfer(l);
}

// $C7:059A
/* The song's twelve percussion entries (an instrument, a note and a pan). */
static void send_percussion(Link *l, uint8_t song)
{
    GameState *g = l->g;
    ORIGINAL_EFFECT(leave_index(g, (uint16_t)(SOUND_PERCUSSION_ENTRIES * song)));
    Stream d = stream_of(g, A_SONGS_PERCUSSION, sound_song_percussion_bytes(song));
    send_header(l, 0xC705CF, 0xC705DB, PERCUSSION_TABLE, KIND_DATA);
    send_messages(l, &d, 0xC705F7, SOUND_PERCUSSION_ENTRIES);
}

// $C7:04A9
/* Command $71's slot is now the last that holds the song's first block; and
 * the effects are due again if the set the game asks for has changed. */
static bool effects_due_with_the_song(GameState *g, const SongBlocks *b)
{
    if (b->count > 0) {
        unsigned slot = SLOTS;
        while (slot > 0 && slot_block(g, slot - 1) != b->blocks[0])
            slot--;
        set_first_sample_slot(g, slot - 1);
    }
    bool battle = battle_effects(sound_driver_effects_asked(g));
    if (battle != sound_driver_effects_sent(g)) {
        send_the_effects_asked(g, battle);
        effects_due(g);
    }
    return sound_driver_effects_due(g) != 0;
}

/* ---- Commands -------------------------------------------------------------------------------- */

/* The original's frames for the routines it calls that wait inside
 * themselves (original_frame.h): a song's command and a block's upload save
 * the flags, the effects' upload doesn't. */
enum {
    SEND_SONG_FRAME = ORIGINAL_RETURN_BYTES + ORIGINAL_SAVED_FLAGS_BYTES,
    UPLOAD_BLOCK_FRAME = ORIGINAL_RETURN_BYTES + ORIGINAL_SAVED_FLAGS_BYTES,
    UPLOAD_EFFECTS_FRAME = ORIGINAL_RETURN_BYTES,
};

/* A command goes to the driver as it is: the parameters on ports 3-1, the
 * command on port 0. */
static uint8_t send_command(Link *l, Site at)
{
    GameState *g = l->g;
    send(l, at, 3, sound_command_parameters(g, 2));
    send(l, at, 2, sound_command_parameters(g, 1));
    send(l, at, 1, sound_command_parameters(g, 0));
    uint8_t command = sound_command(g);
    send(l, at, 0, command);
    return command;
}

enum { CHANGE_ECHO_DELAY = 0xFC };

// $C7:061C
/* A command as it is, sent until the driver echoes it; one the game has
 * cleared meanwhile isn't sent again. A change of the echo's delay forgets
 * the slots its buffer will cover. */
static void carry_out_as_is(Link *l)
{
    GameState *g = l->g;
    for (;;) {
        uint8_t command = send_command(l, 0xC7061C);
        if (au_driver_wait_echo_or_give_up(l->pf, PF_LOOP(0xC70635, 0xC70643), 0xC7063D, command, ECHO_TRIES)) {
            if (command == CHANGE_ECHO_DELAY)
                keep_echo_delay(g, sound_command_parameters(g, 0) & 0x0F);
            return;
        }
        if (sound_command(g) == 0)
            return;
    }
}

/* The driver's answer to a song's command, on port 1: it doesn't want the
 * song (the one already playing). */
enum { SONG_REFUSED = 0x80 };

// $C7:0A39
/* A song's command: its volume as the second parameter and its echo's delay
 * as the third (the echo plays with it from now), sent until the driver
 * echoes it. A song the driver doesn't want, it is told so with the next
 * count, and the CPU half waits for it to take that. True for a song it
 * wants. */
static bool send_song(Link *l)
{
    GameState *g = l->g;
    uint8_t song = sound_command_parameters(g, 0);
    set_sound_command_parameters(g, 1, sound_song_volume(g, song));
    uint8_t delay = sound_song_echo_delay(g, song);
    set_sound_command_parameters(g, 2, delay);
    keep_echo_delay(g, delay);
    uint8_t command;
    do
        command = send_command(l, 0xC70A5A);
    while (!au_driver_wait_echo_or_give_up(l->pf, PF_LOOP(0xC70A71, 0xC70A7B), 0xC70A79, command, ECHO_TRIES));
    set_count(l, count_after(command));
    if (!(shown(l, 0xC70A80, 1) & SONG_REFUSED))
        return true;
    send(l, 0xC70A87, 0, count(l));
    au_driver_wait_moved(l->pf, PF_LOOP(0xC70A8B, 0xC70A90), (uint8_t)(count(l) - 1));
    return false;
}

// $C7:0734
/* The commands that save the song playing first, and the songs listed to be
 * started that way, go to the driver with that bit set. */
enum { SAVING_FIRST = 0x04, FIRST_SAVING_COMMAND = 0x14, END_OF_LIST = 0x80 };

static void mark_a_song_that_saves_first(GameState *g)
{
    uint8_t command = sound_command(g);
    bool mark = command >= FIRST_SAVING_COMMAND;
    for (uint16_t i = 0; !mark; i++) {
        uint8_t listed = sound_song_saving_first(g, i);
        if (listed & END_OF_LIST)
            break;
        mark = listed == sound_command_parameters(g, 0);
    }
    if (mark)
        set_sound_command(g, command | SAVING_FIRST);
}

// $C7:0655
/* Load the block slot `slot` holds into it, in a transfer of its own: the
 * slot's length is the block's, and the next slot starts where it ends. */
static void upload_block(Link *l, unsigned slot)
{
    GameState *g = l->g;
    Block block = slot_block(g, slot);
    ORIGINAL_EFFECT(leave_index(g, (uint16_t)(block - 1)));
    Stream d = stream_of(g, A_BLOCK, sound_block_bytes(g, block));
    send_header(l, 0xC70691, 0xC7069D, slot_start(g, slot), KIND_DATA);
    uint16_t length = sound_block_length(g, block);
    ORIGINAL_EFFECT(leave_work(g, length));
    set_slot_length(g, slot, length);
    set_slot_end(g, slot, (uint16_t)(slot_start(g, slot) + length));
    if (sent_across_banks(&d, length))
        send_messages(l, &d, 0xC706ED, length / 3u);
    else
        send_messages(l, &d, 0xC7070D, length / 3u);
    end_transfer(l);
}

// $C7:0326
/* Load the missing blocks into the slots from the first free one on (from
 * the first, all forgotten, if none is free); the slots after the last one
 * loaded are forgotten, and the free memory starts where it ends. */
static void load_missing_blocks(Link *l, const SongBlocks *b)
{
    GameState *g = l->g;
    ORIGINAL_EFFECT(set_sound_driver_zeroed_as_blocks_load(g, 0));
    unsigned slot = first_free_slot(g);
    if (slot == SLOTS) {
        for (unsigned i = 0; i < SLOTS; i++)
            hold(g, i, NO_BLOCK);
        slot = 0;
    }
    for (unsigned i = 0; i < b->count_missing; i++, slot++) {
        hold(g, slot, b->missing[i]);
        ORIGINAL_EFFECT(set_sound_driver_load_index(g, (uint16_t)(2 * i)));
        ORIGINAL_EFFECT(set_sound_driver_load_slot(g, (uint16_t)(2 * slot)));
        IN_THE_ORIGINALS_FRAME(g, UPLOAD_BLOCK_FRAME, upload_block(l, slot));
    }
    if (b->count_missing < SLOTS) {
        set_sound_driver_free_address(g, slot_start(g, slot));
        for (unsigned k = slot; k < SLOTS; k++) {
            set_slot_end(g, k, 0);
            set_slot_length(g, k, 0);
        }
    }
}

// $C7:0922
/* The set of effects asked for to $2F00 (announced as command $70's, or as a
 * song's data). */
static void upload_effects(Link *l, bool for_the_command)
{
    GameState *g = l->g;
    uint8_t kind = for_the_command ? KIND_COMMANDS_DATA : KIND_DATA;
    ORIGINAL_EFFECT(set_sound_driver_header_kind(g, kind));
    send_header(l, 0xC70941, 0xC7094D, EFFECTS_TABLE, kind);
    bool battle = battle_effects(sound_driver_effects_asked(g));
    uint16_t length = sound_effect_set_length(g, battle);
    ORIGINAL_EFFECT(leave_work(g, length));
    Stream d = stream_of(g, AN_EFFECT_SET, sound_effect_set_bytes(battle));
    /* the original sends each set from a loop of its own */
    if (battle)
        send_messages(l, &d, 0xC709C9, length / 3u);
    else
        send_messages(l, &d, 0xC70987, length / 3u);
    set_sound_driver_effects_due(g, 0);
    end_transfer(l);
}

/* What a song brings, once its blocks are in their slots: the directory's
 * entries, the instruments' tunings and envelopes, the effects again where
 * they are due, the sequence and the percussion. */
static void send_what_the_song_brings(Link *l, uint8_t song, const SongBlocks *b)
{
    send_directory(l, b);
    send_instruments(l, b);
    if (effects_due_with_the_song(l->g, b))
        IN_THE_ORIGINALS_FRAME(l->g, UPLOAD_EFFECTS_FRAME, upload_effects(l, false));
    send_sequence(l, song);
    send_percussion(l, song);
    send_the_end(l, 0xC705FF, 0xC7060C, 0xC70614);
}

// $C7:01A1
/* Commands $10, $11, $14 and $15: play a song. Its command first; then, while
 * the driver turns every other command away, what the song needs that sound
 * memory doesn't have: the blocks it lacks (the slots it doesn't use freed,
 * the rest moved down to close up), and its tables, sequence and percussion.
 * The upload over, the driver is ready again and the slots under the echo
 * are forgotten. A song the driver doesn't want goes no further. */
static void play_song(Link *l)
{
    GameState *g = l->g;
    uint8_t song = sound_command_parameters(g, 0);
    if (song >= sound_songs(g))
        return;
    mark_a_song_that_saves_first(g);
    bool wanted;
    IN_THE_ORIGINALS_FRAME(g, SEND_SONG_FRAME, wanted = send_song(l));
    if (!wanted)
        return;
    set_sound_driver_song(g, song);
    SongBlocks b = blocks_of(g, song);
    if (b.count_missing > 0) {
        free_what_the_song_doesnt_use(g, &b);
        unsigned first_free = first_free_slot(g);
        ORIGINAL_EFFECT(leave_work(g, (uint16_t)(2 * first_free)));
        if (slots_in_use(g) != first_free && !move_blocks_down(l))
            forget_every_slot(l, song, &b);
        load_missing_blocks(l, &b);
        end_transfer(l);
    }
    send_what_the_song_brings(l, song, &b);
    set_sound_driver_song(g, SONG_READY);
    forget_slots_under_the_echo(g);
}

/* Command $70's message to the driver. */
enum { SWITCH_EFFECTS_MESSAGE = 0xFE };

// $C7:08E3
/* Command $70: switch the set of sound effects the driver has. If that changes
 * it, the driver is told, and once it has echoed that, sent the set. */
static void switch_effects(Link *l)
{
    GameState *g = l->g;
    bool battle = battle_effects(sound_command_parameters(g, 0));
    if (battle == sound_driver_effects_sent(g))
        return;
    send_the_effects_asked(g, battle);
    send(l, 0xC708F7, 0, SWITCH_EFFECTS_MESSAGE);
    au_driver_wait_echo(l->pf, PF_LOOP(0xC708FA, 0xC708FF), SWITCH_EFFECTS_MESSAGE);
    set_count(l, count_after(SWITCH_EFFECTS_MESSAGE));
    IN_THE_ORIGINALS_FRAME(g, UPLOAD_EFFECTS_FRAME, upload_effects(l, true));
    send_the_end(l, 0xC70907, 0xC70912, 0xC7091A);
    au_driver_wait_moved(l->pf, PF_LOOP(0xC7091A, 0xC7091F), FIRST_COUNT);
}

/* Command $71's first count. */
enum { SAMPLE_FIRST_COUNT = 0xFE };

// $C7:0755
/* Command $71: load block `$01` into the slot that holds the song's first
 * sample, if a song is loaded and the block fits before the next slot: the
 * block, then the slot's directory entry and the block's tuning and envelope,
 * each in a table of one, none of them sent again. The parameters after the
 * block's number hold its index (times 2) from here on. */
static void load_another_sample(Link *l)
{
    GameState *g = l->g;
    if (slot_free(g, 0))
        return;
    Block block = sound_command_parameters(g, 0);
    if (block == NO_BLOCK)
        return;
    uint16_t index = (uint8_t)(block - 1);
    ORIGINAL_EFFECT(leave_index(g, index));
    set_sound_command_parameters(g, 1, (uint8_t)(2 * index));
    set_sound_command_parameters(g, 2, (uint8_t)((2 * index) >> 8));
    Stream d = stream_of(g, A_BLOCK, sound_block_bytes(g, block));
    uint16_t length = sound_block_length(g, block);
    ORIGINAL_EFFECT(leave_work(g, length));
    unsigned slot = first_sample_slot(g);
    uint16_t start = slot_start(g, slot);
    if ((uint16_t)(length + start - 1) >= slot_end(g, slot))
        return;

    set_count(l, exchange(l, 0xC707B2, SAMPLE_FIRST_COUNT));
    send_address(l, 0xC707CA, start);
    send(l, 0xC707CA, 1, KIND_COMMANDS_DATA);
    counted(l, 0xC707CA);
    set_slot_length(g, slot, length);
    if (sent_across_banks(&d, length))
        send_messages(l, &d, 0xC707FF, length / 3u);
    else
        send_messages(l, &d, 0xC7081F, length / 3u);

    send_table_header(l, 0xC7083A, DIRECTORY_TABLE);
    send_word(l, 0xC70850, start);
    counted(l, 0xC70850);
    send_word(l, 0xC7086B, (uint16_t)(start + sound_block_loop(g, block)));
    (void)exchange(l, 0xC7086B, count(l));
    send_table_word(l, 0xC7087F, TUNINGS_TABLE, 0xC70895, sound_instrument_tuning(g, block));
    send_table_word(l, 0xC708A9, ENVELOPES_TABLE, 0xC708BF, sound_instrument_envelope(g, block));
    send_the_end(l, 0xC708C2, 0xC708CD, 0xC708D5);
    au_driver_wait_moved(l->pf, PF_LOOP(0xC708D5, 0xC708DA), FIRST_COUNT);
    hold(g, slot, block);
}

/* Commands $30-$3F stand for a command and its parameters, from a table. */
enum { SHORTCUTS = 0x30, SHORTCUTS_END = 0x40 };

static void take_the_shortcut(GameState *g, uint8_t command)
{
    uint8_t n = command & 0x0F;
    set_sound_command(g, sound_shortcut_byte(g, n, 0));
    for (uint8_t i = 0; i < 3; i++)
        set_sound_command_parameters(g, i, sound_shortcut_byte(g, n, (uint8_t)(i + 1)));
}

enum { SONGS = 0x10, SONGS_END = 0x18, AS_IS = 0x80, SWITCH_EFFECTS = 0x70, LOAD_ANOTHER_SAMPLE = 0x71 };
/* Of the songs' commands, those with this bit start nothing. */
enum { NOT_A_SONG = 0x02 };

// $C7:0155
/* Carry out the command in the buffer, unless a song is going up. $10-$17
 * start songs ($10, $11, $14 and $15; the others nothing), $18-$2F and
 * $80-$FF go to the driver as they are, $30-$3F are shortcuts, $70 and $71
 * the CPU half's own. */
static void carry_out(Link *l)
{
    GameState *g = l->g;
    for (;;) {
        if (loading_a_song(g))
            return;
        uint8_t command = sound_command(g);
        if (command >= AS_IS || (command >= SONGS_END && command < SHORTCUTS)) {
            carry_out_as_is(l);
        } else if (command >= SONGS && command < SONGS_END) {
            if (!(command & NOT_A_SONG))
                play_song(l);
        } else if (command >= SHORTCUTS && command < SHORTCUTS_END) {
            take_the_shortcut(g, command);
            continue;
        } else if (command == SWITCH_EFFECTS) {
            switch_effects(l);
        } else if (command == LOAD_ANOTHER_SAMPLE) {
            load_another_sample(l);
        }
        return;
    }
}

/* ---- The boot -------------------------------------------------------------------------------- */

enum {
    DRIVER_PAGE_BYTES = 0x100, /* the driver's page of work RAM, at $1E00 */
    BOOT_KICK = 0xCC,          /* the boot ROM's start signal */
    DRIVER_START = 0x0200,     /* where the driver's program starts */
    BOOT_ECHO_DELAY = 5,
};

static void boot_send(Platform *pf, Site at, uint8_t port, uint8_t v) { au_driver_write(pf, PF_AT(at), port, v); }
static void boot_send_address(Platform *pf, Site at, uint16_t to)
{
    boot_send(pf, at, 2, to & 0xFF);
    boot_send(pf, at, 3, to >> 8);
}

/* The boot ROM's echo, waited for in the original's loop from `head` to
 * `exit`. */
static void wait_for_the_boot_rom(Platform *pf, Site head, Site exit, uint8_t echo)
{
    au_driver_wait_echo(pf, PF_LOOP(head, exit), echo);
}

// $C7:005F
/* The boot ROM is ready: upload the driver's blocks to it, a byte at a time,
 * each with its index in the block on port 0, which the boot ROM echoes; a
 * block's address goes with the index past the last block's end, 3 on (never
 * 0), and after the last block the driver's start, which the boot ROM jumps
 * to. The driver's page of work RAM starts empty, and the slots after the
 * boot's own samples. */
static void upload_the_driver(GameState *g, Platform *pf)
{
    boot_send_address(pf, 0xC7005F, sound_boot_block_to(g, 0));
    boot_send(pf, 0xC70072, 1, BOOT_KICK);
    boot_send(pf, 0xC70075, 0, BOOT_KICK);
    wait_for_the_boot_rom(pf, 0xC70078, 0xC7007D, BOOT_KICK);
    uint8_t index = 0;
    for (uint8_t block = 0; block < SOUND_BOOT_BLOCKS; block++) {
        if (block > 0) {
            boot_send_address(pf, 0xC700C5, sound_boot_block_to(g, block));
            boot_send(pf, 0xC700D5, 1, index);
            boot_send(pf, 0xC700D8, 0, index);
            wait_for_the_boot_rom(pf, 0xC700DB, 0xC700E0, index);
        }
        uint16_t length = sound_boot_block_length(g, block);
        ORIGINAL_EFFECT(set_sound_driver_data_pointer(g, sound_boot_block_data(g, block) - 2));
        ORIGINAL_EFFECT(leave_work(g, (uint16_t)(length + 2)));
        index = 0;
        for (uint16_t i = 0; i < length; i++, index++) {
            boot_send(pf, 0xC700A4, 1, sound_boot_block_byte(g, block, i));
            boot_send(pf, 0xC700A8, 0, index);
            wait_for_the_boot_rom(pf, 0xC700AB, 0xC700B0, index);
        }
        index = (uint8_t)(index + 3);
        if (index == 0)
            index = 1;
    }
    boot_send_address(pf, 0xC700E2, DRIVER_START);
    boot_send(pf, 0xC700EF, 1, 0);
    boot_send(pf, 0xC700F3, 0, index);
    wait_for_the_boot_rom(pf, 0xC700F6, 0xC700FB, index);

    boot_send(pf, 0xC700FC, 0, 0);
    wram_fill8(g, 0x7E0000u | RAM_PAGE_SOUND, DRIVER_PAGE_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, DRIVER_PAGE_BYTES));
    uint16_t base = (uint16_t)(sound_boot_block_to(g, SOUND_BOOT_SAMPLES_BLOCK) +
                               sound_boot_block_length(g, SOUND_BOOT_SAMPLES_BLOCK));
    set_sound_driver_base_address(g, base);
    set_sound_driver_slot_addresses(g, 0, base);
    set_sound_driver_free_address(g, base);
}

// $C7:011E
/* The driver has had time to start: the echo's first delay (no slot to
 * forget yet), and ready for songs. */
static void ready_for_songs(GameState *g)
{
    ORIGINAL_EFFECT(set_sound_driver_unread_bank_word(g, 0xC700));
    set_sound_driver_echo_delay(g, BOOT_ECHO_DELAY);
    set_sound_driver_echo_page(g, echo_page_for(BOOT_ECHO_DELAY));
    ORIGINAL_EFFECT(set_sound_driver_zeroed_at_boot(g, 0));
    set_sound_driver_song(g, SONG_READY);
}

/* ---- The machine at the driver's edges -----------------------------------------------------
 *
 * From here on: the translation between the original's registers and the C,
 * which a build for the 65816 replaces with its own calling convention. The
 * driver runs with 16-bit index registers throughout; A is 8-bit except where
 * a routine says. */

/* The driver's code: APU-direct runs it while PC stays in here. */
#define DRIVER_FIRST 0xC70000
#define DRIVER_LAST 0xC70FFF

static void set_widths(Snes *s, bool a16)
{
    s->cpu.p = (uint8_t)((s->cpu.p & ~(CPU_FLAG_M | CPU_FLAG_X)) | (a16 ? 0 : CPU_FLAG_M));
}

/* PLP, which with 8-bit index registers drops their high bytes. */
static void pull_p(Snes *s)
{
    s->cpu.p = m_pull8(s);
    if (s->cpu.p & CPU_FLAG_X) {
        s->cpu.x &= 0xFF;
        s->cpu.y &= 0xFF;
    }
}

// $C7:0140
/* The entries the game calls (and the boot's copy at $C7:0038) save the
 * caller's data bank, direct page, P, A, X and Y on the stack, 16-bit, and
 * set the data bank $00 and direct page $1E00 the driver runs with; A's low
 * byte is left 0 and X $1E00. */
static void enter_driver(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_push16(s, c->a);
    m_push16(s, c->x);
    m_push16(s, c->y);
    set_widths(s, false);
    m_set_a8(s, 0);
    c->db = 0x00;
    c->d = RAM_PAGE_SOUND;
    c->x = RAM_PAGE_SOUND;
}

// $C7:0192
/* The driver's exit: the command is done, so the buffer is cleared, and the
 * caller's registers come back; N and Z come out of its data bank, which is
 * pulled last. */
static void leave_driver(Snes *s)
{
    Cpu *c = &s->cpu;
    set_sound_command(state_of(s), 0);
    c->y = m_pull16(s);
    c->x = m_pull16(s);
    c->a = m_pull16(s);
    pull_p(s);
    c->d = m_pull16(s);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rtl(s);
}

/* Every entry but the two the game calls runs with the driver's data bank and
 * direct page, which it reaches work RAM and the ports through. */
bool sound_driver_frame_is_set(Snes *s) { return s->cpu.db == 0x00 && s->cpu.d == RAM_PAGE_SOUND; }

/* The slots hold what the driver keeps there: every block a number in a byte
 * (the original keeps a word, whose high byte the port leaves alone), every
 * length 0 or longer than a block's number (the directory's search runs on
 * past the slots into their lengths, and would take a short one for a block),
 * the slots in use one after another from the first, and no block in two of
 * them but one (a song loads only the blocks no slot holds, and command $71
 * puts one block more in a slot of the song's): so a song's blocks always fit
 * in the slots. And command $71's slot is a slot. */
static unsigned slots_holding(const GameState *g, Block block)
{
    unsigned holding = 0;
    for (unsigned slot = 0; slot < SLOTS; slot++)
        holding += slot_block(g, slot) == block;
    return holding;
}

static bool slots_are_kept(const GameState *g)
{
    if (sound_driver_first_sample_slot(g) % 2 != 0 || first_sample_slot(g) >= SLOTS)
        return false;
    unsigned used = slots_in_use(g), held_twice = 0;
    for (unsigned slot = 0; slot < SLOTS; slot++) {
        uint16_t length = slot_length(g, slot);
        if (sound_driver_slot_ids_high(g, slot) != 0 || (length != 0 && length <= 0xFF) ||
            (slot < used && slot_free(g, slot)))
            return false;
        if (!slot_free(g, slot)) {
            unsigned holding = slots_holding(g, slot_block(g, slot));
            if (holding > 2)
                return false;
            held_twice += holding == 2;
        }
    }
    return held_twice <= 2;
}

bool sound_driver_slots_are_kept(Snes *s)
{
    vars_peek(true);
    bool kept = slots_are_kept(state_of(s));
    vars_peek(false);
    return kept;
}

/* ---- A host that plays a driver of its own ----------------------------------------------------
 *
 * The C sound driver (game/sound/) played in the sound processor's place
 * takes the boot and the commands as calls (platform/audio.h,
 * au_driver_hosted): nothing is uploaded and nothing waits on the ports. */

/* The boot, as a host takes it: the next song brings the first set of
 * effects, and the command buffer is clear. False where no host plays. */
bool sound_driver_host_boots(GameState *g, Platform *pf)
{
    if (!au_driver_boot_taken(pf))
        return false;
    set_sound_driver_effects_asked(g, 0);
    set_sound_command(g, 0);
    return true;
}

/* The command in the buffer, handed to the host with the set of effects the
 * next song is to bring, which a switch of them changes; then the buffer is
 * clear. */
static void hand_the_command_to_the_host(GameState *g, Platform *pf)
{
    AudioCommand c = { .command = sound_command(g), .p1 = sound_command_parameters(g, 0),
                       .p2 = sound_command_parameters(g, 1), .p3 = sound_command_parameters(g, 2), .count = 3 };
    au_driver_command_taken(pf, c, sound_driver_effects_asked(g));
    if (c.command == SWITCH_EFFECTS)
        set_sound_driver_effects_asked(g, battle_effects(c.p1));
    set_sound_command(g, 0);
}

// $C7:0004 (as a host that plays its own driver takes it)
void sound_driver_send(GameState *g, Platform *pf, AudioCommand c)
{
    const uint8_t parameters[3] = { c.p1, c.p2, c.p3 };
    for (unsigned i = 0; i < c.count && i < 3; i++)
        set_sound_command_parameters(g, i, parameters[i]);
    set_sound_command(g, c.command);
    if (au_driver_hosted(pf))
        hand_the_command_to_the_host(g, pf);
}

/* Back to the caller with its registers, as the driver's exit leaves them. */
static void back_to_the_caller(Snes *s)
{
    m_set_nz8(s, s->cpu.db);
    m_rtl(s);
}

/* What the boot ROM shows when it's ready, which the loop at $C7:0050 looks
 * for in X. */
enum { BOOT_ROM_READY = 0xBBAA };

// $C7:0000
/* The JSL that uploads the driver (a JMP to $C7:0038): the driver's frame, and
 * on to the wait for the boot ROM. */
void sound_driver_boot(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), DRIVER_FIRST, DRIVER_LAST))
        return;
    if (sound_driver_host_boots(state_of(s), m_platform(s))) {
        back_to_the_caller(s);
        return;
    }
    enter_driver(s);
    s->cpu.x = BOOT_ROM_READY;
    m_stop_at(s, 0xC70050, DC_A | DC_FLAGS);
}

/* The countdown that follows, at $C7:011B: $0800 passes of a loop that gives
 * the driver time to start, a wait on time alone, kept as the loop so the
 * time passes (docs/TODO.md). */
enum { BOOT_DELAY = 0x0800 };

void sound_driver_boot_upload(Snes *s)
{
    upload_the_driver(state_of(s), m_platform(s));
    set_widths(s, true);
    s->cpu.x = BOOT_DELAY;
    m_stop_at(s, 0xC7011B, DC_A | DC_Y | DC_FLAGS);
}

void sound_driver_boot_finish(Snes *s)
{
    ready_for_songs(state_of(s));
    leave_driver(s);
}

// $C7:0004
/* The JSL the game sends every command through (a JMP to $C7:0140). With
 * APU-direct the whole command runs at once; a host playing its own driver
 * takes it as a call. */
void sound_driver_command(Snes *s)
{
    if (au_driver_run_direct(m_platform(s), DRIVER_FIRST, DRIVER_LAST))
        return;
    GameState *g = state_of(s);
    if (au_driver_hosted(m_platform(s))) {
        hand_the_command_to_the_host(g, m_platform(s));
        back_to_the_caller(s);
        return;
    }
    enter_driver(s);
    Link l = { g, m_platform(s) };
    carry_out(&l);
    leave_driver(s);
}

/* The three routines that wait inside themselves, as the original's own code
 * calls them: where the command's guard turns a state away, and to fuzz each
 * alone. */

// $C7:0A39 (from $C7:01AE)
/* In a frame of its own: P, saved here, comes back in its return; a refused
 * song drops it and the caller's return, and leaves the driver. The return
 * leaves A as the driver's answer and X as the echo's polls left, which the
 * caller sets again before it reads them. */
void sound_driver_send_song(Snes *s)
{
    m_push8(s, s->cpu.p);
    Link l = { state_of(s), m_platform(s) };
    if (!send_song(&l)) {
        s->cpu.s = (uint16_t)(s->cpu.s + SEND_SONG_FRAME);
        leave_driver(s);
        return;
    }
    pull_p(s);
    m_stop_dont_care(s, 0xC70A97, DC_A | DC_X);
    m_rts(s);
}

// $C7:0655 (from $C7:034F)
/* Takes its slot (times 2) in a cell; saves P, which it pulls on the way
 * out. */
void sound_driver_upload_block(Snes *s)
{
    m_push8(s, s->cpu.p);
    Link l = { state_of(s), m_platform(s) };
    upload_block(&l, sound_driver_load_slot(state_of(s)) / 2u);
    pull_p(s);
    m_rts(s);
}

// $C7:0922 (from $C7:04D8 and $C7:0904)
void sound_driver_upload_effects(Snes *s)
{
    Link l = { state_of(s), m_platform(s) };
    upload_effects(&l, sound_command(state_of(s)) == SWITCH_EFFECTS);
    m_rts(s);
}

/* ---- The two waits the boot still makes on the machine --------------------------------------
 *
 * Idle entries, a pass each, which leave the registers and flags as the
 * original's pass does. */

// $C7:0050
/* The boot ROM shows $BBAA (X) on ports 0-1 when it's ready; meanwhile the
 * driver keeps writing $F0 to port 1 and $FF to port 0. */
void sound_driver_boot_wait_ready_loop(Snes *s)
{
    Platform *pf = m_platform(s);
    au_driver_write(pf, PF_AT(0xC70052), 1, 0xF0);
    au_driver_write(pf, PF_AT(0xC70057), 0, 0xFF);
    uint16_t shown_now = au_driver_read(pf, PF_AT(0xC7005A), 0);
    shown_now |= (uint16_t)(au_driver_read(pf, PF_AT(0xC7005A), 1) << 8);
    m_set_a8(s, 0xFF);
    m_set_nz16(s, (uint16_t)(s->cpu.x - shown_now));
    m_set_flag(s, CPU_FLAG_C, s->cpu.x >= shown_now);
    if (shown_now == s->cpu.x)
        s->cpu.pc = 0x005F;
}

// $C7:011B
void sound_driver_boot_delay_loop(Snes *s)
{
    m_set_nz16(s, --s->cpu.x);
    if (s->cpu.x == 0)
        s->cpu.pc = 0x011E;
}
