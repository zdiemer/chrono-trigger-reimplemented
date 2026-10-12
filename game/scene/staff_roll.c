/* Scenes: the staff roll -- the credits rolling up over a field of stars, and
 * The End.
 *
 * The telepod ending closes the game with the staff credits played fast: the
 * scene program's mode 13 sets it up ($7E:8EA8), its script ($7E:93E3) starts
 * the stars and the credits, and once the last name has rolled on the script
 * goes on to The End, where the picture behind the credits fades in from white
 * and the stars start drifting. The shared ending the Beyond Time ending plays
 * (from $7E:8538) rolls the same credits slowly, with the same two tasks.
 *
 * The pieces:
 *
 * - the setup: the music played fast, the stars scattered, the scene's
 *   pictures unpacked into VRAM (a picture on BG1, the credits' letters for
 *   BG3), its colors, the credits' map blanked, the per-line effect, the
 *   script, and the layers;
 * - the credits' roll, a frame task: every row of 8 lines (every frame when
 *   they roll fast) the next line of text is written into a row buffer as the
 *   layer's tiles, and the layer scrolls up -- a row a frame fast, a pixel
 *   every other frame slowly, the picture's frame parity saying which;
 * - the credits' upload, a vblank task: the row buffer into the row of the
 *   layer's map the line goes to, and the scroll;
 * - the script's two routines: the test for the credits' end, and the fade.
 *
 * The text is in the program: letters as their character codes, a line ended
 * by '/', the whole by a backslash. The letters' tiles follow the codes from
 * '@' on, a space being the blank tile.
 *
 * The stars themselves -- their scatter, their flight and The End's drift --
 * are stars.c's. */
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

/* ---- The credits -------------------------------------------------------------------------------- */

/* The credits' layer: BG3, its 32x32 map at $4800 and its tiles at $4000. A
 * line's letters start at the ninth tile of its row. */
enum { CREDITS_MAP_VRAM = 0x4800, CREDITS_TILES_VRAM = 0x4000, CREDITS_MAP_ROWS_BYTES = 0x0400 };
enum { MAP_ROW_WORDS = 0x20, MAP_ROWS_WORDS = 0x03E0, ROW_TILES = 32, FIRST_LETTER_TILE = 8 };

/* The row buffer, where the uploads read it: low RAM, through bank 0. */
enum { CREDITS_ROW = 0x001414 };

/* The text's markers, and the letters' tiles: a code's tile is its distance
 * from '@', a space the blank tile 0. A 0 passes over itself and the two
 * bytes after it (the staff roll's text has none). */
enum { END_OF_LINE = '/', END_OF_CREDITS = '\\', PASS_OVER = 0, PASSED_OVER_BYTES = 2, BLANK_TILE = 0 };

static uint8_t text_byte(const GameState *g, uint16_t at) { return wram_read8(g, (uint32_t)SCENE_LOWER_WORK_RAM << 16 | at); }

static uint8_t letter_tile(uint8_t code) { return code == ' ' ? BLANK_TILE : (uint8_t)(code - '@'); }

/* A row of text is 8 lines, and the slow roll moves a line every other frame,
 * so a new line is due every 16 frames; rolling fast, every frame. */
enum { FRAMES_A_LINE_SLOWLY = 16 };

static bool roll_fast(const GameState *g) { return scene_credits_fast(g) != 0; }

static bool line_due(const GameState *g)
{
    return roll_fast(g) || scene_frame_counter(g) % FRAMES_A_LINE_SLOWLY == 0;
}

/* The row of the map the next line goes to: the next row down, round to the
 * top after the 32nd. */
static uint16_t next_map_row(uint16_t row) { return (uint16_t)(((row + MAP_ROW_WORDS) & MAP_ROWS_WORDS) + CREDITS_MAP_VRAM); }

/* The credits' upload, which goes with the roll on the vblank list. */
enum { CREDITS_UPLOAD_TASK = 0x9EFC };

