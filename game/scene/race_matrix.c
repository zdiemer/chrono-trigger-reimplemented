/* Scenes: the scaled picture gathered into sprite tiles.
 *
 * The bitmap scaler (scene/scaler.c) leaves a picture a row at a time: each
 * row is $20 bytes, eight groups of four bitplane bytes, one group for every
 * eight pixels across. Sprites want it as 8x8 tiles, each the eight rows of
 * its first two bitplanes and then the eight rows of the other two. The jet
 * bike race scales its two racers this way every frame they change size, and
 * the Beyond Time ending its picture, and both regroup the result into the
 * tilemap buffer, which the vblank sends to VRAM as the racers' tiles.
 *
 * The original does the regrouping with a routine it writes for the purpose at
 * setup, straight-line code with no loop in it: 1024 pairs of a load from the
 * picture and a store to the buffer, the direct page moved on a page every 128
 * pairs. The generator ($C3:09E9) writes that routine at $7E:B574 through the
 * work RAM port, and the port below writes the same bytes. The routine itself
 * ($7E:B574) is the regrouping as a loop. The pairs come in eight strips of
 * eight rows, the strips in the order a table of the generator's names them
 * ($C3:09D9), so the tiles land in the order the sprites ask for them.
 *
 * The generator keeps its offsets in the scene's cells (scene_call).
 *
 * While the race runs, the program's main loop drives the scaler for it: each
 * racer's picture the race asks for is scaled and then gathered here, after
 * the vblank has sent the racer's last tiles ("The main loop's scaling",
 * below). */
#include "scene/race.h"

#include "charge.h"
#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "scene/scene_data.h"
#include "vars.h"

/* The instructions it writes. */
enum { OP_LDA_IMMEDIATE = 0xA9, OP_TCD = 0x5B, OP_LDA_ABSOLUTE = 0xAD, OP_STA_DIRECT = 0x85, OP_RTS = 0x60 };

/* The picture's layout: eight strips (each a table word's offset), eight
 * tiles across a strip, two pairs of bitplanes a tile, eight rows a tile; and
 * how far apart in the picture each is. */
enum { STRIPS = 8, TILES_ACROSS = 8, PLANE_PAIRS = 2, TILE_ROWS = 8 };
enum { TILE_STEP = 4, PLANE_PAIR_STEP = 2, ROW_STEP = 0x20 };

/* The routine is written a byte after another through the work RAM port, as
 * the generator writes it, which the port's time is reckoned by. */
static void emit(GameState *g, uint8_t v) { hw_port_write(g, v); }

static void emit_word(GameState *g, uint16_t v)
{
    emit(g, (uint8_t)v);
    emit(g, (uint8_t)(v >> 8));
}

/* The generator's offsets: the table word of the strip being written, and the
 * tile's, the bitplane pair's and the row's steps within it. */
typedef struct {
    uint16_t table;
    uint8_t tile, plane_pair, row;
} Offsets;

/* The offsets as the generator leaves them in the scene's cells, the
 * destination's page moved on past the last; nothing reads them again but the
 * row step (below). */
static void leave_offsets(GameState *g, const Offsets *o, uint16_t destination)
{
    SceneProgramPage scene = scene_program_own();
    set_scene_call_matrix_table_offset(g, scene, o->table);
    set_scene_call_matrix_tile_step(g, scene, o->tile);
    set_scene_call_matrix_plane_pair_step(g, scene, o->plane_pair);
    set_scene_call_matrix_destination(g, scene, destination);
}

// $C3:09E9
/* Write the routine: pairs store from the page in `destination` on, and load
 * from `source` on. The source base stays in the scene's cells, where the
 * program's next multiply finds it after its product. */
