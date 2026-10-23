/* Field mode: what its vblank work sends to the display, and its line
 * interrupt.
 *
 * Every frame the field's vblank handler (field/nmi.c) uploads the sprite table
 * and the queued transfers of tile graphics, and the deferred uploads of the
 * layers' edge runs, which bring the rows and columns scrolling into view onto
 * the layers' tile maps (field/layers.c works out where they are). When a map
 * loads it sets the video mode up, copies its screens' settings, fills memory
 * and makes the transfers a screen transition describes.
 *
 * The line interrupt comes at line 211: the picture ends there, the rest of
 * the frame is blanked, and the time to vblank goes to one of two jobs. */
#include "field/display.h"
#include "field/dialog_window.h"
#include "field/sprites.h"
#include "field/switches.h"

#include "charge.h"
#include "core/tile_animation.h"
#include "field/bridge.h"
#include "field/field.h"
#include "field/layers.h"
#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "ram.h"
#include "vars.h"

/* Where the sprite table and the layers' tile maps are, as the transfers'
 * sources. */
enum { TILE_MAP_BANK = 0x7E, QUEUE_BANK = 0x7F };

static const VidVramPort word_port = { .step = 1, .after_high = true };
static const VidVramPort column_port = { .step = 32, .after_high = true };

// $C0:ECA3
/* The sprite table, all of it, from the first sprite. */
void field_upload_sprite_table(Platform *pf)
{
    vid_send_oam(pf, PF_AT(0xC0ECA5), PF_GAME(FIELD_SPRITE_TABLE_ADDRESS));
}

// $C0:B1B2
/* The queued transfers of new sprite tiles from bank $7F: each queue entry is
 * two, a VRAM address, a source and a size each (a size of 0 is 64 KiB, as
 * the hardware reads it), up to the first unused entry; then the queue is
 * emptied. */
void field_upload_queued_transfers(GameState *g, Platform *pf)
{
    vid_start_vram_uploads(pf, PF_AT(0xC0B1BC), VID_VRAM_WORD_PORT);
    for (unsigned i = 0; field_vram_queue_used(g, i) != 0; i++) {
        uint16_t size = field_vram_queue_size(g, i);
        vid_send_vram(pf, PF_AT(0xC0B1D7), field_vram_queue_vram(g, i),
                      PF_GAME((uint32_t)QUEUE_BANK << 16 | field_vram_queue_source(g, i)), size ? size : 0x10000u, NULL);
        size = field_vram_queue_size2(g, i);
        vid_send_vram(pf, PF_AT(0xC0B1EB), field_vram_queue_vram2(g, i),
                      PF_GAME((uint32_t)QUEUE_BANK << 16 | field_vram_queue_source2(g, i)), size ? size : 0x10000u, NULL);
    }
    field_forget_the_queued_transfers(g);
}

/* The queue is emptied by marking its first entry unused. */
void field_forget_the_queued_transfers(GameState *g) { set_field_vram_queue_used(g, 0, 0); }

/* ---- The layers' edge runs ------------------------------------------------------------ */

/* The deferred uploads the vblank handler runs from each layer's upload flags
 * (field/layers.h has the runs): as the screen scrolls down, a layer's row
 * below the view; up, its top row; right, the column right of the view;
 * left, its left column. Each sends the run's two or three parts, staged in
 * bank $7E, to VRAM: a part of size 0 isn't sent, and a run's parts follow one
 * another in memory from its staging buffer. The rows step VRAM by a word,
 * the columns by 32 words. */
typedef struct {
    uint8_t layer;
    uint8_t parts;
    uint8_t sizes, addresses;   /* the run's first size and first address, as field/layers.h names them */
    bool down_a_column;
    uint32_t site[3];           /* each part's transfer, for its origin tag */
} EdgeRun;

static uint16_t edge_word(const GameState *g, uint8_t (*byte)(const GameState *, unsigned, unsigned), unsigned layer,
                          unsigned word)
{
    uint8_t low = byte(g, layer, 2 * word);
    return (uint16_t)(byte(g, layer, 2 * word + 1) << 8 | low);
}

/* The runs, in the order of the layers' upload flags: each layer's row
 * below the view, its top row, the column right of the view, and its left
 * column. A layer's two rows share its row buffer, and its two columns its
 * column buffer. */
