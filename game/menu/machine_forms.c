/* Menu contracts at checked C calls and held boundaries. No dispatch registry. */
#include "core/program.h"
#include "menu/machine_forms.h"
#include "menu/bridge.h"
#include "menu/cursor.h"
#include "menu/display.h"
#include "menu/fades.h"
#include "menu/digits.h"
#include "menu/epoch.h"
#include "menu/exchange.h"
#include "menu/gate_screen.h"
#include "menu/equipment.h"
#include "menu/item_screen.h"
#include "menu/tech_screen.h"
#include "menu/settings_screen.h"
#include "menu/shop.h"
#include "menu/techs.h"
#include "menu/map.h"
#include "menu/figures.h"
#include "menu/healing.h"
#include "menu/description.h"
#include "menu/held_cursors.h"
#include "menu/saving.h"
#include "menu/main_menu.h"
#include "menu/naming.h"
#include "menu/lists.h"
#include "menu/members.h"
#include "menu/party.h"
#include "menu/line_effects.h"
#include "menu/load_screen.h"
#include "menu/object_commands.h"
#include "menu/turning_world.h"
#include "menu/map_scenes.h"
#include "menu/screen_objects.h"
#include "menu/script_bridge.h"
#include "menu/menu.h"
#include "menu/objects.h"
#include "menu/opening.h"
#include "menu/saves.h"
#include "menu/save_screen.h"
#include "menu/sprites.h"
#include "menu/tiles.h"
#include "menu/sound.h"
#include "menu/text.h"
#include "menu/typeset.h"
#include "menu/graphics.h"
#include "menu/items.h"
#include "menu/windows.h"
#include "menu/top_layer.h"
#include "menu/vram.h"

