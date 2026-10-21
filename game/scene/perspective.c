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
 * register), and the race's camera check (race_camera.c), an ending or the
 * Epoch's dive asks for the table again when they change. Where the lines
 * start, how far down they go and how the distance steps from one to the next
 * depend on the scene: the program calls a routine of the scene's own there,
 * which the scene's setup writes into the call, and the step is an
 * instruction of the loop a scene can write over. The race's starts from the
 * horizon on, with the plane's top at the horizon, and can ask for the lines
 * in pairs, each pair sharing one matrix and the distance stepping two at a
 * time; the endings' starts from the line above the horizon. The Epoch's dive
 * into Lavos has two: seen from outside, the shell curves away down the
 * screen, each line's distance from a curve the dive made as it began; seen
 * from the cockpit, the shell is above and below, the two halves built in
 * turn, nearest at the top and bottom of the view.
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
#include "ram.h"
#include "vars.h"

/* The perspective is built from the camera's registers (scene.h,
 * SCENE_PLANE_*): its height (the low byte, the matrix's scale), its turn
 * (0-$3FF around), the horizon, the plane's top and the lines paired. */
static uint16_t plane_register(const GameState *g, SceneChannel ch, unsigned reg) { return scene_channel_register(g, ch, reg); }

/* The routines a scene puts at the program's call that starts the lines, by
 * their address, as the call holds it. */
typedef enum {
    START_RACE = 0x45B0,
    START_ENDING = 0x9B46,
    START_SHELL_FROM_OUTSIDE = 0x801F,
    START_SHELL_FROM_THE_COCKPIT = 0x8042,
} PerspectiveStart;

static PerspectiveStart start_routine(const GameState *g) { return (PerspectiveStart)scene_perspective_start(g); }

/* The instructions the loop runs after each line, as words: one step farther
 * (the image's INX; NOP, or the race's NOP; INX), two farther or two nearer
 * (the dive's), the branch the race writes to copy each line into the next (the
 * word at scene_perspective_pair_branch), or the dive's branch to read the
 * next line's distance off the shell's curve. */
enum {
    STEP_IMAGE = 0xEAE8, STEP_RACE = 0xE8EA, STEP_PAIRS = 0x3680, STEP_TWO_FARTHER = 0xE8E8, STEP_TWO_NEARER = 0xCACA,
    STEP_ALONG_THE_CURVE = 0x2C80,
};

/* How the distance goes from one line to the next. */
typedef enum { DISTANCE_STEPS, DISTANCE_PAIRED, DISTANCE_ALONG_THE_CURVE, DISTANCE_UNKNOWN } DistanceWay;

typedef struct {
    DistanceWay way;
    int8_t step;    /* a line's, or a pair's */
} LineStep;

static LineStep line_step(uint16_t step)
{
    switch (step) {
    case STEP_IMAGE:
    case STEP_RACE: return (LineStep){ DISTANCE_STEPS, 1 };
    case STEP_PAIRS: return (LineStep){ DISTANCE_PAIRED, 2 };
    case STEP_TWO_FARTHER: return (LineStep){ DISTANCE_STEPS, 2 };
    case STEP_TWO_NEARER: return (LineStep){ DISTANCE_STEPS, -2 };
    case STEP_ALONG_THE_CURVE: return (LineStep){ DISTANCE_ALONG_THE_CURVE, 0 };
    default: return (LineStep){ DISTANCE_UNKNOWN, 0 };
    }
}

/* Each line is four bytes in each table, and 224 lines end the screen. */
enum { LINE_BYTES = 4, SCREEN_LINES = 224, SCREEN_END = SCREEN_LINES * LINE_BYTES };

/* The page the loop moves the direct page to, the CPU's registers, to reach the
 * divider. */
enum { CPU_REGISTERS_PAGE = M_DP_CPU };

/* The line above the screen's first, which an ending's plane starts from when
 * its horizon is 0 (set_line_above_the_screen, below). */
enum { LINE_ABOVE_THE_SCREEN = -1 };

