/* Menu mode: the Epoch's two Mode 7 screens, and the End of Time's gate.
 *
 * Both Epoch screens run in the menus' sprite-object mode (the one started at
 * $C2:0000, game/menu/objects.c): they blank the screen, build a Mode 7 plane
 * in VRAM, put one object in a slot to drive it, and then loop on the frame
 * task until the mode byte at $027C changes.
 *
 * - **The map view** ($C2:631F), opened while flying the Epoch. It packs the
 *   world map's tiles into one colour each, builds the Mode 7 plane from them
 *   and turns the plane under a cursor that walks a fixed path; markers show
 *   the party, the Epoch and the map it would land on. Its one per-line effect
 *   is a blue gradient down the screen.
 * - **The time dial** ($C2:6A34), the Epoch's time-travel screen. The gate is a
 *   triangle spun by a Mode 7 matrix that changes every line, cut out of the
 *   screen by window 1 whose edges change every line too; seven digit objects
 *   show the periods and the background drifts behind them.
 *
 * The two share their helpers -- the signed multiply and divide their matrices
 * need, and the object-script routines that place and move markers -- so they
 * are one file. Everything they put on the screen goes through the video
 * service; what they write through the work RAM port and compute with the
 * multiplier and divider is memory and arithmetic, which ports reach directly.
 */
#include "menu/epoch.h"

#include "platform/timing.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

/* The work RAM data port and its address register: memory access, not video
 * (docs/PLATFORM.md), so ports reach it directly. */
enum { WRAM_PORT = 0x2180, WRAM_ADDR = 0x2181, WRAM_ADDR_BANK = 0x2183 };

/* The multiplier and divider, which ports may use as the original does. */
enum { MUL_A = 0x4202, MUL_B = 0x4203, DIV_LOW = 0x4204, DIV_HIGH = 0x4205,
       DIVISOR = 0x4206, QUOTIENT = 0x4214, PRODUCT = 0x4216 };

/* The mode uploads over DMA channel 7. Its per-line slots: 1 for the gradient
 * or the window, and 2-5 for the four Mode 7 matrix values. */
enum { MENU_DMA = 7 };
enum { DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01 };
enum { EFFECT_SLOT = 1, MATRIX_A_SLOT = 2, MATRIX_B_SLOT = 3, MATRIX_C_SLOT = 4,
       MATRIX_D_SLOT = 5 };
enum { EFFECT_SLOT_BIT = 1u << EFFECT_SLOT, MATRIX_SLOT_BITS = 0x3C };

/* The VRAM port settings these screens upload with: stepping after the high
 * byte (a whole word, or the high bytes alone), and after the low byte for an
 * upload that writes the low bytes alone. */
static const VidVramPort high_port = { .step = 1, .after_high = true };
static const VidVramPort low_port = { .step = 1, .after_high = false };

/* ---- the screens' own work RAM ---------------------------------------------------------- */

enum {
    LOAD_REQUEST = (uint16_t)RAM_ABS(RAM_MENU_LOAD_REQUEST),
    OBJECTS_FROZEN = (uint16_t)RAM_ABS(RAM_MENU_OBJECTS_FROZEN),
    MAP_GRAPHICS = (uint16_t)RAM_ABS(RAM_MENU_MAP_GRAPHICS),
    MODE7_PLANE = (uint16_t)RAM_ABS(RAM_MENU_MODE7_PLANE),
    GATE_TILES = (uint16_t)RAM_ABS(RAM_MENU_GATE_TILES),
    GATE_PALETTE = (uint16_t)RAM_ABS(RAM_MENU_LOADED_PALETTE),
    GATE_PALETTE_COPY = (uint16_t)RAM_ABS(RAM_MENU_GATE_PALETTE_COPY),
    /* The one colour the screen sets to white before the palette goes out. */
    GATE_PALETTE_WHITE = (uint16_t)RAM_ABS(RAM_MENU_LOADED_PALETTE) + 2 * 0xD1,
    /* $C2:7321: which source row each of the gate's tilemap rows takes. */
    GATE_TILE_ROWS = 0x7321,
    /* The sprite object mode's palette, which its vblank handler uploads to
     * CGRAM ($C2:01AD), and colour $80, where the map view patches in the
     * sixteen colours of the map it packed. */
    OBJECT_PALETTE = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_PALETTE),
    OBJECT_PALETTE_PATCH = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_PALETTE) + 2 * 0x80,
};

/* The per-line tables the time dial reads, in the mode's buffer space. Each is
 * two entries of 112 lines, the second LINE_HALF bytes on, and the matrix ones
 * are double-buffered so a frame rebuilds one while the other runs. */
enum {
    WINDOW_TABLE_A = (uint16_t)RAM_ABS(RAM_MENU_OBJECT_WAVE_SCROLL),
    WINDOW_TABLE_B = 0x8DF6,
    MATRIX_COS_A = 0x9176, MATRIX_SIN_A = 0x9796,
    MATRIX_COS_B = 0x9416, MATRIX_SIN_B = 0x9A36,
    LINE_HALF = 0xE0,        /* where a table's second entry starts */
    MATRIX_LINES = 0xE0,     /* values a matrix table holds */
    MATRIX_LAST = 2 * (MATRIX_LINES - 1),   /* the one a frame appends */
};

/* Where in the mode's direct-page scratch ($00-$1B) each screen keeps its
 * working values; the mode's named cells start at $1C (game/ram.h). */
enum {
    CELL_LOW = RAM_MENU_OBJECT_SCRATCH_DP,      /* the pair being packed, the screen centre, a loop's count */
    CELL_PAIR = 0x02,     /* a corner, a width, the second edge's position */
    CELL_HEIGHT = 0x04,   /* an edge's height, and a half product */
    CELL_OUT = 0x05,      /* where the plane's next block goes */
    CELL_STEP = 0x06,     /* the corners left to place, and the multiply's 8-bit operand at $07 */
    CELL_A = 0x08,        /* the first operand: a scaled cosine, a VRAM address, a line */
    CELL_B = 0x0A,        /* the second: a scaled sine, a source address, a line */
    CELL_C = 0x0C,        /* the third: a zoom, a VRAM address, a table offset */
    CELL_D = 0x0E,        /* the fourth: a zoom, a map record, the multiply's 16-bit operand */
    CELL_PTR1 = 0x10,     /* a long pointer, and the matrix's two sign flags */
    CELL_PTR2 = 0x13,     /* a second long pointer */
    CELL_PTR3 = 0x16,     /* a third, and the per-line table being rebuilt */
    CELL_PRODUCT = 0x19,  /* the signed multiply's 24-bit product, and a count */
    /* Where the map has its top-left tile, from the second of its planes;
     * $C2:2720 sets the first two planes to the same tile. */
    MAP_ORIGIN_X = RAM_MENU_MAP_COLUMN_DP + RAM_MENU_MAP_COLUMN_STRIDE,
    MAP_ORIGIN_Y = RAM_MENU_MAP_ROW_DP + RAM_MENU_MAP_ROW_STRIDE,
};

/* Shadow display bytes, reached on the menus' page. */
enum {
    SHADOW_BG_MODE = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_BG_MODE),
    SHADOW_SCROLL_X = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SCROLL_X),
    SHADOW_SCROLL_Y = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SCROLL_Y),
    SHADOW_MATRIX = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_MODE7_MATRIX),
    SHADOW_CENTER = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_MODE7_CENTER),
    SHADOW_WINDOWS = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_WINDOWS),
    SHADOW_MAIN_WINDOW_MASK = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_MAIN_WINDOW_MASK),
    SHADOW_SUB_WINDOW_MASK = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SUB_WINDOW_MASK),
    SHADOW_MATH_CONTROL = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_MATH_CONTROL),
    SHADOW_MATH_LAYERS = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_MATH_LAYERS),
    SHADOW_FIXED_BLUE = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_FIXED_BLUE),
    SHADOW_FIXED_RED = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_FIXED_RED),
    SHADOW_FIXED_GREEN = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_FIXED_GREEN),
    /* BG3's scroll, which both screens drift and the wave drives per line. */
    SHADOW_BG3_X = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SCROLL_X) + 4,
    SHADOW_BG3_Y = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_SHADOWS_SCROLL_Y) + 4,
    DISPLAY = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_DISPLAY),
    LINE_EFFECTS = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_LINE_EFFECTS),
    FRAME_STATE = (uint8_t)RAM_ABS(RAM_MENU_OBJECT_FRAME_STATE),
};

/* ---- direct page and object slots ------------------------------------------------------ */

static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }
static void set_dp8(Snes *s, uint8_t cell, uint8_t v) { m_write8(s, m_dp(s, cell), v); }
static uint16_t dp16(Snes *s, uint8_t cell) { return m_read16(s, m_dp(s, cell)); }
static void set_dp16(Snes *s, uint8_t cell, uint16_t v) { m_write16(s, m_dp(s, cell), v); }

/* An object slot's fields, at the slot address the mode keeps in $4E. The
 * original reaches them as `field,X` through the data bank, so the sum wraps in
 * 16 bits. */
enum {
    OBJ_STEP = 0x02,      /* which of the handler's steps runs next */
    OBJ_SCRIPT = 0x0B,    /* long pointer to the sprite script */
    OBJ_SCRIPT_STATE = 0x0E,
    OBJ_SPRITE = 0x0F,
    OBJ_FRACTION = 0x10,
    OBJ_X = 0x14,         /* the dial's object keeps the angle it turns to here */
    OBJ_Y = 0x18,         /* and the angle it stands at here */
    OBJ_VELOCITY = 0x1A,  /* 32-bit horizontal, then 32-bit vertical at $1E */
    OBJ_TURN = 0x20,      /* how far the dial turns each frame, signed */
    OBJ_PARAMETER = 0x22, /* the angle, the period, or the step the handler counts */
    OBJ_ZOOM = 0x24,      /* the map view's two zooms, and the path step it has reached at $28 */
    OBJ_PATH_AT = 0x28,
};

static uint16_t object(Snes *s) { return m_read16(s, m_dp(s, RAM_MENU_OBJECT_CURRENT_DP)); }
static uint32_t field_at(Snes *s, uint16_t slot, uint8_t field)
{
    return m_abs(s, (uint16_t)(field + slot));
}
static uint8_t field8(Snes *s, uint16_t slot, uint8_t f) { return m_read8(s, field_at(s, slot, f)); }
static void set_field8(Snes *s, uint16_t slot, uint8_t f, uint8_t v) { m_write8(s, field_at(s, slot, f), v); }
static uint16_t field16(Snes *s, uint16_t slot, uint8_t f) { return m_read16(s, field_at(s, slot, f)); }
static void set_field16(Snes *s, uint16_t slot, uint8_t f, uint16_t v) { m_write16(s, field_at(s, slot, f), v); }

/* An object handler's dispatch: its step doubled, which the jump table needs
 * and which the handler's paths keep in X until they load the slot again. */
static uint8_t dispatch_step(Snes *s)
{
    uint8_t step = m_read8(s, field_at(s, s->cpu.x, OBJ_STEP));
    m_tdc(s);
    m_lda8(s, step);
    m_set_a8(s, m_asl8(s, step));
    m_tax(s);
    return step;
}

/* Guards. Both screens run on the menus' page with the data bank on the I/O
 * banks, which is what lets their slot and shadow accesses be named. */
bool menu_epoch_page_is_menu(Snes *s) { return s->cpu.d == RAM_PAGE_MENU && s->cpu.db == 0x00; }
bool menu_epoch_bank_is_menu(Snes *s) { return s->cpu.db == 0x00; }

/* ---- the screens' data in the ROM ------------------------------------------------------ */

/* $C2:69F0, by location less $1F0: the graphics set the map view loads. The
 * original reads a word at the byte index, so the set is its low byte. */
static uint16_t map_graphics_set(Snes *s, uint16_t index) { return m_read16(s, 0xC269F0 + index); }

/* $C6:F560 and $C6:F5A0: where the cursor stands at each step of its path. */
static uint16_t cursor_path_x(Snes *s, uint16_t step) { return m_read16(s, 0xC6F560 + step); }
static uint16_t cursor_path_y(Snes *s, uint16_t step) { return m_read16(s, 0xC6F5A0 + step); }

/* $C2:68E4, by location less $1F0: where the map view puts the landing marker. */
static uint16_t landing_marker_x(Snes *s, uint16_t index) { return m_read16(s, 0xC268E4 + index); }

/* $C6:F700: two of the gate plane's pixels per byte, a colour in each nibble. */
static uint8_t gate_plane_nibbles(Snes *s, uint16_t i) { return m_read8(s, 0xC6F700 + i); }

/* $C6:F640: four words of velocity for the marker kind a script names. */
static uint16_t marker_velocity(Snes *s, uint16_t index, uint16_t word)
{
    return m_read16(s, 0xC6F640 + index + 2 * word);
}

/* $C2:6C97 and $C2:6CA5: the locations the time dial travels to, seven each,
 * the second set once the story has passed $1BA6 = $CC. */
static uint16_t period_table(Snes *s)
{
    return m_read8(s, RAM_MENU_STORY_PROGRESS) < 0xCC ? 0x6C97 : 0x6CA5;
}
static uint16_t period_location(Snes *s, uint16_t table, uint16_t index)
{
    return m_read16(s, 0xC20000 | (uint16_t)(table + index));
}

/* $C2:6CB3: the angle the gate stands at for each period. */
static uint16_t period_angle(Snes *s, uint16_t index) { return m_read16(s, 0xC26CB3 + index); }

/* $C2:6F2D: the gate triangle's three corners, a signed pair each. */
static uint8_t gate_corner(Snes *s, uint16_t i) { return m_read8(s, 0xC26F2D + i); }

/* $C2:71DD, $C2:71EB and $C2:71EC: a digit object's place on the dial, and the
 * scripts that show and hide it. */
static uint8_t digit_x(Snes *s, uint16_t index) { return m_read8(s, 0xC271DD + index); }
static uint8_t digit_y(Snes *s, uint16_t index) { return m_read8(s, 0xC271DE + index); }
static uint8_t digit_show_script(Snes *s, uint16_t index) { return m_read8(s, 0xC271EB + index); }
static uint8_t digit_hide_script(Snes *s, uint16_t index) { return m_read8(s, 0xC271EC + index); }

/* $C2:7220: the compressed streams the gate screen loads, a source word and a
 * destination word each. */
static uint16_t gate_stream_source(Snes *s, uint16_t index) { return m_read16(s, 0xC27220 + index); }
static uint16_t gate_stream_dest(Snes *s, uint16_t index) { return m_read16(s, 0xC27222 + index); }

