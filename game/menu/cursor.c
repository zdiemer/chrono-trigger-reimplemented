/* The menus' cursor: the hand that points at what the player can choose.
 *
 * Each menu screen lays out the places its cursor can stand (menu_cursor_places:
 * where the hand's sprite goes, and which place is up, down, left and right of
 * each), and every frame the screen reads the controller through the cursor: a
 * push on the direction pad moves it to the neighbouring place that way, and
 * the buttons the player assigned to choosing and going back, and the two to
 * paging, come out as the frame's buttons (menu_cursor_buttons). A screen can
 * hold the cursor against a push for a frame, and a step's first frame reads
 * no buttons, so the press that led to the step doesn't count again in it.
 *
 * The cursor's sprite is drawn every frame, after the menus hide the sprites
 * they own; a screen can leave it off for a frame, or flip it. And the menus'
 * short sounds -- the cursor's tick, the buzz of a choice refused, the rest a
 * screen asks for -- go to the sound driver from here, one at most every three
 * frames. */
#include "menu/cursor.h"

#include "charge.h"
#include "core/program.h"
#include "core/input.h"
#include "menu/bridge.h"
#include "menu/menu_data.h"
#include "original_effects.h"
#include "platform/audio.h"
#include "platform/input.h"
#include "port/native.h"
#include "sound/sound.h"

/* ---- The sprites ------------------------------------------------------------------------------ */

/* Sprites wait out of sight below the screen, large, with nothing drawn. The
 * menus own sprites 16-95, the cursor is sprite 28, and the high table holds
 * four sprites' size and ninth X bit a byte. */
enum { SPRITES = 128, FIRST_MENU_SPRITE = 16, MENU_SPRITES = 80, CURSOR_SPRITE = 28 };
enum { HIDDEN_Y = 0xE0, HIDDEN_ATTRIBUTES = 0xE0, ALL_LARGE = 0xAA, SPRITES_A_HIGH_BYTE = 4 };
enum { FLIPPED = 0x40 };

static void hide_sprites(GameState *g, unsigned first, unsigned count)
{
    for (unsigned i = first; i < first + count; i++) {
        set_oam_buffer_x(g, i, 0);
        set_oam_buffer_y(g, i, HIDDEN_Y);
        set_oam_buffer_tile(g, i, 0);
        set_oam_buffer_attributes(g, i, HIDDEN_ATTRIBUTES);
    }
    for (unsigned i = first; i < first + count; i += SPRITES_A_HIGH_BYTE)
        set_oam_high_table_byte(g, i / SPRITES_A_HIGH_BYTE, ALL_LARGE);
}

// $C2:984A
/* Every sprite out of sight. */
void menu_clear_sprites(GameState *g) { hide_sprites(g, 0, SPRITES); }

bool menu_asked(const GameState *g, MenuButton b) { return (menu_cursor_buttons(g) & b) != 0; }
bool menu_pushed(const GameState *g, MenuPush way) { return (menu_cursor_moves(g) & way) != 0; }

/* The cursor at its place: its sprite's tile and attributes, flipped where the
 * screen says, unless the screen hid it for this one frame. */
static void place_cursor(GameState *g)
{
    bool hidden = menu_cursor_hidden(g) != 0;
    set_menu_cursor_hidden(g, 0);
    if (hidden)
        return;
    uint8_t at = menu_cursor_index(g);
    set_oam_buffer_x(g, CURSOR_SPRITE, menu_cursor_places_x(g, at));
    set_oam_buffer_y(g, CURSOR_SPRITE, menu_cursor_places_y(g, at));
    uint16_t sprite = menu_cursor_sprite(g);
    set_oam_buffer_tile(g, CURSOR_SPRITE, (uint8_t)sprite);
    set_oam_buffer_attributes(g, CURSOR_SPRITE, (uint8_t)(sprite >> 8));
    if (menu_cursor_flip(g) != 0)
        set_oam_buffer_attributes(g, CURSOR_SPRITE, (uint8_t)(oam_buffer_attributes(g, CURSOR_SPRITE) | FLIPPED));
}

