/* Scripts casting their objects: the opening of an object's script says who
 * the object is -- one of the playable characters, one of the people and
 * creatures, or an enemy -- and the field casts it (field/cast.h).
 *
 * A playable character comes in two ways: as the character, which puts it in
 * the party's place it has, or sets it aside where it isn't in the party
 * ($57-$6D name the character, $80 takes it as an operand); or apart from the
 * party, as one of the map's own objects ($81). The people, creatures and
 * enemies have graphics to unpack into bank $7F unless an object of the map
 * shares theirs, and unpacking is still the original's code, so their
 * commands ($82, $83) are bridges: the casting as C either side of the call. */
#include "event/bridge.h"
#include "event/leaves.h"
#include "event/script.h"
#include "field/cast.h"
#include "field/frames.h"

// $C0:41E4-$C0:4214, $C0:421E
/* A playable character takes the object whose script this is. */
static EventFlow cast_character(GameState *g, FieldObject self, uint8_t character)
{
    object_cast_as_character(g, self, character);
    return next_command();
}
EVENT_OPERATION(cast_character, self, character)

// $C0:4476
/* A playable character takes the object as one of the map's own, apart from
 * the party. */
static EventFlow cast_character_apart(GameState *g, FieldObject self, uint8_t character)
{
    object_cast_as_character_apart(g, self, character);
    return next_command();
}
EVENT_OPERATION(cast_character_apart, self, character)

/* The guards': the record tables where the game points them; a character
 * among the seven (each has a cell for the object it is on the map); and a
 * leader the members can stand on, or none: an object the tables hold, kept
 * as the game keeps it (a word whose high byte is clear, which the original
 * reads the leader's place and facing through). */
static bool character_castable(const GameState *g, uint8_t character)
{
    FieldLink leader = party_member_link(g, PARTY_LEADER);
    return character < PLAYABLE_CHARACTERS && cast_tables_in_the_rom(g) &&
           (!field_link_is_object(leader) ||
            (field_link_names_object(leader) && field_party_objects_high(g, PARTY_LEADER) == 0));
}
EVENT_CHECK(character_castable, character)

static bool tables_in_the_rom(const GameState *g) { return cast_tables_in_the_rom(g); }
EVENT_CHECK_TAKING_NOTHING(tables_in_the_rom)

/* ---- The bridges: graphics to unpack ----------------------------------------------------------- */

/* The unpacker ($C3:0002) reads where from and where to from its own cells,
 * and puts back every register it is given: of the registers at the call,
 * the command's machine reads only Y after it, which it leaves to the
 * engine (event/leaves.h). */
enum { UNPACKER = 0xC30002 };
enum { THE_UNPACKER_IGNORES = DC_A | DC_X | DC_N | DC_Z | DC_C };

/* Where each command calls the unpacker, and where the original stops the
 * game when a set runs past the bank's end. */
enum { NPC_UNPACKED_AT = 0xC046BC };
enum { NPC_TOO_FAR = 0xC046D1 };
enum { ENEMY_UNPACKED_AT = 0xC04825 };
enum { ENEMY_TOO_FAR = 0xC0483A };

/* What the command leaves the engine (event/leaves.h): Y where reading the
 * record left it, and the overflow of its last sum (field/leaves.c). */
static void leave_cast_registers(Snes *s, CastGraphics graphics)
{
    const GameState *g = state_of(s);
    s->cpu.y = field_cast_y(graphics.record, graphics.source);
    m_set_flag(s, CPU_FLAG_V, field_cast_overflow(g, graphics.record, graphics.source));
}

/* ... and at the unpacker's call, which keeps them for after it: Y where
 * looking up the set left it, and the overflow of the lookup's sum, clear. */
static void leave_unpack_call_registers(Snes *s, FieldSpriteRecord r)
{
    s->cpu.y = field_cast_y(r, GRAPHICS_TO_UNPACK);
    m_set_flag(s, CPU_FLAG_V, false);
}

/* The set unpacked after the others, from the JSL at `site`; false where the
 * port stopped: the unpacker didn't come back as the engine runs, or the set
 * ran past the bank's end, where the original stops the game (`too_far`, its
 * error's start, on which nothing of the registers is read). */
static bool unpack(Snes *s, FieldSpriteRecord r, uint32_t site, uint32_t too_far)
{
    GameState *g = state_of(s);
    ask_to_unpack(g, r.graphics);
    leave_unpack_call_registers(s, r);
    if (!bridge_call_long(s, site, UNPACKER, THE_UNPACKER_IGNORES, event_machine(s, false)))
        return false;
    if (graphics_unpacked(g))
        return true;
    m_stop_at(s, too_far, DC_A | DC_X | DC_Y | DC_N | DC_V | DC_Z | DC_C);
    return false;
}

// $C0:4590
/* One of the people and creatures takes the object, by its number among
 * them. */