/* $C2:72B8: the sixteen VRAM rows the gate's tiles go to. */
static uint16_t gate_tile_row(Snes *s, uint16_t index) { return m_read16(s, 0xC272B8 + index); }

/* $C0:F900: a half sine wave, 10 bits of phase, the upper half negated. */
static uint16_t sine(Snes *s, uint16_t phase)
{
    uint16_t index = (uint16_t)(phase & 0x03FF);
    uint16_t v = (uint16_t)(m_read16(s, 0xC0F900 + index) & 0x00FF);
    return index >= 0x0200 ? (uint16_t)(0 - v) : v;
}
static uint16_t cosine(Snes *s, uint16_t phase) { return sine(s, (uint16_t)(phase + 0x0100)); }

/* $C0:FE00 through the counter at $1B30: the menus' random bytes. */
static uint8_t next_random(Snes *s)
{
    uint8_t at = m_read8(s, RAM_MENU_RANDOM_INDEX);
    uint8_t v = m_read8(s, 0xC0FE00 + at);
    m_write8(s, RAM_MENU_RANDOM_INDEX, (uint8_t)(at + 1));
    return v;
}

/* ---- arithmetic ------------------------------------------------------------------------- */

/* A 16-bit addition with the carry in, and the flags ADC leaves. */
typedef struct { bool carry, overflow; } Sum;

static uint16_t add16(Sum *f, uint16_t a, uint16_t b)
{
    uint32_t sum = (uint32_t)a + b + (f->carry ? 1 : 0);
    f->carry = sum > 0xFFFF;
    f->overflow = (~(a ^ b) & (a ^ sum) & 0x8000) != 0;
    return (uint16_t)sum;
}

static uint16_t sub16(Sum *f, uint16_t a, uint16_t b) { return add16(f, a, (uint16_t)~b); }

/* ASL repeated, leaving the carry the last shift put out (the original adds
 * with it). */
static uint16_t asl16(Sum *f, uint16_t v, int times)
{
    for (int i = 0; i < times; i++) {
        f->carry = (v & 0x8000) != 0;
        v = (uint16_t)(v << 1);
    }
    return v;
}

/* LSR repeated, likewise: the original adds with the last bit shifted out. */
static uint16_t lsr16(Sum *f, uint16_t v, int times)
{
    for (int i = 0; i < times; i++) {
        f->carry = (v & 1) != 0;
        v = (uint16_t)(v >> 1);
    }
    return v;
}

static uint8_t add8(Sum *f, uint8_t a, uint8_t b)
{
    uint16_t sum = (uint16_t)(a + b + (f->carry ? 1 : 0));
    f->carry = sum > 0xFF;
    return (uint8_t)sum;
}

static uint8_t sub8(Sum *f, uint8_t a, uint8_t b) { return add8(f, a, (uint8_t)~b); }

/* The divider as the original drives it: a 16-bit dividend over an 8-bit
 * divisor, the quotient read back. The original spaces its divides with NOPs
 * for the divider's latency; nothing here has to. */
static uint16_t divide(Snes *s, uint16_t dividend, uint8_t divisor)
{
    m_write16(s, m_abs(s, DIV_LOW), dividend);
    m_write8(s, m_abs(s, DIVISOR), divisor);
    return m_read16(s, m_abs(s, QUOTIENT));
}

/* ---- uploads ---------------------------------------------------------------------------- */

/* STX $2116 : STA $2115 : STX $4370 : STX $4372 : STA $4374 : STX $4375 :
 * STA $420B -- the order most of these uploads program the channel in. */
static const uint8_t upload_steps[] = {
    VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_VRAM_PORT, VID_STEP_CONTROL,
    VID_STEP_DEST,      VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
    VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
};
static const VidSnesXfer word_upload = { MENU_DMA, DMA_WORDS, upload_steps, &high_port };
static const VidSnesXfer pixel_upload = { MENU_DMA, DMA_ONE_REGISTER, upload_steps, &high_port };
static const VidSnesXfer tile_upload = { MENU_DMA, DMA_ONE_REGISTER, upload_steps, &low_port };

/* STY $43x0 : STY $43x2 : STA $43x4 : STA $43x7 -- how both screens program a
 * per-line slot: no size, since HDMA counts the table's own lines. */
static const uint8_t line_effect_steps[] = {
    VID_STEP_CONTROL,     VID_STEP_DEST,          VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
    VID_STEP_SOURCE_BANK, VID_STEP_INDIRECT_BANK, VID_STEP_END,
};

/* ---- the map view ($C2:631F) ------------------------------------------------------------ */

// $C2:6400, with $C2:6453
/* Pack the world map's tiles at $7F:9000 into one colour each at $7E:AB25,
 * through the work RAM port: bit 4 of four of every tile's 32 bytes says which
 * of its quarters is land. */
static void pack_map_rows(Snes *s)
{
    static const uint16_t quarters[4] = { 0x15, 0x14, 0x05, 0x04 };
    m_write16(s, m_abs(s, WRAM_ADDR), RAM_ABS(RAM_MENU_MAP_COLOURS));
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), (uint8_t)(RAM_MENU_MAP_COLOURS >> 16));
    uint8_t outer_bank = s->cpu.db;
    /* From here on the data bank is on the map's own, so the port is reached
     * long, as the original reaches it. */
    s->cpu.db = 0x7F;
    for (uint16_t tile = 0; tile < 0x0380; tile++) {
        uint16_t row = (uint16_t)(MODE7_PLANE + (tile << 5));
        set_dp16(s, CELL_PTR1, row);
        uint8_t packed = 0;
        set_dp8(s, CELL_LOW, 0);
        for (int i = 0; i < 4; i++) {
            uint8_t byte = m_read8(s, (uint32_t)m_abs(s, row) + quarters[i]);
            packed = (uint8_t)(packed << 1 | ((byte >> 4) & 1));
            set_dp8(s, CELL_LOW, packed);
        }
        m_write8(s, WRAM_PORT, packed);
        set_dp16(s, CELL_A, (uint16_t)(tile + 1));
    }
    m_lda16(s, 0x0380);
    m_cmp16(s, 0x0380, 0x0380);
    m_sep(s, CPU_FLAG_M);
    s->cpu.db = outer_bank;
}

// $C2:658E
/* Whether a map colour shows at all on this location's map: the Ocean Palace's
 * map ($1F3) keeps only $40-$4E, and every other map drops $0F, $2F and
 * $24-$2B. A dropped tile leaves the direct page's low word, which is 0 here. */
static uint16_t map_colour_shown(Snes *s, uint16_t colour)
{
    uint8_t v = (uint8_t)colour;
    bool drop = m_read16(s, RAM_LOCATION_ID) == 0x01F3
                    ? (v >= 0x40 && v < 0x4F)
                    : (v == 0x0F || v == 0x2F || (v >= 0x24 && v < 0x2C));
    return drop ? s->cpu.d : colour;
}

// $C2:653E
/* One 8x8 block of the map view's plane: the two tiles the map record at $0E
 * names -- its own and the one four rows down -- each looked up in the packed
 * colours and shaded by the record's bits 2-4. */
static void map_tile_pair(Snes *s)
{
    uint32_t record = m_abs(s, dp16(s, CELL_D));
    uint8_t top_shade = (uint8_t)((m_read8(s, record + 3) & 0x1C) << 2);
    set_dp8(s, CELL_LOW, top_shade);
    uint8_t bottom_shade = (uint8_t)((m_read8(s, record + 7) & 0x1C) << 2);
    set_dp8(s, (uint8_t)(CELL_LOW + 1), bottom_shade);

    uint16_t top = (uint16_t)(m_read16(s, record + 2) & 0x03FF);
    uint8_t colour = m_read8(s, RAM_MENU_MAP_COLOURS + top);
    if (colour != 0) {
        uint16_t shown = map_colour_shown(s, (uint16_t)((s->cpu.a & 0xFF00) | (colour | top_shade)));
        m_write8(s, m_abs(s, dp16(s, CELL_OUT)), (uint8_t)shown);
    }
    uint16_t bottom = (uint16_t)(m_read16(s, record + 6) & 0x03FF);
    colour = m_read8(s, RAM_MENU_MAP_COLOURS + bottom);
    if (colour != 0) {
        uint16_t shown = map_colour_shown(s, (uint16_t)((s->cpu.a & 0xFF00) | (colour | bottom_shade)));
        m_write8(s, (uint32_t)m_abs(s, dp16(s, CELL_OUT)) + 8, (uint8_t)shown);
    }
}

/* The plane buffer at the long pointer $E0, cleared through the block move that
 * propagates its first bytes, as the original clears it. */
static void clear_plane_buffer(Snes *s, uint16_t count, bool word)
{
    Cpu *c = &s->cpu;
    uint16_t buffer = dp16(s, RAM_MENU_OBJECT_UPLOAD_BUFFER_DP);
    set_dp16(s, CELL_OUT, buffer);
    if (word)
        m_write16(s, m_abs(s, buffer), 0);
    else
        m_write8(s, m_abs(s, buffer), 0);
    m_ldx(s, buffer);
    m_ldy(s, (uint16_t)(buffer + 1));
    c->a = count;
    m_mvn(s, 0x7E, 0x7E);
}

// $C2:645C
/* Build the map view's Mode 7 plane: $60 blocks across and $40 rows down, from
 * the two map layers at ($10) and ($13), into the buffer at $E0 and out to the
 * plane's pixels after every four rows. A's high byte on entry is the high byte
 * of the first clear's count, which the caller leaves there. */
static void build_map_plane(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t outer_bank = c->db;
    c->db = 0x7E;
    clear_plane_buffer(s, (uint16_t)((c->a & 0xFF00) | 0xFE), false);

    set_dp16(s, CELL_C, 0x0040);
    for (uint16_t row = 0; row < 0x40; row++) {
        set_dp16(s, CELL_A, row);
        for (uint16_t block = 0; block < 0x60; block++) {
            set_dp16(s, CELL_B, block);
            uint16_t first =
                (uint16_t)(((m_read16(s, (uint32_t)m_abs(s, dp16(s, CELL_PTR1)) + block) & 0x00FF) << 3)
                           + dp16(s, CELL_PTR3));
            set_dp16(s, CELL_D, first);
            map_tile_pair(s);
            if (m_read16(s, RAM_LOCATION_ID) != 0x01F5) {
                uint16_t second =
                    (uint16_t)(((m_read16(s, (uint32_t)m_abs(s, dp16(s, CELL_PTR2)) + block) & 0x00FF) << 3)
                               + dp16(s, CELL_PRODUCT));
                set_dp16(s, CELL_D, second);
                map_tile_pair(s);
            }
            uint16_t next = (uint16_t)(block + 1);
            set_dp16(s, CELL_B, next);
            set_dp16(s, CELL_OUT, (uint16_t)(dp16(s, CELL_OUT) + ((next & 7) ? 1 : 0x39)));
        }
        set_dp16(s, CELL_PTR1, (uint16_t)(dp16(s, CELL_PTR1) + 0x60));
        set_dp16(s, CELL_PTR2, (uint16_t)(dp16(s, CELL_PTR2) + 0x60));
        set_dp16(s, CELL_OUT, (uint16_t)(dp16(s, CELL_OUT) - 0x02F0));
        set_dp16(s, CELL_A, (uint16_t)(row + 1));
        if (((row + 1) & 3) == 0) {
            /* STA $2116 : STA $4372 : STA $4375 : STA $4370 : STA $2115 :
             * STA $4374 : STA $420B */
            static const uint8_t steps[] = {
                VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI,
                VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_CONTROL,   VID_STEP_DEST,
                VID_STEP_VRAM_PORT, VID_STEP_SOURCE_BANK, VID_STEP_START,   VID_STEP_END,
            };
            static const VidSnesXfer x = { MENU_DMA, DMA_ONE_REGISTER, steps, &high_port };
            uint32_t source = (uint32_t)dp8(s, (uint8_t)(RAM_MENU_OBJECT_UPLOAD_BUFFER_DP + 2)) << 16
                              | dp16(s, RAM_MENU_OBJECT_UPLOAD_BUFFER_DP);
            vid_upload_vram_half(m_platform(s), PF_AT(0xC264EB), dp16(s, CELL_C), PF_GAME(source), 0x0300,
                                 VID_VRAM_HIGH_BYTES, &x);
            clear_plane_buffer(s, 0x02FE, true);
            set_dp16(s, CELL_C, (uint16_t)(dp16(s, CELL_C) + 0x0300));
        }
    }
    m_rep(s, CPU_FLAG_M);
    m_lda16(s, 0x0040);
    m_cmp16(s, 0x0040, 0x0040);
    c->db = outer_bank;
}

// $C2:65B2
/* The map view's Mode 7 name table: a twelve by sixteen block of consecutive
 * tile numbers, six rows down and two columns in, built in $7E:F000 and sent to
 * the tile half of the plane's first 32 rows. */
static void build_map_names(Snes *s)
{
    Cpu *c = &s->cpu;
    uint8_t outer_bank = c->db;
    m_write16(s, RAM_MENU_MAP_NAME_TABLE, 0);
    m_ldx(s, RAM_ABS(RAM_MENU_MAP_NAME_TABLE));
    m_ldy(s, (uint16_t)(RAM_ABS(RAM_MENU_MAP_NAME_TABLE) + 1));
    c->a = 0x03FE;
    m_mvn(s, 0x7E, 0x7E);

    set_dp8(s, CELL_LOW, 2);
    set_dp8(s, (uint8_t)(CELL_LOW + 1), 6);
    set_dp16(s, CELL_PTR1, RAM_ABS(RAM_MENU_MAP_NAME_TABLE));
    m_write8(s, MUL_A, dp8(s, (uint8_t)(CELL_LOW + 1)));
    m_write8(s, MUL_B, 0x20);
    Sum f = { false, false };
    set_dp16(s, CELL_PTR1, add16(&f, dp16(s, CELL_PTR1), m_read16(s, PRODUCT)));
    f.carry = false;
    set_dp16(s, CELL_PTR1, add16(&f, dp8(s, CELL_LOW), dp16(s, CELL_PTR1)));

    set_dp8(s, CELL_A, 1);
    set_dp8(s, CELL_B, 0);
    for (uint8_t row = 0; row < 0x10; row++) {
        for (uint16_t col = 0; col < 12; col++) {
            m_write8(s, (uint32_t)m_abs(s, dp16(s, CELL_PTR1)) + col, dp8(s, CELL_A));
            set_dp8(s, CELL_A, (uint8_t)(dp8(s, CELL_A) + 1));
        }
        f.carry = false;
        set_dp16(s, CELL_PTR1, add16(&f, dp16(s, CELL_PTR1), 0x0020));
        set_dp8(s, CELL_B, (uint8_t)(row + 1));
    }
    c->db = outer_bank;

    set_dp8(s, CELL_LOW, 0x20);
    set_dp16(s, CELL_C, 0);
    set_dp16(s, CELL_B, RAM_ABS(RAM_MENU_MAP_NAME_TABLE));
    for (int i = 0; i < 0x20; i++) {
        vid_upload_vram_half(m_platform(s), PF_AT(0xC2662F), dp16(s, CELL_C),
                             PF_GAME((RAM_MENU_MAP_NAME_TABLE & 0xFF0000) | dp16(s, CELL_B)), 0x0020,
                             VID_VRAM_LOW_BYTES, &tile_upload);
        f.carry = false;
        set_dp16(s, CELL_C, add16(&f, dp16(s, CELL_C), 0x0080));
        f.carry = false;
        set_dp16(s, CELL_B, add16(&f, dp16(s, CELL_B), 0x0020));
        set_dp8(s, CELL_LOW, (uint8_t)(dp8(s, CELL_LOW) - 1));
    }
    m_sep(s, CPU_FLAG_M);
}