// $C2:E92D
/* The menus' sprites each frame: theirs hidden, then the cursor placed. */
void menu_reset_sprites_and_cursor(GameState *g)
{
    hide_sprites(g, FIRST_MENU_SPRITE, MENU_SPRITES);
    place_cursor(g);
}

// $C2:E923
/* Every sprite hidden, and the cursor placed facing its own way. */
void menu_clear_sprites_but_cursor(GameState *g)
{
    menu_clear_sprites(g);
    set_menu_cursor_flip(g, 0);
    place_cursor(g);
}

/* ---- The direction pad and the buttons -------------------------------------------------------- */

/* The pushes, in the controller's order, and the cursor's neighbour each
 * follows. A neighbour with its top bit set is the place past the menu's
 * positions that way (another list, the screen's edge), which the screen
 * sees in the frame's moves. */
enum { NO_PUSH = 0x80, PUSHES = 4, OUT_OF_THE_MENU = 0x80 };
typedef enum { PUSH_RIGHT, PUSH_LEFT, PUSH_DOWN, PUSH_UP } CursorPush;

static uint8_t neighbour(const GameState *g, uint8_t at, CursorPush push)
{
    switch (push) {
    case PUSH_RIGHT: return menu_cursor_places_right(g, at);
    case PUSH_LEFT: return menu_cursor_places_left(g, at);
    case PUSH_DOWN: return menu_cursor_places_down(g, at);
    default: return menu_cursor_places_up(g, at);
    }
}

/* The direction pad's pushes this frame, repeating while held: its bits in
 * the controller's high byte. */
static uint8_t pad_pushes(const GameState *g) { return (uint8_t)(input_repeat(g) >> 8); }

// $C2:E9E2
/* Move the cursor with the frame's push, the first of right, left, down and
 * up the pad has, unless the screen holds it this frame. */
static void move_cursor(GameState *g)
{
    uint8_t pushes = pad_pushes(g);
    set_menu_cursor_frame_start(g, menu_cursor_index(g));
    set_menu_cursor_moves(g, pushes);
    unsigned push = 0;
    while (push < PUSHES && !(pushes >> push & 1))
        push++;
    set_menu_cursor_direction(g, push < PUSHES ? (uint8_t)push : NO_PUSH);
    if (push < PUSHES && menu_cursor_held(g) == 0) {
        uint8_t from = menu_cursor_index(g), to = neighbour(g, from, (CursorPush)push);
        set_menu_cursor_left_from(g, from);
        if (to & OUT_OF_THE_MENU) {
            set_menu_cursor_moves(g, (uint8_t)(pushes | OUT_OF_THE_MENU));
            to &= (uint8_t)~OUT_OF_THE_MENU;
        }
        set_menu_cursor_index(g, to);
    }
    set_menu_cursor_held(g, 0);
}

/* The frame's buttons, as the menus take them: choosing and going back are
 * the player's first two assignments pressed this frame, and the seventh and
 * eighth (paging) repeat while held. */
enum { CHOOSE_ASSIGNMENT = 0, BACK_ASSIGNMENT = 1, PAGE_ASSIGNMENT = 6, OTHER_PAGE_ASSIGNMENT = 7 };

static uint8_t buttons_of(const GameState *g)
{
    uint8_t pressed = (uint8_t)input_pressed(g), repeating = (uint8_t)input_repeat(g);
    uint8_t buttons = 0;
    if (button_assignments(g, CHOOSE_ASSIGNMENT) & pressed)
        buttons |= MENU_CHOOSE;
    if (button_assignments(g, BACK_ASSIGNMENT) & pressed)
        buttons |= MENU_GO_BACK;
    if (repeating & button_assignments(g, PAGE_ASSIGNMENT))
        buttons |= MENU_PAGE_ONE_WAY;
    if (repeating & button_assignments(g, OTHER_PAGE_ASSIGNMENT))
        buttons |= MENU_PAGE_THE_OTHER;
    return buttons;
}

// $C2:E984
/* The frame's input through the cursor: the push moves it, and the buttons
 * come out, but for none on the first frame of a step of the screen. */
