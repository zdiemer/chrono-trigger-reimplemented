/* Scenes: the Mode 7 plane seen in perspective.
 *
 * The jet bike race's track and the endings' ground are a Mode 7 plane under
 * a camera, and what makes it recede is a per-line table: for every screen line
 * from the horizon down, the matrix that shows the plane at that line's
 * distance. The camera's turn sets the matrix's direction, its height scales
 * it, and each line farther down the screen is nearer, a step at a time: the
 * line's matrix is the camera's cosine and sine times its height, over the
 * line's distance. Two tables hold it, the first two entries of a line's matrix
 * in one and the last two in the other, which the per-line effects send to the
 * PPU every frame.
 *
 * A channel's script sets the camera through its registers (scene_channel_
 * register), and the race's camera check (race_camera.c) or an ending asks for
 * the table again when they change. Where the lines start depends on the scene:
 * the program calls a routine of the scene's own there, which the scene's setup
 * writes into the call (the race's, from the horizon on, with the plane's top
 * at the horizon; the endings', from the line above it). The race can also ask
 * for the lines in pairs, each pair sharing one matrix and the distance stepping
 * two at a time, which the program does by writing a branch into its own loop.
 *
 * The program keeps the camera's products in its own code (the loop's
 * immediate operands), the table each sign goes to in its stores' operands, and
 * the loop's step and end as instructions; those that the scene sets are
 * variables in re/ram.toml, and those only the loop reads are left as it leaves
 * them, as scaffolding. */
#include "scene/race.h"

#include "cpu_io.h"
#include "original_effects.h"
#include "port/machine.h"
#include "port/vars_ram.h"
#include "vars.h"

/* The channel registers the perspective is built from (the race's camera
 * script sets them: race.h names them as the camera). */
enum {
    PLANE_HEIGHT = 0,        /* the camera's height, its low byte: the matrix's scale */
    PLANE_TURN = 5,          /* which way the camera faces, 0-$3FF around */
    PLANE_HORIZON = 6,       /* the first line under the horizon */
    PLANE_TOP = 8,           /* the distance of the first line: the larger, the farther the plane starts */
    PLANE_LINES_PAIRED = 11, /* nonzero while the race wants the lines in pairs */
};

static uint16_t plane_register(const GameState *g, SceneChannel ch, unsigned reg) { return scene_channel_register(g, ch, reg); }

/* The routines a scene puts at the program's call that starts the lines, by
 * their address, as the call holds it. */
typedef enum { START_RACE = 0x45B0, START_ENDING = 0x9B46 } PerspectiveStart;

/* The instructions the loop runs after each line, as words: one step farther
 * (the image's INX; NOP, or the race's NOP; INX), or the branch the race writes
 * to copy each line into the next (the word at scene_perspective_pair_branch). */
enum { STEP_IMAGE = 0xEAE8, STEP_RACE = 0xE8EA, STEP_PAIRS = 0x3680 };

/* Each line is four bytes in each table, and 224 lines end the screen. */
enum { LINE_BYTES = 4, SCREEN_LINES = 224, SCREEN_END = SCREEN_LINES * LINE_BYTES };

/* The page the loop moves the direct page to, the CPU's registers, to reach the
 * divider. */
enum { CPU_REGISTERS_PAGE = 0x4200 };

/* The first line's offset in the tables, 4 a line: the race's at the horizon
 * (a horizon of 0 counts as line 1), the endings' on the line above it. */
static uint16_t first_line(const GameState *g, SceneChannel ch)
{
    uint16_t horizon = plane_register(g, ch, PLANE_HORIZON);
    if (scene_perspective_start(g) == START_RACE)
        return (uint16_t)((horizon ? horizon : 1) * LINE_BYTES);
    return (uint16_t)((horizon - 1) * LINE_BYTES);
}

// $7E:45B0, $7E:9B46
/* The scene's routine at the start of the lines. The race's puts a horizon of
 * 0 on line 1 and picks whether the lines come in pairs; the endings' changes
 * nothing. Both start at the plane's top. */
static void start_lines(GameState *g, SceneChannel ch)
{
    if (scene_perspective_start(g) != START_RACE)
        return;
    if (plane_register(g, ch, PLANE_HORIZON) == 0)
        set_scene_channel_register(g, ch, PLANE_HORIZON, 1);
    bool paired = plane_register(g, ch, PLANE_LINES_PAIRED) != 0;
    set_scene_perspective_line_step(g, paired ? scene_perspective_pair_branch(g) : STEP_RACE);
}

/* The CPU's divider, which the original uses: a byte divisor, and $FFFF for 0. */
static uint16_t over_distance(uint16_t value, uint16_t distance)
{
    uint8_t by = (uint8_t)distance;
    return by ? (uint16_t)(value / by) : 0xFFFF;
}