static const EdgeRun edge_runs[FIELD_EDGE_UPLOADS] = {
    /* $C0:8445 */ { .layer = FIELD_BG1, .parts = 3, .sizes = EDGE_ROW_SIZES, .addresses = EDGE_BOTTOM_ROW,
                   .down_a_column = false, .site = { 0xC0844D, 0xC08468, 0xC0848A } },
    /* $C0:84C1 */ { .layer = FIELD_BG2, .parts = 3, .sizes = EDGE_ROW_SIZES, .addresses = EDGE_BOTTOM_ROW,
                   .down_a_column = false, .site = { 0xC084C9, 0xC084E4, 0xC08506 } },
    /* $C0:8523 */ { .layer = FIELD_BG3, .parts = 3, .sizes = EDGE_ROW_SIZES, .addresses = EDGE_BOTTOM_ROW,
                   .down_a_column = false, .site = { 0xC0852B, 0xC08546, 0xC08568 } },
    /* $C0:8585 */ { .layer = FIELD_BG1, .parts = 3, .sizes = EDGE_ROW_SIZES, .addresses = EDGE_TOP_ROW,
                   .down_a_column = false, .site = { 0xC0858D, 0xC085A8, 0xC085CA } },
    /* $C0:85E7 */ { .layer = FIELD_BG2, .parts = 3, .sizes = EDGE_ROW_SIZES, .addresses = EDGE_TOP_ROW,
                   .down_a_column = false, .site = { 0xC085EF, 0xC0860A, 0xC0862C } },
    /* $C0:8649 */ { .layer = FIELD_BG3, .parts = 3, .sizes = EDGE_ROW_SIZES, .addresses = EDGE_TOP_ROW,
                   .down_a_column = false, .site = { 0xC08651, 0xC0866C, 0xC0868E } },
    /* $C0:86AB */ { .layer = FIELD_BG1, .parts = 2, .sizes = EDGE_COLUMN_SIZES, .addresses = EDGE_RIGHT_COLUMN,
                   .down_a_column = true, .site = { 0xC086AF, 0xC086C4 } },
    /* $C0:86F7 */ { .layer = FIELD_BG2, .parts = 2, .sizes = EDGE_COLUMN_SIZES, .addresses = EDGE_RIGHT_COLUMN,
                   .down_a_column = true, .site = { 0xC086FB, 0xC08710 } },
    /* $C0:8729 */ { .layer = FIELD_BG3, .parts = 2, .sizes = EDGE_COLUMN_SIZES, .addresses = EDGE_RIGHT_COLUMN,
                   .down_a_column = true, .site = { 0xC0872D, 0xC08742 } },
    /* $C0:875B */ { .layer = FIELD_BG1, .parts = 2, .sizes = EDGE_COLUMN_SIZES, .addresses = EDGE_LEFT_COLUMN,
                   .down_a_column = true, .site = { 0xC0875F, 0xC08774 } },
    /* $C0:878D */ { .layer = FIELD_BG2, .parts = 2, .sizes = EDGE_COLUMN_SIZES, .addresses = EDGE_LEFT_COLUMN,
                   .down_a_column = true, .site = { 0xC08791, 0xC087A6 } },
    /* $C0:87BF */ { .layer = FIELD_BG3, .parts = 2, .sizes = EDGE_COLUMN_SIZES, .addresses = EDGE_LEFT_COLUMN,
                   .down_a_column = true, .site = { 0xC087C3, 0xC087D8 } },
};

void field_upload_layer_edge(GameState *g, Platform *pf, unsigned upload)
{
    const EdgeRun *r = &edge_runs[upload];
    const VidVramPort *port = r->down_a_column ? &column_port : &word_port;
    uint16_t source = field_staged_line(r->layer, r->down_a_column);
    for (unsigned k = 0; k < r->parts; k++) {
        uint16_t size = edge_word(g, field_layer_edge_sizes_byte, r->layer, r->sizes + k);
        if (size != 0) {
            uint16_t vram = edge_word(g, field_layer_edge_vram_byte, r->layer, r->addresses + k);
            vid_send_vram(pf, PF_AT(r->site[k]), vram, PF_GAME((uint32_t)TILE_MAP_BANK << 16 | source), size, port);
        }
        source = (uint16_t)(source + size);
    }
}

