/* Field: the sounds the field plays of its own, and the music round a battle.
 *
 * A switch stepped on and a chest opened each play their effect (the field's
 * start sets which: field_switch_sound, field_chest_sound) on the sound
 * driver's second pair of effect voices, panned where the party's leader
 * stands on the screen (field/requests.c answers both).
 *
 * A battle on the field starts its own song (field_battle_song) and brings
 * the battle's set of sound effects with it; an event can keep the field's
 * song playing through the fight instead, and then only the effects change.
 * Once the battle is over the field's song comes back where it left off, its
 * volume rising over about a second, and the field's effects with it; a
 * battle the party lost brings a song of its own instead, unless the event
 * says the music goes on as after any other. The battle's setup and its
 * close call these (battle/screen_setup.c, battle/closing.c).
 *
 * Everything goes through the audio service's commands to the driver. The
 * original runs the battle's two on the field's page with the data bank $00,
 * which its calls to the driver see; their entries set those up and put the
 * caller's back. */
#include "field/sounds.h"

#include "battle/battle.h"
#include "field/bridge.h"
#include "field/main_loop.h"
#include "field/object.h"
#include "field/party.h"
#include "platform/audio.h"
#include "vars.h"

/* The driver's command entry, called from each of the original's sites; it
 * reads nothing of the registers but A's high byte, which it hands back. */
enum { DRIVER = 0xC70004, DRIVER_IGNORES = DC_AL | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C };

static void send(Platform *pf, uint32_t site, AudioCommand c) { au_command(pf, PF_CALL_DC(site, DRIVER, DRIVER_IGNORES), c); }

/* ---- A switch, a chest -------------------------------------------------------------------------- */

/* Where the leader stands across the screen, as a pan: the low byte of its
 * screen X. With nobody leading the original reads the slot the marker
 * names, the first past the object tables, as the walk does (field/walk.c). */
static uint8_t leader_pan(const GameState *g)
{
    FieldLeader leader = field_leader(g);
    uint16_t x = leader.nobody_leads ? field_object_screen_x_unchecked(g, leader.obj)
                                     : field_object_screen_x(g, leader.obj);
    return (uint8_t)x;
}

// $C0:1B90 (the switch's), $C0:1BA7 (the chest's)
static void play_at_the_leader(GameState *g, Platform *pf, uint8_t effect)
{
    send(pf, 0xC01BA2,
         (AudioCommand){ .command = AU_START_OTHER_EFFECT, .p1 = effect, .p2 = leader_pan(g), .count = 2 });
}

void field_play_the_switch_sound(GameState *g, Platform *pf) { play_at_the_leader(g, pf, field_switch_sound(g)); }
void field_play_the_chest_sound(GameState *g, Platform *pf) { play_at_the_leader(g, pf, field_chest_sound(g)); }

/* ---- The music round a battle ------------------------------------------------------------------- */

/* The set of effects the driver loads with the next song (the CPU half's
 * cell beside the command, sound_driver_effects_asked): the battle's or the
 * field's. */
enum { FIELD_EFFECTS = 0x00, BATTLE_EFFECTS = 0xFF };
static void ask_for_effects(GameState *g, uint8_t set) { set_sound_driver_effects_asked(g, set); }

/* The song a lost battle leaves the field with. */
enum { LOST_SONG = 0x26 };

static bool keeps_the_field_song(const GameState *g) { return battle_terms(g, BATTLE_TERMS_KEEP_THE_FIELD_SONG); }

// $C0:1BAB (through $C0:0008)
/* The battle's song, saving the field's to come back to, and the battle's
 * effects with it; or, where the event keeps the field's song, the battle's
 * effects alone. */
static void start_the_battle_song(GameState *g, Platform *pf)
{
    if (keeps_the_field_song(g)) {
        au_switch_effects(pf, PF_CALL_DC(0xC01BDF, DRIVER, DRIVER_IGNORES), true);
        return;
    }
    ask_for_effects(g, BATTLE_EFFECTS);
    send(pf, 0xC01BCE, (AudioCommand){ .command = AU_START_SONG_SAVING, .p1 = field_battle_song(g), .count = 1 });
}

/* The music silenced at once, then a song started over it and brought up to
 * full volume over the driver's steps, the effects at full volume at once:
 * after a lost battle its own song, after any other the field's back. Each
 * from the original's own sites, in its order. */
typedef enum { AFTER_ANY_OTHER, AFTER_A_LOSS } MusicBack;