// $C2:63DF
/* Everything the map view shows: the world map loaded, its tiles packed, the
 * plane built from the two map layers, and the name table placed. */
void menu_epoch_build_map(Snes *s)
{
    Cpu *c = &s->cpu;
    m_jsr_dc(s, 0xC263DF, 0xC227DE, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_push8(s, c->db);
    pack_map_rows(s);
    c->db = m_pull8(s);
    set_dp16(s, CELL_PTR1, 0x5800);
    set_dp16(s, CELL_PTR2, 0x4000);
    set_dp16(s, CELL_PTR3, 0x3800);
    set_dp16(s, CELL_PRODUCT, 0x3000);
    m_push8(s, c->db);
    build_map_plane(s);
    c->db = m_pull8(s);
    m_push8(s, c->db);
    build_map_names(s);
    (void)m_pull8(s);
    m_rts(s);
}

// $C2:67B3
/* The four quotients the map view's matrix is made of: the scaled cosine and
 * sine ($08, $0A) over each axis's zoom ($0C, $0E), into $00-$07, the third
 * negated. */
static void map_matrix_divide(Snes *s)
{
    uint16_t cos8 = dp16(s, CELL_A), sin8 = dp16(s, CELL_B);
    uint8_t zoom_x = dp8(s, CELL_C), zoom_y = dp8(s, CELL_D);
    set_dp16(s, 0x00, divide(s, cos8, zoom_x));
    set_dp16(s, 0x02, divide(s, sin8, zoom_x));
    uint16_t sin_over_y = divide(s, sin8, zoom_y);
    set_dp16(s, 0x04, (uint16_t)(0 - sin_over_y));
    set_dp16(s, 0x06, divide(s, cos8, zoom_y));
}

// $C2:6736
/* The map view's Mode 7 matrix, from the cursor's angle and its two zooms: the
 * sine and cosine times eight, divided by the zoom on each axis, with the signs
 * put back. The original keeps the two signs as 16-bit flags at $10 and $11,
 * which overlap, so the second lands in the byte after the first. */
static void map_matrix(Snes *s)
{
    /* The angle for the sine comes from whichever slot the caller left in X;
     * everything after it from the slot at $4E, which is the same one when the
     * cursor calls. */
    uint16_t slot = s->cpu.x;
    set_dp16(s, CELL_PTR1, 0);
    uint16_t sin = sine(s, field16(s, slot, OBJ_PARAMETER));
    m_ldx(s, sin);
    if (sin & 0x8000) {
        sin = (uint16_t)(0 - sin);
        set_dp16(s, CELL_PTR1, (uint16_t)(dp16(s, CELL_PTR1) + 1));
    }
    set_dp16(s, CELL_B, (uint16_t)(sin << 3));

    slot = object(s);
    m_ldx(s, slot);
    uint16_t cos = cosine(s, field16(s, slot, OBJ_PARAMETER));
    m_ldx(s, cos);
    if (cos & 0x8000) {
        cos = (uint16_t)(0 - cos);
        set_dp16(s, (uint8_t)(CELL_PTR1 + 1), (uint16_t)(dp16(s, (uint8_t)(CELL_PTR1 + 1)) + 1));
    }
    set_dp16(s, CELL_A, (uint16_t)(cos << 3));

    slot = object(s);
    m_ldx(s, slot);
    set_dp16(s, CELL_C, field16(s, slot, OBJ_ZOOM));
    set_dp16(s, CELL_D, field16(s, slot, (uint8_t)(OBJ_ZOOM + 2)));
    map_matrix_divide(s);

    if (dp8(s, CELL_PTR1) != 0) {
        set_dp16(s, 0x02, (uint16_t)(0 - dp16(s, 0x02)));
        set_dp16(s, 0x04, (uint16_t)(0 - dp16(s, 0x04)));
    }
    if (dp8(s, (uint8_t)(CELL_PTR1 + 1)) != 0) {
        set_dp16(s, 0x00, (uint16_t)(0 - dp16(s, 0x00)));
        set_dp16(s, 0x06, (uint16_t)(0 - dp16(s, 0x06)));
    }
    for (int i = 0; i < 4; i++)
        set_dp16(s, (uint8_t)(SHADOW_MATRIX + 2 * i), dp16(s, (uint8_t)(2 * i)));
    /* SEP #$10 narrows the index registers while the two sign flags are read
     * and REP #$10 widens them again, so Y keeps only its low byte and X comes
     * back as the cosine's flag. */
    s->cpu.y &= 0xFF;
    s->cpu.x = dp8(s, (uint8_t)(CELL_PTR1 + 1));
    m_lda16(s, dp16(s, 0x06));
}

void menu_epoch_map_matrix(Snes *s)
{
    map_matrix(s);
    m_rts(s);
}

// $C2:688A
/* A world position into the map view's plane: an eighth of it, plus the plane's
 * origin. */
static void marker_position(Snes *s, uint16_t slot)
{
    Sum f = { false, false };
    set_field16(s, slot, OBJ_X, add16(&f, lsr16(&f, field16(s, slot, OBJ_X), 3), 0x0020));
    set_field16(s, slot, OBJ_Y, add16(&f, lsr16(&f, field16(s, slot, OBJ_Y), 3), 0x0030));
}

/* A marker's first step: place it and give it a script. */
static void start_marker(Snes *s, uint16_t slot, uint16_t world_x, uint16_t world_y, uint16_t script)
{
    set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
    set_field8(s, slot, OBJ_SPRITE, 0x30);
    set_field16(s, slot, OBJ_FRACTION, 0);
    set_field16(s, slot, OBJ_X, world_x);
    set_field16(s, slot, OBJ_Y, world_y);
    marker_position(s, slot);
    set_field16(s, slot, OBJ_SCRIPT, script);
    set_field8(s, slot, (uint8_t)(OBJ_SCRIPT + 2), 0xC2);
    set_field8(s, slot, OBJ_SCRIPT_STATE, 0);
}

/* Every step after that: run the sprite script unless another one is busy, and
 * say whether the object goes (the carry). */
static void run_marker_script(Snes *s, uint32_t site)
{
    uint8_t busy = m_read8(s, m_abs(s, RAM_ABS(RAM_MENU_SPRITE_SCRIPT_BUSY)));
    m_set_a8(s, busy);
    m_set_nz8(s, busy);
    if (busy == 0) {
        m_jsr_dc(s, site, 0xC20E1D, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
        m_set_flag(s, CPU_FLAG_C, false);
    } else {
        m_set_flag(s, CPU_FLAG_C, true);
    }
    m_rts(s);
}

// $C2:67FA
/* The marker showing where the party stands. */
void menu_epoch_map_party_marker(Snes *s)
{
    uint16_t slot = object(s);
    if (field8(s, slot, OBJ_STEP) == 0)
        start_marker(s, slot, m_read16(s, m_abs(s, RAM_ABS(RAM_WORLD_POSITION_X))),
                     m_read16(s, m_abs(s, RAM_ABS(RAM_WORLD_POSITION_Y))), 0x6A18);
    run_marker_script(s, 0xC26834);
}

// $C2:683B
/* The marker showing where the Epoch stands, placed only while the party is on
 * the map the Epoch is parked on. */
void menu_epoch_map_epoch_marker(Snes *s)
{
    uint16_t slot = object(s);
    if (field8(s, slot, OBJ_STEP) == 0) {
        m_rep(s, CPU_FLAG_M);
        uint16_t here = (uint16_t)(m_read16(s, RAM_LOCATION_ID) & 0x01FF);
        uint16_t parked = m_read16(s, m_abs(s, RAM_ABS(RAM_MENU_EPOCH_LOCATION)));
        if (here != parked) {
            m_lda16(s, here);
            m_cmp16(s, here, parked);
            m_set_flag(s, CPU_FLAG_C, true);
            m_rts(s);
            return;
        }
        start_marker(s, slot, m_read16(s, m_abs(s, RAM_ABS(RAM_MENU_EPOCH_POSITION))),
                     m_read16(s, m_abs(s, RAM_ABS(RAM_MENU_EPOCH_POSITION) + 2)), 0x6A20);
        m_sep(s, CPU_FLAG_M);
    }
    run_marker_script(s, 0xC26883);
}

// $C2:68A3
/* The marker showing which map the Epoch would land on, whose place on the
 * plane is a table entry rather than a world position. */
void menu_epoch_map_location_marker(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t slot = c->x;
    if (field8(s, slot, OBJ_STEP) == 0) {
        set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
        set_field8(s, slot, OBJ_SPRITE, 0x30);
        m_txy(s);
        m_rep(s, CPU_FLAG_M);
        uint16_t index = (uint16_t)(((m_read16(s, RAM_LOCATION_ID) & 0x01FF) - 0x01F0) << 1);
        set_field16(s, slot, OBJ_X, landing_marker_x(s, index));
        set_field16(s, slot, OBJ_Y, 0x00C0);
        set_field16(s, slot, OBJ_FRACTION, 0);
        m_lda16(s, 0x0047);
        m_ldx(s, slot);
        m_jsr_dc(s, 0xC268D3, 0xC21CDA, DC_Y | DC_N | DC_V | DC_Z | DC_C);
        m_sep(s, CPU_FLAG_M);
    }
    run_marker_script(s, 0xC268DD);
}

// $C2:666C
/* The map view's cursor: it walks its path out while the plane turns with it,
 * waits for the confirm button, then walks back and ends the screen. */
void menu_epoch_map_cursor(Snes *s)
{
    uint8_t step = dispatch_step(s);
    uint16_t slot;
    if (step == 0) {
        slot = object(s);
        m_ldx(s, slot);
        set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
        m_rep(s, CPU_FLAG_M);
        Sum f = { false, false };
        uint16_t centre_x =
            add16(&f, (uint16_t)(m_read16(s, m_abs(s, RAM_ABS(RAM_WORLD_POSITION_X))) >> 4), 0x0010);
        set_dp16(s, SHADOW_CENTER, centre_x);
        set_dp16(s, SHADOW_SCROLL_X, (uint16_t)(0 - centre_x));
        f.carry = false;
        set_dp16(s, (uint8_t)(SHADOW_CENTER + 2),
                 add16(&f, (uint16_t)(m_read16(s, m_abs(s, RAM_ABS(RAM_WORLD_POSITION_Y))) >> 3), 0x0030));
        set_dp16(s, SHADOW_SCROLL_Y, 0);
        set_field16(s, slot, OBJ_PARAMETER, 0);
        set_field16(s, slot, OBJ_PATH_AT, 0);
        step = 1;
    }
    if (step <= 1) {
        m_rep(s, CPU_FLAG_M);
        slot = object(s);
        m_ldy(s, slot);
        uint16_t at = field16(s, slot, OBJ_PATH_AT);
        m_ldx(s, at);
        set_field16(s, slot, OBJ_ZOOM, cursor_path_x(s, at));
        set_field16(s, slot, (uint8_t)(OBJ_ZOOM + 2), cursor_path_y(s, at));
        m_tyx(s);
        uint16_t next = (uint16_t)(at + 2);
        set_field16(s, slot, OBJ_PATH_AT, next);
        if (next >= 0x0040) {
            set_field16(s, slot, OBJ_PATH_AT, 0x003E);
            m_sep(s, CPU_FLAG_M);
            set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
            m_set_a8(s, 0xC2);
            m_ldx(s, 0x6A0A);
            m_jsr_dc(s, 0xC266DF, 0xC204D9, DC_Y | DC_N | DC_V | DC_Z | DC_C);
            m_rep(s, CPU_FLAG_M);
        }
        NATIVE_TIME_AS(s, 0xC26736, map_matrix(s));
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    if (step == 2) {
        m_rep(s, CPU_FLAG_M);
        uint16_t buttons = m_read16(s, m_dp(s, RAM_INPUT_MAPPED_DP));
        m_lda16(s, buttons);
        m_set_flag(s, CPU_FLAG_Z, (buttons & 0x0008) == 0);
        m_sep(s, CPU_FLAG_M);
        if (buttons & 0x0008) {
            slot = object(s);
            m_ldx(s, slot);
            set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
            m_set_a8(s, 0xC2);
            m_ldx(s, 0x69FF);
            m_jsr_dc(s, 0xC266FF, 0xC204D9, DC_Y | DC_N | DC_V | DC_Z | DC_C);
        }
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    m_rep(s, CPU_FLAG_M);
    slot = object(s);
    m_ldy(s, slot);
    uint16_t at = field16(s, slot, OBJ_PATH_AT);
    m_ldx(s, at);
    set_field16(s, slot, OBJ_ZOOM, cursor_path_x(s, at));
    set_field16(s, slot, (uint8_t)(OBJ_ZOOM + 2), cursor_path_y(s, at));
    m_tyx(s);
    uint16_t back = (uint16_t)(at - 2);
    set_field16(s, slot, OBJ_PATH_AT, back);
    NATIVE_TIME_AS(s, 0xC26736, map_matrix(s));
    if (back == 0) {
        m_sep(s, CPU_FLAG_M);
        m_set_a8(s, 0x07);
        m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_MODE)), 0x07);
        m_set_flag(s, CPU_FLAG_C, true);
    } else {
        m_set_flag(s, CPU_FLAG_C, false);
    }
    m_rts(s);
}

// $C2:68F4
/* The map view's one per-line effect: the fixed colour's blue channel stepping
 * down the screen, seven lines at a value, out of a table the vblank handler
 * re-points every frame. Only the first 224 bytes are read; the second loop
 * runs its counter all the way round, as the original does. */
static void map_gradient(Snes *s)
{
    m_write16(s, m_abs(s, WRAM_ADDR), RAM_ABS(RAM_MENU_OBJECT_WAVE_SCROLL));
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), (uint8_t)(RAM_MENU_OBJECT_WAVE_SCROLL >> 16));
    uint8_t level = 0;
    for (int pass = 0; pass < 2; pass++) {
        do {
            set_dp8(s, CELL_LOW, level);
            for (int line = 0; line < 7; line++)
                m_write8(s, m_abs(s, WRAM_PORT), (uint8_t)(level | 0x80));
            level = (uint8_t)(level + 1);
            set_dp8(s, CELL_LOW, level);
        } while (level != 0x10);
    }
    m_write8(s, RAM_MENU_OBJECT_LINE_SLOTS_FIRST_LINES, 0xF0);
    m_write8(s, RAM_MENU_OBJECT_LINE_SLOTS_SECOND_LINES, 0xF0);
    m_write8(s, RAM_MENU_OBJECT_LINE_SLOTS_END, 0x00);
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS, RAM_ABS(RAM_MENU_OBJECT_WAVE_SCROLL));
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE,
              (uint16_t)(RAM_ABS(RAM_MENU_OBJECT_WAVE_SCROLL) + 0x70));
    static const VidLineEffect gradient = { .target = VID_LINE_FIXED_COLOR,
                                            .table = PF_GAME(RAM_MENU_OBJECT_LINE_SLOTS_FIRST_LINES),
                                            .indirect = true, .indirect_bank = 0x7E };
    vid_set_line_effect(m_platform(s), PF_AT(0xC2694F), EFFECT_SLOT, &gradient, line_effect_steps);
    m_lda8(s, EFFECT_SLOT_BIT);
    m_tsb8(s, m_dp(s, LINE_EFFECTS));
}

