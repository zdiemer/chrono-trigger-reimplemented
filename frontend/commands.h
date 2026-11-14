/* The battle's animation scripts and the menus' object scripts as text a
 * person can edit (frontend/decoded.h), kept beside the pack as
 * <pack>.commands (docs/PLATFORM.md, "Commands").
 *
 * A script is written as its interpreter runs it, from the language's own
 * table (frontend/languages.h): a command a line, its operation's name and
 * each parameter the row decodes, name=value in hex (as many digits as its
 * operand bytes; `slot:` before a byte of the running object's and `cell:`
 * before a cell of low RAM), the ones the command's byte fixes among them:
 *
 *   [battle_animations_cd]
 *   wait_frames frames=08
 *   play_sound voices=18 sound=4E
 *   set_value which=00 variable=03 value=10
 *
 * Bits of an operand no parameter reads are byteN=XX (N the operand), the
 * bytes a measured command carries after its own data=XX.XX..., and bytes no
 * command could be read from `bytes XX XX ...`. Where another row of the
 * same name would read the line the same, `@XX` gives the opcode. Read back,
 * a line is the first of its name's rows its parameters fit. Other tables
 * point into these scripts, so a script keeps its length: an edit must
 * leave it as many bytes as the cartridge's. */
#ifndef CT_FRONTEND_COMMANDS_H
#define CT_FRONTEND_COMMANDS_H

#include "decoded.h"

/* The battle's animation scripts and the menus' object scripts, as a kind of
 * decoded resource. */
extern const DecodedKind command_text;

#endif