static uint16_t magnitude(int16_t v) { return v < 0 ? (uint16_t)-v : (uint16_t)v; }

/* A line's matrix: its first two entries in one table, its last two in the
 * other; the first and last are the cosine's, the middle two the sine's. */
static void set_line(GameState *g, unsigned line, uint16_t along, uint16_t across)
{
    set_scene_plane_matrix_top_row_a(g, line, along);
    set_scene_plane_matrix_top_row_b(g, line, across);
    set_scene_plane_matrix_bottom_row_c(g, line, (uint16_t)-across);
    set_scene_plane_matrix_bottom_row_d(g, line, along);
}

/* What the camera turned and scaled gives each line: the cosine's and the
 * sine's magnitudes times the height, which every line divides by its
 * distance, and their signs. */
typedef struct {
    uint16_t along, across;
    bool along_negative, across_negative;
} Turned;

static Turned turned_camera(GameState *g, SceneChannel ch)
{
    SceneProgramPage page = scene_program_channel(ch);
    uint16_t turn = plane_register(g, ch, PLANE_TURN);
    uint8_t height = (uint8_t)plane_register(g, ch, PLANE_HEIGHT);
    int16_t cos = race_cosine(g, turn), sin = race_sine(g, turn);
    Turned t;
    t.along = (uint16_t)scene_multiply16(g, page, magnitude(cos), height);
    t.across = (uint16_t)scene_multiply16(g, page, magnitude(sin), height);
    t.along_negative = cos < 0;
    t.across_negative = sin < 0;
    return t;
}

/* The loop's operands and the divider as the original leaves them: the
 * products in its loads, the branch over the cosine's negation, the stores'
 * tables, and the divider dividing the sine's product by the last distance
 * (stored as a word, its high byte in the next register) with the cosine's
 * product loaded for a line after it. */
enum { BRANCH_INTO_NEGATION = 0x0080, BRANCH_PAST_NEGATION = 0x0480 };
enum { B_ENTRIES = 0xF0FE, C_ENTRIES = 0xF484 };

static void leave_loop_operands(GameState *g, const Turned *t, uint16_t last_distance)
{
    set_scene_perspective_along_product(g, t->along);
    set_scene_perspective_across_product(g, t->across);
    set_scene_perspective_cosine_sign(g, t->along_negative ? BRANCH_INTO_NEGATION : BRANCH_PAST_NEGATION);
    set_scene_perspective_across_entries(g, t->across_negative ? C_ENTRIES : B_ENTRIES);
    set_scene_perspective_against_entries(g, t->across_negative ? B_ENTRIES : C_ENTRIES);
    hw_set_dividend(g, t->across);
    hw_divide_by_word(g, last_distance);
    hw_set_dividend(g, t->along);
}

// $7E:357B
/* Build the table for the camera on channel `ch`: from the scene's first line
 * to the bottom of the screen, a line at a time or a pair at a time. */
void scene_build_perspective(GameState *g, SceneChannel ch)
{
    Turned t = turned_camera(g, ch);
    start_lines(g, ch);
    bool paired = scene_perspective_line_step(g) == STEP_PAIRS;
    unsigned lines_each = paired ? 2 : 1;
    uint16_t distance = plane_register(g, ch, PLANE_TOP), last_distance = distance;
    for (unsigned line = first_line(g, ch) / LINE_BYTES; line < scene_perspective_end(g) / LINE_BYTES; line += lines_each) {
        uint16_t along = over_distance(t.along, distance), across = over_distance(t.across, distance);
        along = t.along_negative ? (uint16_t)-along : along;
        across = t.across_negative ? (uint16_t)-across : across;
        for (unsigned i = 0; i < lines_each; i++)
            set_line(g, line + i, along, across);
        last_distance = distance;
        distance = (uint16_t)(distance + lines_each);
    }
    ORIGINAL_EFFECT(leave_loop_operands(g, &t, last_distance));
}

/* ---- Entry ----------------------------------------------------------------------------------- */

/* The program's perspective code, without the operands the program writes
 * (the call's, the loads', the branch's, the stores', the step and the end),
 * the copy that pairs the lines, and the sine and the multiply it calls. */