static void write_matrix_code(GameState *g, uint16_t destination, uint16_t source)
{
    Offsets o = { 0, 0, 0, 0 };
    uint16_t pair = destination;
    uint8_t page = (uint8_t)(destination >> 8);
    for (uint16_t t = 0; t < STRIPS; t++) {
        o.table = scene_matrix_offset(g, t);
        o.tile = 0;
        for (uint8_t i = 0; i < TILES_ACROSS; i++) {
            o.plane_pair = 0;
            for (uint8_t j = 0; j < PLANE_PAIRS; j++) {
                o.row = 0;
                for (uint8_t k = 0; k < TILE_ROWS; k++) {
                    if ((uint8_t)pair == 0) {
                        /* the direct page moved to the next page */
                        emit(g, OP_LDA_IMMEDIATE);
                        emit_word(g, (uint16_t)(page << 8));
                        emit(g, OP_TCD);
                        page++;
                    }
                    emit(g, OP_LDA_ABSOLUTE);
                    emit_word(g, (uint16_t)(o.table + o.tile + o.plane_pair + o.row + source));
                    emit(g, OP_STA_DIRECT);
                    emit(g, (uint8_t)pair);
                    pair = (uint16_t)(pair + 2);
                    o.row = (uint8_t)(o.row + ROW_STEP);
                }
                o.plane_pair = (uint8_t)(o.plane_pair + PLANE_PAIR_STEP);
            }
            o.tile = (uint8_t)(o.tile + TILE_STEP);
        }
    }
    emit(g, OP_RTS);

    SceneProgramPage scene = scene_program_own();
    set_scene_call_matrix_source(g, scene, source);
    /* The row step's cell is also the third byte of the product cell, which
     * the program's next signed multiply adds into its product
     * (scene/math.c): the step as the loop leaves it is game state. */
    set_scene_call_matrix_row_step(g, scene, o.row);
    ORIGINAL_EFFECT(leave_offsets(g, &o, (uint16_t)((destination & 0x00FF) | page << 8)));
}

/* The scenes' setups have it written into its place in the program
 * (scene_matrix_code); the generator's own entry, wherever its caller pointed
 * the port. */
void scene_write_matrix_code_for(GameState *g, uint16_t destination, uint16_t source)
{
    hw_port_seek(g, (uint16_t)RAM_SCENE_MATRIX_CODE, (uint8_t)(RAM_SCENE_MATRIX_CODE >> 16));
    write_matrix_code(g, destination, source);
}

uint16_t scene_matrix_writer_leaves_y(void) { return STRIPS * 2; }

/* Entered with 16-bit index registers: X the first destination page's
 * address, Y the source base. The routine sets the direct page and the data
 * bank to 0 itself, and the overflow is its last sum's, the tile step's 28 + 4,
 * which is clear. */
void scene_write_matrix_code(Snes *s)
{
    write_matrix_code(state_of(s), s->cpu.x, s->cpu.y);
    s->cpu.d = 0x0000;
    s->cpu.db = 0x00;
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.p |= CPU_FLAG_M;
    m_rtl(s);
}

/* ---- The regrouping --------------------------------------------------------------------------- */

/* Where the scaler leaves the picture, in the bank the routine is entered
 * with: the generator is always given this base. */
enum { PICTURE = 0x0200 };

/* The routine the generator writes, guarded by a hash of all of it: the bytes
 * are the generator's output for its one table and base, so any other code
 * there is not this. It reads the picture through the data bank, which every
 * caller points at the second bank of work RAM. */
bool scene_matrix_code_present(Snes *s)
{
    return s->cpu.db == SCENE_UPPER_WORK_RAM && m_code_matches(s, 0x7EB574, 5153, 0x087A234E99D12FFFull);
}

// $7E:B574
/* Regroup the scaled picture into tiles in the tilemap buffer, a word at a
 * time: strip by strip, the tiles of a strip from left to right, in each tile
 * the rows of its first two bitplanes and then of its other two. */
void scene_gather_picture_into_tiles(GameState *g)
{
    uint16_t word = 0;
    for (uint16_t strip = 0; strip < STRIPS; strip++) {
        uint16_t strip_start = (uint16_t)(PICTURE + scene_matrix_offset(g, strip));
        for (uint8_t tile = 0; tile < TILES_ACROSS; tile++) {
            for (uint8_t planes = 0; planes < PLANE_PAIRS; planes++) {
                for (uint8_t row = 0; row < TILE_ROWS; row++) {
                    uint16_t from = (uint16_t)(strip_start + tile * TILE_STEP + planes * PLANE_PAIR_STEP + row * ROW_STEP);
                    uint16_t pixels = scene_race_picture(g, (uint16_t)(from - PICTURE) / 2);
                    set_scene_tilemap_buffer_byte(g, word * 2u, (uint8_t)pixels);
                    set_scene_tilemap_buffer_byte(g, word * 2u + 1, (uint8_t)(pixels >> 8));
                    word++;
                }
            }
        }
    }
}

/* Entered with 16-bit A. The routine moves the direct page as it goes, which
 * its callers put back; nothing it leaves in the registers is read. */
void scene_gather_picture(Snes *s)
{
    scene_gather_picture_into_tiles(state_of(s));
    m_rts(s);
}

/* ---- The main loop's scaling ------------------------------------------------------------------- */

