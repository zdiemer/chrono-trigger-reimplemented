/* Scenes: the attract sequence's pages of credits -- the game's staff, a page
 * at a time.
 *
 * Between the attract sequence's other scenes the scene program shows a page
 * of the people who made the game (its mode 1, set up at $7E:6B04): their
 * names in the credits' letters on BG1, and above them what they did in a
 * small font of sprites -- PRODUCER, DIRECTER, CHARACTER DESIGN and the rest.
 * There are five pages, shown in turn, one each time the mode runs. The page
 * fades in, and on the program's 90th frame it starts to fade out, which ends
 * the scene.
 *
 * The pieces:
 *
 * - the letters: the credits' letters' colors, for the names' layer and for
 *   the sprites; the first part of the letters' resource sent into VRAM as it
 *   is, and the rest widened from two bitplanes to four, the two added blank;
 * - the page: the names' map cleared and every sprite put out of sight, then
 *   the page's names written into the map and its jobs set as sprites;
 * - the page's frame task, which keeps the sprites as the setup left them and
 *   watches for the frame the page goes.
 *
 * The pages' text is in the program, two lists for each page with a table of
 * where each page's starts: the names, each a VRAM address and its letters,
 * and the jobs, each a place on the screen and its letters. A 0 ends a name or
 * a job, and a $FF where the next would start ends the list. A name's tiles
 * are its letters' distance from '@', a space being the blank tile 0; a job's
 * sprites are its letters' distance from '!', set 5 pixels apart, a space
 * leaving a gap. */
#include "scene/scene.h"

#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"
#include "wram.h"

/* ---- The letters ------------------------------------------------------------------------------- */

/* The credits' letters' resource (in the program's list), unpacked into the
 * second bank of work RAM: its first $200 bytes go into VRAM from its start
 * as they are, and its next $36 tiles of two bitplanes, from $100 bytes in,
 * are widened to four from word $100. */
enum { LETTERS = 0xAE, UNPACK_BUFFER = 0x7F0200 };
enum { LETTERS_AS_THEY_ARE_BYTES = 0x0200, LETTERS_AS_THEY_ARE_VRAM = 0x0000 };
enum { NARROW_LETTERS = 0x7F0300, NARROW_LETTER_TILES = 0x36, WIDE_LETTERS_VRAM = 0x0100 };

/* A tile of two bitplanes: eight rows, a word each; widened, eight words of
 * blank bitplanes follow its own. */
enum { TILE_ROWS = 8 };

/* The letters' colors: white, light grey and dark grey on the clear color,
 * the first four of the background's palette and of the sprites'. */
enum { SPRITE_COLORS = 128 };
static const uint16_t letter_colors[] = { 0x0000, 0x77BD, 0x4631, 0x1CE7 };

/* The program's unpacker ($7E:3691): the resource's place in its list in X,
 * the unpacking into the destination set. The letters' loader is a routine of
 * the setup's (called from $7E:6B0A), whose return is under the unpacker's
 * call, as shadow mode pairs the call with the original's. */
enum { UNPACK = 0x7E3691, UNPACK_IGNORES = DC_A | DC_Y | DC_N | DC_Z | DC_C, LOADER_CALL = 0x7E6B0A };

static void unpack_the_letters(Snes *s)
{
    scene_bridge_enter(s, LOADER_CALL);
    s->cpu.x = LETTERS;
    m_jsr_dc(s, 0x7E6C3F, UNPACK, UNPACK_IGNORES);
    scene_bridge_leave(s);
}

// $7E:6BF9
/* The letters' colors, and VRAM made ready at `vram`, a word at a time, for
 * the letters' first part, which is unpacked next ... */
static void start_loading_the_letters(GameState *g, Platform *pf, uint16_t vram)
{
    vid_set_vram_address(pf, PF_AT(0x7E6B07), vram);
    for (unsigned i = 0; i < sizeof letter_colors / sizeof letter_colors[0]; i++) {
        set_palette_working(g, i, letter_colors[i]);
        set_palette_working(g, SPRITE_COLORS + i, letter_colors[i]);
    }
    set_scene_call_transfer_control(g, scene_program_own(), SCENE_TRANSFER_WORDS);
    set_scene_call_transfer_register(g, scene_program_own(), VID_SNES_VRAM_DATA_LOW);
    vid_start_vram_uploads(pf, PF_AT(0x7E6C2E), SCENE_VRAM_WHOLE_WORDS);
    set_decompress_dest(g, UNPACK_BUFFER);
}

/* ... and once it is unpacked, that part sent. */
static void send_the_letters_as_they_are(GameState *g, Platform *pf)
{
    set_decompress_length(g, LETTERS_AS_THEY_ARE_BYTES);
    TIMED_AS(g, 0x7E38E7, scene_send_unpacked(g, pf));
}

