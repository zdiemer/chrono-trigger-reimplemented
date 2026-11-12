/* Scenes: the scene program's screen -- setting it up, and what it sends each
 * frame.
 *
 * Every scene starts from the same screen: blank, the sprite graphics at VRAM
 * $4000, every layer, window and effect off (init_video), its work RAM
 * cleared. Its vblank tasks then send each frame's sprites and palette
 * (upload_sprites, upload_palette), and hide every sprite in the table for
 * the scene to place again (clear_sprite_table); its frame tasks animate the
 * palettes and fade the screen and the music. Its loaders move graphics into
 * VRAM with transfers of their own -- of what the unpacker made, in the way
 * the setup chooses (transfer), and of a buffer given in the call cells
 * (scene_send_from) -- copy resources as they are and make the loaded palette
 * the current one, and a scene with per-line effects programs them from a
 * table of descriptors (setup_line_effects).
 *
 * The work RAM's clear, the sprite table's and the setups' clears of their
 * buffers (scene_clear_buffer) copy the product the Mode 7 multiplier offers,
 * 0 or $E0, out of the PPU: a transfer that reads a register rather than
 * writing one, which no video call expresses, so it goes through the
 * SNES-only escape (vid_snes_copy); the matrix values they leave behind are
 * the video state. */
#include "scene/scene.h"

#include "charge.h"
#include "core/palette.h"
#include "cpu_io.h"
#include "platform/audio.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/vars_ram.h"
#include "scene/scene_data.h"
#include "vars.h"
#include "wram.h"

/* The working palette, in the first bank of work RAM. */
enum { WORKING_PALETTE = 0x7E2200 };
enum { COLORS = 256 };

/* Where the scene keeps the sprites' graphics. */
enum { SPRITE_GRAPHICS = 0x4000 };

/* The rows of this file's tasks (below), where the task lists find them. */
enum ScreenTaskRow {
    ROW_ANIMATE_PALETTES,
    ROW_FINISH_SPRITES,
    ROW_BRIGHTEN,
    ROW_DARKEN,
    ROW_MUSIC_FADES,
    ROW_DARKEN_IN_SILENCE_AND_END,
    ROW_DARKEN_AND_END,
};

/* ---- The program stops ---------------------------------------------------------------------- */

// $7E:302A
/* The program stops: interrupts and per-line effects off and the screen
 * blanked, before the game goes back to the field's start. */
void scene_stops_the_display(Platform *pf)
{
    tm_enable_interrupts(pf, PF_AT(0x7E302F), 0);
    vid_enable_line_effects(pf, PF_AT(0x7E3032), 0);
    vid_set_display(pf, PF_AT(0x7E3035), &(VidDisplay){ .blank = true });
}

/* ---- The program's work RAM cleared --------------------------------------------------------- */

/* What the clear covers: the first page of low RAM, the scene's own cells,
 * then low RAM from past the stack up to the sound driver interface's page --
 * or, for the title coming back, from the task lists, keeping what lies
 * between. */
enum { SCENE_CELLS_BYTES = 0x0100, FROM_THE_STACK = 0x000900, FROM_THE_TASKS = 0x000920, SOUND_PAGE = 0x1E00 };

// $7E:3092
/* Clear the program's work RAM: every byte set to the product the Mode 7
 * multiplier offers, with the matrix set so that it is 0 -- `matrix_left`
 * times 0, `matrix_left` being what the caller hands on (the fades' pace),
 * which the multiplier keeps afterwards. The second transfer keeps the first one's mode,
 * destination and bank, so it programs only its source and size. Returns
 * whether it was the title's shorter clear. */
