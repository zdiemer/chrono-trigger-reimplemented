/* The locations' event scripts read a command at a time (game/event/records.h),
 * worked out from a source's scripts for a target to keep beside them: the
 * GBA's packing step puts the file in the cartridge (targets/gba/host/pack.py,
 * `--event-records`).
 *
 * The file, little-endian: "CTER", the catalogue's version and its count;
 * then for each of the catalogue's resources, in its order, where its
 * records are (from the file's start, a word apart) and their length, 0 and
 * 0 for a resource that isn't a location's script; then the records, each
 * script's as game/event/records.h lays them out.
 *
 * A script's commands are found as its text finds them (frontend/scripts.c):
 * from its code's start on, a command after a command, and from each place
 * its functions' table and its jumps name. A command the engine reaches
 * that the walk didn't find runs from its bytes. */
#ifndef CT_FRONTEND_EVENT_RECORDS_H
#define CT_FRONTEND_EVENT_RECORDS_H

#include "data/assets.h"

/* Write the records of every location's script `source` has to `path`:
 * 0, or nonzero where it can't. */
int event_records_write(const AssetSource *source, const char *path);

#endif