/* The program's main loop, while the race runs, scales the pictures the race
 * asks for (race_drawing.c), one racer after the other, Crono first: it hands a
 * waiting request to the bitmap scaler (scene/scaler.c), whose rows take a
 * frame or more, waits for the racer's last tiles to be sent, and gathers the
 * new picture into the tilemap buffer, for the race to send at its next
 * frame. Then the request is marked gathered. */

static bool picture_waiting(const GameState *g, Racer r) { return race_picture_state(g, r) == PICTURE_WAITING; }

// $7E:51F4
/* A racer's request to the scaler: its settings (the request's state comes
 * along into the cells between them, which nothing reads). */
static void hand_to_scaler(GameState *g, Racer r)
{
    scene_scaler_start(g, (SceneScaling){ .source = scene_race_picture_requests_source(g, r),
                                          .rows = scene_race_picture_requests_rows(g, r),
                                          .pairs = scene_race_picture_requests_pairs(g, r),
                                          .row_step = scene_race_picture_requests_row_step(g, r),
                                          .limit = scene_race_picture_requests_source_limit(g, r) });
    set_scene_scaler_request_state(g, scene_race_picture_requests_state(g, r));
}

/* The next racer from `from` whose picture waits, or RACERS for none. */
enum { RACERS = 2 };

static unsigned next_waiting(const GameState *g, unsigned from)
{
    unsigned r = from;
    while (r < RACERS && !picture_waiting(g, (Racer)r))
        r++;
    return r;
}

// $7E:5215
/* After the racer's last tiles have gone: the new ones are to be sent, and the
 * picture is gathered into them. */
static void gather_scaled_picture(GameState *g)
{
    set_scene_race_tilemap_pending(g, (uint16_t)(scene_race_tilemap_pending(g) + 1));
    TIMED_AS(g, 0x7EB574, scene_gather_picture_into_tiles(g));
}

/* ---- Entries: the main loop's scaling ----------------------------------------------------------- */

/* The scaling's code, with the scaler's start, and the regrouping. */
bool scene_race_scale_pictures_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E51D1, 0x40, 0xCF48DD1914245AD5ull);
}

/* Back from the gathering, which hands the request's new state back in A. */
bool scene_race_scale_after_present(Snes *s) { return scene_race_scale_pictures_present(s) && s->cpu.a == PICTURE_GATHERED; }

bool scene_race_gather_scaled_present(Snes *s)
{
    return scene_on_scene_page(s) && m_code_matches(s, 0x7E5215, 0x0C, 0x4CE857A6AFB42F4Dull) && scene_matrix_code_present(s);
}

/* The first racer from `from` whose picture waits goes to the scaler, and the
 * machine carries on in the scaler's rows, where the original does (the
 * bridge); with none, back to the main loop, the carry left by the last
 * request's compare: whether Johnny's picture was ever asked for. */
static void scale_pictures_from(Snes *s, unsigned from)
{
    GameState *g = state_of(s);
    unsigned r = next_waiting(g, from);
    if (r < RACERS) {
        hand_to_scaler(g, (Racer)r);
        scene_bridge_scale_picture(s, (Racer)r, scene_scaler_row_step(g) & 0x8000);
        return;
    }
    m_set_flag(s, CPU_FLAG_C, race_picture_state(g, RACER_JOHNNY) != PICTURE_NONE);
    m_rts(s);
}

// $7E:51D1
/* The main loop's call each pass, while the race runs. */
void scene_race_scale_pictures(Snes *s) { scale_pictures_from(s, RACER_CRONO); }

/* Back from Crono's picture, gathered: Johnny's next. */
void scene_race_scale_after_crono(Snes *s)
{
    set_scene_race_picture_requests_state(state_of(s), RACER_CRONO, PICTURE_GATHERED);
    scale_pictures_from(s, RACER_JOHNNY);
}

/* Back from Johnny's: the main loop goes on. The carry is the scaling's. */
void scene_race_scale_after_johnny(Snes *s)
{
    set_scene_race_picture_requests_state(state_of(s), RACER_JOHNNY, PICTURE_GATHERED);
    m_rts(s);
}

/* Entered with 16-bit A once the upload is done; back to whichever racer's
 * scaling, the request's new state in A, the data bank the one the scaling left
 * on the stack. The carry, which the main loop's callers may read, is the
 * scaler's, untouched. */
void scene_race_gather_scaled(Snes *s)
{
    gather_scaled_picture(state_of(s));
    s->cpu.db = m_pull8(s);
    s->cpu.a = PICTURE_GATHERED;
    m_rts(s);
}