void event_cmd_cast_npc(Snes *s)
{
    enum { LENGTH = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, 0x82);
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + LENGTH)));
    CastGraphics graphics = object_cast_as_npc(g, c.self, c.npc);
    if (graphics.source == GRAPHICS_TO_UNPACK && !unpack(s, graphics.record, NPC_UNPACKED_AT, NPC_TOO_FAR))
        return;
    npc_loaded(g);
    ORIGINAL_EFFECT(leave_cast_registers(s, graphics));
    event_return(s, (EventStep){ (uint16_t)(at + LENGTH), true });
}

/* What taking its place leaves: the sum of its block's tiles, the overflow
 * clear; and Y's low byte alone, the original working the block out with
 * 8-bit index registers -- or a figure's drawing's, where it drew one. */
static void leave_place_registers(Snes *s, bool figure_drawn)
{
    m_set_flag(s, CPU_FLAG_V, false);
    s->cpu.y = figure_drawn ? field_whole_figure_y() : (uint16_t)(s->cpu.y & 0x00FF);
}

// $C0:46DF
/* An enemy takes the object, and its place among the blocks of sprite tiles
 * its script names.
 *
 * An accepted divergence, for both bridges: the original reads the running
 * object, the command's place and its operands again after the unpacking,
 * and the port takes them as they were. They differ only where a set
 * unpacks over them -- across the end of bank $7F into low RAM (whose
 * length the game's sets never reach: a map's unpacked sets fill less than
 * the bank's upper half), or over the script itself (which ends below where
 * the sets start). Fuzzing's random places reach it: 8 of 6,661 states. */
void event_cmd_cast_enemy(Snes *s)
{
    enum { LENGTH = 3, ENEMY_ENDS = 2 };
    GameState *g = state_of(s);
    uint16_t at = event_command_offset(s);
    EventCommand c = event_command_of(s, 0x83);
    ORIGINAL_EFFECT(event_keep_offset(g, (uint16_t)(at + ENEMY_ENDS)));
    CastGraphics graphics = object_cast_as_enemy(g, c.self, c.enemy);
    if (graphics.source == GRAPHICS_TO_UNPACK && !unpack(s, graphics.record, ENEMY_UNPACKED_AT, ENEMY_TOO_FAR))
        return;
    ORIGINAL_EFFECT(leave_cast_registers(s, graphics));
    enemy_takes_its_place(g, m_platform(s), c.self, c.slot);
    ORIGINAL_EFFECT(leave_place_registers(s, object_sprite_layout(g, c.self) == LAYOUT_24));
    event_return(s, (EventStep){ (uint16_t)(at + LENGTH), true });
}

/* The guards': the record tables where the game points them, and an entry of
 * the shared graphics for the set (the original stops the game with none);
 * for an enemy, its block among the blocks and a figure the port draws. */
static bool npc_castable(const GameState *g, uint8_t npc)
{
    FieldSpriteRecord r = field_sprite_record(g, (uint16_t)(FIRST_NPC_RECORD + npc));
    return cast_tables_in_the_rom(g) && cast_graphics_entry_for(g, r.graphics);
}
EVENT_CHECK(npc_castable, npc)

static bool enemy_castable(const GameState *g, uint8_t enemy, uint8_t slot)
{
    return cast_tables_in_the_rom(g) && cast_enemy_fits(g, enemy, slot);
}
EVENT_CHECK(enemy_castable, enemy, slot)

EVENT_OPCODES(event_characters_opcodes,
    { 0x57, OPERATION(cast_character), .character = CONSTANT(0), .length = 1, CHECK(character_castable) },
    { 0x5C, OPERATION(cast_character), .character = CONSTANT(1), .length = 1, CHECK(character_castable) },
    { 0x62, OPERATION(cast_character), .character = CONSTANT(2), .length = 1, CHECK(character_castable) },
    { 0x6A, OPERATION(cast_character), .character = CONSTANT(3), .length = 1, CHECK(character_castable) },
    { 0x68, OPERATION(cast_character), .character = CONSTANT(4), .length = 1, CHECK(character_castable) },
    { 0x6C, OPERATION(cast_character), .character = CONSTANT(5), .length = 1, CHECK(character_castable) },
    { 0x6D, OPERATION(cast_character), .character = CONSTANT(6), .length = 1, CHECK(character_castable) },
    { 0x80, OPERATION(cast_character), .character = OPERAND(1), .length = 2, CHECK(character_castable) },
    { 0x81, OPERATION(cast_character_apart), .character = OPERAND(1), .length = 2, CHECK(tables_in_the_rom) },
    { 0x82, .npc = OPERAND(1), .length = 2, CHECK(npc_castable) },
    { 0x83, .enemy = OPERAND(1), .slot = OPERAND(2), .length = 3, CHECK(enemy_castable) });
