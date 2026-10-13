/* Scenes: the scene program's screen -- setting it up, and what it sends each
 * frame.
 *
 * Every scene starts from the same screen: blank, the sprite graphics at VRAM
 * $4000, every layer, window and effect off (init_video). Its vblank tasks
 * then send each frame's sprites and palette (upload_sprites, upload_palette),
 * and hide every sprite in the table for the scene to place again
 * (clear_sprite_table). Its loaders move graphics into VRAM with a transfer of
 * their own (transfer), and a scene with per-line effects programs them from a
 * table of descriptors (setup_line_effects).
 *
 * Two of these clear memory by copying the product the Mode 7 multiplier
 * offers, 0 or $E0, out of the PPU: a transfer that reads a register rather
 * than writing one, which no video call expresses, so it goes through the
 * SNES-only escape (vid_snes_copy); the matrix values they leave behind are
 * the video state.
 *
 * The program is in work RAM, where other code and data are at other times, so
 * every entry is guarded by a hash of its code. */
#include "scene/scene.h"

#include "charge.h"
#include "core/palette.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

/* The working palette, in the first bank of work RAM. */
enum { WORKING_PALETTE = 0x7E2200 };
enum { COLORS = 256 };

/* Where the scene keeps the sprites' graphics. */
enum { SPRITE_GRAPHICS = 0x4000 };

/* ---- The program stops ---------------------------------------------------------------------- */

bool scene_program_restart_present(Snes *s) { return m_code_matches(s, 0x7E302A, 14, 0x614033C5F8C83156ull); }

// $7E:302A
/* The program stops: interrupts and per-line effects off and the screen
 * blanked, before it moves the stack and jumps to the reset code. */
static void stop_display(Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0x7E302F), 0);
    vid_enable_line_effects(pf, PF_AT(0x7E3032), 0);
    vid_set_display(pf, PF_AT(0x7E3035), &(VidDisplay){ .blank = true });
}

/* Then the sound put back to normal (scene/sound.c), which waits on the sound
 * driver and so returns to the program's end where it waits (an entry of its
 * own), and the game handed back to the field (scene/program.c). */
enum { SOUND_CALL = 0x7E3038, SOUND_BACK_TO_NORMAL = 0x7E3177, THE_SOUND_READS_NOTHING = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

void scene_program_restart(Snes *s)
{
    stop_display(m_platform(s));
    s->cpu.p |= CPU_FLAG_M;
    if (scene_run(s, SOUND_CALL, SOUND_BACK_TO_NORMAL, THE_SOUND_READS_NOTHING, scene_sound_back_to_normal))
        scene_hand_back_to_the_field(s);
}

/* ---- The program's work RAM cleared --------------------------------------------------------- */

bool scene_clear_work_ram_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E3092, 87, 0x2A1AE096D5B36F10ull);
}

/* What the clear covers: the direct page, then low RAM from past the stack up
 * to the sound driver interface's page -- or, for the title coming back, from
 * the task lists, keeping what lies between. */
enum { DIRECT_PAGE_BYTES = 0x0100, FROM_THE_STACK = 0x000900, FROM_THE_TASKS = 0x000920, SOUND_PAGE = 0x1E00 };

// $7E:3092
/* Clear the program's work RAM: every byte set to the product the Mode 7
 * multiplier offers, with the matrix set so that it is 0 -- A times 0, where A
 * is `matrix_left`, whatever the caller had in the accumulator, which the
 * multiplier keeps afterwards. The second transfer keeps the first one's mode,
 * destination and bank, so it programs only its source and size. Returns
 * whether it was the title's shorter clear. */
