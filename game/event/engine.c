/* The event script engine: running objects' scripts.
 *
 * Every frame each object runs its script for a turn: commands, from where it
 * left off, until one ends the turn or the turn's budget of commands
 * (event_commands_per_turn) is spent, and the script resumes there next frame.
 * When a map loads, every object first runs its script's opening, up to its
 * first zero command byte, all at once.
 *
 * What a turn is, is plain C over the protocol (event/script.h). Running one
 * command is where the protocol meets the original's dispatch: the handlers
 * are reached through the 256-entry table at $C0:5D6E with the registers the
 * original has there, since most are ports registered at their entries and
 * the rest are still the original's code. */
#include "event/bridge.h"
#include "event/event_data.h"

/* ---- A turn -------------------------------------------------------------------------- */

/* The last script offset reads the first byte past bank $7F, an I/O register
 * that holds whatever was last on the bus; no script is that long, and a turn
 * that gets there is left to the original, whose bus it is. */
enum { SCRIPT_END = 0xFFFF };

/* One command of the turn is spent: false when that was the last. */
static bool spend_command(GameState *g)
{
    uint8_t left = (uint8_t)(event_commands_left(g) - 1);
    set_event_commands_left(g, left);
    return left != 0;
}

/* ---- Dispatch ------------------------------------------------------------------------ */

/* Run the command at `at` through the table, from the JSR at `site`, with the
 * registers the original has there: the command byte doubled in A and X (the
 * table index, with the flags its making leaves: its 16-bit shift clears the
 * carry), the command's offset in Y.
 * The step is what the handler leaves: X, the carry, Y and V. False if it
 * came back with other widths, and the original then carries on from the
 * call. */
/* Back from a handler, which returns to `returns_to`: true if it left the
 * machine as the engine runs (event_machine_back); otherwise the port stops
 * there, where A and Y are dead (the entry's own set is for its return), and
 * the original carries on. */
static bool back(Snes *s, uint32_t returns_to, bool x8)
{
    if (event_machine_back(s, x8))
        return true;
    m_stop_at_own(s, returns_to, 0);
    return false;
}

static bool dispatch(Snes *s, uint32_t site, uint16_t at, EventStep *st)
{
    GameState *g = state_of(s);
    uint8_t command = script_byte(g, at);
    uint16_t index = (uint16_t)(command * 2);
    event_set_registers(s, index, index, at);
    m_set_flag(s, CPU_FLAG_C, false);
    m_set_nz16(s, index);
    m_jsr(s, site, 0xC00000u | command_handler(g, command));
    if (!back(s, site + 3, false))
        return false;
    *st = event_step_left(s);
    return true;
}

// $C0:5A93
/* Run the script of the object in X (its slot doubled, 8-bit) for this frame's
 * turn, and keep where it stopped. The engine names the object running in the
 * low byte of field_current_object. Returns with 8-bit index registers; A, X,
 * Y and the flags are dead there. */
void event_run_object_script(Snes *s)
{
    GameState *g = state_of(s);
    uint8_t object = (uint8_t)s->cpu.x;
    set_event_commands_left(g, event_commands_per_turn(g));
    uint16_t at = event_object_script_position(g, slot_of(object));
    set_field_current_object(g, (uint16_t)((field_current_object(g) & 0xFF00) | object));
    m_rep(s, CPU_FLAG_X);
    EventStep st;
    do {
        if (at == SCRIPT_END) {
            s->cpu.x = at;
            m_stop_at_own(s, 0xC05AA3, DC_A | DC_Y | DC_N | DC_Z | DC_C);
            return;
        }
        if (!dispatch(s, 0xC05AB1, at, &st))
            return;
        at = st.next;
    } while (st.go_on && spend_command(g));

    /* $C0:5ABA: the handlers may have named another object running, or left
     * a data bank the store through it doesn't reach low RAM with */
    if (!event_names_object(field_current_object(g)) || !m_bank_reaches_low_ram(s)) {
        s->cpu.x = at;
        m_stop_at_own(s, 0xC05ABA, DC_A | DC_Y | DC_N | DC_Z | DC_C);
        return;
    }
    set_event_object_script_position(g, current_object(g), at);
    m_sep(s, CPU_FLAG_M | CPU_FLAG_X);
    m_rts(s);
}

// $C0:58DE (inside the map setup routine)
/* Run every object's opening: its script up to its first zero command byte,
 * and keep the offset after it, where the per-frame turns carry on. Every
 * object's solidity and acceleration are cleared on the way (and, by the
 * original's 16-bit clears, the sprite state of the object after it); an
 * object whose character byte is negative is skipped. The object running is
 * named in field_current_object, the end of the slots in event_object_end.
 * Goes on at $C0:9DC3 rather than returning; the handlers run as call-outs,
 * since some take several frames. */
void event_run_init_scripts(Snes *s)
{
    GameState *g = state_of(s);
    uint16_t end = (uint16_t)(event_object_count(g) * 2);
    set_event_object_end(g, end);
    uint16_t object = 0;
    do {
        /* $C0:58ED: the object's cells are reached through the data bank,
         * which a handler may have left elsewhere */
        if (!m_bank_reaches_low_ram(s)) {
            s->cpu.x = object;
            m_stop_at(s, 0xC058ED, 0);
            return;
        }
        unsigned slot = slot_of(object);
        set_field_object_solid(g, slot, 0);
        set_field_object_sprite_state(g, slot + 1, 0);
        set_field_object_accelerating(g, slot, 0);
        set_field_object_acceleration(g, slot, 0);
        if (!(field_object_state_character(g, slot) & 0x80)) {
            uint16_t at = event_object_script_position(g, slot);
            set_field_current_object(g, object);
            EventStep st;
            while (at != SCRIPT_END && script_byte(g, at) != 0) {
                if (!dispatch(s, 0xC05910, at, &st))
                    return;
                at = st.next;
            }
            if (at == SCRIPT_END) {
                s->cpu.x = at;
                m_stop_at(s, 0xC05900, 0);
                return;
            }
            /* $C0:5915: the handlers may have named another object running, or
             * left another data bank */
            object = field_current_object(g);
            if ((object & 1) || !m_bank_reaches_low_ram(s)) {
                s->cpu.x = at;
                m_stop_at(s, 0xC05915, 0);
                return;
            }
            set_event_object_script_position(g, slot_of(object), (uint16_t)(at + 1));
        }
        object = (uint16_t)(object + 2);
    } while (object != event_object_end(g));
    m_stop_at(s, 0xC09DC3, DC_A | DC_X | DC_N | DC_Z | DC_C);
}