// $7E:9E7B
/* The next line of the credits, when one is due: the row blanked, the line's
 * letters written into it from the ninth tile, and the row of the map it goes
 * to moved on. The end of the text instead ends the credits: the upload is
 * taken off its list (the roll takes itself off its own, from its entry). */
static void write_next_line(GameState *g)
{
    if (!line_due(g))
        return;
    for (unsigned tile = 0; tile < ROW_TILES; tile++)
        set_scene_credits_row_byte(g, tile, BLANK_TILE);
    uint16_t at = scene_credits_text(g);
    unsigned tile = FIRST_LETTER_TILE;
    uint8_t code;
    while ((code = text_byte(g, at++)) != END_OF_LINE && code != END_OF_CREDITS) {
        if (code == PASS_OVER)
            at = (uint16_t)(at + PASSED_OVER_BYTES);
        else
            set_scene_credits_row_byte(g, tile++, letter_tile(code));
    }
    /* the original writes the letters through the work RAM port */
    ORIGINAL_EFFECT(hw_port_seek(g, (uint16_t)(CREDITS_ROW + tile), 0));
    if (code == END_OF_CREDITS) {
        set_scene_credits_over(g, (uint8_t)(scene_credits_over(g) + 1));
        scene_remove_task(g, SCENE_VBLANK_TASKS, CREDITS_UPLOAD_TASK);
        return;
    }
    set_scene_credits_text(g, at);
    set_scene_credits_row_vram(g, next_map_row(scene_credits_row_vram(g)));
}

/* A row of text is 8 pixels. */
enum { ROW_PIXELS = 8 };

// $7E:9ED6
/* The credits scroll up: a row a frame when they roll fast, and otherwise a
 * pixel on every odd frame. */
static void scroll_credits(GameState *g, bool odd_frame)
{
    uint16_t scroll = scene_credits_scroll(g);
    if (roll_fast(g))
        scroll = (uint16_t)(scroll + ROW_PIXELS);
    else if (odd_frame)
        scroll = (uint16_t)(scroll + 1);
    set_scene_credits_scroll(g, scroll);
}

/* The layer the credits scroll, by the scroll register the upload names: BG2
 * as the program comes, BG3 where the staff roll's setup has said so. */
enum { BG2_VERTICAL_SCROLL = 0x10, BG3_VERTICAL_SCROLL = 0x12 };

static VidLayer credits_layer(const GameState *g)
{
    return scene_credits_scroll_register(g, 0) == BG3_VERTICAL_SCROLL ? VID_BG3 : VID_BG2;
}

static void scroll_credits_on(GameState *g, VidLayer layer)
{
    for (unsigned store = 0; store < 2; store++)
        set_scene_credits_scroll_register(g, store, layer == VID_BG3 ? BG3_VERTICAL_SCROLL : BG2_VERTICAL_SCROLL);
}

enum { CREDITS_ROW_BYTES = ROW_TILES };

// $7E:9EFC
/* In vblank: the row the roll wrote, into the low bytes of its row of the
 * map (the tiles; the high bytes keep the blanking's), and the scroll. */
static void send_credits_row(const GameState *g, Platform *pf)
{
    vid_send_vram_half(pf, PF_AT(0x7E9F1D), scene_credits_row_vram(g), PF_GAME(CREDITS_ROW), CREDITS_ROW_BYTES,
                       VID_VRAM_LOW_BYTES, SCENE_VRAM_LOW_BYTES);
    vid_set_scroll(pf, PF_AT(0x7E9F22), credits_layer(g), 0, scene_credits_scroll(g), VID_SCROLL_V);
}

/* ---- The script's routines ---------------------------------------------------------------------- */

/* The End's part of the script, which its test sends it to once the credits
 * are over. A routine the script calls leaves the script on the call's
 * operand, which the interpreter then steps past. */
enum { THE_END_SCRIPT = 0x9417, CALL_OPERAND = 2 };

// $7E:996F
/* The script waits for the credits to end, then goes on to The End. */
static void go_on_after_the_credits(GameState *g, SceneChannel ch)
{
    if (scene_credits_over(g))
        set_scene_channels_script(g, ch, (uint16_t)(THE_END_SCRIPT - CALL_OPERAND));
}