// $7E:66B0
/* `tiles` tiles of two bitplanes from `from` written into VRAM at `vram` as
 * tiles of four, the third and fourth bitplanes blank. The original streams
 * them through the work RAM port, which it leaves past the last. */
static void widen_letters(GameState *g, Platform *pf, uint16_t vram, uint32_t from, unsigned tiles)
{
    uint8_t bank = (uint8_t)(from >> 16);
    uint16_t at = (uint16_t)from;
    vid_set_vram_address(pf, PF_AT(0x7E6B10), vram);
    for (unsigned t = 0; t < tiles; t++) {
        for (unsigned row = 0; row < TILE_ROWS; row++, at = (uint16_t)(at + 2))
            vid_write_vram(pf, PF_AT(0x7E66BD), wram_read16(g, (uint32_t)bank << 16 | at));
        for (unsigned row = 0; row < TILE_ROWS; row++)
            vid_write_vram(pf, PF_AT(0x7E66C8), 0x0000);
    }
    ORIGINAL_EFFECT(hw_port_seek_address(g, at));
}

/* ---- The page ---------------------------------------------------------------------------------- */

/* The names' layer: BG1, its 32x32 map at $0400 (cleared from a zero byte of
 * the cartridge, the start of the race's plane), and the sprites' tiles from
 * VRAM's start, as the letters are. */
enum { NAMES_MAP_VRAM = 0x0400, NAMES_MAP_BYTES = 0x0800, ZERO_IN_THE_CARTRIDGE = 0xD20000 };
static const VidBg names_bg = { VID_TILEMAP(NAMES_MAP_VRAM, 32, 32) };

/* Every sprite out of sight: every byte of the entries the line below the
 * picture, and every high bit clear. */
enum { NO_HIGH_BITS = 0x00, SPRITE_ENTRIES_BYTES = 0x0200 };

static void hide_the_sprites(Platform *pf)
{
    vid_send_oam_fill(pf, PF_AT(0x7E6B44), VID_OAM_HIGH_BITS_WORD, NO_HIGH_BITS, SCENE_SPRITE_HIGH_TABLE_BYTES);
    vid_send_oam_fill(pf, PF_AT(0x7E6B66), 0x0000, VID_OAM_HIDDEN_Y, SPRITE_ENTRIES_BYTES);
}

/* The pages, shown in turn from the first: the one before is kept below the
 * program's work RAM, a byte as it reads it. */
enum { PAGES = 5, FIRST_PAGE = 1 };

static uint16_t next_page(GameState *g)
{
    uint16_t page = (uint16_t)((scene_credits_page(g) & 0x00FF) + 1);
    if (page == PAGES + 1)
        page = FIRST_PAGE;
    set_scene_credits_page(g, page);
    return page;
}

/* Where each page's names and jobs start, in the program: two tables of
 * words, by the page from the first. */
enum { PAGE_NAMES = 0x7E6CAC, PAGE_JOBS = 0x7E6CB6 };

static uint16_t page_list(const GameState *g, uint32_t table, uint16_t page)
{
    return wram_read16(g, table + (uint16_t)((page - FIRST_PAGE) * 2));
}

/* The lists' markers, and the letters' tiles. */
enum { END_OF_ENTRY = 0x00, END_OF_LIST = 0xFF, SPACE = ' ', BLANK_TILE = 0, LETTER_STEP = 5 };

static uint8_t text_byte(const GameState *g, uint16_t at)
{
    return wram_read8(g, (uint32_t)SCENE_LOWER_WORK_RAM << 16 | at);
}

static uint8_t name_tile(uint8_t letter) { return letter == SPACE ? BLANK_TILE : (uint8_t)(letter - '@'); }
static uint8_t job_tile(uint8_t letter) { return (uint8_t)(letter - '!'); }

// $7E:6C4D
/* The page's names into the names' map, each where its VRAM address says. */
static void write_the_names(GameState *g, Platform *pf, uint16_t at)
{
    for (uint8_t low; (low = text_byte(g, at++)) != END_OF_LIST;) {
        uint8_t high = text_byte(g, at++);
        vid_set_vram_address(pf, PF_AT(0x7E6C57), (uint16_t)(high << 8 | low));
        for (uint8_t letter; (letter = text_byte(g, at++)) != END_OF_ENTRY;)
            vid_write_vram(pf, PF_AT(0x7E6C6A), name_tile(letter));
    }
}

// $7E:6C71
/* The page's jobs as the sprites from the first, each from its place a
 * letter at a time, a space leaving a gap. The original lays a job out in
 * the sprites' offset cells, which it leaves at the last job's place and the
 * place after its last letter. */