/* The uploads' guard: the parts are the size of an edge run, whose sources
 * can't reach round the bank to the stack page (where the original's
 * transfer would copy the return address its call left there, which the
 * port doesn't reproduce). The game's parts are a row or a column of tiles. */
enum { LARGEST_PART = 0x3000 };

bool field_layer_edges_fit(const GameState *g)
{
    for (unsigned layer = 0; layer < FIELD_LAYERS; layer++)
        for (unsigned w = 0; w < EDGE_SIZES; w++)
            if (edge_word(g, field_layer_edge_sizes_byte, layer, w) >= LARGEST_PART)
                return false;
    return true;
}

bool field_edge_runs_small(Snes *s)
{
    if (m_direct_page(s) != FIELD_LAYERS_PAGE || !m_bank_reaches_io(s))
        return false;
    vars_peek(true);
    bool ok = field_layer_edges_fit(state_of(s));
    vars_peek(false);
    return ok;
}

#define EDGE_RUN_ENTRY(name, upload)                                                                                   \
    void name(Snes *s)                                                                                                 \
    {                                                                                                                  \
        field_upload_layer_edge(state_of(s), m_platform(s), (upload));                                                   \
        m_rts(s);                                                                                                      \
    }

EDGE_RUN_ENTRY(field_upload_layer1_bottom_row, 0)
EDGE_RUN_ENTRY(field_upload_layer2_bottom_row, 1)
EDGE_RUN_ENTRY(field_upload_layer3_bottom_row, 2)
EDGE_RUN_ENTRY(field_upload_layer1_top_row, 3)
EDGE_RUN_ENTRY(field_upload_layer2_top_row, 4)
EDGE_RUN_ENTRY(field_upload_layer3_top_row, 5)
EDGE_RUN_ENTRY(field_upload_layer1_right_column, 6)
EDGE_RUN_ENTRY(field_upload_layer2_right_column, 7)
EDGE_RUN_ENTRY(field_upload_layer3_right_column, 8)
EDGE_RUN_ENTRY(field_upload_layer1_left_column, 9)
EDGE_RUN_ENTRY(field_upload_layer2_left_column, 10)
EDGE_RUN_ENTRY(field_upload_layer3_left_column, 11)

/* ---- Setting a map's display up -------------------------------------------------------- */

// $FD:C0D7
/* The field's video mode: mode 1 with BG3 in front, the three layers' tile
 * maps 64 tiles wide at $6800, $7000 and $7800 with their tiles at $1000 and
 * $5000, no sprite or interlace settings, every scroll at 0, BG1, BG2 and the
 * sprites on the main screen, and color math adding the sub screen to a black
 * fixed color. */
void field_set_the_video_mode(Platform *pf)
{
    vid_set_mode(pf, PF_AT(0xFDC0E2), &(VidMode){ .mode = 1, .bg3_priority = true });
    vid_set_bg_chars(pf, PF_AT(0xFDC0E6), VID_BG1, 0x1000, 0x1000);
    vid_set_bg_chars(pf, PF_AT(0xFDC0EA), VID_BG3, 0x5000, 0x0000);
    static const struct { uint16_t map; uint32_t site; } maps[3] = {
        { 0x6800, 0xFDC0EE }, { 0x7000, 0xFDC0F2 }, { 0x7800, 0xFDC0F6 },
    };
    for (int bg = 0; bg < 3; bg++)
        vid_set_bg_map(pf, PF_AT(maps[bg].site), (VidLayer)bg, &(VidBg){ .map_base = maps[bg].map, .map_w = 64, .map_h = 32 });
    vid_set_obj_config(pf, PF_AT(0xFDC0FA), &(VidObjConfig){ 0 });
    vid_set_video_config(pf, PF_AT(0xFDC0FC), &(VidVideoConfig){ 0 });
    static const uint32_t scroll_sites[3] = { 0xFDC0FE, 0xFDC106, 0xFDC10E };
    for (int bg = 0; bg < 3; bg++)
        vid_set_scroll(pf, PF_AT(scroll_sites[bg]), (VidLayer)bg, 0, 0, VID_SCROLL_H | VID_SCROLL_V);
    vid_set_screens(pf, PF_AT(0xFDC118), VID_BG1_BIT | VID_BG2_BIT | VID_OBJ_BIT, 0, VID_SCREEN_MAIN);
    vid_set_color_math(pf, PF_AT(0xFDC11C), &(VidColorMath){ .sub_screen = true }, VID_MATH_CONTROL);
    vid_set_fixed_color(pf, PF_AT(0xFDC120), VID_RED | VID_GREEN | VID_BLUE, 0);
}