static const struct {
    uint32_t silence, song, rise, effects, more_effects;
    uint8_t rise_steps;
} music_back[] = {
    [AFTER_ANY_OTHER] = { 0xC01C2A, 0xC01C3A, 0xC01C52, 0xC01C65, 0xC01C78, 0x40 },
    [AFTER_A_LOSS] = { 0xC01CA9, 0xC01CB7, 0xC01CCF, 0xC01CE2, 0xC01CF5, 0x80 },
};

static void bring_up(Platform *pf, MusicBack after, AudioCommand song)
{
    send(pf, music_back[after].silence,
         (AudioCommand){ .command = AU_FADE_MUSIC, .p1 = 0, .p2 = 0, .p3 = AU_FULL_VOLUME, .count = 3 });
    send(pf, music_back[after].song, song);
    send(pf, music_back[after].rise, (AudioCommand){ .command = AU_FADE_MUSIC, .p1 = music_back[after].rise_steps,
                                                      .p2 = AU_FULL_VOLUME, .p3 = AU_FULL_VOLUME, .count = 3 });
    send(pf, music_back[after].effects,
         (AudioCommand){ .command = AU_FADE_EFFECTS, .p1 = 0, .p2 = AU_FULL_VOLUME, .count = 2 });
    send(pf, music_back[after].more_effects,
         (AudioCommand){ .command = AU_FADE_MORE_EFFECTS, .p1 = 0, .p2 = AU_FULL_VOLUME, .count = 2 });
}

// $C0:1BE6 (through $C0:000B)
/* After the battle: the lost battle's song, saving none; or the field's
 * effects back alone, where the event kept the field's song; or the field's
 * song back where it was saved. Only a defeat brings its song: the result a
 * script sets goes on as a win does, and so does a defeat the battle's terms
 * let the field go on from. */
static void bring_the_music_back(GameState *g, Platform *pf)
{
    bool lost = battle_how_it_ended(g) == BATTLE_DEFEATED && !battle_terms(g, BATTLE_TERMS_DEFEAT_GOES_ON);
    if (lost) {
        ask_for_effects(g, FIELD_EFFECTS);
        bring_up(pf, AFTER_A_LOSS, (AudioCommand){ .command = AU_START_SONG_SAVING, .p1 = LOST_SONG, .count = 1 });
        return;
    }
    if (keeps_the_field_song(g)) {
        au_switch_effects(pf, PF_CALL_DC(0xC01C89, DRIVER, DRIVER_IGNORES), false);
        return;
    }
    ask_for_effects(g, FIELD_EFFECTS);
    bring_up(pf, AFTER_ANY_OTHER,
             (AudioCommand){ .command = AU_START_SONG_SECOND_SET, .p1 = current_song(g), .count = 1 });
}

/* ---- The entries ------------------------------------------------------------------------------- */

void field_play_the_switch_sound_entry(Snes *s)
{
    field_play_the_switch_sound(state_of(s), m_platform(s));
    m_rts(s);
}

void field_play_the_chest_sound_entry(Snes *s)
{
    field_play_the_chest_sound(state_of(s), m_platform(s));
    m_rts(s);
}

/* The battle's two run on the field's page with the data bank $00, keeping
 * the caller's on the stack, and with A $0100, as the original's setting of
 * the page leaves it: the driver's calls see all three. */
static void onto_the_fields_page(Snes *s)
{
    m_push8(s, s->cpu.db);
    m_push16(s, s->cpu.d);
    s->cpu.a = FIELD_PAGE;
    s->cpu.d = FIELD_PAGE;
    s->cpu.db = 0x00;
}

void field_start_the_battle_song(Snes *s)
{
    onto_the_fields_page(s);
    start_the_battle_song(state_of(s), m_platform(s));
    s->cpu.d = m_pull16(s);
    s->cpu.db = m_pull8(s);
    m_rtl(s);
}

/* The original takes the bank back before the page, so each comes back from
 * the other's bytes: the bank is the caller's page's low byte, and the page
 * the caller's bank over its page's high byte. Both callers put theirs back
 * after the call. */
void field_bring_the_music_back(Snes *s)
{
    onto_the_fields_page(s);
    bring_the_music_back(state_of(s), m_platform(s));
    s->cpu.db = m_pull8(s);
    s->cpu.d = m_pull16(s);
    m_rtl(s);
}

/* The switch's and the chest's guard: on the field's page with the data bank
 * $00, through which the original reads the leader's place, and a leader the
 * original can read one for. */
bool field_sound_at_the_leader_can_play(Snes *s)
{
    if (!field_on_page_bank_zero(s))
        return false;
    vars_peek(true);
    bool ok = field_leader_readable(state_of(s));
    vars_peek(false);
    return ok;
}