static void write_the_jobs(GameState *g, Platform *pf, uint16_t at)
{
    uint8_t sprite = 0, x = 0, y = 0;
    bool placed = false;
    for (uint8_t first; (first = text_byte(g, at++)) != END_OF_LIST;) {
        x = first;
        y = text_byte(g, at++);
        placed = true;
        for (uint8_t letter; (letter = text_byte(g, at++)) != END_OF_ENTRY; x = (uint8_t)(x + LETTER_STEP))
            if (letter != SPACE)
                vid_set_sprite(pf, PF_AT(0x7E6C92), sprite++, &(VidSprite){ .x = x, .y = y, .tile = job_tile(letter) });
    }
    if (placed) {
        set_scene_sprite_offset_x(g, (uint16_t)((scene_sprite_offset_x(g) & 0xFF00) | x));
        set_scene_sprite_offset_y(g, y);
    }
}

/* The fades' pace: a step every frame. */
enum { FADE_EVERY_FRAME = 1 };

static void leave_the_text_read(GameState *g, uint16_t page);

// $7E:6B04
/* The page's setup, from the program's mode table (mode 1), up to the
 * letters' unpacking: the letters' first part on its way to VRAM's start. */
static void start_the_page(GameState *g, Platform *pf) { start_loading_the_letters(g, pf, LETTERS_AS_THEY_ARE_VRAM); }

/* After it: the letters sent, the rest widened, a clear screen, the next page
 * written, BG1 and the sprites shown, and the page's fade in and its task.
 * The original hands the widening its source in the unpacker's destination,
 * which the program's loaders all set before they use it, and leaves it
 * there; and it points the sprite table's port at its start before the jobs,
 * as the first job's sprite does (scaffolding: the port's address is
 * compared). */
static void finish_the_page(GameState *g, Platform *pf)
{
    send_the_letters_as_they_are(g, pf);
    set_decompress_dest(g, NARROW_LETTERS);
    widen_letters(g, pf, WIDE_LETTERS_VRAM, NARROW_LETTERS, NARROW_LETTER_TILES);

    vid_send_vram_fill(pf, PF_AT(0x7E6B22), NAMES_MAP_VRAM, PF_GAME(ZERO_IN_THE_CARTRIDGE), NAMES_MAP_BYTES, NULL);
    hide_the_sprites(pf);
    uint16_t page = next_page(g);
    write_the_names(g, pf, page_list(g, PAGE_NAMES, page));
    ORIGINAL_EFFECT(vid_set_oam_address(pf, PF_AT(0x7E6BBB), 0x0000, false));
    write_the_jobs(g, pf, page_list(g, PAGE_JOBS, page));
    ORIGINAL_EFFECT(leave_the_text_read(g, page));

    vid_set_screens(pf, PF_AT(0x7E6BC5), VID_BG1_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN);
    vid_set_bg_map(pf, PF_AT(0x7E6BCA), VID_BG1, &names_bg);
    vid_set_obj_config(pf, PF_AT(0x7E6BCF), &(VidObjConfig){ 0 });
    set_scene_fade_period(g, FADE_EVERY_FRAME);
    set_scene_fade_countdown(g, FADE_EVERY_FRAME);
    scene_add_task(g, SCENE_FRAME_TASKS, SCENE_TASK_BRIGHTEN);
    scene_add_task(g, SCENE_FRAME_TASKS, SCENE_TASK_SHOW_THE_CREDITS_PAGE);
}

/* ---- The page's task --------------------------------------------------------------------------- */

/* The program's frame the page starts to go on. */
enum { THE_PAGE_GOES = 90 };

// $7E:6BE5
/* Every frame: the sprite table's cursor kept at its start, so the frame
 * draws none of its own over the jobs; and on the page's last frame, the
 * screen darkened and the scene ended in its place. */
static TaskEnd show_the_page_task(GameState *g, Platform *pf)
{
    (void)pf;
    set_scene_sprite_index(g, 0);
    set_scene_sprite_high_pointer(g, SCENE_SPRITE_HIGH_TABLE);
    if (scene_frame_counter(g) != THE_PAGE_GOES)
        return task_goes_on();
    return (TaskEnd){ TASK_HANDS_OVER, SCENE_TASK_DARKEN_AND_END };
}

/* It leaves the frame counter in A, read whole. */
static TaskLeftovers show_the_page_leaves(const GameState *g, TaskEnd end)
{
    (void)end;
    return (TaskLeftovers){ .a_set = true, .a = scene_frame_counter(g) };
}

enum { ROW_SHOW_THE_PAGE };

