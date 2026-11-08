/* Event machine forms retained for the field's still-gated callers.
 * These contracts preserve the retired registry's widths, decimal-mode
 * mask, page, bank and state guards. A failed contract is reported before
 * the caller stops at the original entry. */
#include "event/machine_forms.h"
#include "event/event.h"
#include "event/contact.h"
#include "event/party.h"
#include "field/bridge.h"
#include "core/program.h"
#include "port/machine.h"
#include "port/vars_ram.h"

static const struct {
    void (*entry)(Snes *);
    uint32_t address;
    uint8_t mask, value;
    bool (*takes)(Snes *);
} forms[] = {
    { event_cmd_copy_data, 0xC06846, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_copy_data_accepts },
    { event_cmd_play_effect, 0xC03C11, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_play_effect_accepts },
    { event_cmd_play_song, 0xC03C2F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_play_song_accepts },
    { event_cmd_fade_music, 0xC03C4C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_fade_music_accepts },
    { event_cmd_send_sound_command, 0xC03C98, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_send_sound_command_accepts },
    { event_cmd_choose_credits, 0xC03E84, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_choose_credits_accepts },
    { event_cmd_screen_command, 0xC03FAF, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_screen_command_accepts },
    { event_screen_party_routine, 0xC03E7C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_screen_party_routine_accepts },
    { event_cmd_party_routine_6_7, 0xC03E5C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_party_routine_6_7_accepts },
    { event_cmd_party_routine_6, 0xC03E6E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_party_routine_6_accepts },
    { event_cmd_party_routine_7, 0xC03E75, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_party_routine_7_accepts },
    { event_cmd_party_routine_0a, 0xC03A68, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_party_routine_0a_accepts },
    { event_cmd_take_out_of_party, 0xC03A07, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_take_out_of_party_accepts },
    { event_cmd_go_to_location_now, 0xC03B4B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_go_to_location_now_accepts },
    { event_cmd_go_to_location_now_raising_1e, 0xC03B98, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_go_to_location_now_raising_1e_accepts },
    { event_cmd_cast_npc, 0xC04590, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_cast_npc_accepts },
    { event_cmd_cast_enemy, 0xC046DF, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_cast_enemy_accepts },
    { event_cmd_if_in_the_active_party, 0xC038E8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_if_in_the_active_party_accepts },
    { event_cmd_if_recruited, 0xC0388D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_if_recruited_accepts },
    { event_cmd_recruit, 0xC038BE, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_recruit_accepts },
    { event_cmd_dismiss, 0xC038D3, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_dismiss_accepts },
    { event_cmd_bring_into_the_party, 0xC03919, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_bring_into_the_party_accepts },
    { event_cmd_send_to_the_reserve, 0xC0394E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_send_to_the_reserve_accepts },
    { event_cmd_note_item_count, 0xC037F9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_cmd_note_item_count_accepts },
    { event_screen_dispatch, 0xC04009, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_dispatch_accepts },
    { event_screen_open_hole, 0xC04036, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_open_hole_accepts },
    { event_screen_close_hole, 0xC04055, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_close_hole_accepts },
    { event_screen_uncover_right, 0xC0405D, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_uncover_right_accepts },
    { event_screen_uncover_left, 0xC040EF, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_uncover_left_accepts },
    { event_screen_cover_right, 0xC040B3, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_cover_right_accepts },
    { event_screen_cover_left, 0xC04145, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_cover_left_accepts },
    { event_screen_open_oval, 0xC04191, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_open_oval_accepts },
    { event_screen_close_oval, 0xC041B0, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_close_oval_accepts },
    { event_screen_circle_bg3, 0xC041B4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_circle_bg3_accepts },
    { event_screen_blend_circle, 0xC041B8, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_blend_circle_accepts },
    { event_screen_end_field, 0xC041C0, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_end_field_accepts },
    { event_screen_remove_oval, 0xC041C4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_remove_oval_accepts },
    { event_screen_end_push, 0xC041DC, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_end_push_accepts },
    { event_screen_ripple, 0xC041C8, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, event_screen_ripple_accepts },
    { event_run_init_scripts, 0xC058DE, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_has_objects },
    { event_run_map_script, 0xC0595C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_map_script_can_run },
    { event_restart_the_running_object_entry, 0xC0597D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_running_object_can_restart },
    { event_run_object_script, 0xC05A93, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, event_runs_an_object },
    { field_press_confirm, 0xC01AAC, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, field_confirm_can_run },
    { event_party_back_together, 0xC0034B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, event_party_back_can_run },
    { event_go_now_in_the_dark, 0xC03B76, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, field_on_page },
};

bool event_machine_form_takes(Snes *s, void (*entry)(Snes *))
{
    for (unsigned i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        if (forms[i].entry != entry)
            continue;
        if (!s->cpu.e && (s->cpu.p & forms[i].mask) == forms[i].value && forms[i].takes(s))
            return true;
        program_breaks_a_contract(state_of(s), forms[i].address, "an event call outside its contract");
        return false;
    }
    return false;
}
