/* The scripts' text: the event scripts', the battle's animations' and the
 * menus' object scripts' (frontend/scripts.c, frontend/commands.c), which
 * call each command by its operation's name and measure the commands whose
 * operands decide their length. The game needs neither, so a build without
 * the text -- the 65816's, where the languages' tables are ROM -- leaves the
 * names and measures out of the tables with CT_SCRIPT_TEXT 0. */
#ifndef CT_GAME_SCRIPT_TEXT_H
#define CT_GAME_SCRIPT_TEXT_H

#ifndef CT_SCRIPT_TEXT
#define CT_SCRIPT_TEXT 1
#endif

#endif