// $C0:A509
/* The map's screens: the main and sub screens' layers and the color math
 * layers as the map set them, and no mosaic. */
void field_show_the_maps_screens(const GameState *g, Platform *pf)
{
    vid_set_screens(pf, PF_AT(0xC0A50C), field_main_screen(g), 0, VID_SCREEN_MAIN);
    vid_set_screens(pf, PF_AT(0xC0A512), 0, field_sub_screen(g), VID_SCREEN_SUB);
    VidColorMath math = { 0 };
    vid_color_math_decode(&math, VID_MATH_LAYERS, field_color_math_layers(g));
    vid_set_color_math(pf, PF_AT(0xC0A518), &math, VID_MATH_LAYERS);
    vid_set_mosaic(pf, PF_AT(0xC0A51D), 0x00, 1);
}

// $C0:2DC8
/* The transfer a screen transition describes (field_transfer): its VRAM
 * address, mode, source and size (0 is 64 KiB). */
static void vram_transfer(const GameState *g, Platform *pf)
{
    uint16_t size = field_transfer_size(g);
    vid_send_vram_in_mode(pf, PF_AT(0xC02DCA), field_transfer_vram(g), PF_GAME(field_transfer_source(g)),
                          size ? size : 0x10000u, &word_port, field_transfer_mode(g));
}

// $C0:2DF1
/* Clear the memory a screen transition describes (field_transfer's source
 * and size): the Mode 7 matrix set to 0 makes the PPU's product 0, and a
 * transfer from that register copies it over the memory. The transfer reads
 * a register rather than writing one, so it goes through the SNES-only
 * escape; only the two matrix values it leaves behind are video state. */
static void fill_memory(Platform *pf, uint32_t from, uint16_t bytes)
{
    vid_set_mode7(pf, PF_AT(0xC02DF3), &(VidMode7){ .a = 0, .b = 0 }, VID_M7_MATRIX_AB);
    const VidSnesTransfer t = { .dest = VID_SNES_MULTIPLIER_PRODUCT, .source = from, .size = bytes };
    vid_snes_copy(pf, PF_AT(0xC02E01), &t);
}

/* The field describes a clear as it does a transfer, and the clear can reach
 * the description itself: it is written first. */
void field_clear_memory(GameState *g, Platform *pf, uint32_t from, uint16_t bytes)
{
    set_field_transfer_source(g, from);
    set_field_transfer_size(g, bytes);
    fill_memory(pf, from, bytes);
}

void field_describe_transfer(GameState *g, FieldTransfer t)
{
    set_field_transfer_vram(g, t.vram);
    set_field_transfer_mode(g, t.mode);
    set_field_transfer_source(g, t.source);
    set_field_transfer_size(g, t.size);
}

void field_send_transfer(GameState *g, Platform *pf, FieldTransfer t)
{
    field_describe_transfer(g, t);
    vram_transfer(g, pf);
}

/* ---- A layer's tile map, staged ------------------------------------------------------------- */

/* A layer drawn into the staging buffers (field/streaming.h) is sent to its
 * place in VRAM: a screen's tile map, or both of a layer's two side by side
 * where it is drawn whole. The NMI sends BG2's halves to their own places. */
enum { STAGED_SCREEN_BYTES = 0x0800, STAGED_BOTH_SCREENS_BYTES = 0x1000 };

static FieldTransfer staged_layer(uint16_t vram, bool both_screens)
{
    return (FieldTransfer){ .vram = vram, .mode = FIELD_TRANSFER_WORDS, .source = RAM_FIELD_LAYER_STAGING,
                            .size = both_screens ? STAGED_BOTH_SCREENS_BYTES : STAGED_SCREEN_BYTES };
}

// $C0:7F62 (a screen), $C0:7F77 (both screens)
/* To the place the field's transfer names already. */
void field_send_staged_layer(GameState *g, Platform *pf, bool both_screens)
{
    field_send_transfer(g, pf, staged_layer(field_transfer_vram(g), both_screens));
}

// $C0:7F58 (BG2's right screen), $C0:7F5D (its left)
enum { BG2_LEFT_SCREEN_VRAM = 0x7000, BG2_RIGHT_SCREEN_VRAM = 0x7400 };

