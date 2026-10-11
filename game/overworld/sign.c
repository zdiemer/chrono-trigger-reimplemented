/* The place's sign: the name of the place the leader stands by, shown over
 * the party on the world map.
 *
 * The sign is an object of the overworld. When the places' watcher finds the
 * leader on an exit's tile it names the place (overworld_sign, places.c); the
 * sign then types the name a glyph a frame (menu/typeset.h) into a label in
 * work RAM, and once it is set sends its two lines to VRAM and shows them as
 * a row of six sprites, centred over the party -- over the Epoch, a little
 * below it, while the party flies it -- following it as it walks. When the
 * leader leaves the place's ground the name is forgotten and the label
 * cleared, and the sign waits for the next; so it does when the menus put the
 * screen back (overworld_sign_again), to set the name again. While the Epoch
 * leaves through time the sign stays where it was. */
#include "overworld/frames.h"
#include "overworld/sign.h"

#include "charge.h"
#include "menu/sprites.h"
#include "menu/typeset.h"
#include "menu/vram.h"
#include "original_effects.h"
#include "overworld/overworld_data.h"
#include "ram.h"
#include "vars.h"
#include "wram.h"

/* The sign's steps. */
enum { SETS_UP = 0, WAITS_FOR_A_NAME = 1, TYPES_THE_NAME = 2, SHOWS_THE_NAME = 3, SIGN_STEPS = 4 };

/* The label: two lines of sixteen-colour tiles, the upper and the lower
 * halves of the six 16x16 sprites, of which each line's first twelve tiles
 * ($180 bytes) show; they go to the sprites' tiles at VRAM $1E00 and $1F00. */
enum { LABEL_LINE_BYTES = 0x200, LABEL_SHOWN_BYTES = 0x180, LABEL_BYTES = 2 * LABEL_LINE_BYTES };
enum { UPPER_TILES_VRAM = 0x1E00, LOWER_TILES_VRAM = 0x1F00 };

/* The name types a glyph a frame. */
enum { GLYPHS_A_FRAME = 1 };

/* The sprite group's count, in the sign's sprite script (overworld_data.h),
 * is 0 until the sign shows its six sprites. */
enum { GROUP_COUNT_AT = 4, SIGN_SPRITES = 6 };

/* Drawn on the map at the highest priority, in the first palette, from the
 * tiles the name is sent to (the script's sprites name them). */
enum { SIGN_LOOK = MENU_LOOK_ON_THE_MAP | MENU_LOOK_PRIORITY };

/* While the party flies the Epoch the sign shows below it. */
enum { BELOW_THE_EPOCH = 0x40 };

// $C2:5775
void overworld_build_the_sign_script(GameState *g)
{
    for (unsigned at = 0; at < OVERWORLD_SIGN_SPRITES_BYTES; at++)
        set_overworld_sign_sprites(g, at, overworld_sign_sprites_byte(g, at));
}

// $C2:5798
/* The original clears the label's first word and moves the zero on through
 * the rest of it, a block move of all but its first byte. */
void overworld_clear_the_sign_label(GameState *g)
{
    wram_fill8(g, RAM_OVERWORLD_SIGN_LABEL, LABEL_BYTES, 0);
    ORIGINAL_EFFECT(charge_block_move(g, LABEL_BYTES - 1));
}

// $C2:563F
static void set_up(GameState *g, MenuObject self)
{
    overworld_set_step(g, self, WAITS_FOR_A_NAME);
    set_menu_object_slots_look(g, self, SIGN_LOOK);
    set_menu_object_slots_tiles(g, self, 0);
    overworld_build_the_sign_script(g);
    set_menu_object_slots_sprite_script(g, self, RAM_OVERWORLD_SIGN_SPRITES);
}

// $C2:566A
static void start_typing(GameState *g, MenuObject self)
{
    overworld_set_step(g, self, TYPES_THE_NAME);
    set_menu_object_slots_sprite_frames(g, self, 0);
    set_menu_text_string(g, overworld_sign(g));
    set_menu_text_strings(g, OVERWORLD_PLACE_NAMES);
    set_menu_text_layout(g, MENU_TEXT_A_TILE_FURTHER_LEFT);
    set_menu_text_tiles(g, RAM_OVERWORLD_SIGN_LABEL);
    TIMED_AS(g, 0xC257DF, menu_text_start(g));
}