/* The dive's views. From outside, the shell fills the screen from line 26 to
 * the panel-less bottom at 198, each line's distance off the curve from its
 * start. From the cockpit, the shell above runs from the top of the view to
 * line 112, nearest at the top, and the shell below from line 128 to the panel,
 * farthest at its top; each step is two. */
enum {
    SHELL_FROM_OUTSIDE_FIRST = 26, SHELL_FROM_OUTSIDE_END = 198,
    SHELL_ABOVE_FIRST = 0, SHELL_ABOVE_END = 112, SHELL_ABOVE_NEAREST = 0xFF,
    SHELL_BELOW_FIRST = 128, SHELL_BELOW_FARTHEST = 0x20,
    SHELL_CAMERA_FRAMES = 7,   /* the camera's register the half above counts its builds in */
};

/* The lines a build makes: from the first to the end, the first's distance,
 * and how it steps (the loop's instruction for it, which a scene's start may
 * write, and what it does). */
typedef struct {
    int16_t first, end;
    uint16_t distance;
    uint16_t step_instruction;
    LineStep step;
} PlaneLines;

static bool shell_above_built_last(const GameState *g) { return (scene_shell_halves_built(g) & 1) != 0; }

/* The lines the scene's start sets up: the race's from the horizon (a horizon
 * of 0 counts as line 1), in pairs where the camera asks; the endings' from
 * the line above it; both at the plane's top, to the end and with the step the
 * program holds. The dive's views as above, from the cockpit the half above
 * or the half below. */
static PlaneLines lines_of(const GameState *g, SceneChannel ch, bool shell_above)
{
    uint16_t horizon = plane_register(g, ch, SCENE_PLANE_HORIZON);
    PlaneLines l = { 0, (int16_t)(scene_perspective_end(g) / LINE_BYTES), plane_register(g, ch, SCENE_PLANE_TOP),
                     scene_perspective_line_step(g), { DISTANCE_UNKNOWN, 0 } };
    switch (start_routine(g)) {
    case START_RACE:
        l.first = (int16_t)(horizon ? horizon : 1);
        l.step_instruction = plane_register(g, ch, SCENE_PLANE_LINES_PAIRED) ? scene_perspective_pair_branch(g) : STEP_RACE;
        break;
    case START_ENDING: l.first = (int16_t)(horizon - 1); break;
    case START_SHELL_FROM_OUTSIDE:
        l.first = SHELL_FROM_OUTSIDE_FIRST;
        l.end = SHELL_FROM_OUTSIDE_END;
        l.distance = scene_shell_curve(g, 0);
        break;
    case START_SHELL_FROM_THE_COCKPIT:
        l.first = shell_above ? SHELL_ABOVE_FIRST : SHELL_BELOW_FIRST;
        l.end = shell_above ? SHELL_ABOVE_END : (int16_t)scene_shell_below_end(g);
        l.distance = shell_above ? SHELL_ABOVE_NEAREST : SHELL_BELOW_FARTHEST;
        l.step_instruction = shell_above ? STEP_TWO_NEARER : STEP_TWO_FARTHER;
        break;
    }
    l.step = line_step(l.step_instruction);
    return l;
}

/* The lines the next build makes, and those the last one made. */
static PlaneLines next_lines(const GameState *g, SceneChannel ch) { return lines_of(g, ch, !shell_above_built_last(g)); }
static PlaneLines lines_built(const GameState *g, SceneChannel ch) { return lines_of(g, ch, shell_above_built_last(g)); }

// $7E:45B0, $7E:9B46, $7E:801F, $7E:8042
/* The scene's routine at the start of the lines, which sets the program's
 * loop up for them. The race's puts a horizon of 0 on line 1 and writes its
 * step; the endings' changes nothing; from outside, the dive's view writes its
 * end; from the cockpit, it counts the half it builds, writes its step and
 * end, and the half above counts a frame on the camera. */