void menu_read_cursor(GameState *g)
{
    move_cursor(g);
    uint8_t step = menu_screen_step(g);
    bool new_step = step != menu_cursor_step_seen(g);
    set_menu_cursor_step_seen(g, step);
    uint8_t buttons = new_step ? 0 : buttons_of(g);
    if (menu_cursor_always_back(g) != 0)
        buttons |= MENU_GO_BACK;
    set_menu_cursor_buttons(g, buttons);
}

/* ---- The frame's buttons, as the menus' vblank takes them ------------------------------------- */

/* Held buttons repeat slowly at first and then quickly: the first repeat
 * waits the long delay, the rest the short one. The pacing goes back to the
 * long delay whenever nothing but Start is held, and to the short one once
 * the held buttons have been pressed or repeated. A screen can ask for a
 * frame of repeating every frame (menu_fast_repeat), which the input takes
 * and clears. */
enum { LONG_DELAY = 1 };

/* A screen asks the next frame's input for a fast repeat, so that a push or
 * Start held through what it does carries straight on: the request is any
 * count but 0, and the original counts it down from where it is. */
void menu_ask_for_a_fast_repeat(GameState *g) { set_menu_fast_repeat(g, (uint8_t)(menu_fast_repeat(g) - 1)); }

static bool ask_fast_repeat(GameState *g)
{
    if (!menu_fast_repeat(g))
        return false;
    set_input_repeat_delay(g, 1);
    set_input_repeat_countdown(g, 1);
    return true;
}

static void pace_repeats(GameState *g)
{
    if (!(input_held(g) & (Buttons)~BUTTON_START))
        set_menu_repeat_phase(g, LONG_DELAY);
    else if (input_repeat(g) != 0) {
        uint16_t phase = (uint16_t)(menu_repeat_phase(g) - 1);
        set_menu_repeat_phase(g, (phase & 0x8000) ? 0 : phase);
    }
    set_input_repeat_delay(g, menu_repeat_delay(g, (uint8_t)menu_repeat_phase(g)));
    set_menu_fast_repeat(g, 0);
}

/* Holding the going-back assignment's buttons and Start together sends every
 * screen back from then on (menu_cursor_always_back). */
static void note_back_and_start_held(GameState *g)
{
    uint8_t both = (uint8_t)(button_assignments(g, BACK_ASSIGNMENT) | BUTTON_START);
    if (((uint8_t)input_held(g) & both) == both)
        set_menu_cursor_always_back(g, both);
}

/* ---- The sounds --------------------------------------------------------------------------------- */

/* A sound waits out three frames after one plays, and one asked for sooner is
 * dropped; the sound driver's command goes in its mailbox all the same. The
 * menus' sounds play from the middle. */
enum { FRAMES_BETWEEN_SOUNDS = 3 };

static bool sound_goes_out(GameState *g, SoundEffectVoices voices, uint8_t sound)
{
    set_sound_command(g, voices);
    set_sound_command_parameters(g, 0, sound);
    if (menu_wait_frames(g) != 0)
        return false;
    set_menu_wait_frames(g, FRAMES_BETWEEN_SOUNDS);
    return true;
}

/* The sounds a screen queued: the first of them, played at once. */
enum { QUEUED_SOUND_6 = 0x02, QUEUED_SOUND_7 = 0x01 };

static bool take_queued_sound(GameState *g, uint8_t *sound)
{
    uint8_t queued = menu_queued_sounds(g);
    set_menu_wait_frames(g, 0);
    uint8_t taken = (queued & QUEUED_SOUND_6) ? QUEUED_SOUND_6 : (queued & QUEUED_SOUND_7) ? QUEUED_SOUND_7 : 0;
    if (taken == 0)
        return false;
    set_menu_queued_sounds(g, (uint8_t)(queued & ~taken));
    *sound = taken == QUEUED_SOUND_6 ? MENU_SOUND_6 : MENU_SOUND_7;
    return true;
}

/* ---- Places kept --------------------------------------------------------------------------------- */

enum { PLACE_BYTES = 6 };