void field_send_staged_bg2_screen(GameState *g, Platform *pf, bool right)
{
    field_send_transfer(g, pf, staged_layer(right ? BG2_RIGHT_SCREEN_VRAM : BG2_LEFT_SCREEN_VRAM, false));
}

// $C0:EC77
/* A piece of BG3's tile map a script has staged in BG3's staging buffer
 * (field_bg3_piece_vram, field_bg3_piece_bytes), sent there. */
void field_send_bg3_piece(const GameState *g, Platform *pf)
{
    uint32_t staged = (uint32_t)TILE_MAP_BANK << 16 | field_staged_line(FIELD_BG3, false);
    vid_send_vram(pf, PF_AT(0xC0EC7A), field_bg3_piece_vram(g), PF_GAME(staged), field_bg3_piece_bytes(g),
                  VID_VRAM_WORD_PORT);
}

/* ---- The line interrupt ---------------------------------------------------------------- */

/* A line interrupt the field takes: it isn't turned off, and the line one is
 * what came. Acknowledging it is part of asking. */

static bool line_interrupt_for_field(const GameState *g, Platform *pf)
{
    if (field_irq_disabled(g) & FIELD_LINE_INTERRUPT_OFF)
        return false;
    return tm_ack_line_irq(pf, PF_AT(0xC0ECDB)) & TM_LINE_IRQ_PENDING;
}

// $C0:ECF2
/* The picture ends: the rest of the frame blanked, and the line effects off. */
static void end_picture(Platform *pf)
{
    vid_raster_display(pf, PF_AT(0xC0ECF4), VID_AT_LINE_HANDLER, &(VidDisplay){ .blank = true });
    vid_enable_line_effects(pf, PF_AT(0xC0ECF9), 0x00);
}

/* ---- Where it meets the original ------------------------------------------------------- */

bool field_display_bank_is_io(Snes *s) { return m_io_bank(s->cpu.db); }

/* Nothing these leave in the registers is read after (the entries' don't-care
 * sets). */
void field_upload_sprites(Snes *s)
{
    field_upload_sprite_table(m_platform(s));
    m_rts(s);
}

uint16_t field_queue_last_second_size(const GameState *g, uint16_t none)
{
    unsigned entries = 0;
    while (field_vram_queue_used(g, entries) != 0)
        entries++;
    return entries != 0 ? field_vram_queue_size2(g, entries - 1) : none;
}

/* Y is left the last entry's second size, which the callers read. */
void field_upload_vram_queue(Snes *s)
{
    GameState *g = state_of(s);
    s->cpu.y = field_queue_last_second_size(g, s->cpu.y);
    field_upload_queued_transfers(g, m_platform(s));
    m_rts(s);
}

void field_init_video_mode(Snes *s)
{
    field_set_the_video_mode(m_platform(s));
    m_rtl(s);
}

void field_set_screens(Snes *s)
{
    field_show_the_maps_screens(state_of(s), m_platform(s));
    m_rts(s);
}

void field_vram_transfer(Snes *s)
{
    vram_transfer(state_of(s), m_platform(s));
    m_rts(s);
}

/* X is left the size, which the callers read (as it was before the clear,
 * which can clear it). */
void field_fill_memory(Snes *s)
{
    const GameState *g = state_of(s);
    uint16_t size = field_transfer_size(g);
    fill_memory(m_platform(s), field_transfer_source(g), size);
    s->cpu.x = size;
    m_rts(s);
}

/* ---- After the picture ---------------------------------------------------------------------- */

// $C0:ED15
/* The line interrupt's job in the time after the picture, on the frames it
 * isn't bank $FD's: the dialog's choice pointer, where it shows, and the looks
 * of a switch pressed or a chest opened (field/switches.c). */
static void redraw_after_the_picture(GameState *g, Platform *pf)
{
    if (field_choice_pointer_shown(g))
        TIMED_AS(g, FIELD_REFRESH_CHOICE_POINTER_ENTRY, field_refresh_choice_pointer(g, pf));
    field_redraw_changed_tiles(g, pf);
}

void field_redraw_after_picture(Snes *s)
{
    redraw_after_the_picture(state_of(s), m_platform(s));
    m_rts(s);
}

/* ---- The line interrupt ----------------------------------------------------------------------- */