/* The picture's colors (palette 1 but its clear color) fade in from white, a
 * level a frame, in the first animation slot. */
static const PaletteFade picture_fades_in = {
    .first_color = 17, .color_count = 15, .step = 1, .period = 1, .from_level = 0xE0, .to_level = 0,
};
enum { THE_END_FADE_SLOT = 0 };

// $7E:9FA9
static void fade_in_the_picture(GameState *g) { palette_start_fade_to_white(g, THE_END_FADE_SLOT, picture_fades_in); }

/* ---- The sprites --------------------------------------------------------------------------------- */

// $7E:8C05
/* In vblank, every frame: every sprite's high bits cleared -- small, and on
 * the screen's left -- before the stars are drawn. (The original copies the
 * 32 bytes from the cartridge, the start of the race's plane, which is 0.) */
static void clear_sprite_high_bits(GameState *g)
{
    for (unsigned i = 0; i < SCENE_SPRITE_HIGH_TABLE_BYTES; i++)
        set_scene_sprite_high_table_byte(g, i, 0);
}

/* ---- The setup ---------------------------------------------------------------------------------- */

/* The scene's pictures in VRAM: BG1's map and tiles, and the credits' tiles,
 * the first $200 bytes of their resource where its blank tiles are. */
enum { PICTURE_MAP_VRAM = 0x6000, PICTURE_TILES_VRAM = 0x5000, LETTERS_VRAM = 0x4600, BLANK_TILES_BYTES = 0x0200 };

typedef struct {
    uint16_t resource;     /* its place in the program's resource list, as the unpacker takes it */
    uint16_t vram;
    uint32_t vram_site;    /* where the program points VRAM at it */
    uint16_t bytes;        /* how much of it is sent (0: all the unpacker made) */
} StaffRollGraphics;

static const StaffRollGraphics staff_roll_graphics[] = {
    { 0xD5, PICTURE_MAP_VRAM, 0x7E8EEE, 0 },
    { 0x33, LETTERS_VRAM, 0x7E8EFE, 0 },
    { 0xAE, CREDITS_TILES_VRAM, 0x7E8F0E, BLANK_TILES_BYTES },
    { 0x8D, PICTURE_TILES_VRAM, 0x7E8F24, 0 },
};
/* The program's call that unpacks each, beside the table. */
static const uint32_t staff_roll_graphics_unpacked_at[] = { 0x7E8EF4, 0x7E8F04, 0x7E8F14, 0x7E8F2A };
enum { STAFF_ROLL_GRAPHICS = sizeof staff_roll_graphics / sizeof staff_roll_graphics[0] };

/* Where the unpacker leaves what goes on to VRAM. */
enum { UNPACK_BUFFER = 0x7F0200 };

/* Colors copied as they are out of the program's resources (by their number
 * in the list), sixteen at a time: the picture's palette (BG palette 1), and
 * the stars' (sprite palette 1). */
enum { PICTURE_COLORS = 56, STARS_COLORS = 19, COLORS_COPIED_BYTES = 0x20 };
enum { PICTURE_PALETTE = 16, STARS_PALETTE = 144 };

/* The loaded palette, where the copies go: its colors two bytes each from the
 * start of its page. */
enum { LOADED_PALETTE_PAGE = 0x2000, COLOR_BYTES = 2 };

/* A resource's colors into the loaded palette from color `first`, in the first
 * bank: the setup moves the unpacker's destination there (a page and bank of
 * 0) before each copy. */
static void copy_colors(GameState *g, unsigned resource, unsigned first)
{
    set_decompress_dest(g, decompress_dest(g) & 0xFF);
    scene_copy_resource_to(g, scene_program_own(), resource, (uint16_t)(LOADED_PALETTE_PAGE + first * COLOR_BYTES),
                           COLORS_COPIED_BYTES);
}

/* The credits' letters: white, light grey and dark grey on the clear color,
 * the first four colors of the loaded palette. */