static void copy_place(GameState *g, unsigned place, bool keep)
{
    if (keep) {
        set_menu_cursor_places_kept_x(g, place, menu_cursor_places_x(g, place));
        set_menu_cursor_places_kept_y(g, place, menu_cursor_places_y(g, place));
        set_menu_cursor_places_kept_up(g, place, menu_cursor_places_up(g, place));
        set_menu_cursor_places_kept_down(g, place, menu_cursor_places_down(g, place));
        set_menu_cursor_places_kept_left(g, place, menu_cursor_places_left(g, place));
        set_menu_cursor_places_kept_right(g, place, menu_cursor_places_right(g, place));
    } else {
        set_menu_cursor_places_x(g, place, menu_cursor_places_kept_x(g, place));
        set_menu_cursor_places_y(g, place, menu_cursor_places_kept_y(g, place));
        set_menu_cursor_places_up(g, place, menu_cursor_places_kept_up(g, place));
        set_menu_cursor_places_down(g, place, menu_cursor_places_kept_down(g, place));
        set_menu_cursor_places_left(g, place, menu_cursor_places_kept_left(g, place));
        set_menu_cursor_places_right(g, place, menu_cursor_places_kept_right(g, place));
    }
}

void menu_keep_cursor_places(GameState *g)
{
    for (unsigned place = 0; place < MENU_CURSOR_PLACES_KEPT_COUNT; place++)
        copy_place(g, place, true);
    ORIGINAL_EFFECT(charge_block_move(g, MENU_CURSOR_PLACES_KEPT_COUNT * PLACE_BYTES));
}

void menu_put_cursor_places_back(GameState *g)
{
    for (unsigned place = 0; place < MENU_CURSOR_PLACES_KEPT_COUNT; place++)
        copy_place(g, place, false);
    ORIGINAL_EFFECT(charge_block_move(g, MENU_CURSOR_PLACES_KEPT_COUNT * PLACE_BYTES));
}

/* ---- The entries -------------------------------------------------------------------------------- */

// $C2:984A
void menu_clear_sprite_table(Snes *s)
{
    menu_clear_sprites(state_of(s));
    m_rts(s);
}

// $C2:E92D
void menu_reset_sprites(Snes *s)
{
    menu_reset_sprites_and_cursor(state_of(s));
    m_rts(s);
}

// $C2:E923
void menu_clear_sprites_for_cursor(Snes *s)
{
    menu_clear_sprites_but_cursor(state_of(s));
    m_rts(s);
}

/* What the cursor's reading leaves in the registers, which the screens'
 * handlers can't be shown not to read (the analysis follows them into the
 * sound driver): the accumulator the last cell it looked at, X and Y where
 * the push's search and the buttons' loop left them, each a byte; and the
 * loop's bit in its cell. */
MenuCursorRead menu_note_cursor_read(const GameState *g)
{
    vars_peek(true);
    uint8_t pushes = pad_pushes(g);
    unsigned push = 0;
    while (push < PUSHES && !(pushes >> push & 1))
        push++;
    MenuCursorRead r = { menu_screen_step(g) != menu_cursor_step_seen(g), push < PUSHES,
                         push < PUSHES && menu_cursor_held(g) == 0, (uint8_t)push, menu_cursor_index(g) };
    vars_peek(false);
    return r;
}

void menu_leave_cursor_read(Snes *s, MenuCursorRead r)
{
    enum { PLACE_BYTES = 6, BUTTON_LOOPS = 2, LAST_BUTTON_ASSIGNMENT = 1 };
    const GameState *g = state_of(s);
    uint8_t a_low = menu_cursor_always_back(g) != 0 ? MENU_GO_BACK : 0;
    uint16_t a_high = r.moved ? 0 : (uint16_t)(s->cpu.a & 0xFF00);
    s->cpu.a = (uint16_t)(a_high | a_low);
    uint16_t x = r.moved ? (uint16_t)(r.push * 2) : r.pushed ? r.push : PUSHES;
    uint16_t y = r.moved ? (uint8_t)(r.from * PLACE_BYTES) : (uint8_t)s->cpu.y;
    s->cpu.x = r.new_step ? x : BUTTON_LOOPS;
    s->cpu.y = r.new_step ? y : LAST_BUTTON_ASSIGNMENT;
    if (!r.new_step)
        set_menu_scratch_cursor_button_bit(state_of(s), MENU_GO_BACK >> 1);
}