void menu_epoch_map_gradient(Snes *s)
{
    map_gradient(s);
    m_ldx(s, 0x0000);
    m_set_flag(s, CPU_FLAG_C, true);
    m_rts(s);
}

// $C2:6965
/* Load the map view's graphics: the set this location's map uses, with the two
 * story exceptions, decompressed to $7E:8621 and sent to VRAM $6200. */
void menu_epoch_load_map_graphics(Snes *s)
{
    m_jsr_dc(s, 0xC26965, 0xC25798, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    uint16_t index = (uint16_t)((m_read16(s, RAM_LOCATION_ID) & 0x01FF) - 0x01F0);
    uint16_t pair = map_graphics_set(s, index);
    m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_ENTRY)), pair);
    uint8_t set = (uint8_t)pair;
    if (set == 0x6C) {
        if (m_read8(s, RAM_MENU_STORY_PROGRESS) < 0x36)
            m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_ENTRY)), 0x6F);
    } else if (set == 0x6B) {
        if ((m_read8(s, RAM_MENU_STORY_FLAGS_EPOCH) & 1) == 0)
            m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_ENTRY)), 0x6F);
    }
    m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_TABLE)), 0xF400);
    m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_TABLE) + 2), 0xC6);
    m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_KIND)), 0x02);
    m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_DEST)), MAP_GRAPHICS);
    m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_DEST) + 2), 0x7E);
    m_ldx(s, MAP_GRAPHICS);
    m_lda8(s, 0x7E);
    m_jsl_dc(s, 0xC269BB, 0xC257DF, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_lda8(s, 0x0C);
    m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_LOAD_REQUEST_SIZE)), 0x0C);
    m_jsl_dc(s, 0xC269C4, 0xC25823, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    vid_upload_vram(m_platform(s), PF_AT(0xC269C8), 0x6200, PF_GAME(0x7E0000 | MAP_GRAPHICS), 0x0400,
                    &word_upload);
    m_rts(s);
}

// $C2:631F
/* The map view: sprite tiles at $6000 and Mode 7 behind them, the plane and the
 * cursor's graphics built, the cursor object put in a slot, and the frame task
 * run until the mode byte leaves the map view. */
void menu_epoch_map_screen(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    set_dp8(s, SHADOW_BG_MODE, 0x07);
    VidObjConfig objects = vid_obj_config_decode(0x63);
    vid_set_obj_config(pf, PF_AT(0xC26325), &objects);
    set_dp8(s, SHADOW_MATH_CONTROL, 0);
    set_dp8(s, SHADOW_MATH_LAYERS, 0x20);
    m_jsr_dc(s, 0xC2632E, 0xC226A8, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC26331, 0xC203EF, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC26334, 0xC20471, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC26337, 0xC26965, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    NATIVE_TIME_AS(s, 0xC268F4, map_gradient(s));
    m_jsr_dc(s, 0xC2633D, 0xC263DF, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);

    vid_upload_vram(pf, PF_AT(0xC26340), 0x6000, PF_GAME(0xC6F500), 0x0020, &word_upload);
    vid_upload_vram(pf, PF_AT(0xC26367), 0x6020, PF_GAME(0xC6F520), 0x0020, &word_upload);

    m_write16(s, m_abs(s, OBJECT_PALETTE), 0);
    m_push8(s, c->db);
    m_ldx(s, 0xF540);
    m_ldy(s, OBJECT_PALETTE_PATCH);
    m_lda16(s, 0x0020);
    m_mvn(s, 0x00, 0xC6);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_sep(s, CPU_FLAG_M);
    m_ldx(s, 0x666C);
    m_jsr_dc(s, 0xC263A6, 0xC2048A, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_set_a8(s, 0xC2);
    m_ldx(s, 0x69F8);
    m_jsr_dc(s, 0xC263AE, 0xC204D9, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_lda8(s, 0x03);
    m_tsb8(s, m_dp(s, FRAME_STATE));
    m_lda8(s, 0x81);
    tm_enable_interrupts(pf, PF_AT(0xC263B7), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    do {
        m_jsr_dc(s, 0xC263BA, 0xC2046E, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
        m_set_a8(s, m_read8(s, m_abs(s, RAM_ABS(RAM_MENU_MODE))));
        m_cmp8(s, (uint8_t)c->a, 0x06);
    } while ((uint8_t)c->a == 0x06);
    m_ldx(s, 0x0011);
    m_jsr_dc(s, 0xC263C7, 0xC20454, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    set_dp8(s, SHADOW_BG_MODE, 0x09);
    VidObjConfig back = vid_obj_config_decode(0x60);
    vid_set_obj_config(pf, PF_AT(0xC263D0), &back);
    m_lda8(s, 0x00);
    tm_enable_interrupts(pf, PF_AT(0xC263D5), 0);
    vid_stop_uploads(pf, PF_AT(0xC263D8));
    vid_enable_line_effects(pf, PF_AT(0xC263DB), 0);
    m_rts(s);
}

/* ---- the time dial ($C2:6A34) ------------------------------------------------------------ */

// $C2:747A
/* Which of the seven periods the party is in now, or the last one if none
 * matches. */
static uint16_t current_period(Snes *s)
{
    uint16_t table = period_table(s);
    set_dp16(s, CELL_PTR1, table);
    set_dp8(s, (uint8_t)(CELL_PTR1 + 2), 0xC2);
    uint16_t here = (uint16_t)(m_read16(s, RAM_LOCATION_ID) & 0x01FF);
    for (uint16_t i = 0; i < 7; i++) {
        if (period_location(s, table, (uint16_t)(2 * i)) == here)
            return i;
    }
    return 6;
}

// $C2:745A
/* The location the dial's object would travel to at the period it shows. The
 * period is doubled with an 8-bit shift, so one of $80 or more indexes the
 * table from the other end of its own byte. */
static uint16_t period_location_for(Snes *s, uint16_t slot)
{
    uint16_t table = period_table(s);
    set_dp16(s, CELL_PTR1, table);
    set_dp8(s, (uint8_t)(CELL_PTR1 + 2), 0xC2);
    return period_location(s, table, (uint8_t)(field8(s, slot, OBJ_PARAMETER) << 1));
}

// $C2:6F33
/* A signed 8 by 16 bit multiply, $07 times the word at $0E, into the 24-bit
 * product at $19. Each operand is made positive first and the sign put back
 * afterwards, so the original also leaves both absolute. */
static void multiply_signed_logic(Snes *s)
{
    bool negative = ((dp8(s, (uint8_t)(CELL_D + 1)) ^ dp8(s, (uint8_t)(CELL_STEP + 1))) & 0x80) != 0;
    uint8_t by = dp8(s, (uint8_t)(CELL_STEP + 1));
    if (by & 0x80) {
        by = (uint8_t)(0 - by);
        set_dp8(s, (uint8_t)(CELL_STEP + 1), by);
    }
    uint16_t value = dp16(s, CELL_D);
    if (value & 0x8000) {
        value = (uint16_t)(0 - value);
        set_dp16(s, CELL_D, value);
    }
    m_write8(s, m_abs(s, MUL_A), by);
    m_write8(s, m_abs(s, MUL_B), (uint8_t)value);
    set_dp8(s, (uint8_t)(CELL_PRODUCT + 2), 0);
    uint16_t low = m_read16(s, m_abs(s, PRODUCT));
    m_write8(s, m_abs(s, MUL_B), (uint8_t)(value >> 8));
    set_dp16(s, CELL_PRODUCT, low);
    Sum f = { false, false };
    set_dp16(s, (uint8_t)(CELL_PRODUCT + 1),
             add16(&f, m_read16(s, m_abs(s, PRODUCT)), dp16(s, (uint8_t)(CELL_PRODUCT + 1))));
    if (negative) {
        Sum n = { true, false };
        set_dp16(s, CELL_PRODUCT, sub16(&n, 0, dp16(s, CELL_PRODUCT)));
        set_dp8(s, (uint8_t)(CELL_PRODUCT + 2), sub8(&n, 0, dp8(s, (uint8_t)(CELL_PRODUCT + 2))));
    }
}

void menu_epoch_multiply_signed(Snes *s)
{
    multiply_signed_logic(s);
    m_rts(s);
}

// $C2:6CC1
/* Rebuild the gate's per-line window table for this frame, into whichever of
 * the two buffers the flag at $1BF5 says is free. The gate is a triangle: its
 * three corners are turned by the object's angle around the screen centre and
 * written to $1BB8 through the work RAM port, then each edge is given the slope
 * the divider works out, and every line between the highest and lowest corner
 * takes the two edges that cross it. */
static void build_gate_window(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t table;
    uint8_t flags = m_read8(s, RAM_MENU_GATE_FLAGS);
    if (flags & 1) {
        m_write8(s, RAM_MENU_GATE_FLAGS, (uint8_t)(flags & ~1u));
        m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS, WINDOW_TABLE_A);
        m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE,
                  WINDOW_TABLE_A + LINE_HALF);
        table = WINDOW_TABLE_A;
    } else {
        m_write8(s, RAM_MENU_GATE_FLAGS, (uint8_t)(flags | 1u));
        m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS, WINDOW_TABLE_B);
        m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE,
                  WINDOW_TABLE_B + LINE_HALF);
        table = WINDOW_TABLE_B;
    }
    set_dp16(s, CELL_PTR3, table);
    set_dp8(s, CELL_LOW, 0x84);
    set_dp8(s, (uint8_t)(CELL_LOW + 1), 0x70);

    uint16_t slot = object(s);
    uint16_t angle = field16(s, slot, OBJ_Y);
    set_dp16(s, CELL_A, cosine(s, angle));
    set_dp16(s, CELL_B, sine(s, angle));

    /* The table starts closed, and its tail is filled by propagating one pair. */
    uint32_t base = (uint32_t)dp8(s, (uint8_t)(CELL_PTR3 + 2)) << 16 | table;
    uint8_t outer_bank = c->db;
    m_write16(s, base, 1);
    m_write16(s, base + 1, 0);
    m_ldx(s, (uint16_t)(table + 0x90));
    m_ldy(s, (uint16_t)(table + 0x92));
    c->a = 0x009D;
    m_mvn(s, 0x7E, 0x7E);
    c->db = outer_bank;

    set_dp16(s, CELL_C, 0);
    m_write16(s, m_abs(s, WRAM_ADDR), RAM_ABS(RAM_MENU_GATE_CORNERS));
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), 0x00);
    set_dp8(s, CELL_STEP, 3);
    for (int corner = 0; corner < 3; corner++) {
        uint16_t at = dp16(s, CELL_C);
        uint8_t dx = gate_corner(s, at);
        set_dp8(s, CELL_PAIR, dx);
        uint8_t dy = gate_corner(s, (uint16_t)(at + 1));
        set_dp8(s, (uint8_t)(CELL_PAIR + 1), dy);
        set_dp16(s, CELL_C, (uint16_t)(at + 2));

        set_dp16(s, CELL_D, dp16(s, CELL_A));
        set_dp8(s, (uint8_t)(CELL_STEP + 1), dx);
        NATIVE_TIME_AS(s, 0xC26F33, multiply_signed_logic(s));
        set_dp16(s, CELL_HEIGHT, dp16(s, CELL_PRODUCT));
        set_dp16(s, CELL_D, dp16(s, CELL_B));
        set_dp8(s, (uint8_t)(CELL_STEP + 1), dy);
        NATIVE_TIME_AS(s, 0xC26F33, multiply_signed_logic(s));
        Sum f = { false, false };
        uint16_t x = add16(&f, dp16(s, CELL_PRODUCT), dp16(s, CELL_HEIGHT));
        x = (uint16_t)(x >> 8 | x << 8);
        m_write8(s, m_abs(s, WRAM_PORT), add8(&f, (uint8_t)x, dp8(s, CELL_LOW)));

        set_dp16(s, CELL_D, dp16(s, CELL_B));
        set_dp8(s, (uint8_t)(CELL_STEP + 1), dp8(s, CELL_PAIR));
        NATIVE_TIME_AS(s, 0xC26F33, multiply_signed_logic(s));
        set_dp16(s, CELL_HEIGHT, dp16(s, CELL_PRODUCT));
        set_dp16(s, CELL_D, dp16(s, CELL_A));
        set_dp8(s, (uint8_t)(CELL_STEP + 1), dp8(s, (uint8_t)(CELL_PAIR + 1)));
        NATIVE_TIME_AS(s, 0xC26F33, multiply_signed_logic(s));
        f.carry = true;
        uint16_t y = sub16(&f, dp16(s, CELL_PRODUCT), dp16(s, CELL_HEIGHT));
        y = (uint16_t)(y >> 8 | y << 8);
        m_write8(s, m_abs(s, WRAM_PORT), add8(&f, (uint8_t)y, dp8(s, (uint8_t)(CELL_LOW + 1))));
        set_dp8(s, CELL_STEP, (uint8_t)(2 - corner));
    }

    /* The three edges, each taken from its higher corner to its lower one. */
    set_dp8(s, CELL_LOW, 3);
    set_dp16(s, CELL_PRODUCT, 0);
    set_dp16(s, CELL_PTR1, 0);
    set_dp16(s, CELL_PTR2, 0);
    for (int i = 0; i < 3; i++) {
        uint16_t at = dp16(s, CELL_PTR1);
        uint8_t x0 = m_read8(s, RAM_MENU_GATE_CORNERS + at);
        set_dp8(s, CELL_A, x0);
        uint8_t y0 = m_read8(s, RAM_MENU_GATE_CORNERS + at + 1);
        set_dp8(s, CELL_B, y0);
        uint8_t x1, y1;
        if (dp8(s, CELL_LOW) == 1) {
            x1 = m_read8(s, RAM_MENU_GATE_CORNERS);
            y1 = m_read8(s, RAM_MENU_GATE_CORNERS + 1);
        } else {
            x1 = m_read8(s, RAM_MENU_GATE_CORNERS + at + 2);
            y1 = m_read8(s, RAM_MENU_GATE_CORNERS + at + 3);
        }
        set_dp8(s, CELL_C, x1);
        set_dp8(s, CELL_D, y1);
        if (y1 != y0) {
            if (y1 < y0) {
                /* The original swaps through the index registers, which are
                 * 16-bit here, so each swap also carries the byte after it. */
                set_dp16(s, CELL_D, dp16(s, CELL_B));
                set_dp8(s, CELL_B, y1);
                set_dp16(s, CELL_A, dp16(s, CELL_C));
                set_dp8(s, CELL_C, x0);
                uint8_t t = y0; y0 = y1; y1 = t;
                t = x0; x0 = x1; x1 = t;
            }
            y1 = (uint8_t)(y1 - 1);
            set_dp8(s, CELL_D, y1);
            Sum f = { true, false };
            uint8_t height = sub8(&f, y1, y0);
            set_dp8(s, CELL_HEIGHT, height);
            f.carry = true;
            uint8_t width = sub8(&f, x1, x0);
            set_dp8(s, CELL_PAIR, width);
            bool step_negative = (width & 0x80) != 0;
            if (step_negative) {
                width = (uint8_t)(0 - width);
                set_dp8(s, CELL_PAIR, width);
            }
            m_write8(s, m_abs(s, DIV_LOW), (uint8_t)s->cpu.d);
            m_write8(s, m_abs(s, DIV_HIGH), width);
            m_write8(s, m_abs(s, DIVISOR), height);

            uint16_t e = dp16(s, CELL_PTR2);
            m_write8(s, RAM_MENU_GATE_EDGE_ACTIVE + e, 0);
            m_write8(s, RAM_MENU_GATE_EDGE_X + e + 1, x0);
            m_write8(s, RAM_MENU_GATE_EDGE_X + e, 0);
            m_write8(s, RAM_MENU_GATE_EDGE_TOP + e + 1, y0);
            m_write8(s, RAM_MENU_GATE_EDGE_TOP + e, 0);
            f.carry = true;
            m_write8(s, RAM_MENU_GATE_EDGE_LINES + e, sub8(&f, y1, y0));
            m_write8(s, RAM_MENU_GATE_EDGE_LINES + e + 1, 0);
            m_write8(s, RAM_MENU_GATE_EDGE_BOTTOM + e + 1, y1);
            m_write8(s, RAM_MENU_GATE_EDGE_BOTTOM + e, 0);
            uint16_t slope = m_read16(s, m_abs(s, QUOTIENT));
            m_write16(s, RAM_MENU_GATE_EDGE_STEP + e, step_negative ? (uint16_t)(0 - slope) : slope);
            set_dp16(s, CELL_PRODUCT, (uint16_t)(dp16(s, CELL_PRODUCT) + 1));
            set_dp16(s, CELL_PTR2, (uint16_t)(e + 2));
        }
        set_dp16(s, CELL_PTR1, (uint16_t)(dp16(s, CELL_PTR1) + 2));
        set_dp8(s, CELL_LOW, (uint8_t)(dp8(s, CELL_LOW) - 1));
    }

    /* The lines the edges cover, and the two that cross each one. The two
     * searches count down from one less than the number of edges and stop at 0,
     * so with fewer than two they read on past the edges, as the original
     * does. */
    uint16_t edges = dp16(s, CELL_PRODUCT);
    uint8_t top = m_read8(s, RAM_MENU_GATE_EDGE_TOP + 1);
    set_dp8(s, CELL_A, top);
    uint16_t left = (uint16_t)(edges - 1);
    uint16_t at_edge = 2;
    do {
        uint8_t v = m_read8(s, (uint32_t)(RAM_MENU_GATE_EDGE_TOP + 1) + at_edge);
        if (v < dp8(s, CELL_A))
            set_dp8(s, CELL_A, v);
        at_edge = (uint16_t)(at_edge + 2);
        left = (uint16_t)(left - 1);
    } while (left != 0);
    uint8_t bottom = m_read8(s, RAM_MENU_GATE_EDGE_BOTTOM + 1);
    set_dp8(s, CELL_B, bottom);
    left = (uint16_t)(edges - 1);
    at_edge = 2;
    do {
        uint8_t v = m_read8(s, (uint32_t)(RAM_MENU_GATE_EDGE_BOTTOM + 1) + at_edge);
        if (v >= dp8(s, CELL_B))
            set_dp8(s, CELL_B, v);
        at_edge = (uint16_t)(at_edge + 2);
        left = (uint16_t)(left - 1);
    } while (left != 0);
    top = dp8(s, CELL_A);
    bottom = dp8(s, CELL_B);
    Sum f = { true, false };
    uint8_t lines = sub8(&f, bottom, top);
    set_dp8(s, CELL_PTR2, lines);
    f.carry = false;
    m_write16(s, m_abs(s, WRAM_ADDR), add16(&f, (uint16_t)(top << 1), dp16(s, CELL_PTR3)));
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), dp8(s, (uint8_t)(CELL_PTR3 + 2)));
    uint8_t remaining = lines;
    uint16_t out = 0;
    Sum edge = { false, false };
    do {
        uint8_t at = dp8(s, CELL_A);
        uint16_t count = edges;
        uint16_t e = 0;
        do {
            if (m_read8(s, (uint32_t)(RAM_MENU_GATE_EDGE_TOP + 1) + e) == at)
                m_write8(s, RAM_MENU_GATE_EDGE_ACTIVE + e,
                         (uint8_t)(m_read8(s, RAM_MENU_GATE_EDGE_ACTIVE + e) + 1));
            e = (uint16_t)(e + 2);
            count = (uint16_t)(count - 1);
        } while (count != 0);
        /* This pass counts down an 8-bit copy of the edge count at $10, so no
         * edges at all take it all the way round rather than through 65536. */
        set_dp8(s, CELL_PTR1, (uint8_t)edges);
        uint8_t left_edges = (uint8_t)edges;
        e = 0;
        out = 0;
        do {
            if (m_read8(s, RAM_MENU_GATE_EDGE_ACTIVE + e) != 0) {
                uint16_t x = m_read16(s, RAM_MENU_GATE_EDGE_X + e);
                m_write16(s, m_abs(s, out), x);
                edge.carry = false;
                m_write16(s, RAM_MENU_GATE_EDGE_X + e,
                          add16(&edge, x, m_read16(s, RAM_MENU_GATE_EDGE_STEP + e)));
                out = (uint16_t)(out + 2);
                uint8_t lines_left = (uint8_t)(m_read8(s, RAM_MENU_GATE_EDGE_LINES + e) - 1);
                m_write8(s, RAM_MENU_GATE_EDGE_LINES + e, lines_left);
                if (lines_left & 0x80)
                    m_write8(s, RAM_MENU_GATE_EDGE_ACTIVE + e, 0);
            }
            e = (uint16_t)(e + 2);
            left_edges = (uint8_t)(left_edges - 1);
            set_dp8(s, CELL_PTR1, left_edges);
        } while (left_edges != 0);
        uint8_t first = dp8(s, (uint8_t)(CELL_LOW + 1)), second = dp8(s, (uint8_t)(CELL_PAIR + 1));
        m_set_a8(s, first);
        m_cmp8(s, first, second);
        if (first < second) {
            m_write8(s, m_abs(s, WRAM_PORT), first);
            m_write8(s, m_abs(s, WRAM_PORT), second);
            m_set_a8(s, second);
        } else {
            m_write8(s, m_abs(s, WRAM_PORT), second);
            m_write8(s, m_abs(s, WRAM_PORT), first);
            m_set_a8(s, first);
        }
        set_dp8(s, CELL_A, (uint8_t)(at + 1));
        remaining = (uint8_t)(remaining - 1);
        set_dp8(s, CELL_PTR2, remaining);
    } while (remaining != 0);
    /* The registers the fill leaves: the last edge index, the last value put
     * out, and the comparison of the two edges of the last line. */
    m_ldx(s, (uint16_t)(2 * edges));
    m_ldy(s, out);
    m_set_nz8(s, remaining);
}