static bool show_the_page_present(Snes *s);

const SceneTaskPort scene_credit_pages_tasks[] = {
    [ROW_SHOW_THE_PAGE] = { SCENE_TASK_SHOW_THE_CREDITS_PAGE, SCENE_FRAME_TASKS, show_the_page_task,
      show_the_page_present, show_the_page_leaves, false },
};
const unsigned scene_credit_pages_task_count = sizeof scene_credit_pages_tasks / sizeof scene_credit_pages_tasks[0];

/* ---- Entries ----------------------------------------------------------------------------------- */

/* The setup's code with its tables and its task's, the widening, and the
 * program's routines it runs inline: the add of a task and the transfer.
 * Its stores reach the registers and low RAM through the data bank. */
bool scene_credit_pages_set_up_present(Snes *s)
{
    return m_bank_reaches_io(s) && m_code_matches(s, 0x7E6B04, 0x1BC, 0x9D419350E0B42143ull) &&
           m_code_matches(s, 0x7E66B0, 0x2D, 0x668182BFFD2B1A5Bull) &&
           m_code_matches(s, 0x7E395E, 0x16, 0xBC6CC1EBF7C4F36Aull) &&
           m_code_matches(s, 0x7E38E7, 0x24, 0xDD4B67DA62CC8E67ull);
}

/* ---- What the setup leaves ------------------------------------------------------------------- */

/* A list of the page's text read through: where it ends, past its $FF, and
 * its last letter if it has one, with the place across a job's would be set. */
typedef struct {
    uint16_t end;
    bool has_letters;
    uint8_t last_letter, last_x;
} ListRead;

static ListRead read_through(const GameState *g, uint16_t at)
{
    ListRead r = { 0 };
    for (uint8_t first; (first = text_byte(g, at++)) != END_OF_LIST;) {
        uint8_t x = first;
        at++;
        for (uint8_t letter; (letter = text_byte(g, at++)) != END_OF_ENTRY; x = (uint8_t)(x + LETTER_STEP)) {
            r.has_letters = true;
            r.last_letter = letter;
            r.last_x = x;
        }
    }
    r.end = at;
    return r;
}

/* The original reads the page's text through the work RAM port, which it
 * leaves past the jobs' list. */
static void leave_the_text_read(GameState *g, uint16_t page)
{
    hw_port_seek(g, read_through(g, page_list(g, PAGE_JOBS, page)).end, SCENE_LOWER_WORK_RAM);
}

static bool subtraction_overflows(uint8_t a, uint8_t b) { return ((a ^ b) & (a ^ (uint8_t)(a - b)) & 0x80) != 0; }
static bool addition_overflows(uint8_t a, uint8_t b) { return (~(a ^ b) & (a ^ (uint8_t)(a + b)) & 0x80) != 0; }

/* The overflow flag the original's last step of a letter leaves, which the
 * frame's work reads: the step of the last job's place on to the next
 * letter's, or where the jobs have no letters, the subtraction that made the
 * last name's last tile. */
static bool last_letter_overflowed(const GameState *g, uint16_t page)
{
    ListRead jobs = read_through(g, page_list(g, PAGE_JOBS, page));
    if (jobs.has_letters)
        return addition_overflows(jobs.last_x, LETTER_STEP);
    ListRead names = read_through(g, page_list(g, PAGE_NAMES, page));
    return names.has_letters && subtraction_overflows(names.last_letter, '@');
}

/* Entered with 8-bit A. Back, the original has left the direct page on the
 * registers' (its writers point it there and don't put it back), the data
 * bank 0, Y the widening's count run out, and the overflow, which the frame's
 * work reads, as the page's last letter left it. */
bool scene_credit_pages_set_up_scene(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    start_the_page(g, pf);
    unpack_the_letters(s);
    finish_the_page(g, pf);
    s->cpu.d = M_DP_PPU;
    s->cpu.db = 0x00;
    s->cpu.p |= CPU_FLAG_M;
    m_set_flag(s, CPU_FLAG_V, last_letter_overflowed(g, scene_credits_page(g)));
    s->cpu.y = 0x0000;
    return true;
}

void scene_credit_pages_set_up(Snes *s)
{
    scene_credit_pages_set_up_scene(s);
    m_rts(s);
}

/* The task's code; the cells are the scene's page's. */
static bool show_the_page_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E6BE5, 0x14, 0xF8DD5396B960AE74ull);
}

void scene_show_the_credits_page(Snes *s) { scene_task_alone(s, &scene_credit_pages_tasks[ROW_SHOW_THE_PAGE]); }
bool scene_show_the_credits_page_present(Snes *s) { return show_the_page_present(s); }