/* Where the controller read doesn't take the buttons, the port stops where
 * the original does, with the stack the original's input has built by then:
 * its caller's flags, data bank, page and flags again (as the fast repeat's
 * test left them, the index registers passed through 8 bits on the way), and
 * the return into the input from the read. */
enum { RETURN_FROM_THE_READ = 0x8489 };

static void enter_the_read(Snes *s, bool fast)
{
    menu_save_p(s);
    menu_leave_load(s, fast ? 1 : 0);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.p);
    m_push16(s, RETURN_FROM_THE_READ);
}

enum { SECOND_CONTROLLER = 1 };

// $C2:8473
/* The menus' input for the frame, from their vblank: the controller's
 * buttons, the second controller's, a frame of play time, the repeat's
 * pacing, and the going-back hold. Where the controller's read doesn't take
 * the buttons it goes no further, the read's answer said; `fast`, whether
 * the repeat was asked to go fast as the read began. */
static ControllerRead take_frame_input(GameState *g, Platform *pf, bool *fast)
{
    *fast = ask_fast_repeat(g);
    ControllerRead read;
    TIMED_AS(g, 0xC284EC, read = input_read_controller(g, pf));
    if (read != CONTROLLER_READ)
        return read;
    set_second_controller(g, in_read(pf, PF_AT(0xC2848A), SECOND_CONTROLLER));
    TIMED_AS(g, 0xC285AE, input_tick_play_time(g));
    pace_repeats(g);
    note_back_and_start_held(g);
    return CONTROLLER_READ;
}

bool menu_frame_input_taken(GameState *g, Platform *pf)
{
    bool fast;
    if (take_frame_input(g, pf, &fast) == CONTROLLER_READ)
        return true;
    program_needs_the_machine(g, 0xC284EC, "a controller read the port doesn't take");
    return false;
}

void menu_take_frame_input(Snes *s)
{
    bool fast;
    ControllerRead read = take_frame_input(state_of(s), m_platform(s), &fast);
    if (read != CONTROLLER_READ) {
        enter_the_read(s, fast);
        input_stop_reading(s, read);
        return;
    }
    m_rts(s);
}

/* The input runs on the menus' vblank page, with a data bank that reaches
 * low work RAM. */
enum { MENUS_VBLANK_PAGE = 0x0D00 };

bool menu_frame_input_is_takeable(Snes *s) { return m_direct_page(s) == MENUS_VBLANK_PAGE && m_bank_reaches_low_ram(s); }

void menu_step_reads_the_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
}

void menu_hold_and_read_the_cursor(GameState *g)
{
    set_menu_cursor_held(g, (uint8_t)(menu_cursor_held(g) + 1));
    TIMED_AS(g, 0xC2E984, menu_read_cursor(g));
}

void menu_step_holds_and_reads_the_cursor(Snes *s)
{
    GameState *g = state_of(s);
    MenuCursorRead r = menu_note_cursor_read(g);
    menu_hold_and_read_the_cursor(g);
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
}

void menu_step_reads_the_cursor_and_tests_its_buttons(Snes *s)
{
    menu_step_reads_the_cursor(s);
    ORIGINAL_EFFECT(menu_leave_bit_test(s, (uint8_t)s->cpu.a, menu_cursor_buttons(state_of(s))));
}

// $C2:E984
void menu_read_cursor_input(Snes *s)
{
    MenuCursorRead r = menu_note_cursor_read(state_of(s));
    menu_read_cursor(state_of(s));
    ORIGINAL_EFFECT(menu_leave_cursor_read(s, r));
    m_rts(s);
}