bool scene_clear_the_work_ram(GameState *g, Platform *pf, uint8_t matrix_left)
{
    vid_set_mode7(pf, PF_AT(0x7E3096), &(VidMode7){ .a = (int16_t)(matrix_left << 8 | matrix_left) }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E309D), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B);
    const VidSnesTransfer direct_page = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = 0x000000, .size = DIRECT_PAGE_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E30B6), &direct_page);
    bool keep_below_tasks = scene_flags(g) & SCENE_FLAG_TITLE_WITH_LINE_IRQ;
    uint32_t from = keep_below_tasks ? FROM_THE_TASKS : FROM_THE_STACK;
    const VidSnesTransfer rest = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = from, .size = (uint16_t)(SOUND_PAGE - from) };
    vid_snes_copy(pf, PF_AT(keep_below_tasks ? 0x7E30D2 : 0x7E30E6), &rest);
    return keep_below_tasks;
}

/* The shorter clear returns with A's high byte 0 and D back on the scene's
 * page; the longer one stops at $7E:30E9, where the program goes on. */
void scene_clear_work_ram(Snes *s)
{
    bool shorter = scene_clear_the_work_ram(state_of(s), m_platform(s), (uint8_t)s->cpu.a);
    s->cpu.d = 0x0000;
    if (!shorter) {
        m_stop_at(s, 0x7E30E9, DC_A | DC_X | DC_Y | DC_C);
        return;
    }
    s->cpu.a = 0x0000;
    m_rts(s);
}

/* ---- A scene's first screen ------------------------------------------------------------------ */

bool scene_init_video_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E30F9, 126, 0x4F3F340A8EB09E78ull);
}

/* The screen every scene starts from: blanked, BG mode 0 with every layer's
 * tilemap and tiles at VRAM 0, 32 tiles square and unscrolled; no mosaic, no
 * Mode 7 plane, no window or color math, nothing on either screen and a black
 * fixed color; the sprites small, their graphics at SPRITE_GRAPHICS; and VRAM
 * written a byte at a time. */
static const VidScreen first_screen = {
    .display = &(const VidDisplay){ .blank = true },
    .sprites = { .size_select = 0, .base = SPRITE_GRAPHICS, .name_select = 0 },
    .mosaic_size = 1,
    .bg = { { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) }, { VID_TILEMAP(0x0000, 32, 32) },
            { VID_TILEMAP(0x0000, 32, 32) } },
    .vram_port = { .step = 1, .after_high = false },
};

// $7E:30F9
/* Set first_screen up, with the sprite cursor at the start of the high table. */
void scene_set_up_first_screen(GameState *g, Platform *pf)
{
    set_scene_sprite_high_pointer(g, SCENE_SPRITE_HIGH_TABLE);
    vid_apply_screen(pf, PF_AT(0x7E30F9), &first_screen);
}

/* Written through a direct page on the register block, which it leaves on the
 * scene's page again; A's high byte ends 0, with 8-bit A and 16-bit X. */
void scene_init_video(Snes *s)
{
    scene_set_up_first_screen(state_of(s), m_platform(s));
    s->cpu.p = (uint8_t)((s->cpu.p | CPU_FLAG_M) & ~CPU_FLAG_X);
    s->cpu.a = 0x0000;
    m_rts(s);
}

/* ---- Each frame's sprites and palette -------------------------------------------------------- */

bool scene_upload_sprites_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E380E, 53, 0x5555FCBF0F118757ull);
}

// $7E:380E
/* Send the sprite table to OAM, draw from the sprite the scene put on top
 * (with the priority rotation), and start the sprite cursor over: the first
 * entry, and the first high-table byte. */
static void upload_sprites(GameState *g, Platform *pf)
{
    vid_send_oam(pf, PF_AT(0x7E3811), PF_GAME(SCENE_SPRITE_TABLE));
    vid_set_oam_address(pf, PF_AT(0x7E3830), scene_sprite_priority_start(g), true);
    set_scene_sprite_index(g, 0);
    set_scene_sprite_high_pointer(g, SCENE_SPRITE_HIGH_TABLE);
}

/* Nothing it leaves in the registers is read. */
void scene_upload_sprites(Snes *s)
{
    upload_sprites(state_of(s), m_platform(s));
    m_rts(s);
}

