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
enum { CHOOSE = 0x80, GO_BACK = 0x40, PAGE_ONE_WAY = 0x02, PAGE_THE_OTHER = 0x01 };
enum { CHOOSE_ASSIGNMENT = 0, BACK_ASSIGNMENT = 1, PAGE_ASSIGNMENT = 6, OTHER_PAGE_ASSIGNMENT = 7 };

static uint8_t buttons_of(const GameState *g)
{
    uint8_t pressed = (uint8_t)input_pressed(g), repeating = (uint8_t)input_repeat(g);
    uint8_t buttons = 0;
    if (button_assignments(g, CHOOSE_ASSIGNMENT) & pressed)
        buttons |= CHOOSE;
    if (button_assignments(g, BACK_ASSIGNMENT) & pressed)
        buttons |= GO_BACK;
    if (repeating & button_assignments(g, PAGE_ASSIGNMENT))
        buttons |= PAGE_ONE_WAY;
    if (repeating & button_assignments(g, OTHER_PAGE_ASSIGNMENT))
        buttons |= PAGE_THE_OTHER;
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
        buttons |= GO_BACK;
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
    uint8_t a_low = menu_cursor_always_back(g) != 0 ? GO_BACK : 0;
    uint16_t a_high = r.moved ? 0 : (uint16_t)(s->cpu.a & 0xFF00);
    s->cpu.a = (uint16_t)(a_high | a_low);
    uint16_t x = r.moved ? (uint16_t)(r.push * 2) : r.pushed ? r.push : PUSHES;
    uint16_t y = r.moved ? (uint8_t)(r.from * PLACE_BYTES) : (uint8_t)s->cpu.y;
    s->cpu.x = r.new_step ? x : BUTTON_LOOPS;
    s->cpu.y = r.new_step ? y : LAST_BUTTON_ASSIGNMENT;
    if (!r.new_step)
        set_menu_scratch_cursor_button_bit(state_of(s), GO_BACK >> 1);
}

/* The menus' per-frame input calls the controller read and the play time's
 * tick as the original does, the caller's data bank, page and flags saved
 * around them, since the read can stop there while the console still reads
 * the controller. The read comes back on its own page and data bank with 16-bit
 * A and 8-bit index registers; the tick with 8-bit ones, on a page nothing
 * reads before the frame's restore (the original's clock page, or where the
 * port of the tick leaves it), so its return isn't held to one. */
enum {
    CONTROLLER_PAGE = 0x0000, CONTROLLER_BANK = 0x00,
    READ_IGNORES = DC_A | DC_X | DC_N | DC_V | DC_Z | DC_C,
    TICK_IGNORES = DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C,
    SECOND_CONTROLLER = 1,
};

static void save_the_callers_frame(Snes *s)
{
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);   /* the index registers pass through 8 bits on the way */
    m_rep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    m_push8(s, s->cpu.p);
}

static void restore_the_callers_frame(Snes *s)
{
    s->cpu.p = m_pull8(s);
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
}

// $C2:8473
/* The menus' input for the frame, from their vblank: the controller's
 * buttons, the second controller's, a frame of play time, the repeat's
 * pacing, and the going-back hold. */
void menu_take_frame_input(Snes *s)
{
    GameState *g = state_of(s);
    menu_save_p(s);
    bool fast = ask_fast_repeat(g);
    /* the flags saved with the frame, which the controller read's stop leaves
     * on the stack: those of the request's test, or of the repeat's setting */
    ORIGINAL_EFFECT(menu_leave_load(s, fast ? 1 : 0));
    save_the_callers_frame(s);
    m_jsr_dc(s, 0xC28487, 0xC284EC, READ_IGNORES);
    if (!menu_back(s, 0xC2848A, (MenuMachine){ false, true, CONTROLLER_PAGE, CONTROLLER_BANK }))
        return;
    set_second_controller(g, in_read(m_platform(s), PF_AT(0xC2848A), SECOND_CONTROLLER));
    m_jsr_dc(s, 0xC28490, 0xC285AE, TICK_IGNORES);
    if (!menu_back(s, 0xC28493, (MenuMachine){ true, true, m_direct_page(s), CONTROLLER_BANK }))
        return;
    restore_the_callers_frame(s);
    pace_repeats(g);
    note_back_and_start_held(g);
    menu_restore_p(s);
    m_rts(s);
}

/* The input runs on the menus' vblank page, with a data bank that reaches
 * low work RAM. */
enum { MENUS_VBLANK_PAGE = 0x0D00 };

bool menu_frame_input_is_takeable(Snes *s) { return m_direct_page(s) == MENUS_VBLANK_PAGE && m_bank_reaches_low_ram(s); }

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
 * (tools/dontcare_check.py checks it): the accumulator's low byte, X and the
 * flags. The accumulator holds the pan the original loads last before it. */
enum { DRIVER_IGNORES = DC_AL | DC_X | DC_N | DC_V | DC_Z | DC_C };

static void leave_pan(Snes *s) { m_set_a8(s, AU_CENTRE); }

/* Send it: false where the driver came back otherwise than it was called, and
 * the port has stopped where the original carries on. */
static bool send(Snes *s, SoundEffectVoices voices, uint8_t sound)
{
    MenuMachine m = menu_machine_now(s);
    ORIGINAL_EFFECT(leave_pan(s));
    au_command(m_platform(s), PF_CALL_DC(0xC2EB1A, 0xC70004, DRIVER_IGNORES),
               (AudioCommand){ .command = voices, .p1 = sound, .p2 = AU_CENTRE, .count = 2 });
    return menu_back(s, 0xC2EB1E, m);
}

/* A dropped sound leaves the wait's count in the accumulator's low byte. */
static void leave_wait(Snes *s) { m_set_a8(s, menu_wait_frames(state_of(s))); }

static bool play(Snes *s, SoundEffectVoices voices, uint8_t sound)
{
    if (!sound_goes_out(state_of(s), voices, sound)) {
        ORIGINAL_EFFECT(leave_wait(s));
        return true;
    }
    return send(s, voices, sound);
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
/* A sound on the effects' other voices, sent whatever the wait. */
void menu_send_sound_on_other_voices(Snes *s)
{
    set_menu_wait_frames(state_of(s), 0);
    if (play(s, SOUND_EFFECT_ON_VOICES_4_5, (uint8_t)s->cpu.a))
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

/* ---- The guards -------------------------------------------------------------------------------- */

/* The cursor stands on one of its menu's places. */
bool menu_cursor_is_placeable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    vars_peek(true);
    bool ok = menu_cursor_index(state_of(s)) < MENU_CURSOR_PLACES_COUNT;
    vars_peek(false);
    return ok;
}

/* The cursor's neighbour a push reaches is one of its menu's places too. */
bool menu_cursor_is_movable(Snes *s)
{
    if (!menu_on_page_work_ram(s))
        return false;
    const GameState *g = state_of(s);
    vars_peek(true);
    uint8_t at = menu_cursor_index(g), pushes = pad_pushes(g);
    unsigned push = 0;
    while (push < PUSHES && !(pushes >> push & 1))
        push++;
    bool ok = at < MENU_CURSOR_PLACES_COUNT
              && (push == PUSHES || (uint8_t)(neighbour(g, at, (CursorPush)push) & ~OUT_OF_THE_MENU) < MENU_CURSOR_PLACES_COUNT);
    vars_peek(false);
    return ok;
}

bool menu_sounds_on_page(Snes *s) { return menu_on_page(s) && m_bank_reaches_low_ram(s); }