static const struct {
    void (*entry)(Snes *);
    uint32_t address;
    uint8_t mask, value;
    bool (*takes)(Snes *);
} forms[] = {
    { menu_wait_for_menu_music_playing_loop, 0xC2CBE0, CPU_FLAG_M, CPU_FLAG_M, NULL },

    { menu_draw_empty_label_entry, 0xC2D28D, CPU_FLAG_D, 0, menu_slot_labels_are_drawable },

    { menu_build_items, 0xC29875, CPU_FLAG_D, 0, menu_items_are_buildable },
    { menu_text_start_entry, 0xC257DF, CPU_FLAG_X | CPU_FLAG_D, 0, menu_text_is_startable },
    { menu_text_go_on_entry, 0xC25823, CPU_FLAG_X | CPU_FLAG_D, 0, menu_text_can_go_on },
    { menu_wait_frames_sending_the_sound_entry, 0xC20454, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_on_page_low_ram },
    { menu_wait_for_frame, 0xC20346, CPU_FLAG_M, CPU_FLAG_M, menu_frame_wait_on_page },
    { menu_wait_for_frame, 0xC2046E, CPU_FLAG_M, CPU_FLAG_M, menu_frame_wait_on_page },
    { menu_wait_for_frame_loop, 0xC20348, CPU_FLAG_M, CPU_FLAG_M, menu_frame_wait_on_page },
    { menu_task_waits_a_frame, 0xC2821E, CPU_FLAG_D, 0, menu_task_can_wait },
    { menu_task_waits_frames, 0xC2822B, CPU_FLAG_D, 0, menu_task_can_wait },
    { menu_task_ends, 0xC28216, CPU_FLAG_D, 0, menu_task_can_end },
    { menu_run_the_tasks, 0xC281CF, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, menu_tasks_can_run },
    { menu_run_the_main_task, 0xC2826F, CPU_FLAG_D, 0, menu_screen_step_is_known },
    { menu_main_task_waits, 0xC2826C, CPU_FLAG_D, 0, menu_on_page },
    { menu_start_the_tasks, 0xC28190, CPU_FLAG_D, 0, menu_tasks_can_start },
    { menu_wait_for_the_first_frame, 0xC281B9, CPU_FLAG_D, 0, NULL },
    { menu_wait_ready_loop, 0xC281CA, CPU_FLAG_M, CPU_FLAG_M, menu_ready_wait_reaches_low_ram },
    { menu_mode_enter, 0xC28000, 0, 0, NULL },
    { menu_mode_start, 0xC2800E, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_close_entry, 0xC282B2, 0, 0, menu_on_page_work_ram },
    { menu_close_for_the_game, 0xC28320, 0, 0, menu_on_page_work_ram },
    { menu_start_the_fade_in_entry, 0xC283B2, 0, 0, menu_on_page },
    { menu_fade_the_screen_out, 0xC283CA, 0, 0, menu_on_page_low_ram },
    { menu_fade_in_task_entry, 0xC283BD, 0, 0, menu_on_page_low_ram },
    { menu_mode_return, 0xC282C1, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_mode_restart, 0xC2832F, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_take_frame_input, 0xC28473, CPU_FLAG_D, 0, menu_frame_input_is_takeable },
    { menu_nmi_enter, 0xC2840E, 0, 0, NULL },
    { menu_nmi_work, 0xC2842A, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_nmi_work_runs },
    { menu_vblank_upload, 0xC285D6, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_vblank_work_runs },
    { menu_upload_vram_queue, 0xC28663, 0, 0, menu_vram_queue_is_whole },
    { menu_object_mode_start, 0xC2000F, 0, 0, NULL },
    { menu_object_mode_start, 0xC20000, 0, 0, NULL },
    { menu_object_init_video, 0xC20043, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_nmi, 0xC2015E, 0, 0, NULL },
    { menu_object_upload_vram_queue, 0xC20405, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_queue_is_sendable },
    { menu_map_scroll_x, 0xC20568, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_map_scroll_x_runs },
    { menu_map_scroll_y, 0xC2066C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_map_scroll_y_runs },
    { menu_map_redraw, 0xC209C5, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_map_redraw_runs },
    { menu_clear_vram, 0xC226A8, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_display_bank_is_io },
    { menu_init_video, 0xC2940D, 0, 0, NULL },
    { menu_load_carried_over, 0xFFFAED, CPU_FLAG_D, 0, menu_carry_over_runs },
    { menu_draw_gate_screen_entry, 0xFFF677, CPU_FLAG_D, 0, menu_gate_screen_runs },
    { menu_run_the_cursor_task, 0xC2E91B, CPU_FLAG_D, 0, menu_cursor_is_placeable },
    { menu_run_the_cursor_task, 0xC2E921, CPU_FLAG_D, 0, menu_cursor_is_placeable },
    { menu_send_sound_on_other_voices, 0xC2EAF9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, menu_sounds_on_page },
    { menu_play_cursor_sound, 0xC2EAC2, CPU_FLAG_D, 0, menu_sounds_on_page },
    { menu_play_refusal_sound, 0xC2EACC, CPU_FLAG_D, 0, menu_sounds_on_page },
    { menu_play_sound, 0xC2EABA, CPU_FLAG_D, 0, menu_sounds_on_page },
    { menu_show_save_slot, 0xC2E743, CPU_FLAG_D, 0, menu_save_slots_are_showable },
    { menu_move_among_the_slots, 0xC2CFA2, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_slot_under_the_cursor_is_showable },
    { menu_open, 0xC28096, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_opening_runs },
    { menu_read_save_slots, 0xC2D156, CPU_FLAG_X | CPU_FLAG_D, 0, menu_save_slots_are_readable },
    { menu_read_slot_summary, 0xC2D19F, CPU_FLAG_D, 0, menu_slot_summary_is_readable },
    { menu_read_design_and_chapter, 0xC2D266, CPU_FLAG_X | CPU_FLAG_D, 0, menu_slot_summary_is_readable },
    { menu_draw_chapter_label, 0xC2D296, CPU_FLAG_D, 0, menu_chapter_label_is_drawable },
    { menu_draw_slot_name, 0xC2D2C4, CPU_FLAG_X | CPU_FLAG_D, 0, menu_slot_labels_are_drawable },
    { menu_open_load_screen_entry, 0xC2E613, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_load_screen_draws },
    { menu_choose_on_load_screen_entry, 0xC2E61B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_load_screen_runs },
    { menu_choose_game_to_carry_on_from_entry, 0xC2E6AE, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_load_screen_runs },
    { menu_answer_erase_entry, 0xC2E705, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_erase_is_answerable },
    { menu_draw_load_screen, 0xC2E7C3, CPU_FLAG_X | CPU_FLAG_D, 0, menu_load_screen_draws },
    { menu_ask_which_to_carry_on_from, 0xC2E841, CPU_FLAG_D, 0, menu_on_page_work_ram },
    { menu_erase_slot_entry, 0xC2E8B7, CPU_FLAG_X | CPU_FLAG_D, 0, menu_slot_window_is_drawable },
    { menu_take_the_frames_input, 0xC28002, 0, 0, NULL },
    { menu_show_what_the_party_wears, 0xC2834D, CPU_FLAG_D, 0, menu_party_wear_is_showable },
    { menu_type_the_description, 0xC2FAA8, CPU_FLAG_D, 0, menu_typing_can_go_on },
    { menu_type_the_description_task_entry, 0xC2FA9F, CPU_FLAG_D, 0, menu_typing_can_go_on },
    { menu_type_the_setting_task_entry, 0xC2F9B2, CPU_FLAG_D, 0, menu_setting_typing_can_start },
    { menu_type_the_value_task_entry, 0xC2FA06, CPU_FLAG_D, 0, menu_value_typing_can_start },
    { menu_heal_and_show_entry, 0xC28F55, CPU_FLAG_D, 0, menu_healing_runs },
    { menu_show_amounts_entry, 0xC28F6C, CPU_FLAG_X | CPU_FLAG_D, 0, menu_amounts_can_be_shown },
    { menu_raise_amounts, 0xC29016, CPU_FLAG_D, 0, menu_amounts_can_be_shown },
    { menu_raise_amount_kind, 0xC29047, CPU_FLAG_D, 0, menu_amounts_can_rise },
    { menu_run_the_member_cursor_task, 0xC28A45, CPU_FLAG_D | CPU_FLAG_X, 0, menu_member_cursor_is_drawable },
    { menu_run_the_member_cursor_task, 0xC28A94, CPU_FLAG_D | CPU_FLAG_X, 0, menu_member_cursor_is_drawable },
    { menu_run_the_held_cursor_task, 0xC28AB2, CPU_FLAG_D, 0, menu_held_cursor_is_drawable },
    { menu_run_the_held_cursor_task, 0xC28AB8, CPU_FLAG_D, 0, menu_held_cursor_is_drawable },
    { menu_run_the_list_cursor_task, 0xC289F2, CPU_FLAG_D, 0, menu_list_cursor_is_drawable },
    { menu_run_the_list_cursor_task, 0xC289F8, CPU_FLAG_D, 0, menu_list_cursor_is_drawable },
    { menu_run_the_members_cursors_task, 0xC28AF5, CPU_FLAG_D, 0, menu_members_cursors_are_drawable },
    { menu_run_the_members_cursors_task, 0xC28AFB, CPU_FLAG_D, 0, menu_members_cursors_are_drawable },
    { menu_run_the_scroll_bar_task, 0xC28BB8, CPU_FLAG_D, 0, menu_scroll_bar_is_drawable },
    { menu_run_the_scroll_bar_task, 0xC28C33, CPU_FLAG_D, 0, menu_scroll_bar_is_drawable },
    { menu_draw_member_card_entry, 0xC2A22F, CPU_FLAG_X | CPU_FLAG_D, 0, menu_member_card_is_drawable },
    { menu_redraw_member_card_entry, 0xC2A21F, CPU_FLAG_D, 0, menu_member_card_is_drawable },
    { menu_open_equipment_entry, 0xC29C31, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_opens },
    { menu_set_up_equipment, 0xC2A11A, CPU_FLAG_D, 0, menu_equipment_sets_up },
    { menu_choose_equipment_member_entry, 0xC29C39, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_member_step_runs },
    { menu_point_at_equipment_member, 0xC29D17, CPU_FLAG_D, 0, menu_equipment_member_is_pointable },
    { menu_start_sliding_equipment_members, 0xC29D5B, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M | CPU_FLAG_X, menu_equipment_members_slide_starts },
    { menu_slide_equipment_members, 0xC29DAF, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_members_slide },
    { menu_choose_equipment_slot_entry, 0xC29C70, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_slot_step_runs },
    { menu_point_at_equipment_slot, 0xC29ECD, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_slot_is_pointable },
    { menu_switch_equipment_member_entry, 0xC2A00B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_member_switches },
    { menu_choose_equipment_item_entry, 0xC29CA5, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_item_step_runs },
    { menu_point_along_equipment_list_entry, 0xC29F05, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_list_is_pointable },
    { menu_show_equipment_panel, 0xC2A051, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_panel_shows },
    { menu_back_to_equipment_members, 0xC2A2CD, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_members_come_back },
    { menu_open_equipment_list, 0xC2A41A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_list_opens },
    { menu_redraw_equipment_list, 0xC2A45D, CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_list_can_be_made },
    { menu_make_equipment_list_entry, 0xC2A463, CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_list_can_be_made },
    { menu_slide_equipment_list_in, 0xC2A4AF, CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_list_slides },
    { menu_slide_equipment_list_out, 0xC2A697, CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_list_slides },
    { menu_back_to_equipment_slots, 0xC2A64D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_equipment_slots_come_back },
    { menu_open_slots_window, 0xC2A71E, CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_slots_window_slides },
    { menu_close_slots_window, 0xC2A85C, CPU_FLAG_X | CPU_FLAG_D, 0, menu_equipment_slots_window_slides },
    { menu_open_naming_screen, 0xC2E1ED, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_naming_opens },
    { menu_open_the_naming, 0xC2E201, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_naming_opens },
    { menu_wait_to_name, 0xC2E209, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_on_page_work_ram },
    { menu_name_the_character, 0xC2E21E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_naming_step_runs },
    { menu_choose_battle_mode, 0xC2E236, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_cursor_is_movable },
    { menu_move_on_the_name_grid, 0xC2E267, CPU_FLAG_D | CPU_FLAG_X, 0, menu_naming_runs },
    { menu_type_on_the_name_grid, 0xC2E2F4, CPU_FLAG_D | CPU_FLAG_X, 0, menu_naming_runs },
    { menu_accept_the_name, 0xC2E363, CPU_FLAG_D | CPU_FLAG_X, 0, menu_naming_name_is_checkable },
    { menu_check_the_name, 0xC2E3E0, CPU_FLAG_D | CPU_FLAG_X, 0, menu_naming_name_is_checkable },
    { menu_choose_shop_member_entry, 0xC2D6C3, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_members_step_runs },
    { menu_choose_shop_slot_entry, 0xC2D715, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_slots_step_runs },
    { menu_choose_shop_equipment_entry, 0xC2D778, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_slots_step_runs },
    { menu_open_shop_member, 0xC2E0A5, CPU_FLAG_D, 0, menu_shop_member_opens },
    { menu_show_shop_member_slots, 0xC2E0F1, CPU_FLAG_D, 0, menu_shop_equipping_runs },
    { menu_switch_shop_member, 0xC2E108, CPU_FLAG_D, 0, menu_shop_equipping_runs },
    { menu_open_main_menu_entry, 0xC299CE, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_main_menu_opens },
    { menu_choose_on_main_menu_entry, 0xC29A1B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_main_menu_step_runs },
    { menu_back_to_the_main_menu_entry, 0xC29A98, CPU_FLAG_D, 0, menu_main_menu_comes_back },
    { menu_show_main_menu_panel, 0xC29B54, CPU_FLAG_D | CPU_FLAG_X, 0, menu_main_menu_panel_shows },
    { menu_show_save_panel_entry, 0xC2D065, CPU_FLAG_D | CPU_FLAG_X, 0, menu_save_panel_shows },
    { menu_open_save_screen_entry, 0xC2D4F8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_save_panel_shows },
    { menu_choose_on_save_screen_entry, 0xC2D506, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_cursor_is_movable },
    { menu_open_saving_entry, 0xC2CEDC, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_saving_opens },
    { menu_choose_slot_to_save_in_entry, 0xC2CEFD, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_cursor_is_movable },
    { menu_answer_whether_to_save_over_entry, 0xC2CF61, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_cursor_is_movable },
    { menu_close_the_question_on_a_button_entry, 0xC2CF92, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_cursor_is_movable },
    { menu_save_in_the_slot, 0xC2CF0A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_slot_can_be_saved_in },
    { menu_open_items_entry, 0xC2AA40, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_items_screen_opens },
    { menu_choose_item_command_entry, 0xC2AA4A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_command_step_runs },
    { menu_choose_on_item_list_entry, 0xC2AA86, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_list_step_runs },
    { menu_move_picked_item_entry, 0xC2AAC8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_move_step_runs },
    { menu_sort_items_entry, 0xC2AB4A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_items_sort_step_runs },
    { menu_close_item_users_entry, 0xC2AC1D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_users_step_runs },
    { menu_point_at_listed_item, 0xC2AC59, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_listed_item_is_pointable },
    { menu_describe_listed_item, 0xC2AD2E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_listed_item_is_pointable },
    { menu_show_items_panel, 0xC2AD50, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_items_panel_shows },
    { menu_use_item, 0xC2B0AB, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_use_runs },
    { menu_use_restoring_item, 0xC2B17F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_use_runs },
    { menu_use_item_by_chance, 0xC2B107, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_use_runs },
    { menu_rest_the_party, 0xC2B1F8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_use_runs },
    { menu_use_tab, 0xC2B200, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_item_use_runs },
    { menu_open_member_window, 0xC2AF42, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_member_window_opens },
    { menu_point_at_tab_member, 0xC2B24A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tab_member_is_pointable },
    { menu_look_at_tab_member, 0xC2B275, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, menu_tab_member_is_pointable },
    { menu_raise_figure, 0xC2B2A7, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_figure_is_raisable },
    { menu_use_one_more, 0xC2B31C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_one_more_is_usable },
    { menu_open_techs_entry, 0xC2B35D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_techs_screen_opens },
    { menu_lay_out_tech_members_entry, 0xC2B365, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_members_step_runs },
    { menu_choose_tech_member_entry, 0xC2B36D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_members_step_runs },
    { menu_choose_tech_entry, 0xC2B3AE, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_list_step_runs },
    { menu_choose_tech_target_entry, 0xC2B48E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_targets_step_runs },
    { menu_move_among_tech_members, 0xC2B520, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_members_move },
    { menu_slide_tech_members, 0xC2B568, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, menu_tech_members_slide },
    { menu_slide_tech_member_rows, 0xC2B5C8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, 0, menu_tech_members_slide },
    { menu_slide_tech_list, 0xC2B72F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_list_slides },
    { menu_point_at_tech_target, 0xC2B823, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_target_is_pointable },
    { menu_point_at_member_to_restore_entry, 0xC2B83E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_member_to_restore_is_pointable },
    { menu_note_member_to_restore_entry, 0xC2B870, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_member_to_restore_is_pointable },
    { menu_show_techs_panel, 0xC2B8DA, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_techs_panel_shows },
    { menu_show_tech_lister, 0xC2B9E1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_lister_is_showable },
    { menu_load_tech_lister_sprite, 0xC2BA09, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_lister_is_showable },
    { menu_describe_pointed_tech, 0xC2BE01, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_is_describable },
    { menu_describe_tech_kind, 0xC2BE16, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M | CPU_FLAG_X, menu_tech_members_move },
    { menu_describe_tech, 0xC2BE33, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_is_describable },
    { menu_open_tech_targets, 0xC2BE79, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tech_targets_open },
    { menu_open_settings_entry, 0xC2BF3D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_settings_screen_opens },
    { menu_choose_setting_entry, 0xC2BF45, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_settings_options_step_runs },
    { menu_choose_buttons_entry, 0xC2BF6D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_settings_buttons_step_runs },
    { menu_choose_gauge_layout_entry, 0xC2BF9F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_settings_gauge_step_runs },
    { menu_take_setting_option, 0xC2BFD4, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_set_sound_output, 0xC2C017, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_flip_keep_menu_cursors, 0xC2C038, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_flip_keep_battle_command, 0xC2C04A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_flip_keep_battle_lists, 0xC2C05C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_flip_active_time, 0xC2C06E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_choose_battle_gauge, 0xC2C080, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_flip_descriptions, 0xC2C095, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_choose_speeds, 0xC2C0A7, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_step_battle_speed, 0xC2C0B9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_step_message_speed, 0xC2C0D1, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_take_controls, 0xC2C0E9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_step_window_design, 0xC2C12C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_option_is_takeable },
    { menu_step_setting_meter, 0xC2C164, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_setting_meter_steps },
    { menu_show_settings_panel, 0xC2C2FF, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_settings_panel_shows },
    { menu_open_shop_entry, 0xC2D52B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_opens },
    { menu_set_up_shop, 0xC2DACA, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_sets_up },
    { menu_show_shop_item_panel, 0xC2DD7C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_panel_shows },
    { menu_choose_shop_tab_entry, 0xC2D546, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_tabs_step_runs },
    { menu_open_goods_list, 0xC2DC7B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_list_opens },
    { menu_open_for_sale_list, 0xC2DE98, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_list_opens },
    { menu_choose_goods_entry, 0xC2D58B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_buying_step_runs },
    { menu_choose_for_sale_entry, 0xC2D645, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_selling_step_runs },
    { menu_choose_how_many_to_buy_entry, 0xC2D618, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_quantity_step_runs },
    { menu_choose_how_many_to_sell_entry, 0xC2D690, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_shop_quantity_step_runs },
    { menu_reset_sprite_nodes, 0xC20C4D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_sprite_list_is_resettable },
    { menu_hide_all_sprites, 0xC2034D, CPU_FLAG_D, 0, menu_sprites_on_page },
    { menu_run_objects, 0xC2051D, CPU_FLAG_X | CPU_FLAG_D, 0, menu_objects_on_page },
    { menu_draw_sprite_nodes, 0xC20CEA, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_sprite_list_is_drawable },
    { menu_run_object_script_entry, 0xC20F63, CPU_FLAG_X | CPU_FLAG_D, 0, menu_object_script_runs },
    { menu_object_wave_hdma, 0xC27759, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_handler_runs },
    { menu_stream_tiles_entry, 0xC21CF5, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_tile_streamer_runs },
    { menu_load_palette_row_entry, 0xC21DD4, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_palette_loader_runs },
    { menu_fade_palette_row_entry, 0xC21E38, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_palette_fade_runs },
    { menu_fade_out_entry, 0xC220A2, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_fade_out_runs },
    { menu_fade_in_entry, 0xC22105, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_fade_in_runs },
    { menu_clear_mosaic_entry, 0xC22194, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_mosaic_runs },
    { menu_grow_mosaic_entry, 0xC221F8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_mosaic_runs },
    { menu_scene_object_in_view_entry, 0xC278A1, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_scene_object_in_view_runs },
    { menu_scene_drift_the_map_entry, 0xC27849, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_scene_drift_runs },
    { menu_epoch_map_screen, 0xC2631F, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_epoch_build_map, 0xC263DF, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_epoch_map_is_buildable },
    { menu_epoch_sample_map_colours, 0xC26400, CPU_FLAG_X | CPU_FLAG_D, 0, menu_epoch_page_is_menu },
    { menu_epoch_place_map_picture, 0xC265B2, CPU_FLAG_X | CPU_FLAG_D, 0, menu_epoch_page_is_menu },
    { menu_epoch_map_cursor, 0xC2666C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_epoch_cursor_runs },
    { menu_epoch_map_party_marker, 0xC267FA, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_epoch_marker_runs },
    { menu_epoch_map_epoch_marker, 0xC2683B, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_epoch_marker_runs },
    { menu_epoch_map_location_marker, 0xC268A3, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_epoch_marker_runs },
    { menu_epoch_map_gradient, 0xC268F4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_epoch_load_map_graphics, 0xC26965, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_epoch_time_dial_screen, 0xC26A34, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_epoch_time_dial_object, 0xC26AC8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_epoch_dial_runs },
    { menu_epoch_gate_matrix_object, 0xC26F85, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_epoch_spin_runs },
    { menu_epoch_time_dial_digit, 0xC27143, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_epoch_digit_runs },
    { menu_epoch_load_gate_stream, 0xC271F9, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_epoch_gate_graphics, 0xC27389, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_epoch_slot_runs },
    { menu_top_layer_wave, 0xC27652, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_top_layer_object_runs },
    { menu_top_layer_drift, 0xC27702, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_top_layer_object_runs },
    { menu_top_layer_fast_drift, 0xC277F2, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_top_layer_object_runs },
    { menu_top_layer_slow_drift, 0xC275C3, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_top_layer_object_runs },
    { menu_top_layer_wave_drift, 0xC275FD, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_top_layer_object_runs },
    { menu_object_place_under_the_view, 0xC2754D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_can_be_placed },
    { menu_object_place_under_the_stretch, 0xC27575, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_can_be_placed },
    { menu_object_give_random_velocity, 0xC27598, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_can_be_placed },
    { menu_scene_round_iris_entry, 0xC27911, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_runs },
    { menu_scene_window2_lines_entry, 0xC27957, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_flattened_iris_entry, 0xC27981, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_scene_advance_scroll_entry, 0xC27AAF, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, NULL },
    { menu_scene_show_moving_scroll_entry, 0xC27ADA, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_scene_show_uniform_scroll_entry, 0xC27AEB, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_accelerate_down, 0xC279C8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_mark_scene_event, 0xC27B4F, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_keep_first_scene_result, 0xC27C48, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_page },
    { menu_object_keep_second_scene_result, 0xC27C4F, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_page },
    { menu_scene_start_horizontal_wave, 0xC27C56, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_start_vertical_wave, 0xC27C97, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_advance_horizontal_wave, 0xC27CD8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_advance_vertical_wave, 0xC27D5E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_load_scene_map_graphics, 0xC27AFC, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_scene_map_graphics_fit },
    { menu_object_load_scene_top_layer, 0xC27B20, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_scene_top_layer_fits },
    { menu_object_lay_out_scene_pattern, 0xC27BD8, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_page },
    { menu_scene_start_gray_ramp, 0xC6E072, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_start_gray_band, 0xC6E170, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_move_gray_band, 0xC6E1FF, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_scene_start_scroll_pattern, 0xC27A56, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_place_on_scene_edge, 0xC6E101, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_scene_edge_is_readable },
    { menu_object_step_right_at_random, 0xC279E0, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_place_in_the_east_stretch, 0xC27A0C, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_place_in_the_west_stretch, 0xC27A31, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_hide_the_party, 0xC27860, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_keep_the_party, 0xC2786F, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_bring_the_party_back, 0xC27888, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_mark_the_scene_played, 0xC27B44, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_ask_the_story, 0xC27BC4, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_object_note_the_song, 0xC27BD1, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_map_page_is_menu },
    { menu_start_gate_screen_entry, 0xC2CE9D, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_run_the_gate_screen_task, 0xC2CECE, CPU_FLAG_D, 0, menu_gate_swirl_turns },
    { menu_load_record_sprites, 0xC2F3CA, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_record_sprites_are_loadable },
    { menu_load_record_sprite, 0xC2F422, CPU_FLAG_M | CPU_FLAG_D, CPU_FLAG_M, menu_record_sprite_is_loadable },
    { menu_run_record_task, 0xC2F657, 0, 0, menu_record_task_is_runnable },
    { menu_run_record_task, 0xC2F699, 0, 0, menu_record_task_is_runnable },
    { menu_open_exchange_entry, 0xC2C627, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_exchange_opens },
    { menu_choose_first_to_exchange_entry, 0xC2C65D, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_exchange_step_runs },
    { menu_choose_second_to_exchange_entry, 0xC2C6B9, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_exchange_step_runs },
    { menu_open_exchange_at_the_gate_entry, 0xC2C7AC, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_exchange_opens_at_the_gate },
    { menu_choose_first_at_the_gate_entry, 0xC2C7E0, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_exchange_step_runs },
    { menu_move_the_exchange_cursor, 0xC2C805, CPU_FLAG_X | CPU_FLAG_D, 0, menu_exchange_cursor_moves },
    { menu_lay_out_the_exchange, 0xC2CAF3, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_exchange_lays_out },
    { menu_show_exchange_panel_entry, 0xC2C9E5, CPU_FLAG_X | CPU_FLAG_D, 0, menu_exchange_panel_shows },
    { menu_switch_to_the_gate_song_entry, 0xC2CB8B, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_on_page_work_ram },
    { menu_play_the_gate_song_entry, 0xC2CBBF, CPU_FLAG_X | CPU_FLAG_D, 0, menu_on_page_work_ram },
    { menu_switch_back_to_the_field_song_entry, 0xC2CC0E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_on_page_work_ram },
    { menu_object_read_scene_mode, 0xC6E237, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_page },
    { menu_object_clear_scene_flag, 0xC6E2AF, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_keep_song, 0xC6E63D, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_let_song_go, 0xC6E644, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, NULL },
    { menu_object_advance_line_tables, 0xC6E568, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, NULL },
    { menu_object_bob_second_plane, 0xC6E583, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_raise_second_plane, 0xC6E590, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_page },
    { menu_object_accelerate, 0xC6E77F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_drift, 0xC6E2B5, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_jolt, 0xC6E6D0, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_jolt_runs },
    { menu_object_place_about_the_view, 0xC6E694, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_can_be_placed_about_the_view },
    { menu_epoch_outside_sides, 0xC6E74E, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_routine_page },
    { menu_epoch_in_area, 0xC6E797, CPU_FLAG_X | CPU_FLAG_D, 0, menu_epoch_on_world_map },
    { menu_object_window1_lines, 0xC6E000, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_window1_lines_2, 0xC6E318, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_window1_lines_3, 0xC6E448, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_window2_lines, 0xC6E340, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_window2_lines_2, 0xC6E470, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_start_curtain, 0xC6E241, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_curtain_can_start },
    { menu_object_step_curtain, 0xC6E27E, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_curtain_can_step },
    { menu_object_bg1_scroll_lines, 0xC6E595, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_line_effect_can_start },
    { menu_object_ripple_the_first_plane, 0xC6E5C5, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_ripple_can_draw },
    { menu_object_squashed_iris_1, 0xC6E368, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_squashed_iris_can_draw },
    { menu_object_squashed_iris_2, 0xC6E391, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_squashed_iris_can_draw },
    { menu_object_capsule_iris_1, 0xC6E498, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_capsule_iris_can_draw },
    { menu_object_capsule_iris_2, 0xC6E4BE, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_capsule_iris_can_draw },
    { menu_object_draw_the_world, 0xC6E7FA, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_world_can_be_drawn },
    { menu_object_start_the_world_turning, 0xC6E90F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_world_can_be_turned },
    { menu_object_turn_the_world, 0xC6E9F5, CPU_FLAG_X | CPU_FLAG_D, 0, menu_world_can_turn_a_frame },
    { menu_scene_note_the_epoch_s_arrival_entry, 0xC6EE31, CPU_FLAG_M, CPU_FLAG_M, menu_object_routine_runs },
    { menu_object_turn_the_world_on, 0xC6EE23, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_routine_runs },
    { menu_load_omen_graphics_entry, 0xC27B5A, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_bank_reaches_low_ram },
    { menu_load_omen_palette_entry, 0xC27B9F, CPU_FLAG_M | CPU_FLAG_X | CPU_FLAG_D, CPU_FLAG_M, menu_object_bank_reaches_low_ram },
    { menu_object_load_mode7_graphics, 0xC6E028, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_bank_reaches_low_ram },
    { menu_object_load_mode7_palette, 0xC6E04D, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_bank_reaches_low_ram },
    { menu_object_load_second_mode7_graphics, 0xC6E64A, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_bank_reaches_low_ram },
    { menu_object_load_second_mode7_palette, 0xC6E66F, CPU_FLAG_M | CPU_FLAG_X, CPU_FLAG_M, menu_object_bank_reaches_low_ram },
};

