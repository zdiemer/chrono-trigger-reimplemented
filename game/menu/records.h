/* The menus' object scripts worked out ahead: each command's parameters read
 * from its bytes once, by a builder, rather than every time the command runs.
 *
 * The interpreter (menu/script.c) reads a command's parameters by its row of
 * the language each time the object's script reaches it, which on a small
 * target costs more than many commands' own work. Every parameter the
 * language has is decided by the command's bytes alone -- a number, a word,
 * an address, which byte of the object or cell of low RAM it names -- so a
 * builder can read each command of each script once (frontend/
 * script_records.c, which the GBA's packing step puts in the cartridge) and
 * the interpreter take the values from there: a record per command, the
 * row's named parameters' values in their order.
 *
 * A record is only ever a shortcut. It keeps the bytes it was read from, and
 * the interpreter uses one only where the script's bytes are still those, so
 * a scene's script the game changed, or unpacked something else over, runs
 * from its bytes as before. A source with no records (ct_native's) runs every
 * command from its bytes. Where a record stands in for the reading, the interpreter charges
 * the reads of the script's bytes the reading would have made.
 *
 * The scripts that have records are the cartridge's (the catalogue's
 * `script:object` resources) and a world map's scene's, unpacked into work
 * RAM as the map loads (`lz:object`, menu_records_use). A script's records,
 * as a source keeps them (asset_form, ASSET_FORM_SCRIPT_RECORDS, by the
 * script's resource), little-endian:
 *
 *     u32 language       menu_records_language(): the rows they were read by
 *     u32 covered        the script offsets the index covers
 *     u16 index[covered] each offset's record, in words from the records' start, or 0
 *     (to a word)
 *     records            MenuRecord, each a whole number of words
 */
#ifndef CT_GAME_MENU_RECORDS_H
#define CT_GAME_MENU_RECORDS_H

#include <stdbool.h>
#include <stdint.h>

#include "state.h"

/* The most of a command's bytes a record is read from: the longest command's
 * (ten), to a whole number of words. */
enum { MENU_RECORD_BYTES = 12 };

/* A command read ahead: its opcode, the bytes it was read from, how many of
 * the script's bytes reading its parameters takes, and a value for each
 * parameter its row names, in the language's order (menu_script_decode_record
 * says what a value holds). */
typedef struct {
    uint8_t opcode;
    uint8_t bytes;                        /* of made_from, the opcode's included */
    uint8_t values;                       /* of value[] */
    uint8_t reads;                        /* of the script's bytes, reading the parameters */
    uint8_t made_from[MENU_RECORD_BYTES];
    uint32_t value[];
} MenuRecord;

/* A record's bytes before its values, and the most values a row's record
 * holds (the most parameters a row of the language names). */
enum { MENU_RECORD_HEAD = 16, MENU_RECORD_MOST_VALUES = 8 };

/* One script's records, as a source keeps them. */
typedef struct {
    const uint16_t *index;
    const uint32_t *records;
    uint16_t covered;   /* offsets of the index */
    uint32_t words;     /* of the records */
} MenuRecordBlock;

/* The records of the scene's scripts in work RAM, where the source has them:
 * kept beside the game's state, set as the scene's scripts load. */
typedef struct {
    MenuRecordBlock scene;
    uint32_t scene_at;   /* where the scripts were unpacked to */
} MenuRecords;
MenuRecords *menu_records(GameState *g);

/* The records of the scripts unpacked from `packed` (a cartridge address) to
 * `at` in work RAM from now on, or none where the source has none for them. */
void menu_records_use(GameState *g, uint32_t packed, uint32_t at);

/* The record of the command at `at` (a 24-bit address), whose opcode is
 * `opcode`, where there is one and the script's bytes there are still those
 * it was read from; NULL otherwise. Its reads of the script take no time. */
const MenuRecord *menu_record_at(GameState *g, uint32_t at, uint8_t opcode);

/* The record of the command whose bytes are `command` (`available` of them,
 * from its opcode): its size in bytes, written into `out` (room for
 * MENU_RECORD_HEAD and MENU_RECORD_MOST_VALUES words), or 0 where the command
 * has none: no row runs its opcode, or reading it would go past `available`.
 * The record's `bytes` are as many as the command's. */
unsigned menu_record_read(const uint8_t *command, uint16_t available, MenuRecord *out);

/* The language the records are read by: a hash of what each row decodes
 * from where. Records read by other rows are not used. */
uint32_t menu_records_language(void);

#endif
