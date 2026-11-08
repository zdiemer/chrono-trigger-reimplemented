/* Event scripts: the bytecode that drives map objects, cutscenes and dialog.
 * The language -- its operations and the table of opcodes -- is in
 * event/script.h; the engine that runs it in event/engine.c; the
 * translation to the original's machine, and the guards, in event/bridge.h.
 *
 * These are the machine forms the still-gated field calls as C.
 * The opcode table owns ordinary commands; they have no entry wrappers.
 * machine_forms.c keeps the original contracts as checked assertions. */
#ifndef CT_GAME_EVENT_H
#define CT_GAME_EVENT_H

#include <stdbool.h>

#include "snes/system.h"

/* The engine's entries on the machine (event/bridge.c) */
void event_run_init_scripts(Snes *s);
void event_run_object_script(Snes *s);
bool event_has_objects(Snes *s);
bool event_runs_an_object(Snes *s);
void event_run_map_script(Snes *s);
bool event_map_script_can_run(Snes *s);
void event_restart_the_running_object_entry(Snes *s);
bool event_running_object_can_restart(Snes *s);
#define EVENT_SCREEN_ENTRIES(X) \
    X(event_screen_open_hole, 0) \
    X(event_screen_close_hole, 1) \
    X(event_screen_uncover_right, 2) \
    X(event_screen_uncover_left, 3) \
    X(event_screen_cover_right, 4) \
    X(event_screen_cover_left, 5) \
    X(event_screen_open_oval, 7) \
    X(event_screen_close_oval, 8) \
    X(event_screen_circle_bg3, 9) \
    X(event_screen_blend_circle, 10) \
    X(event_screen_end_field, 11) \
    X(event_screen_remove_oval, 12) \
    X(event_screen_end_push, 13) \
    X(event_screen_ripple, 14)
#define DECLARE_SCREEN_ENTRY(name, command) void name(Snes *s); bool name##_accepts(Snes *s);
EVENT_SCREEN_ENTRIES(DECLARE_SCREEN_ENTRY)
#undef DECLARE_SCREEN_ENTRY
void event_screen_dispatch(Snes *s);
void event_scene_fade_continues(Snes *s);
bool event_screen_dispatch_accepts(Snes *s);

/* The commands, by their rows (event/entries.c): entry name, opcode. The
 * operations' entries run the command by its row; the bridges' entries are the
 * bridges themselves. */
#define EVENT_BRIDGES(X) \
    X(event_cmd_go_to_location_now_raising_1e, 0xDF) \
    X(event_cmd_go_to_location_now, 0xE1) \
    X(event_cmd_copy_data, 0x4E) \
    X(event_cmd_party_routine_0a, 0xD5) \
    X(event_cmd_take_out_of_party, 0xD6) \
    X(event_cmd_party_routine_6_7, 0xF8) \
    X(event_cmd_party_routine_6, 0xF9) \
    X(event_cmd_party_routine_7, 0xFA) \
    X(event_cmd_play_effect, 0xE8) \
    X(event_cmd_play_song, 0xEA) \
    X(event_cmd_fade_music, 0xEB) \
    X(event_cmd_send_sound_command, 0xEC) \
    X(event_cmd_choose_credits, 0x29) \
    X(event_cmd_screen_command, 0xFF) \
    X(event_cmd_cast_npc, 0x82) \
    X(event_cmd_cast_enemy, 0x83) \
    X(event_cmd_if_in_the_active_party, 0xD2) \
    X(event_cmd_if_recruited, 0xCF) \
    X(event_cmd_recruit, 0xD0) \
    X(event_cmd_dismiss, 0xD1) \
    X(event_cmd_bring_into_the_party, 0xD3) \
    X(event_cmd_send_to_the_reserve, 0xD4) \
    X(event_cmd_note_item_count, 0xD7)

#define EVENT_DECLARE_ENTRY(name, opcode) \
    void name(Snes *s);                  \
    bool name##_accepts(Snes *s);
EVENT_BRIDGES(EVENT_DECLARE_ENTRY)

/* Event $FF's call of the menus' party routine 8, which its dispatch reaches
 * through its own table (event/screen.c), with the command's guard. */
void event_screen_party_routine(Snes *s);
bool event_screen_party_routine_accepts(Snes *s);

/* Events $E1's and $DF's change at once, after their wait for the vblank
 * ($C0:3B76, event/locations.c). */
void event_go_now_in_the_dark(Snes *s);

#endif