static const uint16_t letter_colors[] = { 0x0000, 0x77BD, 0x4631, 0x1CE7 };

// $7E:8CB2
void scene_set_letter_colors(GameState *g)
{
    for (unsigned i = 0; i < sizeof letter_colors / sizeof letter_colors[0]; i++)
        set_palette_base(g, i, letter_colors[i]);
}

/* The credits' map blanked: every tile the blank one, in front. The program
 * fills the low bytes from its blank tile and the high bytes from the byte
 * before the roll's code, $20, the priority bit; a fill of one half of each
 * word is what the upload calls don't model, so both go through the SNES-only
 * escape. The high bytes' fill is the caller's own code, at `in_front_site`. */
enum { IN_FRONT = 0x7E9E7A };

// $7E:90A7
void scene_blank_credits_map(Platform *pf, PfOrigin in_front_site)
{
    const VidSnesTransfer tiles = { .dest = VID_SNES_VRAM_DATA_LOW, .source = SCENE_BLANK_TILE,
                                    .size = CREDITS_MAP_ROWS_BYTES, .target_reg = VID_SNES_VRAM_ADDRESS,
                                    .target = CREDITS_MAP_VRAM, .port = SCENE_VRAM_LOW_BYTES };
    vid_snes_copy(pf, PF_AT(0x7E90BD), &tiles);
    const VidSnesTransfer attributes = { .dest = VID_SNES_VRAM_DATA_HIGH, .source = IN_FRONT,
                                         .size = CREDITS_MAP_ROWS_BYTES, .target_reg = VID_SNES_VRAM_ADDRESS,
                                         .target = CREDITS_MAP_VRAM, .port = SCENE_VRAM_WHOLE_WORDS };
    vid_snes_copy(pf, in_front_site, &attributes);
}

/* The scene's layers: the picture on BG1, scrolled to where it shows, its map
 * at $6000 and its tiles at $5000; the credits on BG3; mode 1; the credits
 * subtracting from what is under them; BG3 and the sprites on the main
 * screen, nothing on the sub screen, and no window masking either. */
enum { PICTURE_SCROLL_X = 0x90, PICTURE_SCROLL_Y = 0x48 };

static const VidBg picture_bg = { VID_TILEMAP(PICTURE_MAP_VRAM, 32, 32) };
static const VidBg credits_bg = { VID_TILEMAP(CREDITS_MAP_VRAM, 32, 32) };
static const VidColorMath credits_math = { .subtract = true, .layers = VID_BG3_BIT };

static void set_up_layers(Platform *pf)
{
    vid_set_scroll(pf, PF_AT(0x7E8FA8), VID_BG1, PICTURE_SCROLL_X, PICTURE_SCROLL_Y, VID_SCROLL_H | VID_SCROLL_V);
    vid_set_bg_chars(pf, PF_AT(0x7E8FB4), VID_BG1, PICTURE_TILES_VRAM, 0x0000);
    vid_set_bg_chars(pf, PF_AT(0x7E8FB8), VID_BG3, CREDITS_TILES_VRAM, 0x0000);
    vid_set_bg_map(pf, PF_AT(0x7E8FBC), VID_BG1, &picture_bg);
    vid_set_bg_map(pf, PF_AT(0x7E8FC0), VID_BG3, &credits_bg);
    vid_set_mode(pf, PF_AT(0x7E8FC4), &(VidMode){ .mode = 1 });
    vid_set_color_math(pf, PF_AT(0x7E8FC8), &credits_math, VID_MATH_LAYERS);
    vid_set_screens(pf, PF_AT(0x7E8FCD), VID_BG3_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN | VID_SCREEN_SUB);
    vid_set_windows(pf, PF_AT(0x7E8FD2), &(VidWindows){ .win_main = 0, .win_sub = 0 }, VID_WINDOWS_MAIN | VID_WINDOWS_SUB);
}

/* The stars come at the viewer: the star field's speed, kept in channel 1's
 * first register, at -7. */
enum { STARS_CHANNEL = 1, STARS_SPEED = 0, STARS_COMING = 0xFFF9 };

