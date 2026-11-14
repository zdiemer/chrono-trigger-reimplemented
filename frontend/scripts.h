/* The locations' event scripts and the world maps' places as text a person can edit
 * (frontend/decoded.h), kept beside the pack as <pack>.scripts (docs/PLATFORM.md, "Scripts").
 *
 * A script is written as the engine runs it (game/event/engine.c): the
 * text is made from the language's own table (game/event/script.h), so it
 * names each command and its parameters as the port decodes them and can't
 * disagree with what the port runs. After how the stream packs, a script is
 *
 *   objects 3                        the count of its objects
 *   functions 00 L0060 L0060 ...     each object's 16 functions, where they start
 *   L0060:                           a place the table or a jump names
 *   cast_character character=01      a command: its operation and parameters
 *   if_compares left=var:08 right=01 comparison=00 to=L0081
 *   face object=self facing=03 @A6   ... and its opcode, where a shorter form reads the same
 *   bytes 00 3F 12                   bytes no command was read from, in hex
 *
 * A parameter is name=value: a value in hex, as many digits as its operand
 * has bytes (`var:`, `var16:`, `obj:`, `member:`, `story:` and `bit:` before
 * the operand that names a variable, a word variable, an object, a party
 * member, a word of the story's memory or a bit; `self` and `progress`
 * where the row takes the running object or the story's progress), and the
 * bytes of a record or a command's own data in hex joined by dots. Bits of
 * an operand byte that no parameter reads are kept as byteN=XX, the inline
 * data after a command's operands as data=XX.XX..., and a jump's place as
 * to=L.... (or to=+XX, its distance as it is, where it lands on no place).
 * A command that says a line shows the line beside it as a comment, from the
 * table of strings the script last chose: say line=13   # "LARA: Oh no…!"
 *
 * Read back, the places are worked out again, so commands may be added or
 * taken away: a jump must still reach its place (a byte's distance), and
 * the script must fit the game's buffer for it. A command's opcode is the
 * shortest of its name's rows the line's parameters fit (a jump's to a
 * place already named goes back), or the one `@XX` gives.
 *
 * A world map's lists of places (`lz:place-lists`, game/overworld/setup.c)
 * are kept here too, a record a line, each list's records together in the
 * game's order: `exit` (column, row, name, location, x, y), `event` (column,
 * row, script), `halt` (column, row, a byte), `script` (where an event's
 * script starts), then `rest`, the bytes after the lists. A list's count is
 * its records'; one with none is refused, since the game reads its count of
 * 0 as 65,536. */
#ifndef CT_FRONTEND_SCRIPTS_H
#define CT_FRONTEND_SCRIPTS_H

#include "decoded.h"

/* The event scripts, as a kind of decoded resource. */
extern const DecodedKind script_text;

#endif