void menu_epoch_build_gate_window(Snes *s)
{
    build_gate_window(s);
    m_rts(s);
}

// $C2:6AC8
/* The time dial's one object: it programs window 1's per-line edges, then lets
 * the player turn the dial through the seven periods and either travel or
 * leave. */
void menu_epoch_time_dial_object(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t slot;
    uint8_t step = dispatch_step(s);
    if (step == 0) {
        slot = object(s);
        m_ldx(s, slot);
        set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
        m_set_a8(s, (uint8_t)current_period(s));
        m_ldx(s, slot);
        set_field8(s, slot, OBJ_PARAMETER, (uint8_t)c->a);
        m_write8(s, RAM_MENU_OBJECT_LINE_SLOTS_FIRST_LINES, 0xF0);
        m_write8(s, RAM_MENU_OBJECT_LINE_SLOTS_SECOND_LINES, 0xF0);
        m_write8(s, RAM_MENU_OBJECT_LINE_SLOTS_END, 0x00);
        set_dp8(s, (uint8_t)(CELL_PTR3 + 2), 0x7E);
        static const VidLineEffect window = { .target = VID_LINE_WINDOW1,
                                              .table = PF_GAME(RAM_MENU_OBJECT_LINE_SLOTS_FIRST_LINES),
                                              .indirect = true, .indirect_bank = 0x7E };
        vid_set_line_effect(m_platform(s), PF_AT(0xC26AFA), EFFECT_SLOT, &window, line_effect_steps);
        m_lda8(s, EFFECT_SLOT_BIT);
        m_tsb8(s, m_dp(s, LINE_EFFECTS));
        set_dp8(s, CELL_LOW, 0x84);
        set_dp8(s, (uint8_t)(CELL_LOW + 1), 0x70);
        m_rep(s, CPU_FLAG_M);
        uint8_t outer_bank = c->db;
        m_write16(s, RAM_MENU_OBJECT_WAVE_SCROLL, 1);
        m_write16(s, RAM_MENU_OBJECT_WAVE_SCROLL + 1, 0);
        m_ldx(s, RAM_ABS(RAM_MENU_OBJECT_WAVE_SCROLL));
        m_ldy(s, (uint16_t)(RAM_ABS(RAM_MENU_OBJECT_WAVE_SCROLL) + 2));
        c->a = 0x01BD;
        m_mvn(s, 0x7E, 0x7E);
        m_ldx(s, WINDOW_TABLE_A);
        m_ldy(s, WINDOW_TABLE_B);
        c->a = 0x01BF;
        m_mvn(s, 0x7E, 0x7E);
        c->db = outer_bank;
        slot = object(s);
        m_ldy(s, slot);
        uint16_t first = (uint16_t)((field16(s, slot, OBJ_PARAMETER) & 0x00FF) << 1);
        m_ldx(s, first);
        uint16_t angle = period_angle(s, first);
        set_field16(s, slot, OBJ_Y, angle);
        set_field16(s, slot, OBJ_X, angle);
        m_sep(s, CPU_FLAG_M);
        m_set_a8(s, field8(s, slot, OBJ_PARAMETER));
        m_write8(s, RAM_MENU_TIME_PERIOD, (uint8_t)c->a);
        NATIVE_TIME_AS(s, 0xC26CC1, build_gate_window(s));
        step = 1;
    }
    if (step == 1) {
        m_rep(s, CPU_FLAG_M);
        slot = object(s);
        m_ldx(s, slot);
        uint16_t buttons = m_read16(s, m_dp(s, (uint8_t)(RAM_INPUT_MAPPED_DP + RAM_INPUT_MAPPED_STRIDE)));
        /* The six directions are tested one at a time, down and up in turn, so
         * a frame holding one of each takes whichever comes first here. */
        static const struct { uint16_t button; int turn; } turns[] = {
            { 0x0010, -1 }, { 0x0020, 1 }, { 0x0100, -1 },
            { 0x0200, 1 },  { 0x0800, -1 }, { 0x0400, 1 },
        };
        int turn = 0;
        for (unsigned i = 0; i < sizeof turns / sizeof turns[0] && turn == 0; i++)
            if (buttons & turns[i].button)
                turn = turns[i].turn;
        if (turn == 0 && (buttons & 0x0080)) {
            set_dp16(s, CELL_A, (uint16_t)(m_read16(s, RAM_LOCATION_ID) & 0x01FF));
            uint16_t there = period_location_for(s, slot);
            m_lda16(s, there);
            m_cmp16(s, there, dp16(s, CELL_A));
            if (there != dp16(s, CELL_A)) {
                m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_TRAVEL_LOCATION)), there);
                m_sep(s, CPU_FLAG_M);
                m_set_a8(s, 0x07);
                m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_MODE)), 0x07);
                m_set_flag(s, CPU_FLAG_C, false);
                m_rts(s);
                return;
            }
            m_sep(s, CPU_FLAG_M);
            m_set_a8(s, 0x18);
            m_write8(s, m_abs(s, RAM_ABS(RAM_SOUND_COMMAND)), 0x18);
            m_set_a8(s, 0x01);
            m_write8(s, m_abs(s, RAM_ABS(RAM_SOUND_COMMAND_PARAMETERS)), 0x01);
            m_set_a8(s, 0x80);
            m_write8(s, m_abs(s, RAM_ABS(RAM_SOUND_COMMAND_PARAMETERS) + 1), 0x80);
            m_jsl_dc(s, 0xC26BE9, 0xC70004, DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        } else if (turn == 0 && (buttons & 0x0008)) {
            m_lda16(s, 0xFFFF);
            m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_TRAVEL_LOCATION)), 0xFFFF);
            m_sep(s, CPU_FLAG_M);
            m_set_a8(s, 0x07);
            m_write8(s, m_abs(s, RAM_ABS(RAM_MENU_MODE)), 0x07);
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        } else if (turn == 0) {
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
        m_sep(s, CPU_FLAG_M);
        if (turn < 0) {
            uint8_t period = (uint8_t)(field8(s, slot, OBJ_PARAMETER) - 1);
            set_field8(s, slot, OBJ_PARAMETER, period);
            if (period & 0x80)
                set_field8(s, slot, OBJ_PARAMETER, 0);
            set_field16(s, slot, OBJ_TURN, 0xFFFC);
        } else {
            uint8_t period = (uint8_t)(field8(s, slot, OBJ_PARAMETER) + 1);
            set_field8(s, slot, OBJ_PARAMETER, period);
            if (period >= 7)
                set_field8(s, slot, OBJ_PARAMETER, 6);
            set_field16(s, slot, OBJ_TURN, 0x0004);
        }
        m_rep(s, CPU_FLAG_M);
        m_txy(s);
        uint16_t step_index = (uint16_t)((field16(s, slot, OBJ_PARAMETER) & 0x00FF) << 1);
        m_ldx(s, step_index);
        set_field16(s, slot, OBJ_X, period_angle(s, step_index));
        m_sep(s, CPU_FLAG_M);
        m_tyx(s);
        set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
    }
    /* Step 2: turn towards the angle the chosen period stands at. */
    m_rep(s, CPU_FLAG_M);
    slot = object(s);
    m_ldx(s, slot);
    if (field16(s, slot, OBJ_Y) == field16(s, slot, OBJ_X)) {
        set_dp16(s, CELL_A, period_location_for(s, slot));
        uint16_t here = (uint16_t)(m_read16(s, RAM_LOCATION_ID) & 0x01FF);
        m_lda16(s, here);
        m_cmp16(s, here, dp16(s, CELL_A));
        if (here != dp16(s, CELL_A)) {
            m_sep(s, CPU_FLAG_M);
            m_set_a8(s, field8(s, slot, OBJ_PARAMETER));
            m_write8(s, RAM_MENU_TIME_PERIOD, (uint8_t)c->a);
            m_set_a8(s, 1);
            set_field8(s, slot, OBJ_STEP, 1);
            m_set_flag(s, CPU_FLAG_C, false);
            m_rts(s);
            return;
        }
        m_lda16(s, field16(s, slot, OBJ_TURN));
        m_sep(s, CPU_FLAG_M);
        if ((field16(s, slot, OBJ_TURN) & 0x8000) == 0) {
            uint8_t period = (uint8_t)(field8(s, slot, OBJ_PARAMETER) + 1);
            set_field8(s, slot, OBJ_PARAMETER, period);
            if (period >= 7) {
                set_field8(s, slot, OBJ_PARAMETER, (uint8_t)(period - 2));
                m_rep(s, CPU_FLAG_M);
                set_field16(s, slot, OBJ_TURN, (uint16_t)(0 - field16(s, slot, OBJ_TURN)));
            }
        } else {
            uint8_t period = (uint8_t)(field8(s, slot, OBJ_PARAMETER) - 1);
            set_field8(s, slot, OBJ_PARAMETER, period);
            if (period & 0x80) {
                set_field8(s, slot, OBJ_PARAMETER, (uint8_t)(period + 2));
                m_rep(s, CPU_FLAG_M);
                set_field16(s, slot, OBJ_TURN, (uint16_t)(0 - field16(s, slot, OBJ_TURN)));
            }
        }
        m_rep(s, CPU_FLAG_M);
        m_ldy(s, object(s));
        uint16_t turn_index = (uint16_t)((field16(s, slot, OBJ_PARAMETER) & 0x00FF) << 1);
        m_ldx(s, turn_index);
        set_field16(s, slot, OBJ_X, period_angle(s, turn_index));
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    Sum f = { false, false };
    set_field16(s, slot, OBJ_Y, add16(&f, field16(s, slot, OBJ_Y), field16(s, slot, OBJ_TURN)));
    m_sep(s, CPU_FLAG_M);
    NATIVE_TIME_AS(s, 0xC26CC1, build_gate_window(s));
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* The time dial's four Mode 7 matrix channels read their values through line
 * slots 2 and 3's small tables (menu/line_effects.h). */
enum { GATE_MATRIX_TABLES = RAM_MENU_OBJECT_LINE_SLOTS + RAM_MENU_OBJECT_LINE_SLOTS_STRIDE };

// $C2:6F85
/* The object that drives the gate's Mode 7 matrix: four per-line tables, two of
 * cosines and two of sines, double-buffered and advanced by one entry a frame
 * so the gate turns. */
void menu_epoch_gate_matrix_object(Snes *s)
{
    Cpu *c = &s->cpu;
    uint16_t slot;
    if (dispatch_step(s) != 0) {
        /* Every frame after the first: swap the buffers and append one value.
         * The flags are read and changed a word at a time here, the byte after
         * them included. */
        m_rep(s, CPU_FLAG_M);
        uint16_t flags = m_read16(s, RAM_MENU_GATE_FLAGS);
        uint16_t cos_table, sin_table;
        uint8_t outer_bank = c->db;
        if ((flags & 2) == 0) {
            m_write16(s, RAM_MENU_GATE_FLAGS, (uint16_t)(flags | 2u));
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 2 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_COS_B);
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 3 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_COS_B + LINE_HALF);
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 4 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_SIN_B);
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 5 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_SIN_B + LINE_HALF);
            /* The whole table moves one value on into the other buffer. */
            m_ldy(s, MATRIX_COS_B);
            m_ldx(s, MATRIX_COS_A + 2);
            c->a = MATRIX_LAST;
            m_mvn(s, 0x7E, 0x7E);
            m_ldy(s, MATRIX_SIN_B);
            m_ldx(s, MATRIX_SIN_A + 2);
            c->a = MATRIX_LAST;
            m_mvn(s, 0x7E, 0x7E);
            cos_table = MATRIX_COS_B;
            sin_table = MATRIX_SIN_B;
        } else {
            m_write16(s, RAM_MENU_GATE_FLAGS, (uint16_t)(flags & ~2u));
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 2 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_COS_A);
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 3 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_COS_A + LINE_HALF);
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 4 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_SIN_A);
            m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 5 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_SIN_A + LINE_HALF);
            m_ldy(s, MATRIX_COS_A);
            m_ldx(s, MATRIX_COS_B + 2);
            c->a = MATRIX_LAST;
            m_mvn(s, 0x7E, 0x7E);
            m_ldy(s, MATRIX_SIN_A);
            m_ldx(s, MATRIX_SIN_B + 2);
            c->a = MATRIX_LAST;
            m_mvn(s, 0x7E, 0x7E);
            cos_table = MATRIX_COS_A;
            sin_table = MATRIX_SIN_A;
        }
        set_dp16(s, CELL_PTR1, cos_table);
        set_dp16(s, CELL_PTR2, sin_table);
        slot = object(s);
        m_ldy(s, slot);
        uint16_t angle = field16(s, slot, OBJ_Y);
        m_ldx(s, cos_table);
        m_write16(s, 0x7E0000 | (uint16_t)(cos_table + MATRIX_LAST), cosine(s, angle));
        m_ldx(s, sin_table);
        m_write16(s, 0x7E0000 | (uint16_t)(sin_table + MATRIX_LAST), sine(s, (uint16_t)(angle + 0x0080)));
        Sum step = { false, false };
        set_field16(s, slot, OBJ_Y, add16(&step, angle, 4));
        c->db = outer_bank;
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }

    slot = object(s);
    m_ldx(s, slot);
    set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
    set_field8(s, slot, OBJ_Y, 0);
    set_field8(s, slot, (uint8_t)(OBJ_Y + 1), 0);
    m_write8(s, GATE_MATRIX_TABLES, 0xF0);
    m_write8(s, GATE_MATRIX_TABLES + 0x0D, 0xF0);
    m_write8(s, GATE_MATRIX_TABLES + 3, 0xF0);
    m_write8(s, GATE_MATRIX_TABLES + 0x10, 0xF0);
    m_write8(s, GATE_MATRIX_TABLES + 6, 0x00);
    m_write8(s, GATE_MATRIX_TABLES + 0x13, 0x00);
    {
        Platform *pf = m_platform(s);
        static const VidLineEffect matrix_a = { .target = VID_LINE_MODE7_A,
                                                .table = PF_GAME(GATE_MATRIX_TABLES),
                                                .indirect = true, .indirect_bank = 0x7E };
        static const VidLineEffect matrix_b = { .target = VID_LINE_MODE7_B,
                                                .table = PF_GAME(GATE_MATRIX_TABLES + 0x0D),
                                                .indirect = true, .indirect_bank = 0x7E };
        /* A and D take the cosines, B and C the sines, so each pair shares a
         * table. */
        static const VidLineEffect matrix_c = { .target = VID_LINE_MODE7_C,
                                                .table = PF_GAME(GATE_MATRIX_TABLES + 0x0D),
                                                .indirect = true, .indirect_bank = 0x7E };
        static const VidLineEffect matrix_d = { .target = VID_LINE_MODE7_D,
                                                .table = PF_GAME(GATE_MATRIX_TABLES),
                                                .indirect = true, .indirect_bank = 0x7E };
        /* The original programs the four channels a step at a time, in pairs. */
        static const uint8_t control_only[] = { VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_END };
        static const uint8_t table_only[] = { VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_END };
        static const uint8_t source_bank[] = { VID_STEP_SOURCE_BANK, VID_STEP_END };
        static const uint8_t indirect_bank[] = { VID_STEP_INDIRECT_BANK, VID_STEP_END };
        vid_set_line_effect(pf, PF_AT(0xC26FBB), MATRIX_A_SLOT, &matrix_a, control_only);
        vid_set_line_effect(pf, PF_AT(0xC26FC1), MATRIX_D_SLOT, &matrix_d, control_only);
        vid_set_line_effect(pf, PF_AT(0xC26FC7), MATRIX_A_SLOT, &matrix_a, table_only);
        vid_set_line_effect(pf, PF_AT(0xC26FCA), MATRIX_D_SLOT, &matrix_d, table_only);
        vid_set_line_effect(pf, PF_AT(0xC26FCF), MATRIX_A_SLOT, &matrix_a, source_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FD2), MATRIX_A_SLOT, &matrix_a, indirect_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FD5), MATRIX_D_SLOT, &matrix_d, source_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FD8), MATRIX_D_SLOT, &matrix_d, indirect_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FDE), MATRIX_B_SLOT, &matrix_b, control_only);
        vid_set_line_effect(pf, PF_AT(0xC26FE4), MATRIX_C_SLOT, &matrix_c, control_only);
        vid_set_line_effect(pf, PF_AT(0xC26FEA), MATRIX_B_SLOT, &matrix_b, table_only);
        vid_set_line_effect(pf, PF_AT(0xC26FED), MATRIX_C_SLOT, &matrix_c, table_only);
        vid_set_line_effect(pf, PF_AT(0xC26FF2), MATRIX_B_SLOT, &matrix_b, source_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FF5), MATRIX_B_SLOT, &matrix_b, indirect_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FF8), MATRIX_C_SLOT, &matrix_c, source_bank);
        vid_set_line_effect(pf, PF_AT(0xC26FFB), MATRIX_C_SLOT, &matrix_c, indirect_bank);
    }
    m_lda8(s, MATRIX_SLOT_BITS);
    m_tsb8(s, m_dp(s, LINE_EFFECTS));
    uint8_t flags = m_read8(s, RAM_MENU_GATE_FLAGS);
    m_write8(s, RAM_MENU_GATE_FLAGS, (uint8_t)(flags & ~2u));
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 2 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_COS_A);
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 3 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_COS_A + LINE_HALF);
    set_dp16(s, CELL_PTR1, MATRIX_COS_A);
    flags = m_read8(s, RAM_MENU_GATE_FLAGS);
    m_write8(s, RAM_MENU_GATE_FLAGS, (uint8_t)(flags & ~4u));
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 4 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_SIN_A);
    m_write16(s, RAM_MENU_OBJECT_HDMA_POINTERS + 5 * RAM_MENU_OBJECT_HDMA_POINTERS_STRIDE, MATRIX_SIN_A + LINE_HALF);
    set_dp16(s, CELL_PTR2, MATRIX_SIN_A);

    uint8_t outer_bank = c->db;
    c->db = 0x7E;
    m_rep(s, CPU_FLAG_M);
    slot = object(s);
    m_ldx(s, slot);
    set_dp16(s, CELL_A, field16(s, slot, OBJ_Y));
    set_dp16(s, CELL_LOW, MATRIX_LINES);
    for (int line = 0; line < MATRIX_LINES; line++) {
        uint16_t angle = dp16(s, CELL_A);
        Sum f = { false, false };
        m_ldy(s, dp16(s, CELL_PTR1));
        m_write16(s, m_abs(s, dp16(s, CELL_PTR1)), cosine(s, angle));
        set_dp16(s, CELL_PTR1, (uint16_t)(dp16(s, CELL_PTR1) + 2));
        m_ldy(s, dp16(s, CELL_PTR2));
        m_write16(s, m_abs(s, dp16(s, CELL_PTR2)), sine(s, add16(&f, angle, 0x0080)));
        set_dp16(s, CELL_PTR2, (uint16_t)(dp16(s, CELL_PTR2) + 2));
        f.carry = false;
        set_dp16(s, CELL_A, add16(&f, angle, 4));
        set_dp16(s, CELL_LOW, (uint16_t)(MATRIX_LINES - line - 1));
    }
    slot = object(s);
    m_ldx(s, slot);
    m_lda16(s, dp16(s, CELL_A));
    set_field16(s, slot, OBJ_Y, dp16(s, CELL_A));
    m_sep(s, CPU_FLAG_M);
    c->db = outer_bank;
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:7143
/* One of the seven digit objects on the dial: it shows its period's digits
 * while the dial stands at it and hides them when it moves on. */