/* The per-line effect: the descriptors at $9023, one channel. */
enum { STAFF_ROLL_LINE_EFFECTS = 0x9023, STAFF_ROLL_LINE_CHANNELS = 0x01 };

/* The star field's drawing, made to draw every star behind the layers: its
 * branch for the far stars made a branch to the next instruction. */
enum { EVERY_STAR_BEHIND = 0x0080 };

/* The credits' text, and the row before the map's first, which the first
 * line's step brings to the first. */
enum { CREDITS_TEXT = 0xAB1A, ROW_BEFORE_THE_MAP = 0xFFE0 };

/* The scene's script, on channel 0, and its song. */
enum { STAFF_ROLL_SCRIPT = 0x93E3, STAFF_ROLL_SONG = 0x1B };

/* ---- Where the setup calls the original's code -------------------------------------------------- */

/* The program's own send of the command in the sound command buffer
 * ($7E:377E), called with 8-bit A: it waits for the driver to take it. It
 * reads none of the registers but Y and the data bank. What it leaves in A and
 * the carry the stars' scatter mixes into its random numbers, so nothing
 * touches them between the two. */
enum { SCENE_SEND = 0x7E377E, SEND_IGNORES = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C };

static void play_music_fast(Snes *s)
{
    GameState *g = state_of(s);
    set_sound_command(g, AU_FAST_FORWARD);
    set_sound_command_parameters(g, 0, 1);
    m_set_flag(s, CPU_FLAG_M, true);
    au_driver_call(m_platform(s), PF_CALL(0x7E8EB2, SCENE_SEND), SEND_IGNORES);
}

/* The stars scattered at random, their first draw from what the send left in
 * A and the carry (the original hands them on unexamined); the overflow its
 * last draw leaves goes on to the unpacker. */
enum { SCATTER_STARS = 0x7E5777 };

static void scatter_stars(Snes *s)
{
    GameState *g = state_of(s);
    SceneDraw first = scene_draw_from(s->cpu.a, m_flag(s, CPU_FLAG_C), TM_ENTROPY_BYTES);
    TIMED_AS(g, SCATTER_STARS, scene_scatter_stars(g, m_platform(s), first));
    ORIGINAL_EFFECT(m_set_flag(s, CPU_FLAG_V, scene_random_last_overflow()));
}

/* The program's unpacker ($7E:3691): the resource's place in its list in X,
 * and the unpacking into the destination set. */
enum { UNPACK = 0x7E3691, UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C };

static void unpack(Snes *s, uint32_t site, uint16_t resource)
{
    s->cpu.x = resource;
    m_jsr_dc(s, site, UNPACK, UNPACK_IGNORES);
}

/* The script's first step ($7E:3749), called with 16-bit A and the program's
 * bank as the data bank, which the program sets from a word it pushes: the
 * word's other byte, 0, waits on the stack to be the data bank again after.
 * It reads Y and the overflow as the per-line effects' setup leaves them:
 * where the descriptors end, and clear. It comes back on its channel's page;
 * the program sets the layers up on the PPU's registers' and leaves the
 * direct page on the scene's. */
enum { FIRST_STEP = 0x7E3749, FIRST_STEP_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C, SCENE_PAGE = 0x0000 };

static void first_script_step(Snes *s, uint16_t descriptors_end)
{
    m_push8(s, 0x00);
    s->cpu.db = SCENE_LOWER_WORK_RAM;
    m_set_flag(s, CPU_FLAG_M, false);
    s->cpu.y = descriptors_end;
    m_set_flag(s, CPU_FLAG_V, false);
    m_jsr_dc(s, 0x7E8F94, FIRST_STEP, FIRST_STEP_IGNORES);
    s->cpu.db = m_pull8(s);
    m_set_flag(s, CPU_FLAG_M, true);
    s->cpu.d = SCENE_PAGE;
}

/* The program's transfer of what the unpacker made, a word at a time into
 * VRAM. */