static void start_lines(GameState *g, SceneChannel ch, const PlaneLines *l)
{
    switch (start_routine(g)) {
    case START_RACE:
        if (plane_register(g, ch, SCENE_PLANE_HORIZON) == 0)
            set_scene_channel_register(g, ch, SCENE_PLANE_HORIZON, 1);
        set_scene_perspective_line_step(g, l->step_instruction);
        break;
    case START_ENDING: break;
    case START_SHELL_FROM_OUTSIDE: set_scene_perspective_end(g, (uint16_t)(l->end * LINE_BYTES)); break;
    case START_SHELL_FROM_THE_COCKPIT:
        set_scene_shell_halves_built(g, (uint16_t)(scene_shell_halves_built(g) + 1));
        set_scene_perspective_line_step(g, l->step_instruction);
        if (shell_above_built_last(g))
            set_scene_channel_register(g, ch, SHELL_CAMERA_FRAMES,
                                       (uint16_t)(plane_register(g, ch, SHELL_CAMERA_FRAMES) + 1));
        set_scene_perspective_end(g, (uint16_t)(l->end * LINE_BYTES));
        break;
    }
}

/* Through the CPU's divider, as the original divides: by the distance's low
 * byte. */
static uint16_t over_distance(uint16_t value, uint16_t distance) { return scene_divider_quotient(value, (uint8_t)distance); }

static uint16_t magnitude(int16_t v) { return v < 0 ? (uint16_t)-v : (uint16_t)v; }

/* The line above the screen, where an ending's plane starts from a horizon of
 * 0. Nothing shows it, but the original builds it as it builds the others,
 * through an index that has run below the tables: its middle entries land
 * just before them, and the first and last, whose index wraps, a bank on. The
 * cells are kept as the original writes them. */
static void set_line_above_the_screen(GameState *g, uint16_t along, uint16_t across)
{
    set_scene_plane_line_above_a(g, along);
    set_scene_plane_line_above_b(g, across);
    set_scene_plane_line_above_c(g, (uint16_t)-across);
    set_scene_plane_line_above_d(g, along);
}

/* A line's matrix: its first two entries in one table, its last two in the
 * other; the first and last are the cosine's, the middle two the sine's. */