bool scene_clear_the_work_ram(GameState *g, Platform *pf, uint8_t matrix_left)
{
    vid_set_mode7(pf, PF_AT(0x7E3096), &(VidMode7){ .a = (int16_t)(matrix_left << 8 | matrix_left) }, VID_M7_MATRIX_A);
    vid_set_mode7(pf, PF_AT(0x7E309D), &(VidMode7){ .b = 0 }, VID_M7_MATRIX_B);
    const VidSnesTransfer scene_cells = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = 0x000000, .size = SCENE_CELLS_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E30B6), &scene_cells);
    bool keep_below_tasks = scene_flags(g) & SCENE_FLAG_TITLE_WITH_LINE_IRQ;
    uint32_t from = keep_below_tasks ? FROM_THE_TASKS : FROM_THE_STACK;
    const VidSnesTransfer rest = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = from, .size = (uint16_t)(SOUND_PAGE - from) };
    vid_snes_copy(pf, PF_AT(keep_below_tasks ? 0x7E30D2 : 0x7E30E6), &rest);
    return keep_below_tasks;
}

/* ---- A scene's first screen ------------------------------------------------------------------ */

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

// $7E:380B
/* The vblank task that sends the frame's sprites: the frame's last group of
 * four finished (scene/sprites.c), then the table sent. */
static TaskEnd finish_sprites_task(GameState *g, Platform *pf)
{
    TIMED_AS(g, 0x7E3870, scene_hide_rest_of_group(g));
    upload_sprites(g, pf);
    return task_goes_on();
}

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

// $7E:388B
/* Send the working palette to CGRAM. */
void scene_send_palette(Platform *pf)
{
    vid_send_palette(pf, PF_AT(0x7E388D), 0, PF_GAME(WORKING_PALETTE), COLORS);
}

/* ---- The palettes' animations, a frame at a time ---------------------------------------------- */

/* The task keeps the multiplier's operands aside in its first scratch word
 * around the animations, which multiply. The game can't read them back: both
 * bytes come out as $42. The word stays where the Epoch's dive's random draws
 * stir it in after it. */
enum { MULTIPLIER_READ_BACK = 0x4242 };

static void keep_the_multiplier_s_word(GameState *g, uint16_t kept) { scene_leave_for_the_draws(g, kept, TM_ENTROPY_WORDS); }

// $7E:37FA
/* A frame task: the palettes animate a frame (core/palette.c, through bank
 * $FD's jump to them), where every slot holds an animation the game makes;
 * otherwise the program goes no further (palette_animated). */
static TaskEnd animate_the_palettes_task(GameState *g, Platform *pf)
{
    (void)pf;
    bool animated = false;
    TIMED_AS(g, 0xFDFFF7, animated = palette_animated(g));
    if (!animated)
        return task_goes_on();
    keep_the_multiplier_s_word(g, MULTIPLIER_READ_BACK);
    charge_legacy_io_accesses(g, 4);
    return task_goes_on();
}

/* The Epoch's dive into Lavos (scene/epoch_dive.c) has its sequence run a
 * frame of the animations itself as it starts a fade, where the word the
 * routine keeps aside is two bytes of the program's own code, put back as
 * they were; the copy stays in the channel's call cell. */