// $7E:380B
/* The vblank task that sends the frame's sprites: the frame's last group of
 * four finished (scene/sprites.c), then the table sent. */
static TaskEnd finish_sprites_task(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0x7E3870, scene_hide_rest_of_group(g));
    upload_sprites(g, pf);
    return task_goes_on();
}

/* Nothing it leaves in the registers is read. */
static TaskLeftovers finish_sprites_leaves(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    return (TaskLeftovers){ 0 };
}

void scene_finish_and_upload_sprites(Snes *s) { scene_task_alone(s, SCENE_TASK_FINISH_SPRITES); }

/* The task's call of the group's end, and the upload after it. */
bool scene_finish_and_upload_sprites_present(Snes *s)
{
    return m_code_matches(s, 0x7E380B, 3, 0xC2A0DA17CDCB993Full) && scene_hide_rest_of_group_present(s) &&
           scene_upload_sprites_present(s);
}

bool scene_clear_sprite_table_present(Snes *s) { return m_code_matches(s, 0x7E3843, 45, 0x36CEE4DC10A522A8ull); }

/* A Y below the picture, where a sprite shows nowhere: the product of 1 times
 * -32 the clear copies over every byte of the table. */
enum { BELOW_THE_PICTURE = -32 };

// $7E:3843
/* Hide every sprite: every byte of the sprite table set to a Y below the
 * picture, for the scene to place its sprites again. */
void scene_hide_every_sprite(Platform *pf)
{
    vid_set_mode7(pf, PF_AT(0x7E3845), &(VidMode7){ .a = 1 }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E384D), &(VidMode7){ .b = BELOW_THE_PICTURE * 256 }, VID_M7_MATRIX_B_HIGH);
    const VidSnesTransfer t = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = SCENE_SPRITE_TABLE, .size = SCENE_SPRITE_TABLE_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E3850), &t);
    vid_set_mode7(pf, PF_AT(0x7E386C), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B_HIGH);
}

void scene_clear_sprite_table(Snes *s)
{
    scene_hide_every_sprite(m_platform(s));
    m_rts(s);
}

bool scene_upload_palette_present(Snes *s) { return m_code_matches(s, 0x7E388B, 36, 0x84B3B18FAFEEAE44ull); }

// $7E:388B
/* Send the working palette to CGRAM. */
void scene_send_palette(Platform *pf)
{
    vid_send_palette(pf, PF_AT(0x7E388D), 0, PF_GAME(WORKING_PALETTE), COLORS);
}

void scene_upload_palette(Snes *s)
{
    scene_send_palette(m_platform(s));
    m_rts(s);
}

/* ---- The palettes' animations, a frame at a time ---------------------------------------------- */

/* The task reads the multiplier's operands into its first scratch word before
 * the animations, which multiply, and writes them back after. The registers
 * can't be read: both bytes read back as the bus last had them, the read's own
 * address's high byte, and the write multiplies them. Scaffolding: the
 * multiplier and the cell left as the original leaves them. */
enum { MULTIPLIER_READ_BACK = 0x4242 };

static void put_the_multiplier_back(GameState *g, uint16_t kept)
{
    set_scene_call_first(g, scene_program_own(), kept);
    hw_set_multiplicand(g, (uint8_t)kept);
    (void)hw_multiply_by(g, (uint8_t)(kept >> 8));
}

// $7E:37FA
/* A frame task: the palettes animate a frame (core/palette.c, through bank
 * $FD's jump to them). */
static TaskEnd animate_the_palettes_task(GameState *g, Platform *pf)
{
    (void)pf;
    TIMED_AS(g, 0xFDFFF7, palette_animate_a_frame(g));
    ORIGINAL_EFFECT(put_the_multiplier_back(g, MULTIPLIER_READ_BACK));
    return task_goes_on();
}