/* The animated tiles' uploads, through the original's jump to them at
 * $FD:FFFD; the handler puts every register back after them. */
enum { TILE_ANIMATION_UPLOADS = 0xFDFFFD, THE_HANDLER_PUTS_EVERYTHING_BACK = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

/* For the original's code at $FD:FFFD, should it not come back as the field
 * runs and keep the machine as it is: A and the flags as the choice of job
 * leaves them, the table set's bit. */
static void leave_the_choice_in_a(Snes *s, uint8_t second_set)
{
    s->cpu.a = (uint16_t)((s->cpu.a & 0xFF00) | second_set);
    m_set_nz8(s, second_set);
}

/* The line handler's start, where the line interrupt is the field's (false
 * where it isn't): only the vblank and controller interrupts left on for the
 * rest of the frame, the line let be drawn, and the picture ended. */
static bool begin_line_handler(const GameState *g, Platform *pf)
{
    if (!line_interrupt_for_field(g, pf))
        return false;
    tm_enable_interrupts(pf, PF_AT(0xC0ECE3), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    tm_wait_line_drawn(pf, PF_LOOP(0xC0ECEB, 0xC0ECF2));
    end_picture(pf);
    return true;
}

// $C0:ECCC
/* The handler as C, for a program with no machine under it. */
void field_line(GameState *g, Platform *pf)
{
    if (!begin_line_handler(g, pf))
        return;
    if (!(field_display_flags(g) & FIELD_SECOND_TABLE_SET))
        TIMED_AS(g, 0xC0ED15, redraw_after_the_picture(g, pf));
    else
        TIMED_AS(g, TILE_ANIMATIONS_SEND_ENTRY, tile_animations_send(g, pf));
}

// $C0:ECCC (installed at the $0504 line-interrupt stub by field_install_irq)
/* The field's line interrupt, at the line its picture ends after. Where it is
 * the field's, it ends the picture (begin_line_handler); then the time to
 * vblank goes to one of two jobs, which alternate with the line effects'
 * table set: the dialog's choice pointer and the tiles that changed
 * ($C0:ED15), or bank $FD's animated tiles ($FD:FFFD, the original's code);
 * and back to what it interrupted. The data bank is the registers', as the
 * original's has it at its wait and for bank $FD's work (where the interrupt
 * isn't the field's, the return puts the interrupted code's back). */
void field_irq(Snes *s)
{
    GameState *g = state_of(s);
    Platform *pf = m_platform(s);
    m_save_interrupted_registers(s);
    s->cpu.db = 0x00;
    if (begin_line_handler(g, pf)) {
        uint8_t second_set = field_display_flags(g) & FIELD_SECOND_TABLE_SET;
        if (!second_set) {
            NATIVE_TIME_AS(s, 0xC0ED15, redraw_after_the_picture(g, pf));
        } else {
            leave_the_choice_in_a(s, second_set);
            if (!field_run_long(s, 0xC0ED08, TILE_ANIMATION_UPLOADS, THE_HANDLER_PUTS_EVERYTHING_BACK,
                                tile_animation_upload, bridge_machine_now(s)))
                return;
        }
    }
    m_return_to_interrupted(s);
}

/* Its guard: the data bank $00, on whatever page the interrupt has (it
 * reaches its cells as the field's page does). */
bool field_redraw_can_run(Snes *s) { return m_bank_is_zero(s); }

/* The staged layer's sends and BG3's piece, on the field's page with the
 * data bank reaching the registers; what they leave in A, X and the flags
 * nobody reads, and they leave Y as it was. */
void field_send_staged_screen(Snes *s)
{
    field_send_staged_layer(state_of(s), m_platform(s), false);
    m_rts(s);
}

void field_send_staged_screens(Snes *s)
{
    field_send_staged_layer(state_of(s), m_platform(s), true);
    m_rts(s);
}

void field_send_staged_bg2_right(Snes *s)
{
    field_send_staged_bg2_screen(state_of(s), m_platform(s), true);
    m_rts(s);
}

void field_send_staged_bg2_left(Snes *s)
{
    field_send_staged_bg2_screen(state_of(s), m_platform(s), false);
    m_rts(s);
}

void field_send_bg3_piece_entry(Snes *s)
{
    field_send_bg3_piece(state_of(s), m_platform(s));
    m_rts(s);
}