static void send_unpacked_as_words(GameState *g)
{
    set_scene_call_transfer_control(g, scene_program_own(), SCENE_TRANSFER_WORDS);
    set_scene_call_transfer_register(g, scene_program_own(), VID_SNES_VRAM_DATA_LOW);
}

static void load_graphics(Snes *s, uint32_t unpack_site, const StaffRollGraphics *gr)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    vid_set_vram_address(pf, PF_AT(gr->vram_site), gr->vram);
    unpack(s, unpack_site, gr->resource);
    if (gr->bytes)
        set_decompress_length(g, gr->bytes);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
}

// $7E:8EA8
/* The staff roll's setup, from the program's mode table (mode 13). */
static void set_up_staff_roll(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    play_music_fast(s);
    set_scene_credits_fast(g, (uint8_t)(scene_credits_fast(g) + 1));
    set_scene_channel_register(g, STARS_CHANNEL, STARS_SPEED, STARS_COMING);
    scatter_stars(s);
    TIMED_AS(g, 0xFDE275, palette_stop_animations(g));
    copy_colors(g, PICTURE_COLORS, PICTURE_PALETTE);

    set_decompress_dest(g, UNPACK_BUFFER);
    send_unpacked_as_words(g);
    vid_start_vram_uploads(pf, PF_AT(0x7E8EE8), SCENE_VRAM_WHOLE_WORDS);
    for (unsigned i = 0; i < STAFF_ROLL_GRAPHICS; i++)
        load_graphics(s, staff_roll_graphics_unpacked_at[i], &staff_roll_graphics[i]);

    copy_colors(g, STARS_COLORS, STARS_PALETTE);
    scene_set_letter_colors(g);
    scene_use_loaded_palette(g);
    scene_blank_credits_map(pf, PF_AT(0x7E8F77));
    uint16_t descriptors_end = 0;
    TIMED_AS(g, 0x7E36CC, descriptors_end = scene_set_up_line_effects(g, pf, STAFF_ROLL_LINE_EFFECTS));
    set_scene_line_effects(g, STAFF_ROLL_LINE_CHANNELS);

    set_scene_stars_depth_branch(g, EVERY_STAR_BEHIND);
    set_scene_channels_script(g, 0, STAFF_ROLL_SCRIPT);
    first_script_step(s, descriptors_end);
    scroll_credits_on(g, VID_BG3);
    set_up_layers(pf);
    set_scene_credits_text(g, CREDITS_TEXT);
    set_scene_credits_row_vram(g, ROW_BEFORE_THE_MAP);
    set_scene_song(g, STAFF_ROLL_SONG);
    set_scene_song_playing(g, STAFF_ROLL_SONG);
    scene_bridge_start_song(s, 0x7E8FE6, STAFF_ROLL_SONG);
}

/* ---- Entries ------------------------------------------------------------------------------------ */

/* The setup's code and the program's routines it runs inline: the copy of a
 * resource's colors, the unpacker's destination, the letters' colors, the
 * palette's copy, the map's blanking, the stars' drawing's patch, the
 * transfer, the per-line effects and their descriptors, and the song's
 * start. */
bool scene_staff_roll_set_up_present(Snes *s)
{
    return scene_on_scene_page(s) && m_bank_reaches_io(s) &&
           m_code_matches(s, 0x7E8EA8, 0x142, 0x2FB419A4403F1357ull) &&
           m_code_matches(s, 0x7E36AA, 0x22, 0xCCA1DE26086A09CBull) &&
           m_code_matches(s, 0x7E89ED, 0x14, 0x22A581F747CC4E58ull) &&
           m_code_matches(s, 0x7E8CB2, 0x1E, 0xDA761D73F1A6D611ull) &&
           m_code_matches(s, 0x7E38AF, 0x14, 0xD31DAC893D4743DCull) &&
           m_code_matches(s, 0x7E90A7, 0x1A, 0x841D0D5589A89675ull) &&
           m_code_matches(s, 0x7E5640, 0x0D, 0x521AA074E2851935ull) &&
           m_code_matches(s, 0x7E38E7, 0x24, 0xDD4B67DA62CC8E67ull) &&
           m_code_matches(s, 0x7E36CC, 0x3C, 0x45191A42A1EEF6A8ull) &&
           m_code_matches(s, 0x7E9023, 0x06, 0xF09D7328034315D8ull) &&
           m_code_matches(s, 0x7E375F, 0x1B, 0x5BCDF92C47B920DDull) && scene_stars_scatter_code_present(s);
}