bool menu_machine_form_takes(Snes *s, uint32_t address, void (*entry)(Snes *))
{
    for (unsigned i = 0; i < sizeof forms / sizeof forms[0]; i++) {
        if (forms[i].entry != entry || forms[i].address != address)
            continue;
        if (!s->cpu.e && (s->cpu.p & forms[i].mask) == forms[i].value &&
            (!forms[i].takes || forms[i].takes(s)))
            return true;
        program_breaks_a_contract(state_of(s), address, "a menu call outside its contract");
        return false;
    }
    return false;
}

/* Object slots select these C handlers; the bridge checks the call frame. */
void (*menu_object_machine_form(uint16_t handler))(Snes *)
{
    switch (0xC20000u | handler) {
    case 0xC20F63: return menu_run_object_script_entry;
    case 0xC21CF5: return menu_stream_tiles_entry;
    case 0xC21DD4: return menu_load_palette_row_entry;
    case 0xC21E38: return menu_fade_palette_row_entry;
    case 0xC220A2: return menu_fade_out_entry;
    case 0xC22105: return menu_fade_in_entry;
    case 0xC22194: return menu_clear_mosaic_entry;
    case 0xC221F8: return menu_grow_mosaic_entry;
    case 0xC275C3: return menu_top_layer_slow_drift;
    case 0xC275FD: return menu_top_layer_wave_drift;
    case 0xC27652: return menu_top_layer_wave;
    case 0xC27702: return menu_top_layer_drift;
    case 0xC27759: return menu_object_wave_hdma;
    case 0xC277F2: return menu_top_layer_fast_drift;
    case 0xC27849: return menu_scene_drift_the_map_entry;
    case 0xC2666C: return menu_epoch_map_cursor;
    case 0xC267FA: return menu_epoch_map_party_marker;
    case 0xC2683B: return menu_epoch_map_epoch_marker;
    case 0xC268A3: return menu_epoch_map_location_marker;
    case 0xC26AC8: return menu_epoch_time_dial_object;
    case 0xC26F85: return menu_epoch_gate_matrix_object;
    case 0xC27143: return menu_epoch_time_dial_digit;
    default: return NULL;
    }
}