/* The sounds on the machine. The driver's call is made from the one JSL the
 * original's routines share ($C2:EB1A), so a routine that plays one stands in
 * for its whole chain: the P it saves as it starts, the return of its call to
 * the send, and the call itself. What the driver doesn't read at the call
 * (tools/dontcare_check.py checks it): the accumulator's low byte, X, Y and the
 * flags. The accumulator holds the pan the original loads last before it. */
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static void leave_pan(Snes *s) { m_set_a8(s, AU_CENTRE); }

/* The sound sent to the driver, from the original's one call. */
static void send_the_sound(Platform *pf, SoundEffectVoices voices, uint8_t sound)
{
    au_command(pf, PF_CALL_DC(0xC2EB1A, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = voices, .p1 = sound, .p2 = AU_CENTRE, .count = 2 });
}

/* A sound played where it goes out (sound_goes_out), sent as C or on the
 * machine (below); one that doesn't is dropped, which leaves only what the
 * original's code reads (scaffolding). False where the send went no
 * further. */
typedef struct {
    bool (*sent)(void *ctx, SoundEffectVoices voices, uint8_t sound);
    void (*dropped)(void *ctx);
} SoundCalls;

// $C2:EB03
static bool play_a_sound(GameState *g, SoundEffectVoices voices, uint8_t sound, const SoundCalls *c, void *ctx)
{
    if (sound_goes_out(g, voices, sound))
        return c->sent(ctx, voices, sound);
    c->dropped(ctx);
    return true;
}

typedef struct {
    GameState *game;
    Platform *pf;
} MenuSoundContext;

static bool sent(void *ctx, SoundEffectVoices voices, uint8_t sound)
{
    send_the_sound(((MenuSoundContext *)ctx)->pf, voices, sound);
    return true;
}

static void dropped(void *ctx) { (void)ctx; }

void menu_sound_played(GameState *g, Platform *pf, uint8_t sound)
{
    static const SoundCalls in_the_program = { sent, dropped };
    MenuSoundContext c = { g, pf };
    (void)play_a_sound(g, SOUND_EFFECT_ON_VOICES_6_7, sound, &in_the_program, &c);
}

// $C2:EAF9
/* A sound on the effects' other voices, sent whatever the wait. */
static bool play_on_other_voices(GameState *g, uint8_t sound, const SoundCalls *c, void *ctx)
{
    set_menu_wait_frames(g, 0);
    return play_a_sound(g, SOUND_EFFECT_ON_VOICES_4_5, sound, c, ctx);
}

void menu_sound_played_on_other_voices(GameState *g, Platform *pf, uint8_t sound)
{
    static const SoundCalls in_the_program = { sent, dropped };
    MenuSoundContext c = { g, pf };
    (void)play_on_other_voices(g, sound, &in_the_program, &c);
}

void menu_read_the_cursor_for_a_step(GameState *g) { TIMED_AS(g, 0xC2E984, menu_read_cursor(g)); }

// $C2:EAC2
void menu_tick(GameState *g, Platform *pf) { TIMED_AS(g, 0xC2EAC2, menu_sound_played(g, pf, MENU_SOUND_CURSOR)); }

// $C2:EACC
void menu_buzz(GameState *g, Platform *pf) { TIMED_AS(g, 0xC2EACC, menu_sound_played(g, pf, MENU_SOUND_REFUSAL)); }

/* On the machine the send is a call-out to the driver: false where the
 * driver came back otherwise than it was called, and the port has stopped
 * where the original carries on. */
static bool sent_on_the_machine(void *ctx, SoundEffectVoices voices, uint8_t sound)
{
    Snes *s = ctx;
    MenuMachine m = menu_machine_now(s);
    ORIGINAL_EFFECT(leave_pan(s));
    send_the_sound(m_platform(s), voices, sound);
    return menu_back(s, 0xC2EB1E, m);
}

/* A dropped sound leaves the wait's count in the accumulator's low byte. */
static void leave_wait(Snes *s) { m_set_a8(s, menu_wait_frames(state_of(s))); }

static void dropped_on_the_machine(void *ctx) { ORIGINAL_EFFECT(leave_wait(ctx)); }

static const SoundCalls on_the_machine = { sent_on_the_machine, dropped_on_the_machine };