/* It leaves A and Y as the animations leave them, past their slots. */
static TaskLeftovers animate_the_palettes_leaves(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    uint16_t past_the_slots = palette_animations_leave();
    return (TaskLeftovers){ .a_set = true, .a = past_the_slots, .y_set = true, .y = past_the_slots, .a_8bit = true,
                            .overflow_clear = true };
}

void scene_animate_the_palettes(Snes *s) { scene_task_alone(s, SCENE_TASK_ANIMATE_PALETTES); }

/* The task's code, and the animations' room in their slots (core/palette.c). */
bool scene_animate_the_palettes_present(Snes *s)
{
    return scene_on_scene_page_io_bank(s) && m_code_matches(s, 0x7E37FA, 0x11, 0x87E8A981D5C5DAFEull) &&
           palette_animate_runs(s);
}

/* ---- The screen's fades ---------------------------------------------------------------------- */

/* The scenes' fades of the whole screen, a step of brightness every so many
 * frames (the fades' pace, which a scene sets): up to full brightness, down
 * to dark, and down to dark at the end of a scene -- after the music has faded
 * out over $40 steps and with the sound then stopped, or as it is -- where the
 * program's main loop ends the scene. */
enum { FULL_BRIGHTNESS = 0x0F, DARK = 0x00, MUSIC_FADE_STEPS = 0x40, SILENT = 0x00 };

/* Whether this frame takes a step: the countdown runs out, and starts over. */
static bool fade_steps(GameState *g)
{
    uint8_t left = (uint8_t)(scene_fade_countdown(g) - 1);
    set_scene_fade_countdown(g, left);
    if (left)
        return false;
    set_scene_fade_countdown(g, scene_fade_period(g));
    return true;
}

/* A step darker; true at dark. */
static bool darker(GameState *g)
{
    uint8_t level = (uint8_t)(scene_brightness(g) - 1);
    set_scene_brightness(g, level);
    return level == DARK;
}

/* The scene is over: the program's main loop leaves (its flag counted up, a
 * byte). */
static void end_the_scene(GameState *g)
{
    uint16_t over = scene_program_over(g);
    set_scene_program_over(g, (uint16_t)((over & 0xFF00) | (uint8_t)(over + 1)));
}

// $7E:347A
static TaskEnd brighten_task(GameState *g, Platform *pf)
{
    (void)pf;
    if (!fade_steps(g))
        return task_goes_on();
    uint8_t level = (uint8_t)(scene_brightness(g) + 1);
    set_scene_brightness(g, level);
    return level == FULL_BRIGHTNESS ? task_is_done() : task_goes_on();
}

// $7E:3495
static TaskEnd darken_task(GameState *g, Platform *pf)
{
    (void)pf;
    return fade_steps(g) && darker(g) ? task_is_done() : task_goes_on();
}

/* The driver's command reads A's high byte, the list's task, as it called. */
enum { DRIVER = 0xC70004, THE_DRIVER_READS_A_HIGH_AND_Y = DC_AL | DC_X | DC_N | DC_Z };

// $7E:34AB
/* The music fades out, and the screen goes dark after it, in silence. */
static TaskEnd fade_the_music_out_task(GameState *g, Platform *pf)
{
    (void)g;
    au_fade(pf, PF_CALL_DC(0x7E34B8, DRIVER, THE_DRIVER_READS_A_HIGH_AND_Y), AU_FADE_MUSIC, MUSIC_FADE_STEPS, SILENT);
    return (TaskEnd){ TASK_HANDS_OVER, SCENE_TASK_DARKEN_IN_SILENCE_AND_END };
}

// $7E:34C2
static TaskEnd darken_in_silence_and_end_task(GameState *g, Platform *pf)
{
    if (!fade_steps(g) || !darker(g))
        return task_goes_on();
    au_command(pf, PF_CALL_DC(0x7E34DA, DRIVER, THE_DRIVER_READS_A_HIGH_AND_Y),
               (AudioCommand){ .command = AU_STOP_ALL, .count = 0 });
    end_the_scene(g);
    return task_is_done();
}