void menu_epoch_time_dial_digit(Snes *s)
{
    uint16_t slot;
    uint8_t step = dispatch_step(s);
    if (step == 0) {
        slot = object(s);
        m_ldx(s, slot);
        m_txy(s);
        set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
        m_set_a8(s, 0xB0);
        set_field8(s, slot, OBJ_SPRITE, 0xB0);
        set_field8(s, slot, OBJ_FRACTION, 0);
        set_field8(s, slot, (uint8_t)(OBJ_FRACTION + 1), 0);
        uint8_t index = m_asl8(s, field8(s, slot, OBJ_PARAMETER));
        m_ldx(s, index);
        m_lda8(s, digit_x(s, index));
        set_field8(s, slot, OBJ_X, digit_x(s, index));
        m_lda8(s, digit_y(s, index));
        set_field8(s, slot, OBJ_Y, digit_y(s, index));
        m_tyx(s);
        set_field8(s, slot, (uint8_t)(OBJ_X + 1), 0);
        set_field8(s, slot, (uint8_t)(OBJ_Y + 1), 0);
        step = 1;
    }
    if (step == 1) {
        slot = object(s);
        uint8_t period = m_read8(s, RAM_MENU_TIME_PERIOD);
        m_ldx(s, slot);
        m_set_a8(s, period);
        m_cmp8(s, period, field8(s, slot, OBJ_PARAMETER));
        if (period == field8(s, slot, OBJ_PARAMETER)) {
            set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
            uint8_t index = m_asl8(s, field8(s, slot, OBJ_PARAMETER));
            m_ldx(s, index);
            m_lda8(s, digit_show_script(s, index));
            m_jsr_dc(s, 0xC27197, 0xC21CDA, DC_Y | DC_N | DC_V | DC_Z | DC_C);
            m_jsr(s, 0xC2719A, 0xC20E1D);
        }
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    if (step == 2) {
        slot = object(s);
        m_ldx(s, slot);
        m_lda8(s, field8(s, slot, OBJ_SCRIPT_STATE));
        if ((uint8_t)s->cpu.a == 0) {
            uint8_t next = (uint8_t)(field8(s, slot, OBJ_STEP) + 1);
            set_field8(s, slot, OBJ_STEP, next);
            m_set_nz8(s, next);
        }
        m_jsr(s, 0xC271A9, 0xC20E1D);
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    if (step == 3) {
        slot = object(s);
        uint8_t period = m_read8(s, RAM_MENU_TIME_PERIOD);
        m_ldx(s, slot);
        m_set_a8(s, period);
        m_cmp8(s, period, field8(s, slot, OBJ_PARAMETER));
        if (period != field8(s, slot, OBJ_PARAMETER)) {
            set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
            uint8_t index = m_asl8(s, field8(s, slot, OBJ_PARAMETER));
            m_ldx(s, index);
            m_lda8(s, digit_hide_script(s, index));
            m_jsr_dc(s, 0xC271C4, 0xC21CDA, DC_Y | DC_N | DC_V | DC_Z | DC_C);
        }
        m_jsr(s, 0xC271C7, 0xC20E1D);
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    slot = object(s);
    m_ldx(s, slot);
    m_lda8(s, field8(s, slot, OBJ_SCRIPT_STATE));
    if ((uint8_t)s->cpu.a == 0) {
        m_lda8(s, 1);
        set_field8(s, slot, OBJ_STEP, 1);
    }
    m_jsr(s, 0xC271D8, 0xC20E1D);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:71F9
/* Decompress one of the gate screen's streams into $7F, by the index at $08 and
 * the bank the caller passes in A. */
void menu_epoch_load_gate_stream(Snes *s)
{
    Cpu *c = &s->cpu;
    m_write8(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_SOURCE) + 2), (uint8_t)c->a);
    uint16_t index = (uint16_t)((dp16(s, CELL_A) & 0x00FF) << 2);
    m_write16(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_SOURCE)), gate_stream_source(s, index));
    m_write16(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_DEST)), gate_stream_dest(s, index));
    m_write8(s, m_abs(s, RAM_ABS(RAM_DECOMPRESS_DEST) + 2), 0x7F);
    m_lda8(s, 0x7F);
    m_jsl_dc(s, 0xC2721B, 0xC30002, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_rts(s);
}

