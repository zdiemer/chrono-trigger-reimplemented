/* The enemies' scripts as text, one of the kinds of script frontend/commands.h
 * keeps (docs/PLATFORM.md, "Commands"): where each enemy's script starts, a
 * row of words, then the scripts as the battle reads them
 * (battle/enemy_scripts.h) -- each two sections, what the enemy does on its
 * turn and what it does back, of blocks of conditions and the actions they
 * choose:
 *
 *   starts 8D08 8D1E ...         where each enemy's script starts, in bank CC
 *   # enemy 00                   (a comment where enemies' scripts start)
 *   if at_half_hp 03 00 00       a condition, by its test's name, and its three bytes
 *   then                         the conditions' end
 *   do tech 02 01 03             an action, by name, and its bytes
 *   done                         the actions' end
 *   end                          the section's end
 *
 * A condition's name is its test's; where kinds share one, `@XX` gives the
 * kind. Bytes the battle wouldn't read as these are `bytes XX ...`. Other
 * scripts' places are counted in bytes, so the scripts keep their length. */
#ifndef CT_FRONTEND_ENEMY_SCRIPTS_H
#define CT_FRONTEND_ENEMY_SCRIPTS_H

#include "decoded.h"

bool enemy_scripts_write(DecodedText *t, const AssetStore *store, const AssetEntry *e, const uint8_t *bytes);
const char *enemy_scripts_read_line(Decoded *d, DecodedReading *r, const char *s, const char *end);
const char *enemy_scripts_read_end(DecodedReading *r);

#endif