// $7E:34E3
static TaskEnd darken_and_end_task(GameState *g, Platform *pf)
{
    (void)pf;
    if (!fade_steps(g) || !darker(g))
        return task_goes_on();
    end_the_scene(g);
    return task_is_done();
}

/* Each works in 8-bit A, whose high byte is still what the list called it
 * with, the task. */
static TaskLeftovers fade_leaves(SceneTask task)
{
    return (TaskLeftovers){ .a_set = true, .a = task, .a_8bit = true, .a_high_as_called = true };
}
static TaskLeftovers brighten_leaves(const GameState *g, TaskEnd end) { (void)g; (void)end; return fade_leaves(SCENE_TASK_BRIGHTEN); }
static TaskLeftovers darken_leaves(const GameState *g, TaskEnd end) { (void)g; (void)end; return fade_leaves(SCENE_TASK_DARKEN); }
static TaskLeftovers fade_the_music_out_leaves(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    return fade_leaves(SCENE_TASK_MUSIC_FADES);
}
static TaskLeftovers darken_in_silence_and_end_leaves(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    return fade_leaves(SCENE_TASK_DARKEN_IN_SILENCE_AND_END);
}
static TaskLeftovers darken_and_end_leaves(const GameState *g, TaskEnd end)
{
    (void)g;
    (void)end;
    return fade_leaves(SCENE_TASK_DARKEN_AND_END);
}

void scene_brighten(Snes *s) { scene_task_alone(s, SCENE_TASK_BRIGHTEN); }
void scene_darken(Snes *s) { scene_task_alone(s, SCENE_TASK_DARKEN); }
void scene_fade_the_music_out(Snes *s) { scene_task_alone(s, SCENE_TASK_MUSIC_FADES); }
void scene_darken_in_silence_and_end(Snes *s) { scene_task_alone(s, SCENE_TASK_DARKEN_IN_SILENCE_AND_END); }
void scene_darken_and_end(Snes *s) { scene_task_alone(s, SCENE_TASK_DARKEN_AND_END); }

/* Each one's code; the cells and the sound buffer are reached through the data
 * bank. */
bool scene_brighten_present(Snes *s) { return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E347A, 0x1B, 0x4B223E782CD157AAull); }
bool scene_darken_present(Snes *s) { return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E3495, 0x16, 0x6311E869565DC92Eull); }
bool scene_fade_the_music_out_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E34AB, 0x17, 0x929CC3F85DA015F6ull);
}
bool scene_darken_in_silence_and_end_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E34C2, 0x21, 0x0DBC28C33AF5017Full);
}
bool scene_darken_and_end_present(Snes *s)
{
    return scene_on_scene_page_low_bank(s) && m_code_matches(s, 0x7E34E3, 0x18, 0xCC5C4AA8697F86C8ull);
}

/* ---- The frame's own tasks, for the lists ---------------------------------------------------- */

const SceneTaskPort scene_screen_tasks[] = {
    { SCENE_TASK_ANIMATE_PALETTES, SCENE_FRAME_TASKS, animate_the_palettes_task, scene_animate_the_palettes_present,
      animate_the_palettes_leaves, false },
    { SCENE_TASK_FINISH_SPRITES, SCENE_VBLANK_TASKS, finish_sprites_task, scene_finish_and_upload_sprites_present,
      finish_sprites_leaves, false },
    { SCENE_TASK_BRIGHTEN, SCENE_FRAME_TASKS, brighten_task, scene_brighten_present, brighten_leaves, false },
    { SCENE_TASK_DARKEN, SCENE_FRAME_TASKS, darken_task, scene_darken_present, darken_leaves, false },
    { SCENE_TASK_MUSIC_FADES, SCENE_FRAME_TASKS, fade_the_music_out_task, scene_fade_the_music_out_present,
      fade_the_music_out_leaves, true },
    { SCENE_TASK_DARKEN_IN_SILENCE_AND_END, SCENE_FRAME_TASKS, darken_in_silence_and_end_task,
      scene_darken_in_silence_and_end_present, darken_in_silence_and_end_leaves, true },
    { SCENE_TASK_DARKEN_AND_END, SCENE_FRAME_TASKS, darken_and_end_task, scene_darken_and_end_present,
      darken_and_end_leaves, false },
};
const unsigned scene_screen_task_count = sizeof scene_screen_tasks / sizeof scene_screen_tasks[0];