/* Entered with 8-bit A; back, the song's start has left the overflow, which
 * the program reads, and the data bank and the direct page are 0. */
void scene_staff_roll_set_up(Snes *s)
{
    set_up_staff_roll(s);
    s->cpu.d = 0x0000;
    s->cpu.db = 0x00;
    s->cpu.p |= CPU_FLAG_M;
    m_rts(s);
}

/* The roll and its scroll run on the scene's page with the data bank on the
 * registers, from the frame's list. A line must fit its row: the guard reads
 * the next line as the roll will, to its end or the credits', within the
 * row's tiles. The end takes the upload off the vblank list, which has it
 * whenever the roll runs. The speed's word is tested where the roll's line is
 * due and its low byte where the scroll is, which agree while its high byte is
 * 0, as the endings leave it. */
enum { BYTES_A_LINE_MAY_TAKE = 3 * ROW_TILES };

static bool next_line_fits(const GameState *g)
{
    uint16_t at = scene_credits_text(g);
    unsigned tile = FIRST_LETTER_TILE;
    for (unsigned read = 0; read < BYTES_A_LINE_MAY_TAKE; read++) {
        uint8_t code = text_byte(g, at++);
        if (code == END_OF_LINE || code == END_OF_CREDITS)
            return true;
        if (code == PASS_OVER)
            at = (uint16_t)(at + PASSED_OVER_BYTES);
        else if (++tile > ROW_TILES)
            return false;
    }
    return false;
}