static bool perspective_code_present(Snes *s)
{
    return m_code_matches(s, 0x7E357B, 0x82, 0x034D2698BB682904ull) && m_code_matches(s, 0x7E35FF, 0x0C, 0xA4E8E26A45E46C41ull) &&
           m_code_matches(s, 0x7E360D, 0x09, 0x36B856197D3AE555ull) && m_code_matches(s, 0x7E3618, 0x0B, 0xAD5C2E09D467B7D5ull) &&
           m_code_matches(s, 0x7E3625, 0x06, 0x82137DEC2E4A159Cull) && m_code_matches(s, 0x7E362D, 0x05, 0x5893105BB749B77Bull) &&
           m_code_matches(s, 0x7E3636, 0x01, 0xAF647D4C8602FE1Full) && m_code_matches(s, 0x7E3639, 0x03, 0xE3ADF41C6920B8B2ull) &&
           m_code_matches(s, 0x7E366C, 0x1D, 0xFA950297A325CF91ull) && m_code_matches(s, 0x7E3461, 0x19, 0xA2486F18B991C163ull) &&
           m_code_matches(s, 0x7E31BF, 74, 0x479627EBF1311CAFull);
}

/* The scenes' first lines, the race's and the endings', and their code. */
static bool start_known(Snes *s, uint16_t start)
{
    if (start == START_RACE)
        return m_code_matches(s, 0x7E45B0, 0x26, 0xCF140E55949D99BFull);
    return start == START_ENDING && m_code_matches(s, 0x7E9B46, 0x09, 0x55C26FE176ECC36Cull);
}

/* The lines the port builds: a step it knows, ending at the bottom of the
 * screen, from a first line on it that the step reaches the end from (in
 * pairs, an even number of lines). Past these the original's loop runs on
 * through memory, or it was patched to another scene's step. */
static bool lines_known(const GameState *g, SceneChannel ch)
{
    uint16_t step = scene_perspective_line_step(g);
    bool paired = step == STEP_PAIRS;
    if (scene_perspective_start(g) == START_RACE) {
        paired = plane_register(g, ch, PLANE_LINES_PAIRED) != 0;
        if (paired && scene_perspective_pair_branch(g) != STEP_PAIRS)
            return false;
    } else if (step != STEP_IMAGE && step != STEP_RACE && step != STEP_PAIRS)
        return false;
    uint16_t first = first_line(g, ch);
    if (scene_perspective_end(g) != SCREEN_END || first >= SCREEN_END)
        return false;
    return !paired || (SCREEN_END - first) % (2 * LINE_BYTES) == 0;
}

/* Whether the port builds the perspective for the channel at `d`: on a
 * channel's page, the code as the port knows it, a scene's first lines it
 * knows. */
bool scene_perspective_as_ported(Snes *s, uint16_t d)
{
    SceneChannel ch;
    if (!scene_channel_at(d, &ch) || !perspective_code_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool known = start_known(s, scene_perspective_start(g)) && lines_known(g, ch);
    vars_peek(false);
    return known;
}

bool scene_build_perspective_present(Snes *s) { return scene_perspective_as_ported(s, m_direct_page(s)); }

/* The turn's sum in the sine leaves the overflow: a turn from $7F00 to $7FFF
 * passes $7FFF a quarter turn on. */
enum { QUARTER_TURN = 0x100 };

/* What the original leaves, from the state after: Y at the end of the tables,
 * X the distance after the last line's, A the last entry it stored (the sine's
 * negated, or the copy's last), the loop's last compare in the flags, the
 * direct page on the CPU's registers and the data bank its own. */
void scene_leave_perspective_registers(Snes *s, SceneChannel ch)
{
    GameState *g = state_of(s);
    unsigned last = SCREEN_LINES - 1;
    bool paired = scene_perspective_line_step(g) == STEP_PAIRS;
    bool sine_stored_last_in_b = !paired && scene_perspective_against_entries(g) == B_ENTRIES;
    s->cpu.a = sine_stored_last_in_b ? scene_plane_matrix_top_row_b(g, last) : scene_plane_matrix_bottom_row_c(g, last);
    s->cpu.x = (uint16_t)(plane_register(g, ch, PLANE_TOP) + (SCREEN_END - first_line(g, ch)) / LINE_BYTES);
    s->cpu.y = SCREEN_END;
    uint16_t turn = plane_register(g, ch, PLANE_TURN);
    m_set_flag(s, CPU_FLAG_V, ((turn ^ (uint16_t)(turn + QUARTER_TURN)) & ~(turn ^ QUARTER_TURN) & 0x8000) != 0);
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    m_set_flag(s, CPU_FLAG_C, true);
    s->cpu.d = CPU_REGISTERS_PAGE;
    s->cpu.db = SCENE_LOWER_WORK_RAM;
}

/* Entered by a jump from the race's camera check or an ending's, 16-bit, on
 * the channel's page. */
void scene_build_perspective_entry(Snes *s)
{
    SceneChannel ch = 0;
    scene_channel_at(m_direct_page(s), &ch);
    scene_build_perspective(state_of(s), ch);
    ORIGINAL_EFFECT(scene_leave_perspective_registers(s, ch));
    m_rts(s);
}