/* ---- The loaders' transfer ------------------------------------------------------------------ */

bool scene_transfer_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E38E7, 36, 0xDD4B67DA62CC8E67ull);
}

// $7E:38E7
/* The program's own transfer: what the decompressor has just unpacked (its
 * destination and length) into the register and in the mode the caller put in
 * the scene's scratch. Both are game data the loaders choose, so it goes
 * through the SNES-only escape; the movies' loaders all feed the VRAM data port. */
void scene_send_unpacked(const GameState *g, Platform *pf)
{
    SceneProgramPage scene = scene_program_own();
    const VidSnesTransfer t = { .dest = scene_call_transfer_register(g, scene), .source = decompress_dest(g),
                                .size = decompress_length(g), .mode_from_data = true,
                                .mode = scene_call_transfer_control(g, scene) };
    vid_snes_copy(pf, PF_AT(0x7E38EA), &t);
}

/* Returns with RTL; nothing it leaves in the registers is read. */
void scene_transfer(Snes *s)
{
    scene_send_unpacked(state_of(s), m_platform(s));
    m_rtl(s);
}

/* ---- Resources copied as they are, and the palette made current ------------------------------ */

/* The bank of work RAM the unpacker's destination names, by its bank byte's
 * low bit, as the unpacker and the resource copies take it. */
uint8_t scene_unpacked_bank(const GameState *g)
{
    return ((decompress_dest(g) >> 16) & 1) ? SCENE_UPPER_WORK_RAM : SCENE_LOWER_WORK_RAM;
}

// $7E:36AA
/* `bytes` of a resource (by its number in the program's list) as they are, to
 * `to` in the bank the unpacker's destination names. The caller hands the
 * count over in its page's first cell, which keeps it. */
void scene_copy_resource_to(GameState *g, SceneProgramPage page, unsigned resource, uint16_t to, uint16_t bytes)
{
    ORIGINAL_EFFECT(set_scene_call_first(g, page, bytes));
    scene_copy_resource_bytes(g, scene_unpacked_bank(g), scene_resource_offset(g, resource), to, bytes);
    ORIGINAL_EFFECT(charge_block_move(g, bytes));
}

/* The loaded palette and the working one the frame sends, in the first bank. */
enum { LOADED_PALETTE = 0x7E2000, PALETTE_BYTES = 0x200 };

// $7E:38AF
/* The palette as loaded becomes the one the frame sends. */
void scene_use_loaded_palette(GameState *g)
{
    wram_copy(g, LOADED_PALETTE, WORKING_PALETTE, PALETTE_BYTES);
    ORIGINAL_EFFECT(charge_block_move(g, PALETTE_BYTES));
}

/* ---- Per-line effects from descriptors --------------------------------------------------- */

/* A table of descriptors, five bytes each, ended by a first byte of $FF: a
 * channel's mode and the register it feeds, the address of its table, and the
 * bank the table's indirect addresses lie in. The program reads the last value
 * as a word, so the byte after it -- the next descriptor's mode, or the end --
 * lands in the channel's size and table-cursor registers, which HDMA rewrites
 * (VidLineEffect.snes_spill). */
typedef struct {
    uint8_t mode, target;
    uint16_t table;
    uint8_t bank, spill;
} LineEffectDescriptor;

enum { DESCRIPTOR_BYTES = 5, END_OF_DESCRIPTORS = 0xFF, LINE_CHANNELS = 8, DESCRIPTOR_BANK = SCENE_LOWER_WORK_RAM };