// $C2:5699, $C2:56AB
/* A glyph of the name; once it is all set, its lines go to VRAM in the next
 * vblank and the sign shows. True once it does. */
static bool type_a_glyph(GameState *g, MenuObject self)
{
    set_menu_text_budget(g, GLYPHS_A_FRAME);
    TIMED_AS(g, 0xC25823, menu_text_go_on(g));
    if (menu_text_stop(g) == MENU_TEXT_OUT_OF_BUDGET)
        return false;
    overworld_set_step(g, self, SHOWS_THE_NAME);
    const MenuObjectUpload lines[2] = {
        { RAM_OVERWORLD_SIGN_LABEL, UPPER_TILES_VRAM, LABEL_SHOWN_BYTES, MENU_PORT_WORD_BY_WORD },
        { RAM_OVERWORLD_SIGN_LABEL + LABEL_LINE_BYTES, LOWER_TILES_VRAM, LABEL_SHOWN_BYTES, MENU_PORT_WORD_BY_WORD },
    };
    (void)menu_queue_object_uploads(g, lines, 2);
    return true;
}

// $C2:571E
/* Over the party, or below the Epoch it flies, the name's middle over it. */
static void show_over_the_party(GameState *g, MenuObject self)
{
    WorldSpot at = overworld_party_travel(g) == TRAVEL_IN_THE_EPOCH
                       ? (WorldSpot){ overworld_epoch_x(g), (uint16_t)(overworld_epoch_y(g) + BELOW_THE_EPOCH) }
                       : overworld_party_spot(g, PARTY_LEADER);
    uint16_t half_the_name = menu_text_reading_pen(g) / 2;
    overworld_put_walker(g, self, (WorldSpot){ (uint16_t)(at.x - half_the_name), at.y });
    set_overworld_sign_sprites(g, GROUP_COUNT_AT, SIGN_SPRITES);
    overworld_animate(g);
}

// $C2:56F9
/* The name shows while the leader stays on the place's ground; off it, or
 * when the screen comes back from the menus, the label is cleared for the
 * next name (the first time with one last frame shown). */
static void show(GameState *g, MenuObject self)
{
    if (overworld_sign(g) == 0) {
        overworld_set_step(g, self, WAITS_FOR_A_NAME);
        overworld_clear_the_sign_label(g);
    } else if (overworld_sign_again(g)) {
        overworld_set_step(g, self, WAITS_FOR_A_NAME);
        set_overworld_sign_again(g, 0);
        overworld_clear_the_sign_label(g);
        return;
    }
    if (overworld_epoch_state(g) & EPOCH_LEAVING_THROUGH_TIME)
        return;
    show_over_the_party(g, self);
}

// $C2:5628
void overworld_show_the_sign(GameState *g, MenuObject self)
{
    switch (overworld_step(g, self)) {
    case SETS_UP:
        set_up(g, self);
        /* fall through */
    case WAITS_FOR_A_NAME:
        if (overworld_sign(g) == 0)
            break;
        start_typing(g, self);
        /* fall through */
    case TYPES_THE_NAME:
        if (!type_a_glyph(g, self))
            break;
        /* fall through */
    default:
        show(g, self);
        break;
    }
}

bool overworld_sign_can_show(const GameState *g, MenuObject self)
{
    enum { QUEUE_BYTES = MENU_OBJECT_VRAM_QUEUE_COUNT * MENU_OBJECT_UPLOAD_BYTES, LINES = 2 };
    OverworldStep step = overworld_step(g, self);
    if (step >= SIGN_STEPS || !menu_object_queue_is_whole(g))
        return false;
    if (step <= WAITS_FOR_A_NAME && overworld_sign(g) != 0
        && !menu_text_can_be_typed(g, OVERWORLD_PLACE_NAMES, overworld_sign(g), RAM_OVERWORLD_SIGN_LABEL))
        return false;
    if (step == TYPES_THE_NAME && !menu_text_can_carry_on(g))
        return false;
    if (step != SHOWS_THE_NAME
        && menu_object_vram_queue_end(g) + LINES * MENU_OBJECT_UPLOAD_BYTES > QUEUE_BYTES)
        return false;
    return menu_sprite_list_is_sound(g) && (step == SETS_UP || menu_sprite_script_is_well_formed(g, self));
}