// $C2:722C
/* The gate's plane pixels: $800 bytes of $7F:9000 into the high half of the
 * plane's first words. */
void menu_epoch_upload_gate_pixels(Snes *s)
{
    vid_upload_vram_half(m_platform(s), PF_AT(0xC2722F), 0x0000, PF_GAME(0x7F0000 | MODE7_PLANE), 0x0800,
                         VID_VRAM_HIGH_BYTES, &pixel_upload);
    m_rts(s);
}

// $C2:7254
/* The gate's tiles: sixteen VRAM rows, each filled from $7F:9000 in 32 pieces
 * of $40 bytes. */
void menu_epoch_upload_gate_tiles(Snes *s)
{
    set_dp16(s, CELL_C, 0);
    for (uint16_t row = 0; row < 0x10; row++) {
        set_dp16(s, CELL_A, gate_tile_row(s, dp16(s, CELL_C)));
        set_dp16(s, CELL_B, MODE7_PLANE);
        set_dp8(s, CELL_LOW, 0x20);
        for (int piece = 0; piece < 0x20; piece++) {
            vid_upload_vram(m_platform(s), PF_AT(0xC2726D), dp16(s, CELL_A),
                            PF_GAME(0x7F0000 | dp16(s, CELL_B)), 0x0040, &word_upload);
            Sum f = { false, false };
            set_dp16(s, CELL_A, add16(&f, dp16(s, CELL_A), 0x0080));
            f.carry = false;
            set_dp16(s, CELL_B, add16(&f, dp16(s, CELL_B), 0x0040));
            set_dp8(s, CELL_LOW, (uint8_t)(0x20 - piece - 1));
        }
        set_dp16(s, CELL_C, (uint16_t)(2 * (row + 1)));
    }
    m_rts(s);
}

// $C2:72D8
/* The gate's tilemap rows: for each of the $00 rows, the byte at the long
 * pointer $10 picks a source row in the bank at $15, which goes to VRAM $08. */
void menu_epoch_upload_gate_rows(Snes *s)
{
    /* The count is taken from $00 and stepped down at the end of each row, so a
     * count of 0 goes all the way round, as the original does. */
    uint16_t rows = dp16(s, CELL_LOW);
    do {
        uint32_t table = (uint32_t)dp8(s, (uint8_t)(CELL_PTR1 + 2)) << 16 | dp16(s, CELL_PTR1);
        uint16_t pick = (uint16_t)(m_read16(s, table) & 0x00FF);
        set_dp16(s, CELL_PTR1, (uint16_t)(dp16(s, CELL_PTR1) + 1));
        uint16_t swapped = (uint16_t)(pick >> 8 | pick << 8);
        Sum f = { false, false };
        for (int j = 0; j < 3; j++) {
            f.carry = (swapped & 1) != 0;
            swapped = (uint16_t)(swapped >> 1);
        }
        set_dp16(s, CELL_B, add16(&f, swapped, dp16(s, CELL_PTR2)));
        /* STX $4370 : STA $2115 : STX $2116 : STX $4372 : STA $4374 :
         * STX $4375 : STA $420B -- this one sets the VRAM port before the
         * address, where the rest of the screen's uploads do it the other way
         * round, and the port decides what the address latches. */
        static const uint8_t steps[] = {
            VID_STEP_CONTROL,   VID_STEP_DEST,      VID_STEP_VRAM_PORT, VID_STEP_TARGET_LO,
            VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
            VID_STEP_SIZE_LO,   VID_STEP_SIZE_HI,   VID_STEP_START,     VID_STEP_END,
        };
        static const VidSnesXfer x = { MENU_DMA, DMA_WORDS, steps, &high_port };
        vid_upload_vram(m_platform(s), PF_AT(0xC272EE), dp16(s, CELL_A),
                        PF_GAME((uint32_t)dp8(s, (uint8_t)(CELL_PTR2 + 2)) << 16 | dp16(s, CELL_B)),
                        0x0020, &x);
        f.carry = false;
        set_dp16(s, CELL_A, add16(&f, dp16(s, CELL_A), 0x0010));
        rows = (uint16_t)(rows - 1);
        set_dp16(s, CELL_LOW, rows);
    } while (rows != 0);
    m_rts(s);
}

// $C2:7361
/* Expand the gate plane's packed colours into $7F:9000, a nibble per pixel. */
void menu_epoch_expand_gate_nibbles(Snes *s)
{
    m_tdc(s);
    m_write16(s, m_abs(s, WRAM_ADDR), MODE7_PLANE);
    m_write8(s, m_abs(s, WRAM_ADDR_BANK), 0x7F);
    for (uint16_t i = 0; i < 0x0400; i++) {
        uint8_t pair = gate_plane_nibbles(s, i);
        m_write8(s, m_abs(s, WRAM_PORT), (uint8_t)(pair >> 4));
        m_write8(s, m_abs(s, WRAM_PORT), (uint8_t)(pair & 0x0F));
    }
    m_rts(s);
}

// $C2:7389
/* Build everything the time dial shows: the gate's plane and tiles, its
 * tilemap, and the objects that drive it -- the dial itself, the matrix, two
 * scripts and the seven digits. The original ends by jumping to the object
 * starter, so the port hands back at that jump. */