static bool play(Snes *s, SoundEffectVoices voices, uint8_t sound)
{
    return play_a_sound(state_of(s), voices, sound, &on_the_machine, s);
}

/* A routine that saves P and calls the send ($C2:EB03) from `site`. */
static void play_from(Snes *s, uint32_t site, uint8_t sound)
{
    menu_save_p(s);
    menu_set_widths(s, true, true);
    m_push16(s, (uint16_t)(site + MENU_JSR_SIZE - 1));
    if (!play(s, SOUND_EFFECT_ON_VOICES_6_7, sound))
        return;
    (void)m_pull16(s);
    menu_restore_p(s);
    m_rts(s);
}

// $C2:EB03
void menu_send_sound(Snes *s)
{
    if (play(s, SOUND_EFFECT_ON_VOICES_6_7, (uint8_t)s->cpu.a))
        m_rts(s);
}

// $C2:EAF9
void menu_send_sound_on_other_voices(Snes *s)
{
    if (play_on_other_voices(state_of(s), (uint8_t)s->cpu.a, &on_the_machine, s))
        m_rts(s);
}

// $C2:EAC2
void menu_play_cursor_sound(Snes *s) { play_from(s, 0xC2EAC7, MENU_SOUND_CURSOR); }

// $C2:EACC
void menu_play_refusal_sound(Snes *s) { play_from(s, 0xC2EAD1, MENU_SOUND_REFUSAL); }

// $C2:EABA
void menu_play_sound(Snes *s) { play_from(s, 0xC2EABD, (uint8_t)s->cpu.a); }

// $C2:EAD6
void menu_play_queued_sound(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    menu_set_widths(s, true, true);
    uint8_t sound;
    if (take_queued_sound(g, &sound)) {
        m_push16(s, (uint16_t)(0xC2EAF4 + MENU_JSR_SIZE - 1));
        if (!play(s, SOUND_EFFECT_ON_VOICES_6_7, sound))
            return;
        (void)m_pull16(s);
    }
    menu_restore_p(s);
    m_rts(s);
}

bool menu_cursor_kept_among(const GameState *g, uint8_t first, uint8_t count, bool left_free)
{
    enum { OUT_OF_THE_MENU = 0x80 };
    uint8_t at = menu_cursor_index(g);
    if (at < first || at >= first + count || at >= MENU_CURSOR_PLACES_COUNT)
        return false;
    uint8_t ways[] = { menu_cursor_places_up(g, at), menu_cursor_places_down(g, at), menu_cursor_places_right(g, at),
                       menu_cursor_places_left(g, at) };
    for (unsigned way = 0; way < (left_free ? 3u : 4u); way++) {
        uint8_t to = (uint8_t)(ways[way] & ~OUT_OF_THE_MENU);
        if (to < first || to >= first + count)
            return false;
    }
    return true;
}

/* ---- The guards -------------------------------------------------------------------------------- */

/* The cursor stands on one of its menu's places. */
bool menu_cursor_can_be_placed(const GameState *g) { return menu_cursor_index(g) < MENU_CURSOR_PLACES_COUNT; }

bool menu_cursor_is_placeable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_cursor_can_be_placed(state_of(s));
    vars_peek(false);
    return ok;
}

/* The cursor's neighbour a push reaches is one of its menu's places too. */
bool menu_cursor_can_move(const GameState *g)
{
    uint8_t at = menu_cursor_index(g), pushes = pad_pushes(g);
    unsigned push = 0;
    while (push < PUSHES && !(pushes >> push & 1))
        push++;
    return at < MENU_CURSOR_PLACES_COUNT
           && (push == PUSHES || (uint8_t)(neighbour(g, at, (CursorPush)push) & ~OUT_OF_THE_MENU) < MENU_CURSOR_PLACES_COUNT);
}

bool menu_cursor_is_movable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_cursor_can_move(state_of(s));
    vars_peek(false);
    return ok;
}

bool menu_sounds_on_page(Snes *s) { return menu_on_page(s) && m_bank_reaches_low_ram(s); }