void scene_animate_the_palettes_for(GameState *g, SceneChannel ch)
{
    (void)ch;
    bool animated = false;
    TIMED_AS(g, 0xFDFFF7, animated = palette_animated(g));
    if (animated)
        charge_legacy_accesses(g, 4);
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

/* The sound driver's entry. */
enum { DRIVER = 0xC70004 };

// $7E:34AB
/* The music fades out, and the screen goes dark after it, in silence. */
static TaskEnd fade_the_music_out_task(GameState *g, Platform *pf)
{
    (void)g;
    au_fade(pf, PF_CALL(0x7E34B8, DRIVER), AU_FADE_MUSIC, MUSIC_FADE_STEPS, SILENT);
    return (TaskEnd){ TASK_HANDS_OVER, SCENE_TASK_DARKEN_IN_SILENCE_AND_END };
}

// $7E:34C2
static TaskEnd darken_in_silence_and_end_task(GameState *g, Platform *pf)
{
    if (!fade_steps(g) || !darker(g))
        return task_goes_on();
    au_command(pf, PF_CALL(0x7E34DA, DRIVER),
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

/* ---- The frame's own tasks, for the lists ---------------------------------------------------- */

const SceneTaskPort scene_screen_tasks[] = {
    [ROW_ANIMATE_PALETTES] = { SCENE_TASK_ANIMATE_PALETTES, SCENE_FRAME_TASKS, animate_the_palettes_task },
    [ROW_FINISH_SPRITES] = { SCENE_TASK_FINISH_SPRITES, SCENE_VBLANK_TASKS, finish_sprites_task },
    [ROW_BRIGHTEN] = { SCENE_TASK_BRIGHTEN, SCENE_FRAME_TASKS, brighten_task },
    [ROW_DARKEN] = { SCENE_TASK_DARKEN, SCENE_FRAME_TASKS, darken_task },
    [ROW_MUSIC_FADES] = { SCENE_TASK_MUSIC_FADES, SCENE_FRAME_TASKS, fade_the_music_out_task },
    [ROW_DARKEN_IN_SILENCE_AND_END] = { SCENE_TASK_DARKEN_IN_SILENCE_AND_END, SCENE_FRAME_TASKS, darken_in_silence_and_end_task },
    [ROW_DARKEN_AND_END] = { SCENE_TASK_DARKEN_AND_END, SCENE_FRAME_TASKS, darken_and_end_task },
};
const unsigned scene_screen_task_count = sizeof scene_screen_tasks / sizeof scene_screen_tasks[0];

// $7E:38E7
/* The program's own transfer: what the decompressor has just unpacked (its
 * destination and length) into the register and in the mode the caller put in
 * the scene's scratch. Both are game data the loaders choose, so it goes
 * through the SNES-only escape; the movies' loaders all feed the VRAM data port. */
void scene_send_unpacked_as(const GameState *g, Platform *pf, uint8_t mode, uint8_t port_register)
{
    uint16_t bytes = decompress_length(g);
    const VidSnesTransfer t = { .dest = port_register,
                                .data = scene_upload_data(decompress_dest(g), bytes ? bytes : 0x10000u, mode), .size = bytes,
                                .mode_from_data = true, .mode = mode };
    vid_snes_copy(pf, PF_AT(0x7E38EA), &t);
}

// $7E:38C3
/* The program's other transfer: `bytes` from `source` into a register, in
 * the mode given, with its channel programmed from the scene's call cells,
 * which keep it (the program's multiply carries in the byte after its
 * product, which the count lies in). */
static void keep_the_transfer_in_the_call(GameState *g, uint8_t mode, uint8_t port_register, uint32_t source, uint16_t bytes)
{
    SceneProgramPage scene = scene_program_own();
    set_scene_call_transfer_control(g, scene, mode);
    set_scene_call_transfer_register(g, scene, port_register);
    set_scene_call_transfer_source(g, scene, (uint16_t)source);
    set_scene_call_transfer_source_bank(g, scene, (uint8_t)(source >> 16));
    set_scene_call_transfer_bytes(g, scene, bytes);
}

void scene_send_from(GameState *g, Platform *pf, uint8_t mode, uint8_t port_register, uint32_t source, uint16_t bytes)
{
    keep_the_transfer_in_the_call(g, mode, port_register, source, bytes);
    const VidSnesTransfer t = { .dest = port_register, .data = scene_upload_data(source, bytes ? bytes : 0x10000u, mode), .size = bytes, .mode_from_data = true, .mode = mode };
    vid_snes_copy(pf, PF_AT(0x7E38E3), &t);
}

/* Clear a buffer by copying the Mode 7 multiplier's product (0 here) out of
 * the PPU, the way scene_clear_work_ram does: a transfer that reads a register
 * rather than writing one, which goes through the SNES-only escape. */
void scene_clear_buffer(Platform *pf, uint32_t buffer, uint16_t bytes, uint32_t site)
{
    const VidSnesTransfer t = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = buffer, .size = bytes };
    vid_snes_copy(pf, PF_AT(site), &t);
}

const SceneTransfer scene_pixels_into_high_bytes = { SCENE_TRANSFER_BYTES, VID_SNES_VRAM_DATA_HIGH };
const SceneTransfer scene_whole_words = { SCENE_TRANSFER_WORDS, VID_SNES_VRAM_DATA_LOW };

void scene_choose_transfer(GameState *g, SceneTransfer t)
{
    set_scene_call_transfer_control(g, scene_program_own(), t.mode);
    set_scene_call_transfer_register(g, scene_program_own(), t.vram_register);
}

void scene_send_unpacked_by(GameState *g, Platform *pf, SceneTransfer t)
{
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked_as(g, pf, t.mode, t.vram_register));
}

void scene_send_unpacked(const GameState *g, Platform *pf)
{
    SceneProgramPage scene = scene_program_own();
    scene_send_unpacked_as(g, pf, scene_call_transfer_control(g, scene), scene_call_transfer_register(g, scene));
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
    (void)page;
    charge_legacy_accesses(g, 2);
    scene_copy_resource_bytes(g, scene_unpacked_bank(g), scene_resource_offset(g, resource), to, bytes);
    charge_block_move(g, bytes);
}

/* ---- A Mode 7 plane's name table --------------------------------------------------------------- */

/* A name table unpacked 64 tiles a row, each row widened with 64 empty tiles
 * across the plane's 128, over the buffer's 64 rows whatever a picture uses
 * of them. */
enum { UNPACKED_ROWS = 0x40, UNPACKED_ROW_BYTES = 0x40 };

// $7E:37CA
/* The rows' count, run down to 0, is left in the scene's first cell. */
void scene_widen_the_plane_map(GameState *g)
{
    for (uint32_t row = 0; row < UNPACKED_ROWS; row++) {
        uint32_t to = SCENE_PLANE_MAP + row * 2 * UNPACKED_ROW_BYTES;
        wram_copy(g, SCENE_UNPACK_BUFFER + row * UNPACKED_ROW_BYTES, to, UNPACKED_ROW_BYTES);
        wram_fill8(g, to + UNPACKED_ROW_BYTES, UNPACKED_ROW_BYTES, 0);
        /* The original moves the row's 64 bytes, then stores the first empty
         * tile and moves it on over the other 63, one byte on. */
        charge_block_move(g, UNPACKED_ROW_BYTES + (UNPACKED_ROW_BYTES - 1));
    }
    charge_legacy_accesses(g, 2);
}

// $7E:90A7
/* Blank the unused part of the plane's name table, keeping the pixels in
 * the high bytes. Continue immediately after the picture just uploaded. */
void scene_blank_past_the_plane(Platform *pf, PfOrigin site, uint16_t bytes)
{
    vid_fill_vram_half(pf, site, PF_GAME(SCENE_BLANK_TILE), bytes ? bytes : 0x10000u, VID_VRAM_LOW_BYTES);
}

/* The loaded palette and the working one the frame sends, in the first bank. */
enum { LOADED_PALETTE = 0x7E2000, PALETTE_BYTES = 0x200 };

// $7E:38AF
/* The palette as loaded becomes the one the frame sends. */
void scene_use_loaded_palette(GameState *g)
{
    wram_copy(g, LOADED_PALETTE, WORKING_PALETTE, PALETTE_BYTES);
    charge_block_move(g, PALETTE_BYTES);
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

// $7E:36CC
/* Program the per-line effects from the descriptors at `at`, one to each
 * channel from 0 up. The race's table drives window 1's left edge, the Mode 7
 * matrix in two halves, BG1's scroll, the color math layers and the BG mode.
 * A descriptor no target names ends the table there; the game's tables
 * have none. Returns where the descriptors end. */
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
