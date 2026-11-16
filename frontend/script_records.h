/* The scripts read a command at a time, worked out from a source's scripts
 * for a target to keep beside them: the locations' event scripts
 * (game/event/records.h) and the menus' object scripts (game/menu/records.h).
 * The GBA's packing step puts the file in the cartridge
 * (targets/gba/host/pack.py, `--script-records`).
 *
 * The file, little-endian: "CTSR", the catalogue's version and its count;
 * then for each of the catalogue's resources, in its order, where its
 * records are (from the file's start, a word apart) and their length, 0 and
 * 0 for a resource that isn't a script; then the records, each script's as
 * its language's header lays them out.
 *
 * An event script's commands are found as its text finds them
 * (frontend/scripts.c): from its code's start on, a command after a command,
 * and from each place its functions' table and its jumps name. An object
 * script's, the cartridge's or a scene's unpacked, as frontend/commands.c
 * finds them: a command after a command from its start, and past bytes no
 * command could be read from, the next place one could. A command the game
 * reaches that the walk didn't find runs from its bytes. */
#ifndef CT_FRONTEND_SCRIPT_RECORDS_H
#define CT_FRONTEND_SCRIPT_RECORDS_H

#include "data/assets.h"

/* Write the records of every script `source` has to `path`: 0, or nonzero
 * where it can't. */
int script_records_write(const AssetSource *source, const char *path);

#endif