void menu_epoch_gate_graphics(Snes *s)
{
    Cpu *c = &s->cpu;
    set_dp8(s, CELL_A, 0);
    m_set_a8(s, 0xC3);
    m_jsr_dc(s, 0xC2738D, 0xC271F9, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    vid_upload_vram(m_platform(s), PF_AT(0xC27393), 0x6400, PF_GAME(0x7F0000 | GATE_TILES), 0x0800,
                    &word_upload);
    set_dp16(s, CELL_A, 0x6000);
    set_dp16(s, CELL_PTR1, GATE_TILE_ROWS);
    set_dp8(s, (uint8_t)(CELL_PTR1 + 2), 0xC2);
    set_dp16(s, CELL_PTR2, MODE7_PLANE);
    set_dp8(s, (uint8_t)(CELL_PTR2 + 2), 0x7F);
    set_dp16(s, CELL_LOW, 0x0040);
    m_jsr_dc(s, 0xC273D3, 0xC272D8, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    set_dp8(s, CELL_A, 1);
    m_set_a8(s, 0xC6);
    m_jsr_dc(s, 0xC273DC, 0xC271F9, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    uint8_t outer_bank = c->db;
    m_ldx(s, GATE_PALETTE);
    m_ldy(s, GATE_PALETTE_COPY);
    c->a = 0x01FF;
    m_mvn(s, 0x7E, 0x7F);
    m_write16(s, 0x7F0000 | GATE_PALETTE_WHITE, 0xFFFF);
    m_ldx(s, GATE_PALETTE);
    m_ldy(s, OBJECT_PALETTE);
    c->a = 0x01FF;
    m_mvn(s, 0x00, 0x7F);
    c->db = outer_bank;
    set_dp8(s, CELL_A, (uint8_t)(dp8(s, CELL_A) + 1));
    m_set_a8(s, 0xC6);
    m_jsr_dc(s, 0xC27408, 0xC271F9, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC2740B, 0xC27254, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC2740E, 0xC27361, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC27411, 0xC2722C, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_ldx(s, 0x6AC8);
    m_jsr_dc(s, 0xC27417, 0xC2048A, DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_set_a8(s, 0xC2);
    m_ldx(s, 0x74B8);
    m_jsr_dc(s, 0xC2741F, 0xC204D9, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_set_a8(s, 0xC2);
    m_ldx(s, 0x7504);
    m_jsr_dc(s, 0xC27427, 0xC204D9, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_ldx(s, 0x6F85);
    m_jsr_dc(s, 0xC2742D, 0xC2048A, DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    uint16_t slot = object(s);
    set_dp8(s, CELL_B, field8(s, slot, OBJ_PARAMETER));
    set_field8(s, slot, OBJ_PARAMETER, 0);
    m_lda8(s, 7);
    set_dp8(s, (uint8_t)(CELL_LOW + 1), 7);
    for (int i = 0; i < 7; i++) {
        m_ldx(s, 0x7143);
        m_jsr_dc(s, 0xC27441, 0xC2048A, DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C);
        slot = object(s);
        set_field8(s, slot, OBJ_PARAMETER, (uint8_t)(field8(s, slot, OBJ_PARAMETER) + 1));
        set_dp8(s, (uint8_t)(CELL_LOW + 1), (uint8_t)(6 - i));
    }
    m_ldx(s, slot);
    set_field8(s, slot, OBJ_PARAMETER, dp8(s, CELL_B));
    m_set_a8(s, 0xC2);
    m_ldx(s, 0x74AE);
    m_stop_at_own(s, 0xC27457, DC_Y | DC_N | DC_V | DC_Z | DC_C);
}

// $C2:6A34
/* The time dial: Mode 7 with the identity matrix behind sprites at $6000, the
 * gate built and its objects started, and the frame task run until the mode
 * byte leaves the dial. */
void menu_epoch_time_dial_screen(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    m_jsr_dc(s, 0xC26A34, 0xC226A8, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC26A37, 0xC203EF, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_jsr_dc(s, 0xC26A3A, 0xC20471, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    set_dp8(s, SHADOW_BG_MODE, 0x07);
    VidObjConfig objects = vid_obj_config_decode(0x03);
    vid_set_obj_config(pf, PF_AT(0xC26A43), &objects);
    set_dp16(s, SHADOW_SCROLL_X, 0xFF80);
    set_dp16(s, SHADOW_SCROLL_Y, 0xFF90);
    set_dp16(s, (uint8_t)(SHADOW_SCROLL_X + 2), 0);
    set_dp16(s, (uint8_t)(SHADOW_SCROLL_Y + 2), 0);
    set_dp16(s, SHADOW_BG3_X, 0);
    set_dp16(s, SHADOW_BG3_Y, 0);
    set_dp16(s, (uint8_t)(SHADOW_MATRIX + 2), 0);
    set_dp16(s, (uint8_t)(SHADOW_MATRIX + 4), 0);
    set_dp16(s, SHADOW_CENTER, 0);
    set_dp16(s, (uint8_t)(SHADOW_CENTER + 2), 0);
    set_dp8(s, (uint8_t)(SHADOW_MATRIX + 1), 1);
    set_dp8(s, (uint8_t)(SHADOW_MATRIX + 7), 1);
    set_dp8(s, SHADOW_WINDOWS, 0);
    set_dp8(s, (uint8_t)(SHADOW_WINDOWS + 1), 0);
    set_dp8(s, (uint8_t)(SHADOW_WINDOWS + 2), 0x20);
    set_dp8(s, SHADOW_MAIN_WINDOW_MASK, 0x11);
    set_dp8(s, SHADOW_SUB_WINDOW_MASK, 0x11);
    set_dp8(s, SHADOW_MATH_CONTROL, 0x10);
    set_dp8(s, SHADOW_MATH_LAYERS, 0x17);
    m_lda8(s, 0x0F);
    set_dp8(s, SHADOW_FIXED_BLUE, 0x0F);
    set_dp8(s, SHADOW_FIXED_RED, 0x0F);
    set_dp8(s, SHADOW_FIXED_GREEN, 0x0F);
    m_write8(s, RAM_MENU_GATE_FLAGS, 0);
    m_jsr_dc(s, 0xC26A8A, 0xC27389, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_lda8(s, 0x03);
    m_tsb8(s, m_dp(s, FRAME_STATE));
    m_lda8(s, 0x81);
    tm_enable_interrupts(pf, PF_AT(0xC26A93), TM_INT_VBLANK | TM_INT_AUTO_JOYPAD);
    m_ldx(s, 0x0012);
    m_jsr_dc(s, 0xC26A99, 0xC20454, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    do {
        m_jsr_dc(s, 0xC26A9C, 0xC2046E, DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
        m_set_a8(s, m_read8(s, m_abs(s, RAM_ABS(RAM_MENU_MODE))));
        m_cmp8(s, (uint8_t)c->a, 0x08);
    } while ((uint8_t)c->a == 0x08);
    m_set_a8(s, 0xC2);
    m_ldx(s, 0x74B3);
    m_jsr_dc(s, 0xC26AAB, 0xC204D9, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_ldx(s, 0x0012);
    m_jsr_dc(s, 0xC26AB1, 0xC20454, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    set_dp8(s, SHADOW_BG_MODE, 0x09);
    VidObjConfig back = vid_obj_config_decode(0x60);
    vid_set_obj_config(pf, PF_AT(0xC26ABA), &back);
    m_tdc(s);
    tm_enable_interrupts(pf, PF_AT(0xC26ABE), 0);
    vid_stop_uploads(pf, PF_AT(0xC26AC1));
    vid_enable_line_effects(pf, PF_AT(0xC26AC4), 0);
    m_rts(s);
}

/* ---- the objects that move the screens' backgrounds -------------------------------------- */

/* Both drifts end by taking in whatever scroll the menu code left pending. */
static void take_pending_scroll(Snes *s)
{
    Sum f = { false, false };
    set_dp16(s, SHADOW_BG3_X, add16(&f, dp16(s, SHADOW_BG3_X), m_read16(s, RAM_MENU_PENDING_SCROLL_X)));
    f.carry = false;
    set_dp16(s, SHADOW_BG3_Y, add16(&f, dp16(s, SHADOW_BG3_Y), m_read16(s, RAM_MENU_PENDING_SCROLL_Y)));
}

// $C2:7652
/* The gate screen's BG3 wave: a sine over the scroll, rebuilt every frame into
 * a circular buffer that the per-line effect reads. Hidden while the screen is
 * blanked, and still while the objects are frozen. */
void menu_epoch_gate_scroll_hdma(Snes *s)
{
    Cpu *c = &s->cpu;
    if (dp8(s, DISPLAY) & 0x80) {
        m_lda8(s, EFFECT_SLOT_BIT);
        m_trb8(s, m_dp(s, LINE_EFFECTS));
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    if (m_read8(s, m_abs(s, OBJECTS_FROZEN)) & 0x10) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    uint16_t slot = c->x;
    if (field8(s, slot, OBJ_STEP) == 0) {
        set_field8(s, slot, OBJ_STEP, (uint8_t)(field8(s, slot, OBJ_STEP) + 1));
        set_field8(s, slot, OBJ_PARAMETER, 0);
        set_field8(s, slot, (uint8_t)(OBJ_PARAMETER + 1), 0);
        m_write16(s, RAM_MENU_GATE_SCROLL_AT, RAM_ABS(RAM_MENU_GATE_SCROLL));
    }
    m_write8(s, RAM_MENU_GATE_SCROLL_TABLE, 0xC0);
    m_write8(s, RAM_MENU_GATE_SCROLL_TABLE + 3, 0xC0);
    m_write8(s, RAM_MENU_GATE_SCROLL_TABLE + 6, 0xC0);
    m_write8(s, RAM_MENU_GATE_SCROLL_TABLE + 9, 0xA0);
    m_write8(s, RAM_MENU_GATE_SCROLL_TABLE + 12, 0x00);
    static const VidLineEffect wave = { .target = VID_LINE_SCROLL_BG3_H,
                                        .table = PF_GAME(RAM_MENU_GATE_SCROLL_TABLE),
                                        .indirect = true, .indirect_bank = 0x7E };
    vid_set_line_effect(m_platform(s), PF_AT(0xC2769A), EFFECT_SLOT, &wave, line_effect_steps);
    m_lda8(s, EFFECT_SLOT_BIT);
    m_tsb8(s, m_dp(s, LINE_EFFECTS));
    set_dp16(s, CELL_PTR1, RAM_ABS(RAM_MENU_GATE_SCROLL));
    set_dp8(s, (uint8_t)(CELL_PTR1 + 2), 0x7E);
    m_rep(s, CPU_FLAG_M);
    uint16_t phase = field16(s, slot, OBJ_PARAMETER);
    set_dp16(s, CELL_B, phase);
    Sum f = { false, false };
    set_field16(s, slot, OBJ_PARAMETER, add16(&f, phase, 1));
    for (uint16_t i = 0; i < 0x40; i++) {
        uint16_t v = sine(s, dp16(s, CELL_B));
        set_dp16(s, CELL_A, v);
        f.carry = false;
        set_dp16(s, CELL_B, add16(&f, dp16(s, CELL_B), 0x0010));
        uint16_t scaled = (v & 0x8000) ? (uint16_t)((v >> 2) | 0xC000) : (uint16_t)(v >> 2);
        f.carry = false;
        m_write16(s, RAM_MENU_GATE_SCROLL + 2 * i, add16(&f, scaled, dp16(s, SHADOW_BG3_X)));
    }
    uint8_t outer_bank = c->db;
    m_ldx(s, RAM_ABS(RAM_MENU_GATE_SCROLL));
    m_ldy(s, (uint16_t)(RAM_ABS(RAM_MENU_GATE_SCROLL) + 0x80));
    c->a = 0x007F;
    m_mvn(s, 0x7E, 0x7E);
    c->db = outer_bank;
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:7702
/* The object that drifts the gate screen's background by a velocity of its
 * own. */
void menu_epoch_gate_drift(Snes *s)
{
    if (m_read8(s, m_abs(s, OBJECTS_FROZEN)) & 0x10) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    uint16_t slot = object(s);
    m_ldx(s, slot);
    uint8_t step = field8(s, slot, OBJ_STEP);
    m_set_a8(s, step);
    m_rep(s, CPU_FLAG_M);
    if (step == 0) {
        set_field16(s, slot, OBJ_STEP, (uint16_t)(field16(s, slot, OBJ_STEP) + 1));
        set_field16(s, slot, OBJ_VELOCITY, 0x8931);
        set_field16(s, slot, (uint8_t)(OBJ_VELOCITY + 2), 0xFFFC);
        set_field16(s, slot, (uint8_t)(OBJ_VELOCITY + 4), 0);
        set_field16(s, slot, (uint8_t)(OBJ_VELOCITY + 6), 2);
    }
    set_field16(s, slot, OBJ_X, dp16(s, SHADOW_BG3_X));
    set_field16(s, slot, OBJ_Y, dp16(s, SHADOW_BG3_Y));
    m_ldx(s, slot);
    m_jsr_dc(s, 0xC27734, 0xC20F38, DC_A | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    slot = s->cpu.x;
    set_dp16(s, SHADOW_BG3_X, field16(s, slot, OBJ_X));
    set_dp16(s, SHADOW_BG3_Y, field16(s, slot, OBJ_Y));
    take_pending_scroll(s);
    m_write16(s, RAM_MENU_PENDING_SCROLL_X, 0);
    m_write16(s, RAM_MENU_PENDING_SCROLL_Y, 0);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:75C3
/* A slow drift of BG3: one step left every fourth frame and one down every
 * eighth. */
void menu_epoch_gate_scroll_slow(Snes *s)
{
    if (m_read8(s, m_abs(s, OBJECTS_FROZEN)) & 0x10) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    m_rep(s, CPU_FLAG_M);
    uint16_t slot = object(s);
    m_ldx(s, slot);
    uint16_t count = (uint16_t)(field16(s, slot, OBJ_PARAMETER) + 1);
    set_field16(s, slot, OBJ_PARAMETER, count);
    if ((count & 3) == 0)
        set_dp16(s, SHADOW_BG3_X, (uint16_t)(dp16(s, SHADOW_BG3_X) - 1));
    if ((count & 7) == 0)
        set_dp16(s, SHADOW_BG3_Y, (uint16_t)(dp16(s, SHADOW_BG3_Y) + 1));
    take_pending_scroll(s);
    m_write16(s, RAM_MENU_PENDING_SCROLL_X, 0);
    m_write16(s, RAM_MENU_PENDING_SCROLL_Y, 0);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

// $C2:75FD
/* The same drift with the wave's buffer moved along too, kept inside the
 * buffer's two halves so it wraps rather than running off. */
void menu_epoch_gate_scroll_wrap(Snes *s)
{
    if (m_read8(s, m_abs(s, OBJECTS_FROZEN)) & 0x10) {
        m_set_flag(s, CPU_FLAG_C, false);
        m_rts(s);
        return;
    }
    m_rep(s, CPU_FLAG_M);
    uint16_t slot = object(s);
    m_ldx(s, slot);
    uint16_t count = (uint16_t)(field16(s, slot, OBJ_PARAMETER) + 1);
    set_field16(s, slot, OBJ_PARAMETER, count);
    if ((count & 3) == 0) {
        set_dp16(s, SHADOW_BG3_X, (uint16_t)(dp16(s, SHADOW_BG3_X) - 1));
        set_dp16(s, SHADOW_BG3_Y, (uint16_t)(dp16(s, SHADOW_BG3_Y) + 1));
    }
    take_pending_scroll(s);
    uint16_t pending = (uint16_t)(m_read16(s, RAM_MENU_PENDING_SCROLL_Y) << 1);
    m_write16(s, RAM_MENU_PENDING_SCROLL_Y, pending);
    Sum f = { false, false };
    uint16_t at = add16(&f, m_read16(s, RAM_MENU_GATE_SCROLL_AT), pending);
    if (at < RAM_ABS(RAM_MENU_GATE_SCROLL)) {
        f.carry = false;
        at = add16(&f, at, 0x0080);
    } else if (at >= (uint16_t)(RAM_ABS(RAM_MENU_GATE_SCROLL) + 0x80)) {
        f.carry = true;
        at = sub16(&f, at, 0x0080);
    }
    m_write16(s, RAM_MENU_GATE_SCROLL_AT, at);
    m_write16(s, RAM_MENU_PENDING_SCROLL_X, 0);
    m_write16(s, RAM_MENU_PENDING_SCROLL_Y, 0);
    m_set_flag(s, CPU_FLAG_C, false);
    m_rts(s);
}

/* ---- object script routines the screens' scripts call ($C2:1819) ------------------------ */

// $C2:754D
/* Place the object at a random spot in the map view's visible area. */
void menu_epoch_place_marker_random(Snes *s)
{
    uint8_t r = next_random(s);
    set_dp8(s, CELL_A, (uint8_t)(r & 0x7F));
    set_dp8(s, (uint8_t)(CELL_A + 1), 0);
    uint16_t slot = object(s);
    m_ldx(s, slot);
    m_rep(s, CPU_FLAG_M);
    Sum f = { false, false };
    uint16_t x = add16(&f, asl16(&f, dp16(s, MAP_ORIGIN_X), 3), dp16(s, CELL_A));
    f.carry = false;
    set_field16(s, slot, OBJ_X, add16(&f, x, 0x0040));
    f.carry = false;
    set_field16(s, slot, OBJ_Y, asl16(&f, add16(&f, dp16(s, MAP_ORIGIN_Y), 0x0020), 3));
    m_set_flag(s, CPU_FLAG_C, f.carry);
    m_rts(s);
}

// $C2:7575
/* Place the object at the right-hand edge of the map view's area. */
void menu_epoch_place_marker_edge(Snes *s)
{
    uint8_t r = next_random(s);
    set_dp8(s, CELL_A, r);
    set_dp8(s, (uint8_t)(CELL_A + 1), 0);
    uint16_t slot = object(s);
    m_ldx(s, slot);
    m_rep(s, CPU_FLAG_M);
    Sum f = { false, false };
    set_field16(s, slot, OBJ_X, add16(&f, asl16(&f, 0x002F, 3), dp16(s, CELL_A)));
    f.carry = false;
    set_field16(s, slot, OBJ_Y, asl16(&f, add16(&f, dp16(s, MAP_ORIGIN_Y), 0x0020), 3));
    m_set_flag(s, CPU_FLAG_C, f.carry);
    m_rts(s);
}

// $C2:7598
/* Give the object one of sixteen velocities, picked at random. */
void menu_epoch_set_marker_velocity(Snes *s)
{
    uint8_t r = next_random(s);
    uint16_t slot = object(s);
    m_ldy(s, slot);
    m_rep(s, CPU_FLAG_M);
    Sum f = { false, false };
    uint16_t index = asl16(&f, (uint16_t)(r & 0x000F), 3);
    m_ldx(s, index);
    m_set_flag(s, CPU_FLAG_C, f.carry);
    for (uint16_t word = 0; word < 4; word++)
        set_field16(s, slot, (uint8_t)(OBJ_VELOCITY + 2 * word), marker_velocity(s, index, word));
    m_rts(s);
}

/* ---- the End of Time's gate --------------------------------------------------------------- */

// $C2:CE9D
/* Start the End of Time's gate screen: BG3's tilemap moves to $6800 in 32 by 32
 * quadrants, the screen's frame task is added, and its eleven bytes of setup are
 * copied out of the ROM. */
void menu_start_gate_screen(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->p);
    m_sep(s, CPU_FLAG_M);
    VidBg bg3;
    vid_bg_map_decode(&bg3, 0x68);
    vid_set_bg_map(m_platform(s), PF_AT(0xC2CEA2), VID_BG3, &bg3);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_write16(s, m_abs(s, RAM_ABS(RAM_MENU_GATE_SCREEN_STATE)), 0);
    m_lda16(s, 0xCECE);
    m_ldx(s, 0x0010);
    m_jsr_dc(s, 0xC2CEB1, 0xC28249, DC_Y | DC_N | DC_V | DC_Z | DC_C);
    m_ldx(s, 0xD57A);
    m_ldy(s, (uint16_t)(RAM_ABS(RAM_MENU_GATE_SCREEN_STATE) + 2));
    c->a = 0x000B;
    m_mvn(s, 0x7E, 0xFF);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    m_rts(s);
}