static bool roll_runs(Snes *s, bool with_line)
{
    if (!scene_on_scene_page(s) || !m_bank_reaches_io(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool ok = scene_credits_fast_high(g) == 0 &&
              (!with_line || (scene_task_listed(g, SCENE_VBLANK_TASKS, CREDITS_UPLOAD_TASK) && next_line_fits(g)));
    vars_peek(false);
    return ok;
}

bool scene_credits_roll_present(Snes *s)
{
    return m_code_matches(s, 0x7E9E7B, 0x5B, 0x0080D1A783699A6Full) &&
           m_code_matches(s, 0x7E9EF1, 0x0B, 0x9AF276F2EA10E555ull) &&
           m_code_matches(s, 0x7E3949, 0x15, 0x7B575BDC799477B3ull) && roll_runs(s, true);
}

bool scene_credits_scroll_present(Snes *s)
{
    return m_code_matches(s, 0x7E9ED6, 0x1B, 0xB492A350B7353CA3ull) && roll_runs(s, false);
}

/* Where the roll goes on: its scroll, a routine of its own, which reads the
 * data bank and Y; and the frame list's exit for a task that takes itself
 * off. */
enum { SCROLL_THE_CREDITS = 0x7E9ED6, REMOVE_THIS_TASK = 0x7E39A7 };
enum { SCROLL_IGNORES = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C };

/* What a line leaves: Y past the row buffer, which the original blanks with a
 * block move, and the data bank the move's, 0. */
enum { PAST_THE_ROW = 0x1434 };

static void leave_line_written(Snes *s)
{
    s->cpu.y = PAST_THE_ROW;
    s->cpu.db = 0x00;
}

/* At the credits' end, A is the upload as the list's search leaves it, with
 * 8-bit A. */
static void leave_credits_over(Snes *s)
{
    s->cpu.a = CREDITS_UPLOAD_TASK;
    s->cpu.p |= CPU_FLAG_M;
}

/* A frame task, entered with 16-bit A: the line, then on to the scroll, or
 * at the credits' end out through the list's exit. Which, the state says: the
 * credits counted over, or the text moved on to the next line. */
void scene_roll_credits(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t text = scene_credits_text(g);
    uint8_t over = scene_credits_over(g);
    write_next_line(g);
    if (scene_credits_over(g) != over) {
        ORIGINAL_EFFECT(leave_line_written(s));
        ORIGINAL_EFFECT(leave_credits_over(s));
        m_stop_at(s, REMOVE_THIS_TASK, DC_X | DC_N | DC_V | DC_Z | DC_C);
        return;
    }
    if (scene_credits_text(g) != text)
        ORIGINAL_EFFECT(leave_line_written(s));
    m_stop_at_own(s, SCROLL_THE_CREDITS, SCROLL_IGNORES);
}

/* What the scroll leaves in A: the fast roll's scroll, or the word the slow
 * roll read for the frame's parity, shifted, as the original reads it. Its high
 * byte is what the liveness analysis has read after a frame task: past the
 * list's last slot, the frame's work returns with it to the NMI, whose song
 * send enters the sound driver, and the driver saves A (the NMI's restore then
 * overwrites it). That reader is why the roll stops at its scroll, a routine of
 * its own, rather than scroll itself. */
static void leave_scroll(Snes *s)
{
    const GameState *g = state_of(s);
    s->cpu.a = roll_fast(g) ? scene_credits_scroll(g) : (uint16_t)(tm_snes_odd_frame_read() << 1);
}

/* The slow roll asks for the frame's parity. */
void scene_scroll_credits_entry(Snes *s)
{
    GameState *g = state_of(s);
    bool odd = !roll_fast(g) && tm_odd_frame(m_platform(s), PF_AT(0x7E9EDF));
    scroll_credits(g, odd);
    ORIGINAL_EFFECT(leave_scroll(s));
    m_rts(s);
}

bool scene_credits_upload_present(Snes *s)
{
    if (!scene_on_scene_page(s) || !m_bank_reaches_io(s) || !m_code_matches(s, 0x7E9EFC, 0x27, 0x0032933E04C32463ull) ||
        !m_code_matches(s, 0x7E9F24, 0x04, 0x5374236A9A72910Bull) || !m_code_matches(s, 0x7E9F29, 0x02, 0x07C24207B48BF144ull))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    uint8_t named = scene_credits_scroll_register(g, 0);
    bool ok = named == scene_credits_scroll_register(g, 1) && (named == BG2_VERTICAL_SCROLL || named == BG3_VERTICAL_SCROLL);
    vars_peek(false);
    return ok;
}

/* A vblank task; nothing it leaves is read. */
void scene_upload_credits(Snes *s)
{
    send_credits_row(state_of(s), m_platform(s));
    m_rts(s);
}

bool scene_clear_sprite_high_bits_present(Snes *s) { return m_code_matches(s, 0x7E8C05, 0x0F, 0xFAF32235E29AEF30ull); }

/* A vblank task, which comes back with 16-bit A; nothing it leaves in the
 * registers is read. */
void scene_clear_sprite_high_bits(Snes *s)
{
    clear_sprite_high_bits(state_of(s));
    s->cpu.p &= (uint8_t)~CPU_FLAG_M;
    m_rts(s);
}

/* The script's routines read the scene's cells through a data bank on low
 * RAM; the test for the credits' end runs on its channel's page. */
bool scene_credits_test_present(Snes *s)
{
    return scene_on_channel_page(s) && m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E996F, 0x0E, 0x34341D9D63782078ull);
}

void scene_go_on_after_the_credits(Snes *s)
{
    SceneChannel ch = 0;
    scene_channel_at(m_direct_page(s), &ch);
    go_on_after_the_credits(state_of(s), ch);
    m_rts(s);
}

bool scene_fade_in_the_picture_present(Snes *s)
{
    return m_bank_reaches_low_ram(s) && m_code_matches(s, 0x7E9FA9, 0x1F, 0x9E4DB4B69E6159DAull);
}

void scene_fade_in_the_picture(Snes *s)
{
    fade_in_the_picture(state_of(s));
    m_rts(s);
}