static uint32_t descriptor_at(uint16_t at, unsigned offset) { return (uint32_t)DESCRIPTOR_BANK << 16 | (uint16_t)(at + offset); }

static LineEffectDescriptor read_descriptor(const GameState *g, uint16_t at)
{
    uint16_t head = wram_read16(g, descriptor_at(at, 0));
    uint16_t table = wram_read16(g, descriptor_at(at, 2));
    uint16_t bank = wram_read16(g, descriptor_at(at, 4));
    return (LineEffectDescriptor){ (uint8_t)head, (uint8_t)(head >> 8), table, (uint8_t)bank, (uint8_t)(bank >> 8) };
}

/* A `VidLineEffect` carries the transfer mode and the indirect bit and nothing
 * else, so a mode byte with any other bit set would be written back as a
 * different one. */
enum { LINE_EFFECT_MODE_BITS = 0x47 };

/* The code, and a table every descriptor of which names a target the service
 * knows. There are eight channels to fill, so a table without its end in eight
 * descriptors is one the original would run off the end of. Peeks only, from
 * bank $7E, which the routine sets itself. */
bool scene_setup_line_effects_present(Snes *s)
{
    if (!m_code_matches(s, 0x7E36CC, 60, 0x45191A42A1EEF6A8ull))
        return false;
    uint16_t at = s->cpu.y;
    for (int channel = 0; channel < LINE_CHANNELS; channel++) {
        uint8_t mode = snes_bus_peek(&s->bus, descriptor_at(at, 0));
        if (mode == END_OF_DESCRIPTORS)
            return true;
        if (mode & ~LINE_EFFECT_MODE_BITS)
            return false;
        VidLineEffect e = { 0 };
        if (!vid_line_effect_decode(mode, snes_bus_peek(&s->bus, descriptor_at(at, 1)), &e))
            return false;
        at = (uint16_t)(at + DESCRIPTOR_BYTES);
    }
    return false;
}

// $7E:36CC
/* Program the per-line effects from the descriptors at `at`, one to each
 * channel from 0 up. The race's table drives window 1's left edge, the Mode 7
 * matrix in two halves, BG1's scroll, the color math layers and the BG mode.
 * The guard has walked the table and refused one a descriptor of which no
 * target names, so every descriptor decodes. Returns where the descriptors
 * end. */
uint16_t scene_set_up_line_effects(const GameState *g, Platform *pf, uint16_t at)
{
    for (uint8_t channel = 0;; channel++) {
        LineEffectDescriptor d = read_descriptor(g, at);
        VidLineEffect e = { 0 };
        if (d.mode == END_OF_DESCRIPTORS || !vid_line_effect_decode(d.mode, d.target, &e))
            return at;
        e.table = PF_GAME((uint32_t)d.bank << 16 | d.table);
        e.indirect_bank = d.bank;
        e.snes_spill = d.spill;
        vid_set_line_slot(pf, PF_AT(0x7E36E8), channel, &e);
        at = (uint16_t)(at + DESCRIPTOR_BYTES);
    }
}

/* The overflow of stepping past a descriptor, which the program reads after. */
static bool overflows_stepping_past(uint16_t at)
{
    uint16_t next = (uint16_t)(at + DESCRIPTOR_BYTES);
    return (~(at ^ DESCRIPTOR_BYTES) & (at ^ next) & 0x8000) != 0;
}

/* Y is the table, and back it is where the descriptors end, with the overflow
 * of the last step past one (as it was when there is none). The routine works
 * with the direct page on the DMA registers and the data bank $7E, and puts both
 * back. */
void scene_setup_line_effects(Snes *s)
{
    uint16_t start = s->cpu.y;
    uint16_t end = scene_set_up_line_effects(state_of(s), m_platform(s), start);
    if (end != start)
        m_set_flag(s, CPU_FLAG_V, overflows_stepping_past((uint16_t)(end - DESCRIPTOR_BYTES)));
    s->cpu.y = end;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}
