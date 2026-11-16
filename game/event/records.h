/* A location's event script worked out ahead: each command's operands read
 * from its bytes once, by a builder, rather than every time the command runs.
 *
 * The engine (event/engine.c) reads a command's operands by its row of the
 * language, which costs more than the command's own work on a small target.
 * What the bytes alone decide -- a number, a word, bits of one, an address,
 * which variable or object an operand names -- is the same every time a
 * command runs, so a builder can read it once for every command of every
 * script (frontend/event_records.c, which the GBA's packing step puts in the
 * cartridge) and the engine take it from there: a record per command, the
 * row's named parameters' values in their order, what reads the game's state
 * (a variable's value, a party member's object) left as the operand for the
 * engine to look up.
 *
 * A record is only ever a shortcut. It keeps the bytes it was read from, and
 * the engine uses it only where the script's bytes are still those, so a
 * script the game changed as it ran (the party's own scripts after it, a
 * battle's or a scene's data over the buffer) runs from its bytes as before.
 * A source with no records (ct_native's) runs every command from its bytes.
 *
 * The records of a script, as a source keeps them (asset_form,
 * ASSET_FORM_SCRIPT_RECORDS, by the packed script's resource), little-endian:
 *
 *     u32 language       event_records_language(): the rows they were read by
 *     u32 covered        the script offsets the index covers
 *     u16 index[covered] each offset's record, in words from the records' start, or 0
 *     (to a word)
 *     records            EventRecord, each a whole number of words
 */
#ifndef CT_GAME_EVENT_RECORDS_H
#define CT_GAME_EVENT_RECORDS_H

#include <stdbool.h>
#include <stdint.h>

#include "event/script.h"

/* The most of a command's bytes a record is read from: what the engine reads
 * of any command (its own, and up to eight). */
enum { EVENT_RECORD_BYTES = 8 };

/* A command read ahead: its opcode, the bytes it was read from, and a value
 * for each parameter its row names, in the language's order. A parameter
 * the bytes decide is its value; one that reads the game's state holds its
 * operand (a byte, a word), which the engine looks up as it runs. Each
 * value's top byte is the parameter's kind (EventArgKind), so the engine
 * needn't look in the row for it. */
enum { EVENT_RECORD_KIND_SHIFT = 24, EVENT_RECORD_VALUE_BITS = 0xFFFFFF };
typedef struct {
    uint8_t opcode;
    uint8_t bytes;                         /* of made_from, the opcode's included */
    uint8_t values;                        /* of value[] */
    uint8_t unused;
    uint8_t made_from[EVENT_RECORD_BYTES];
    uint32_t value[];
} EventRecord;

enum { EVENT_RECORD_HEAD = 12 };   /* a record's bytes before its values */

/* The loaded script's records, where the source has them: kept beside the
 * game's state, set as the script loads. */
typedef struct {
    const uint16_t *index;
    const uint32_t *records;
    uint16_t covered;   /* offsets of the index */
    uint32_t words;     /* of the records */
} EventRecords;
EventRecords *event_records(GameState *g);

/* The records of the script unpacked from `packed` (a cartridge address)
 * from now on, or none where the source has none for it. */
void event_records_use(GameState *g, uint32_t packed);

/* The record of the command at `at`, whose opcode is `opcode`, where there
 * is one and the script's bytes there are still those it was read from;
 * NULL otherwise. */
const EventRecord *event_record_at(GameState *g, uint16_t at, uint8_t opcode);

/* The record of the command whose bytes are `command` (`available` of them,
 * from its opcode): its size in bytes, written into `out` (room for
 * EVENT_RECORD_HEAD and four bytes a parameter), or 0 where the command has
 * no record: its row reads its own operands, or decodes one into a record of
 * the language's, or reads past `available`, or its operands name what their
 * kinds can never name (the engine reports those as it reaches them). */
unsigned event_record_read(const uint8_t *command, uint16_t available, EventRecord *out);

/* The language the records are read by: a hash of what each row decodes
 * from where. Records read by other rows are not used. */
uint32_t event_records_language(void);

#endif