static void set_line(GameState *g, int16_t line, uint16_t along, uint16_t across)
{
    if (line == LINE_ABOVE_THE_SCREEN) {
        set_line_above_the_screen(g, along, across);
        return;
    }
    set_scene_plane_matrix_top_row_a(g, (unsigned)line, along);
    set_scene_plane_matrix_top_row_b(g, (unsigned)line, across);
    set_scene_plane_matrix_bottom_row_c(g, (unsigned)line, (uint16_t)-across);
    set_scene_plane_matrix_bottom_row_d(g, (unsigned)line, along);
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
    uint16_t turn = plane_register(g, ch, SCENE_PLANE_TURN);
    uint8_t height = (uint8_t)plane_register(g, ch, SCENE_PLANE_HEIGHT);
    int16_t cos = scene_cosine(g, turn), sin = scene_sine(g, turn);
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
 * product loaded for a line after it. The dive's view from outside reads the
 * curve through the work RAM port, its first distance and then the next after
 * every line, and leaves the port past what it read. */
enum { BRANCH_INTO_NEGATION = 0x0080, BRANCH_PAST_NEGATION = 0x0480 };
enum { B_ENTRIES = 0xF0FE, C_ENTRIES = 0xF484 };

static void leave_loop_operands(GameState *g, const Turned *t, const PlaneLines *l, uint16_t last_distance)
{
    set_scene_perspective_along_product(g, t->along);
    set_scene_perspective_across_product(g, t->across);
    set_scene_perspective_cosine_sign(g, t->along_negative ? BRANCH_INTO_NEGATION : BRANCH_PAST_NEGATION);
    set_scene_perspective_across_entries(g, t->across_negative ? C_ENTRIES : B_ENTRIES);
    set_scene_perspective_against_entries(g, t->across_negative ? B_ENTRIES : C_ENTRIES);
    hw_set_dividend(g, t->across);
    hw_divide_by_word(g, last_distance);
    hw_set_dividend(g, t->along);
    if (start_routine(g) == START_SHELL_FROM_OUTSIDE) {
        unsigned read = 1 + (l->step.way == DISTANCE_ALONG_THE_CURVE ? (unsigned)(l->end - l->first) : 0);
        hw_port_seek(g, (uint16_t)(RAM_SCENE_SHELL_CURVE + read), RAM_SCENE_SHELL_CURVE >> 16);
    }
}

/* The distance of the line (or pair) after `line`, which was at `distance`. */
static uint16_t next_distance(const GameState *g, const PlaneLines *l, int16_t line, uint16_t distance)
{
    if (l->step.way == DISTANCE_ALONG_THE_CURVE)
        return scene_shell_curve(g, (unsigned)(line - l->first) + 1);
    return (uint16_t)(distance + l->step.step);
}

// $7E:357B
/* Build the table for the camera on channel `ch`: from the scene's first line
 * to its end, a line at a time or a pair at a time. */
void scene_build_perspective(GameState *g, SceneChannel ch)
{
    Turned t = turned_camera(g, ch);
    PlaneLines l = next_lines(g, ch);
    start_lines(g, ch, &l);
    unsigned lines_each = l.step.way == DISTANCE_PAIRED ? 2 : 1;
    uint16_t distance = l.distance, last_distance = distance;
    for (int16_t line = l.first; line < l.end; line = (int16_t)(line + lines_each)) {
        uint16_t along = over_distance(t.along, distance), across = over_distance(t.across, distance);
        along = t.along_negative ? (uint16_t)-along : along;
        across = t.across_negative ? (uint16_t)-across : across;
        for (unsigned i = 0; i < lines_each; i++)
            set_line(g, (int16_t)(line + i), along, across);
        last_distance = distance;
        distance = next_distance(g, &l, line, distance);
    }
    ORIGINAL_EFFECT(leave_loop_operands(g, &t, &l, last_distance));
}

/* ---- The camera it was built for ------------------------------------------------------------- */

/* The camera's registers the perspective was last built for, in the order
 * scene_perspective_camera keeps them. */
static const unsigned camera_registers[] = { SCENE_PLANE_HEIGHT, SCENE_PLANE_TURN, SCENE_PLANE_HORIZON, SCENE_PLANE_TOP, SCENE_PLANE_LINES_PAIRED };
enum { CAMERA_REGISTERS = sizeof camera_registers / sizeof camera_registers[0] };

unsigned scene_keep_the_camera(GameState *g, unsigned registers)
{
    unsigned first = registers;
    for (unsigned i = 0; i < registers && i < CAMERA_REGISTERS && first == registers; i++)
        if (plane_register(g, SCENE_CAMERA_CHANNEL, camera_registers[i]) != scene_perspective_camera(g, i))
            first = i;
    for (unsigned i = first; i < registers && i < CAMERA_REGISTERS; i++)
        set_scene_perspective_camera(g, i, plane_register(g, SCENE_CAMERA_CHANNEL, camera_registers[i]));
    return first;
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

/* The scenes' starts of the lines, and their code. */
static bool start_known(Snes *s, PerspectiveStart start)
{
    switch (start) {
    case START_RACE: return m_code_matches(s, 0x7E45B0, 0x26, 0xCF140E55949D99BFull);
    case START_ENDING: return m_code_matches(s, 0x7E9B46, 0x09, 0x55C26FE176ECC36Cull);
    case START_SHELL_FROM_OUTSIDE: return m_code_matches(s, 0x7E801F, 0x23, 0x232CC79CDE1BF32Bull);
    case START_SHELL_FROM_THE_COCKPIT: return m_code_matches(s, 0x7E8042, 0x37, 0x91210B2B8A983E26ull);
    default: return false;
    }
}

/* The lines the port builds: a step it knows, from a first line on the screen
 * or the line above it to an end on it that the step reaches (a whole line's
 * place in the tables, and in pairs an even number of lines), and along the
 * curve only from the dive's start that aims the work RAM port at it. Past
 * these the loop would run on through memory. */
static bool lines_known(Snes *s, const GameState *g, SceneChannel ch)
{
    PerspectiveStart start = start_routine(g);
    PlaneLines l = next_lines(g, ch);
    if (l.step.way == DISTANCE_UNKNOWN || l.first < LINE_ABOVE_THE_SCREEN || l.first >= l.end || l.end > SCREEN_LINES)
        return false;
    if ((start == START_RACE || start == START_ENDING) && scene_perspective_end(g) % LINE_BYTES != 0)
        return false;
    if (l.step.way == DISTANCE_ALONG_THE_CURVE
        && (start != START_SHELL_FROM_OUTSIDE || !m_code_matches(s, 0x7E3662, 0x0A, 0x11939EFB434666F3ull)))
        return false;
    return l.step.way != DISTANCE_PAIRED || (l.end - l.first) % 2 == 0;
}

/* Whether the port builds the perspective for the channel at `d`: on a
 * channel's page, the code as the port knows it, a scene's lines it knows. */
bool scene_perspective_as_ported(Snes *s, uint16_t d)
{
    SceneChannel ch;
    if (!scene_channel_at(d, &ch) || !perspective_code_present(s))
        return false;
    vars_peek(true);
    const GameState *g = state_of(s);
    bool known = start_known(s, start_routine(g)) && lines_known(s, g, ch);
    vars_peek(false);
    return known;
}

bool scene_build_perspective_present(Snes *s) { return scene_perspective_as_ported(s, m_direct_page(s)); }

/* What the original leaves, from the state after: Y at the end of the tables,
 * X the distance after the last line's (along the curve, the curve's next,
 * which A holds too), else A the last entry it stored (the sine's negated, or
 * the copy's last), the loop's last compare in the flags but V, which the
 * cosine of the turn left, the direct page on the CPU's registers and the
 * data bank its own. */
void scene_leave_perspective_registers(Snes *s, SceneChannel ch)
{
    GameState *g = state_of(s);
    PlaneLines l = lines_built(g, ch);
    unsigned lines = (unsigned)(l.end - l.first), last = (unsigned)(l.end - 1);
    bool sine_stored_last_in_b = l.step.way != DISTANCE_PAIRED && scene_perspective_against_entries(g) == B_ENTRIES;
    if (l.step.way == DISTANCE_ALONG_THE_CURVE) {
        s->cpu.x = scene_shell_curve(g, lines);
        s->cpu.a = s->cpu.x;
    } else {
        int steps = l.step.way == DISTANCE_PAIRED ? (int)lines / 2 : (int)lines;
        s->cpu.x = (uint16_t)(l.distance + steps * l.step.step);
        s->cpu.a = sine_stored_last_in_b ? scene_plane_matrix_top_row_b(g, last) : scene_plane_matrix_bottom_row_c(g, last);
    }
    s->cpu.y = (uint16_t)(l.end * LINE_BYTES);
    uint16_t turn = plane_register(g, ch, SCENE_PLANE_TURN);
    m_set_flag(s, CPU_FLAG_V, scene_quarter_turn_on_overflows(turn));
    m_set_flag(s, CPU_FLAG_N, false);
    m_set_flag(s, CPU_FLAG_Z, true);
    m_set_flag(s, CPU_FLAG_C, true);
    s->cpu.d = CPU_REGISTERS_PAGE;
    s->cpu.db = SCENE_LOWER_WORK_RAM;
}

/* Entered by a jump from the race's camera check, an ending's or the dive's,
 * 16-bit, on the channel's page. */
void scene_build_perspective_entry(Snes *s)
{
    SceneChannel ch = 0;
    scene_channel_at(m_direct_page(s), &ch);
    scene_build_perspective(state_of(s), ch);
    ORIGINAL_EFFECT(scene_leave_perspective_registers(s, ch));
    m_rts(s);
}
