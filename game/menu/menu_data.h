/* What the menus' code reads from the ROM.
 *
 * Every such read goes through an accessor here, named for what the data is,
 * so a build that loads it from somewhere other than the ROM image changes
 * this file and nothing that uses it. The accessors take the game's state
 * (game/rom.h). */
#ifndef CT_GAME_MENU_DATA_H
#define CT_GAME_MENU_DATA_H

#include <stdbool.h>
#include <stdint.h>

#include "menu/vram.h"
#include "charge.h"
#include "rom.h"
#include "state.h"

/* ---- The menus' graphics ------------------------------------------------------------------- */

/* The graphics the menus build their tile sets from (game/menu/graphics.c):
 * four-colour tiles (the small font, 224 of them, its second $100 bytes then
 * replaced; more after the gate screen's setup), three-plane tiles (149 read,
 * the last of them running on into the gate's tiles), the eight windows'
 * designs ($280 bytes a design), and four sixteen-colour tiles, the window
 * layer's. */
enum {
    MENU_ROM_FOUR_COLOUR_TILES_BYTES = 0x0E00, MENU_ROM_REPLACED_TILES_BYTES = 0x0100,
    MENU_ROM_MORE_FOUR_COLOUR_TILES_BYTES = 0x0600, MENU_THREE_PLANE_TILES = 0x95,
    MENU_WINDOW_DESIGN_BYTES = 0x280, MENU_WINDOW_DESIGNS = 8,
};

/* The runs of colours the menus load, a word a colour, from the windows'
 * colours ($FF:9C68), the screens' ($FF:9DF0) and the menus' own ($FF:CB9C):
 * two sets of four for the palette's first rows and two of sixteen; the
 * healing numbers', the items screen's, the main menu's and its options',
 * the arrows', the equipment screen's, the panels' (menu_rom_colour). */
typedef enum {
    MENU_ROM_FIRST_COLOURS, MENU_ROM_SECOND_COLOURS, MENU_ROM_THIRD_COLOURS,
    MENU_ROM_ROW_7_COLOURS, MENU_ROM_ROW_8_COLOURS,
    MENU_ROM_AMOUNT_COLOURS, MENU_ROM_ITEMS_LIST_COLOURS, MENU_ROM_ITEMS_HIGHLIGHT_COLOURS,
    MENU_ROM_MAIN_MENU_COLOURS, MENU_ROM_OPTION_COLOURS, MENU_ROM_ARROW_COLOURS,
    MENU_ROM_EQUIPMENT_COLOURS, MENU_ROM_PANEL_HIGHLIGHT_COLOURS,
    MENU_ROM_TECH_PANEL_COLOURS, MENU_ROM_TECH_PANEL_LIST_COLOURS,
    MENU_ROM_EXCHANGE_PANEL_COLOURS, MENU_ROM_SETTINGS_PANEL_COLOURS,
    MENU_COLOUR_RUNS
} MenuColourRun;

/* The healing's rising numbers: their four colours ($FF:CB9C), the first
 * sprite palette's fifth to eighth, and the upload of their digits' tiles
 * ($C2:FC5A, a record of menu_upload_record's). */
enum {
    MENU_AMOUNT_COLOURS = 4,
    MENU_FIRST_AMOUNT_COLOUR = 0x84,
    MENU_UPLOAD_AMOUNT_DIGITS = 0xFC5A,
};

/* The upload records the sets are sent with (bank C2, menu_upload_record). */
enum {
    MENU_UPLOADS_FIRST_SETS = 0xFC6F, MENU_UPLOAD_SECOND_SET = 0xFC7F, MENU_UPLOAD_LAST_SET = 0xFC87,
    MENU_UPLOADS_WINDOW_SETS = 0xFC8F, MENU_UPLOAD_WINDOW_DESIGN = 0xC548,
};

/* ---- The items ------------------------------------------------------------------------------ */

/* The items' ids fall in five categories, each from its first id
 * ($C2:88A7: weapons 0, armour $5A, helmets $7B, accessories $94, the rest
 * $BC, and the end $F2). Each category's data (the kind, the price, who can
 * use it) and figures (attack or defence, more for an accessory) are records
 * in bank $CC, where each table starts and how long its records are in bank
 * $FF ($CF0E and $CF18, $CF1D and $CF27). */
enum { MENU_ITEM_CATEGORIES = 5, MENU_ITEMS = 0xF2 };

/* ---- The items screen ----------------------------------------------------------------------- */

/* The items screen's window command lists (bank $FF, menu/windows.h): the
 * screen's, the main menu's panel for it, and the window of members an item
 * is used on. */
enum { MENU_WINDOWS_ITEMS_SCREEN = 0xC0A6, MENU_WINDOWS_ITEMS_PANEL = 0xC05D, MENU_WINDOWS_ITEMS_MEMBERS = 0xC0E4 };

/* The same tables by stat: a character's seven stats in the order of their
 * caps, each one's place among the record's stats ($FF:CF2C) and the most it
 * comes to with what the character wears ($FF:CF33, the speed's 16 and the
 * others' 99; the text's limits are the same bytes, menu_number_limit). */
enum { MENU_STATS = 7 };

/* Where each member's sprite stands beside its row of the window an item is
 * used on ($C2:B045, its Y and its X's low byte). */
typedef struct {
    uint8_t y, x;
} MenuSpritePlace;

/* The items screen's colours in bank $FF: the list's eight ($CC34, row 5's
 * first half) and its highlight's four ($CBB4, row 2's), for
 * menu_rom_colour. */

/* ---- What the typesetter inserts ----------------------------------------------------------- */

/* A number's decimal digits are the glyphs from $D4 on. */
enum { MENU_FIRST_DIGIT_GLYPH = 0xD4 };

/* The characters' names are in work RAM, six bytes each from $7E:2C23, Crono's
 * first and the Epoch's eighth; the text finds a character's through the
 * table at $C2:5FD8 (a word each, seven long: an index past it reads on into
 * what follows). */
enum { MENU_NAMES_BANK = 0x7E0000u, MENU_FIRST_NAME = 0x2C23, MENU_NAME_BYTES = 6 };

/* The names of the items ($CC:0B5E), the techs ($CC:15C4) and the enemies
 * ($CC:6500), eleven bytes each; an item's and a tech's start with an icon. */
enum { MENU_NAME_TABLE_BYTES = 11 };

/* Marle's real name, five glyphs ($C2:6146). */
static inline uint32_t menu_marle_real_name(void) { return asset_address(ASSET_MENU_MARLE_REAL_NAME); }

/* The eight window designs' colours ($FF:B210): eight colours a design, the
 * upper half of the palette row a window is drawn in. */
enum { MENU_WINDOW_DESIGN_COLOURS = 8 };

/* A character's portrait: the first of its four tiles ($C2:D4B4, seven
 * characters), and its 16 colours ($FF:CC84, 32 bytes a character; the
 * original's index is 16-bit, so it stays in the bank). */
enum { MENU_PORTRAITS = 7 };

/* The load screen's colours past its windows' ($FF:CC74, eight for the
 * upper half of palette row 2; $FF:CE30, the sixteen of row 7). */
enum { MENU_LOAD_SCREEN_TEXT_COLOURS = 8, MENU_LOAD_SCREEN_ROW_7_COLOURS = 16 };

/* The chapters of the story a slot's label names, by where the story had got
 * to ($FF:D024, 26 of them, each the first value of the story's progress in
 * that chapter). */
enum { MENU_CHAPTERS = 26 };

/* The table of the era labels' graphics ($FF:D396), which the menus' loader
 * takes an era's from. */
static inline uint32_t menu_era_label_strings(void) { return asset_address(ASSET_MENU_ERA_LABELS); }

/* The main menu ($FF): its window ($BD2E) and the window an option's
 * screen opens from ($BD51); its icons' sprites, the six options' along the
 * top, each its place, tile and attributes ($BD5C), the lit icon's tile $20
 * on; the icons' colours, eight ($CC14), and the lit option's own eight
 * ($CC24, a set an option); and the arrows' three ($9E0A). */
enum {
    MENU_WINDOWS_MAIN_MENU = 0xBD2E, MENU_WINDOWS_OPTION_OPENING = 0xBD51,
    MENU_MAIN_MENU_OPTIONS = 6, MENU_LIT_ICON_TILES = 0x20,
    MENU_MAIN_MENU_COLOURS = 8, MENU_ARROW_COLOURS = 3,
};

typedef struct {
    uint8_t x, y, tile, attributes;
} MenuIcon;

/* Naming a character: the letters' grid, eight rows of ten letters, each
 * the letter's text code ($FF:C9AC); the battle mode's screen, its tiles
 * ($FF:F008, tilemap words), and its sixteen colours ($FF:CE9A). */
enum { MENU_NAME_GRID_COLUMNS = 10 };

/* The screens' uploads of their tilemap buffers and line tables, by what
 * they send: the front layer's and the back layer's whole ($7E:2E00, $C00
 * bytes; $7E:3E00, $1000 bytes); the front layer's rows from the third
 * ($7E:2E80, $640 bytes), from $7E:3400 (the shop's members' strips, $600
 * bytes),
 * its lower half ($7E:3600), the back layer's rows from the third
 * ($7E:3E80), the text layer's ($7E:4E00, $680 bytes), the window line
 * tables ($7E:5D00), the text layer's first two rows ($7E:4E00, $80
 * bytes) where a window over the list keeps its lines, and the text
 * layer's whole ($1000 bytes) to the third layer's tilemap where the gate's
 * screen moves it. */
enum {
    MENU_UPLOAD_FRONT_LAYER_WHOLE = 0xFBCE,
    MENU_UPLOAD_BACK_LAYER_WHOLE = 0xFBF8,
    MENU_UPLOAD_FRONT_LAYER = 0xFBE3,
    MENU_UPLOAD_FRONT_LAYER_LOWER_HALF = 0xFBEA,
    MENU_UPLOAD_FRONT_LAYER_STRIPS = 0xFBF1,
    MENU_UPLOAD_BACK_LAYER = 0xFBFF,
    MENU_UPLOAD_TEXT_LAYER = 0xFC1B,
    MENU_UPLOAD_LINE_TABLES = 0xFC3E,
    MENU_UPLOAD_WINDOW_LINES = 0xFC37,
    MENU_UPLOAD_TEXT_LAYER_WHOLE = 0xFC14,
};

/* The load screen's uploads: the era label's graphic, the slot's tilemap in
 * three parts, and the label of the question which finished game to carry
 * on from ($7E:5E00, $600 bytes). */
enum {
    MENU_UPLOAD_ERA_LABEL = 0xD45F,
    MENU_UPLOAD_SLOT_TILEMAP = 0xFBDC,
    MENU_UPLOAD_SLOT_TILEMAP_2 = 0xFC0D,
    MENU_UPLOAD_SLOT_TILEMAP_3 = 0xFC29,
    MENU_UPLOAD_CARRY_ON_LABEL = 0xE88A,
};

/* The graphic the load and save screens set the slots' labels into
 * (menu/save_screen.c), from $7E:6E00: each slot's name, $20 bytes a slot
 * from $7E:6E10, and its chapter's, $200 bytes a slot from $7E:7000. The
 * screens send it to VRAM $7800 on, so a slot's chapter lies at VRAM $7900
 * on, $100 words a slot. Its uploads: the whole ($800 bytes), as the screens
 * open; the chapters' alone ($600 bytes), as the save screen sends them once
 * it has saved in a slot; and its first $200 bytes, the names', to VRAM
 * $7000, once the summary has set them. */
enum {
    MENU_SLOT_NAMES_AT = 0x6E10, MENU_SLOT_NAME_BYTES = 0x20,
    MENU_CHAPTER_LABELS_AT = 0x7000, MENU_CHAPTER_LABEL_BYTES = 0x200,
    MENU_CHAPTER_LABELS_VRAM = 0x7900, MENU_CHAPTER_LABEL_VRAM_WORDS = 0x100,
};
enum { MENU_UPLOAD_SLOT_LABELS = 0xFC53, MENU_UPLOAD_CHAPTER_LABELS = 0xCF30, MENU_UPLOAD_SLOT_NAMES = 0xD198 };

/* The sprite scripts an object script gives its object, by number: where each
 * starts in bank C3 ($C3:E000, a word each). */
enum { MENU_SPRITE_SCRIPT_BANK = 0xC3 };

/* The scenes' resources the object scripts load (MD2 resources in bank C6,
 * 24-bit pointers to streams in bank C3): graphics, decompressed to the Mode 7
 * plane's buffer, and palettes, to the loaded palette's. A resource is named
 * by its number in its table. */
typedef enum { MENU_SCENE_GRAPHICS, MENU_SCENE_PALETTES } MenuSceneResource;
enum { MENU_SCENE_POINTER_BYTES = 3 };

/* A sine of 1024 steps a turn, its values -255 to 255: the wave's size at
 * the step, below zero in the second half of the turn ($C2:225E); and the
 * cosine, the sine a quarter turn on ($C2:225A). */
enum { MENU_SINE_STEPS = 0x400 };

/* The tilts the Day of Lavos's turning world leans back by
 * (menu/turning_world.c): how much its distances grow a line pair down the
 * screen, in 256ths ($C6:EB23, the steeper the later). The frame's check
 * keeps the tilt among them; the original's index would read on into the
 * code after the table. */
enum { MENU_WORLD_TILTS = 24 };

/* The figures a window of the member equipping shows, in the order it lists
 * them, each by where it is among the figures of the member's status record
 * (menu_status_record_figures, which starts MENU_RECORD_FIGURES_PLACE into
 * the record): the equipment screen's nine ($C2:A642: the attack and the
 * defence, then the seven stats) and the shop's seven ($C2:E1DC: the stats
 * alone, in the same order). */
enum { MENU_RECORD_FIGURES_PLACE = 0x36, MENU_EQUIPMENT_FIGURES_SHOWN = 9, MENU_SHOP_FIGURES_SHOWN = 7 };

/* Beside it, an arctangent ($C0:F300): the angle, in the same 256 steps a
 * turn, from 0 along the axis across to 64 along the axis down, at which a
 * point lies `down` and `across` from the start, both in steps of 4 pixels
 * and fewer than 32; a row of 32 for each step down. And the way an angle
 * faces ($C0:F700, a byte an angle: field/object.h's Facing). */
enum { MENU_ARCTANGENT_STEP = 4, MENU_ARCTANGENT_STEPS = 32 };

/* ---- The Epoch's screens -------------------------------------------------------------------- */

/* The map view: the graphics set its markers are drawn from for each world
 * map ($C2:69F0, a byte a map; the story swaps two of them for $6F), and
 * where on the view the landing marker stands, across, for each ($C2:68E4, a
 * word a map). Maps count from the first world map's location, $1F0. */
enum { MENU_FIRST_WORLD_MAP = 0x01F0, MENU_WORLD_MAPS = 8 };


/* The time dial's seven periods: the location each travels to, in the two
 * sets the story moves between ($C2:6C97, and $C2:6CA5 once the story has
 * passed $CC), a word a period, and the angle the gate stands at for each
 * ($C2:6CB3). The original doubles a period in a byte to read the
 * locations, so one past $7F reads from the set's start again. */
enum { MENU_PERIODS = 7, MENU_LATER_PERIODS = 0xCC };


/* ---- The sprites the menus' records show -------------------------------------------------- */

/* A sprite's assignment says what it is made of: its graphics, its frames,
 * its colours and its animations, a number each into the tables below, and
 * its size (bits 0-1: 1, 2, 3 or 6 blocks of 32 by 32 pixels; bit 4: the
 * second of its colour sets). The playable characters' sprites and the
 * others' are three sets of assignments in bank $E4, whose places the table
 * at $C2:F55C gives (a word a set, taken at the set's number doubled as a
 * byte); the first two sets' assignments are five bytes, the rest's ten. The
 * tables of places are at the end of the bank ($E4:FFE0 on, a word each). */
enum { MENU_SPRITE_ASSIGNMENT_BYTES = 5 };

/* The first tile of each of the opening's seven records' sprites ($C2:F548, a
 * word each: tile and attributes), and of the eighth's ($C2:F556). */
enum { MENU_OPENING_RECORDS = 7 };

/* A character's large portrait: the first of its 36 tiles ($FF:CE70, a byte
 * a character), and where in bank $FF its sixteen colours are ($FF:CE78, a
 * word each of the eight). */

/* A character's element: lightning, shadow, water, fire or none ($C2:A2C6, a
 * byte a character, which the ROM keeps doubled: the offset of the element's
 * word in its label's tables). */
typedef enum { MENU_LIGHTNING, MENU_SHADOW, MENU_WATER, MENU_FIRE, MENU_NO_ELEMENT } MenuElement;

/* The equipment screen's slots: each slot's label, ten glyphs in bank C2
 * ($C2:A3BA, ten bytes a slot: where the string starts), and the slot's
 * item's place in a character's record ($C2:A4AB). */
enum { MENU_SLOT_LABEL_BYTES = 10 };

/* The equipment screen's per-line band table at rest, as the slots window
 * closes ($C2:AA06): six bands, three bytes each (the lines, the value),
 * and the table's end. */
enum { MENU_SLOTS_BANDS_AT_REST = 6 };

/* The equipment screen's four colours ($FF:9C70) and the four the main
 * menu's panel for it highlights with ($FF:CBAC), in bank $FF for
 * menu_rom_colour. */

/* ---- The techs ------------------------------------------------------------------------------ */

/* The techs come in groups whose members do them together: each character's
 * own eight (groups 0-6), then fifteen groups of three double techs, then
 * fifteen triple techs, a group each. Each group's first tech is a byte at
 * $C2:BD40; the next group's first ends it, so the last group's end is read
 * from the byte past the table, the first of the kinds' table that follows. */
enum { MENU_TECH_GROUPS = 37 };

/* A tech's effect record ($CC:213F, twelve bytes a tech, as the battle reads
 * it): for a healing tech, the second byte's low five bits are its restoring's
 * factor and the third's top two bits what it restores (HP, MP). */
enum { MENU_TECH_EFFECT_BYTES = 12, MENU_TECH_EFFECT_FACTOR = 1, MENU_TECH_EFFECT_RESTORES = 2 };

/* The items' descriptions: a table of where each string starts, a word each
 * in bank $CC ($CC:2EB1), by the item's number. */
static inline uint32_t menu_item_description_strings(void) { return asset_address(ASSET_MENU_ITEM_DESCRIPTIONS); }

/* The techs' descriptions: a table of where each string starts, a word each
 * in bank $CC ($CC:3A09); after the techs' come the captions of the kinds,
 * from $76. */
enum { MENU_TECH_KIND_CAPTIONS = 0x76 };
static inline uint32_t menu_tech_description_strings(void) { return asset_address(ASSET_BATTLE_DESCRIPTIONS); }

/* The main menu's panel for the techs: the colours of its kinds' tabs and
 * its list ($FF:CE04 and $FF:CDF4, eight each), for menu_rom_colour. */

/* ---- The party exchange -------------------------------------------------------------------- */

/* Its panel's colours ($FF:CC64, eight, palette row 6's first half); its
 * highlight takes the items screen's four. */
enum { MENU_EXCHANGE_PANEL_COLOURS = 8, MENU_EXCHANGE_HIGHLIGHT_COLOURS = 4 };

/* The menus' labels, set by the typesetter (menu/typeset.h): the strings
 * at $FF:CF3B. */
static inline uint32_t menu_label_strings(void) { return asset_address(ASSET_MENU_LABEL_STRINGS); }

/* ---- The save code ------------------------------------------------------------------------ */

/* What a saved game holds: eight runs of work RAM, stored one after the other
 * from the slot's start ($FF:FA7B, five bytes a run: where it is, a 24-bit
 * address, then its length). */
enum { MENU_SAVED_RUNS = 8, MENU_SAVED_RUN_BYTES = 5 };
typedef struct {
    uint32_t at;
    uint16_t bytes;
} MenuSavedRun;

/* What a New Game+ takes over from the game it carries on from: two runs
 * ($FF:FBC1, seven bytes a run: where it starts in the saved game, where it
 * goes in work RAM, a 24-bit address, and its length). */
enum { MENU_CARRIED_RUNS = 2, MENU_CARRIED_RUN_BYTES = 7 };
typedef struct {
    uint16_t from;
    uint32_t at;
    uint16_t bytes;
} MenuCarriedRun;

/* A character's record as a new game starts it ($CC:0000, $50 bytes a
 * character), and the places in it of the equipment a New Game+ puts back on
 * ($FF:FBCF, four bytes: the helmet's, the armour's, the accessory's and the
 * weapon's, taken from the last). */
enum { MENU_STARTING_RECORD_BYTES = 0x50, MENU_STARTING_EQUIPMENT = 4 };

/* What a new game starts the characters' records and what they have learned
 * from ($CC:0000: the seven records, then the techs learned); and the
 * settings it starts with ($C2:FCA6, before the button assignments). */
enum { MENU_NEW_GAME_RECORDS_BYTES = 0x280 };

/* A weapon's colours: its colour set ($CD:6CEC, a byte a weapon) and the
 * set's three colours ($D1:4B00, three words a set), which go in its
 * wearer's sprite palette -- the battle's colours for its members too
 * (battle/setup_data.h). */
enum { MENU_WEAPON_COLOURS = 3 };

/* The single techs each of the five triple techs a rock gives needs learned
 * by its characters ($CC:2953, three a rock: the tech's number from 1). */
enum { MENU_ROCKS = 5 };

/* ---- The screens' digits ------------------------------------------------------------------ */

/* Two sets of the ten digits in four-colour tiles: the small digits a
 * screen's number tiles are made from ($FF:CAFC), and the items screen's
 * amounts' ($FF:9390), sixteen bytes a digit. */
enum { MENU_DIGITS = 10, MENU_DIGIT_BYTES = 16 };

/* ---- The settings screen ------------------------------------------------------------------ */

/* Each option's icon at the description window's left ($FF:D562): the tile
 * word of its top left, the four tiles on from it. The descriptions' strings
 * ($FF:D3FE), an option's first, then the values' texts. */
static inline uint32_t menu_settings_description_strings(void) { return asset_address(ASSET_MENU_SETTINGS_DESCRIPTIONS); }

/* Each of the ten boxes' place in the front layer's tilemap ($C2:C41B, a
 * tile's index), and whether it is ticked for a cell of 0 ($C2:C42F: the
 * sound output's, whose cell holds mono). */
enum { MENU_SETTINGS_BOXES = 10 };

/* The buttons an assignment can be given, in the order left and right step
 * through them ($C2:C207, seven and an end); the game's own assignments
 * ($C2:FCA9, nine); and the glyph of a button ($FF:D554, the buttons from
 * the top bit down and a ninth for none). */
enum { MENU_SETTINGS_BUTTON_CHOICES = 7, MENU_SETTINGS_NO_MORE_CHOICES = 0xFF };

/* Each assignment's row on the settings screen ($C2:C5A7: its tile index in
 * the front layer's tilemap, from $7E:2E00's start), and the assignment a
 * row gives a button to ($C2:C2BD). */
enum { MENU_SETTINGS_BUTTON_ROWS = 6 };


/* ---- The shops ---------------------------------------------------------------------------- */

/* Each shop's goods ($CC:2C6D, the place of a shop's list in bank $CC; a
 * list is the items it sells, 0 after the last, thirty-two bytes read). */
enum { MENU_SHOP_LIST_BYTES = 32 };

/* The upload of the member sprites' graphics the shop unpacks ($C2:DB98, a
 * record of menu_upload_record's). */
enum { MENU_UPLOAD_SHOP_SPRITES = 0xDB98 };

/* ---- The End of Time's gate screen -------------------------------------------------------- */

/* The swirl's graphics, packed ($FF:E8DC); the first tile of each of its six
 * rows ($FF:F6F2, a tilemap word each); its eight colours ($FF:F706) and four
 * more ($FF:F6FE); and the wave its bands of lines follow ($FF:F759, 64
 * words). */
enum {
    MENU_GATE_ROWS = 6,
    MENU_GATE_PALETTE_COLOURS = 8,
    MENU_GATE_COLOURS = 4,
    MENU_GATE_WAVE = 64,
};

/* ---- The font ---------------------------------------------------------------------------- */

/* The proportional font the dialog and the menus write with: glyphs 12
 * pixels wide and 12 lines tall, 2 bits a pixel, a line's two bitplanes one
 * byte after the other. A glyph's left eight pixels are 24 bytes of their own
 * at $FF:2060 + 24 a glyph; its right four share a byte with the next glyph's,
 * 24 bytes a pair at $FF:3860, the even glyph in the high nibble. The text
 * engine works out both addresses and hands them to the glyph drawer, which
 * reads the font through them: `at` is a 24-bit address. */
static inline uint8_t menu_font_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

static inline uint16_t menu_text_column_offset(const GameState *g, unsigned column)
{
    return asset_read16(g, ASSET_MENU_CHARACTER_NAME_PLACE, 0xEu + 2u * column);
}

static inline uint16_t menu_text_column_offset_16_colour(const GameState *g, unsigned column)
{
    return asset_read16(g, ASSET_MENU_CHARACTER_NAME_PLACE, 0x8Eu + 2u * column);
}

/* How far the pen moves after each of the font's glyphs ($C2:60E6), by the
 * glyph's number less $A0 (a byte: the two-byte glyphs take their low byte's
 * entry, and the table runs on into what follows it). */
static inline uint8_t menu_glyph_width(const GameState *g, uint8_t index)
{
    return asset_read8(g, ASSET_DIALOG_FONT_WIDTHS, index);
}

/* A byte of the menus' three-plane tiles, `at` bytes into them. */
static inline uint8_t menu_three_plane_tile_byte(const GameState *g, uint32_t at)
{
    return asset_read8(g, ASSET_MENU_THREE_PLANE_TILES, at);
}

/* The heights the healing's numbers rise to, a frame at a time, above
 * their member's sprite: the HP's path from its start, the MP's from the
 * 43rd, each ending with a negative height ($FF:CEBA). */
static inline uint8_t menu_amount_height(const GameState *g, uint8_t step)
{
    return asset_read8(g, ASSET_MENU_AMOUNT_HEIGHT, step);
}

/* A colour of a run, `past` bytes on from the run's first (a set of an
 * option's). */
static inline uint16_t menu_rom_colour_past(const GameState *g, MenuColourRun run, unsigned past, unsigned i)
{
    static const struct {
        AssetId colours;
        uint16_t from;
    } runs[MENU_COLOUR_RUNS] = {
        [MENU_ROM_FIRST_COLOURS] = { ASSET_WINDOW_COLOURS, 0x00 },
        [MENU_ROM_EQUIPMENT_COLOURS] = { ASSET_WINDOW_COLOURS, 0x08 },
        [MENU_ROM_SECOND_COLOURS] = { ASSET_WINDOW_COLOURS, 0x10 },
        [MENU_ROM_ROW_8_COLOURS] = { ASSET_SCREEN_COLOURS, 0x00 },
        [MENU_ROM_ARROW_COLOURS] = { ASSET_SCREEN_COLOURS, 0x1A },
        [MENU_ROM_AMOUNT_COLOURS] = { ASSET_MENU_COLORS, 0x00 },
        [MENU_ROM_PANEL_HIGHLIGHT_COLOURS] = { ASSET_MENU_COLORS, 0x10 },
        [MENU_ROM_ITEMS_HIGHLIGHT_COLOURS] = { ASSET_MENU_COLORS, 0x18 },
        [MENU_ROM_THIRD_COLOURS] = { ASSET_MENU_COLORS, 0x20 },
        [MENU_ROM_ROW_7_COLOURS] = { ASSET_MENU_COLORS, 0x28 },
        [MENU_ROM_SETTINGS_PANEL_COLOURS] = { ASSET_MENU_COLORS, 0x48 },
        [MENU_ROM_MAIN_MENU_COLOURS] = { ASSET_MENU_COLORS, 0x78 },
        [MENU_ROM_OPTION_COLOURS] = { ASSET_MENU_COLORS, 0x88 },
        [MENU_ROM_ITEMS_LIST_COLOURS] = { ASSET_MENU_COLORS, 0x98 },
        [MENU_ROM_EXCHANGE_PANEL_COLOURS] = { ASSET_MENU_COLORS, 0xC8 },
        [MENU_ROM_TECH_PANEL_LIST_COLOURS] = { ASSET_MENU_COLORS, 0x258 },
        [MENU_ROM_TECH_PANEL_COLOURS] = { ASSET_MENU_COLORS, 0x268 },
    };
    return asset_read16(g, runs[run].colours, runs[run].from + past + 2u * i);
}

static inline uint16_t menu_rom_colour(const GameState *g, MenuColourRun run, unsigned i)
{
    return menu_rom_colour_past(g, run, 0, i);
}

static inline uint8_t menu_item_category_start(const GameState *g, unsigned category)
{
    return asset_read8(g, ASSET_MENU_ITEM_CATEGORY_START, category);
}

static inline uint16_t menu_item_data_table(const GameState *g, unsigned category)
{
    return asset_read16(g, ASSET_MENU_ITEM_DATA_TABLE, 2u * category);
}

static inline uint8_t menu_item_data_record_bytes(const GameState *g, unsigned category)
{
    return asset_read8(g, ASSET_MENU_ITEM_DATA_RECORD_BYTES, category);
}

static inline uint16_t menu_item_figures_table(const GameState *g, unsigned category)
{
    return asset_read16(g, ASSET_MENU_ITEM_FIGURES_TABLE, 2u * category);
}

static inline uint8_t menu_item_figures_record_bytes(const GameState *g, unsigned category)
{
    return asset_read8(g, ASSET_MENU_ITEM_FIGURES_RECORD_BYTES, category);
}

static inline uint8_t menu_item_table_byte(const GameState *g, uint16_t at)
{
    return rom_read8_in_bank_of(g, ASSET_WEAPON_RECORDS, at);
}

/* The items' names, eleven bytes each (menu_item_name): where an item's is
 * in their bank, which the menus keep an item each, and what such a place
 * names. */
static inline uint16_t menu_item_names_place(uint8_t item)
{
    return (uint16_t)((uint16_t)asset_address(ASSET_BATTLE_ITEM_NAME_CHAR) + item * MENU_NAME_TABLE_BYTES);
}

static inline uint32_t menu_item_name_at(uint16_t place) { return asset_place_address(ASSET_BATTLE_ITEM_NAME_CHAR, place); }

/* The label of an item's kind its row shows ("Weapon", "Item" and the
 * rest): where the string is in bank $FF, from a word an item at $FF:B310. */
static inline uint32_t menu_item_kind_label(const GameState *g, uint8_t item)
{
    return asset_place_address(ASSET_MENU_ITEM_KIND_LABEL, asset_read16(g, ASSET_MENU_ITEM_KIND_LABEL, 2u * item));
}

/* Who an item of the rest is used on, a byte an item by its place in the
 * category ($CC:1BB5): one member, or every member. */
static inline uint8_t menu_item_targets(const GameState *g, uint8_t index)
{
    return asset_read8(g, ASSET_ITEM_AIM_MODE, index);
}

/* The categories in the order the sort writes them (the rest first, then the
 * weapons, the armour, the helmets and the accessories): each one's first
 * item ($C2:AC12) and how many it has ($C2:AC17). */
static inline uint8_t menu_sort_category_first(const GameState *g, unsigned order)
{
    return asset_read8(g, ASSET_MENU_SORT_CATEGORY_FIRST, order);
}

static inline uint8_t menu_sort_category_items(const GameState *g, unsigned order)
{
    return asset_read8(g, ASSET_MENU_SORT_CATEGORY_FIRST, 0x5u + order);
}

/* The label of the figure the window of who can equip an item shows, by the
 * item's category ($C2:AF3A, a word each): where in bank $FF its string is. */
static inline uint16_t menu_item_power_label_at(const GameState *g, uint8_t category)
{
    return asset_read16(g, ASSET_MENU_ITEM_POWER_LABEL_AT, 2u * category);
}

/* The chances an item used by chance has, the limit of each of its first
 * three outcomes in a hundred ($C2:B13D). */
static inline uint8_t menu_chance_limit(const GameState *g, unsigned outcome)
{
    return asset_read8(g, ASSET_MENU_CHANCE_LIMIT, outcome);
}

/* A tab's figure (menu_use_effect's index): its offset among a character's
 * figures ($FF:CF2B), its cap ($FF:CF32), and its base's offset among the
 * bases ($C2:B315). */
static inline uint8_t menu_figure_offset(const GameState *g, uint8_t figure)
{
    return asset_read8(g, ASSET_MENU_FIGURE_OFFSET, figure);
}

static inline uint8_t menu_figure_cap(const GameState *g, uint8_t figure)
{
    return asset_read8(g, ASSET_MENU_FIGURE_CAP, figure);
}

static inline uint8_t menu_base_figure_offset(const GameState *g, uint8_t figure)
{
    return asset_read8(g, ASSET_MENU_BASE_FIGURE_OFFSET, figure);
}

static inline uint8_t menu_stat_place(const GameState *g, unsigned stat)
{
    return asset_read8(g, ASSET_MENU_STAT_PLACE, stat);
}

static inline uint8_t menu_stat_cap(const GameState *g, unsigned stat)
{
    return asset_read8(g, ASSET_MENU_STAT_CAP, stat);
}

/* Ayla's fists' part of her attack, by her level ($CC:2D69, a word a
 * level; the original doubles the level in a byte). */
static inline uint16_t menu_fist_power(const GameState *g, uint8_t level)
{
    return asset_read16(g, ASSET_MENU_FIST_POWER, (uint8_t)(level * 2));
}

static inline MenuSpritePlace menu_member_sprite_place(const GameState *g, uint8_t member)
{
    return (MenuSpritePlace){ asset_read8(g, ASSET_MENU_MEMBER_SPRITE_PLACE, 2u * member),
                              asset_read8(g, ASSET_MENU_MEMBER_SPRITE_PLACE, 0x1u + 2u * member) };
}

/* The glyph a hex digit is set with ($C2:5D03, a word each: 0-9, then A-F). */
static inline uint16_t menu_hex_digit_glyph(const GameState *g, uint8_t digit)
{
    return asset_read16(g, ASSET_MENU_HEX_DIGIT_GLYPH, 2u * (digit & 0x0Fu));
}

/* The dictionary: the commonest pieces of the script's English, each its
 * count of glyphs and the glyphs ($DE:FA00, a word each, in bank $DE). */
static inline uint32_t menu_dictionary_word(const GameState *g, uint16_t entry)
{
    return asset_place_address(ASSET_MENU_DICTIONARY_WORD, asset_read16(g, ASSET_MENU_DICTIONARY_WORD, 2u * entry));
}

static inline uint16_t menu_character_name_place(const GameState *g, unsigned index)
{
    return asset_read16(g, ASSET_MENU_CHARACTER_NAME_PLACE, 2u * index);
}

static inline uint32_t menu_item_name(const GameState *g, uint8_t item)
{
    (void)g;
    return asset_address(ASSET_BATTLE_ITEM_NAME_CHAR) + (uint32_t)(item * MENU_NAME_TABLE_BYTES);
}

static inline uint32_t menu_tech_name(const GameState *g, uint8_t tech)
{
    (void)g;
    return asset_address(ASSET_BATTLE_TECH_NAME_CHAR) + (uint32_t)(tech * MENU_NAME_TABLE_BYTES);
}

static inline uint32_t menu_enemy_name(const GameState *g, uint8_t enemy)
{
    (void)g;
    return asset_address(ASSET_SETUP_ENEMY_NAME_CHAR) + (uint32_t)(enemy * MENU_NAME_TABLE_BYTES);
}

/* ---- Text --------------------------------------------------------------------------------- */

/* The menus' strings, and the numbers they show, wherever the caller's
 * pointer puts them: the ROM's own strings, or names and numbers in work RAM.
 * `at` is a 24-bit address; a string read on past the end of its bank goes on
 * into the next, as the original's indexed reads carry. */
static inline uint8_t menu_text_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The tiles a hex digit is drawn with ($C2:F27D), one for each of 0-15. */
static inline uint8_t menu_hex_digit_tile(const GameState *g, uint8_t digit)
{
    return asset_read8(g, ASSET_MENU_HEX_DIGIT_TILE, (digit & 0x0Fu));
}

/* The limits the text's limited numbers are shown under ($FF:CF33): a value
 * at or over its limit shows as dashes. `which` is the string's operand, the
 * table's index. */
static inline uint8_t menu_number_limit(const GameState *g, uint8_t which)
{
    return asset_read8(g, ASSET_MENU_STAT_CAP, which);
}

/* The address of each control code's handler in bank C2 ($C2:EFD4), which the
 * original's dispatch leaves behind it. */
static inline uint16_t menu_text_code_handler(const GameState *g, uint8_t code)
{
    return asset_read16(g, ASSET_MENU_TEXT_CODE_HANDLER, 2u * code);
}

/* ---- The windows --------------------------------------------------------------------------- */

/* The window command lists, and the runs of tilemap bytes their commands
 * copy, in bank $FF (menu/windows.c). `at` is the offset in the bank. */
static inline uint8_t menu_window_byte(const GameState *g, uint16_t at) { return rom_read8_in_bank_of(g, ASSET_MENU_WINDOW_STRINGS, at); }

/* A string a window command names, where its place in their bank leads. */
static inline uint32_t menu_window_string_at(uint16_t place) { return asset_place_address(ASSET_MENU_WINDOW_STRINGS, place); }

/* The strings a window command names by number: where each starts in bank
 * $FF ($FF:C457, a word each). */
static inline uint16_t menu_window_string(const GameState *g, uint16_t number)
{
    return asset_read16(g, ASSET_MENU_WINDOW_STRINGS, (uint16_t)(2u * number));
}

/* A window's inside is a pattern of four rows of four tiles ($C2:EEF5), each
 * the tile of the style that many past its first. */
static inline uint8_t menu_window_inside_tile(const GameState *g, unsigned row, unsigned column)
{
    return asset_read8(g, ASSET_MENU_WINDOW_INSIDE_TILE, 4u * row + column);
}

static inline uint16_t menu_window_design_colour(const GameState *g, uint8_t design, unsigned i)
{
    return asset_read16(g, ASSET_WINDOW_DESIGN_COLORS, 16u * design + 2u * i);
}

/* ---- The load screen ---------------------------------------------------------------------- */

/* The values the load screen's per-line effect gives its slot rows ($C2:D044,
 * 33 bytes). */
static inline uint8_t menu_slot_row_value(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_SLOT_ROW_VALUE, i);
}

static inline uint8_t menu_portrait_first_tile(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_MENU_PORTRAIT_FIRST_TILE, character);
}

static inline uint16_t menu_portrait_colour(const GameState *g, uint8_t character, unsigned i)
{
    enum { PORTRAIT_COLOURS = 0xE8 };   /* in menu_colors, a portrait's 16 a character */
    return asset_read16(g, ASSET_MENU_COLORS, PORTRAIT_COLOURS + character * 32u + 2u * i);
}

static inline uint16_t menu_load_screen_text_colour(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_COLORS, 0xD8u + 2u * i);
}

static inline uint16_t menu_load_screen_row_7_colour(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_COLORS, 0x294u + 2u * i);
}

static inline uint8_t menu_chapter_start(const GameState *g, uint8_t chapter)
{
    return asset_read8(g, ASSET_MENU_CHAPTER_START, chapter);
}

/* How far right each slot's name label is moved ($C2:D329): pairs of pixels. */
static inline uint8_t menu_slot_name_shift(const GameState *g, uint8_t slot)
{
    return asset_read8(g, ASSET_MENU_SLOT_NAME_SHIFT, slot);
}

/* The era each location is in ($FF:D296): a nibble a location, the even
 * location's in the low half of its byte. */
static inline uint8_t menu_era_of_location(const GameState *g, uint16_t location)
{
    uint16_t pair = asset_read16(g, ASSET_MENU_ERA_OF_LOCATION, location >> 1);
    uint16_t era = (uint16_t)(pair & 0x01FF);
    return (uint8_t)(((location & 1) ? era >> 4 : era) & 0x0F);
}

/* ---- Uploads ------------------------------------------------------------------------------ */

/* An upload of the menus' own, a record in bank C2 laid out as the VRAM
 * queue's entries (menu/vram.h): the VRAM word, the source's address and bank,
 * and the size. `at` is its place in the bank, which the record
 * wraps in. */
static inline MenuUpload menu_upload_record(const GameState *g, uint16_t at)
{
    uint8_t b[7];
    for (unsigned i = 0; i < 7; i++)
        b[i] = rom_read8_in_bank_of(g, ASSET_MENU_NEW_GAME_SETTING, (uint16_t)(at + i));
    /* The old queue copied an unused eighth byte, sometimes from code. */
    charge_rom_accesses((GameState *)g, 1);
    return (MenuUpload){ (uint16_t)(b[0] | b[1] << 8), (uint32_t)b[4] << 16 | (uint16_t)(b[2] | b[3] << 8),
                         (uint16_t)(b[5] | b[6] << 8), 0 };
}

static inline uint16_t menu_option_colour(const GameState *g, uint8_t option, unsigned i)
{
    return menu_rom_colour_past(g, MENU_ROM_OPTION_COLOURS, 2u * MENU_MAIN_MENU_COLOURS * option, i);
}

static inline MenuIcon menu_main_menu_icon(const GameState *g, unsigned option)
{
    uint32_t at = 4u * option;
    return (MenuIcon){ asset_read8(g, ASSET_MENU_MAIN_MENU_ICON, at), asset_read8(g, ASSET_MENU_MAIN_MENU_ICON, at + 1),
                       asset_read8(g, ASSET_MENU_MAIN_MENU_ICON, at + 2),
                       asset_read8(g, ASSET_MENU_MAIN_MENU_ICON, at + 3) };
}

static inline uint8_t menu_name_grid_letter(const GameState *g, unsigned row, unsigned column)
{
    return asset_read8(g, ASSET_MENU_NAME_GRID_LETTER, row * MENU_NAME_GRID_COLUMNS + column);
}

static inline uint16_t menu_battle_mode_screen_tile(const GameState *g, unsigned tile)
{
    return asset_read16(g, ASSET_MENU_BATTLE_MODE_SCREEN_TILE, 2u * tile);
}

static inline uint16_t menu_battle_mode_colour(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_BATTLE_MODE_COLOUR, 2u * i);
}

/* ---- The sprite-object mode --------------------------------------------------------------- */

/* The object scripts (menu/script.h), the sprite scripts and the sprite groups
 * they show (menu/sprites.c), wherever their pointers put them: bank C2's and
 * C3's scripts, or ones a screen built in work RAM. `at` is a 24-bit address,
 * whose offset the callers wrap in its bank. */
/* The items each of the equipment screen's slots takes ($C2:89DC): a range
 * of item numbers, the first and the one past the last, a pair a slot. */
static inline uint8_t menu_slot_first_item(const GameState *g, uint8_t slot)
{
    return asset_read8(g, ASSET_MENU_SLOT_FIRST_ITEM, 2u * slot);
}

static inline uint8_t menu_slot_past_items(const GameState *g, uint8_t slot)
{
    return asset_read8(g, ASSET_MENU_SLOT_FIRST_ITEM, 0x1u + 2u * slot);
}

static inline uint8_t menu_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* A colour of the palettes the scripts load rows from, a word (its second
 * byte the next address's, across a bank's end too). */
static inline uint16_t menu_colour(const GameState *g, uint32_t at)
{
    return (uint16_t)(rom_read8(g, at & 0xFFFFFFu) | rom_read8(g, (at + 1) & 0xFFFFFFu) << 8);
}

static inline uint8_t menu_sprite_script_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

static inline uint8_t menu_sprite_group_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

/* The address in bank C2 of each object script command's handler ($C2:0F91),
 * by which the port finds the command's own handler and its time. */
static inline uint16_t menu_script_handler(const GameState *g, uint8_t opcode)
{
    return asset_read16(g, ASSET_MENU_SCRIPT_HANDLER, 2u * opcode);
}

static inline uint16_t menu_sprite_script_start(const GameState *g, uint8_t script)
{
    return asset_read16(g, ASSET_MENU_SPRITE_SCRIPT_START, 2u * script);
}

/* For each plane of the map ($C2:0556, $C2:055C, $C2:0562, a word a plane):
 * where its tile groups and its map are in bank $7E, and where its tilemap is
 * in VRAM. The original indexes them by the plane's number doubled, a byte
 * that a script's plane number 0 turns into $1FE, past the tables into the
 * code after them. */
static inline uint16_t menu_plane_word(const GameState *g, uint32_t table, uint8_t plane)
{
    return asset_read16(g, ASSET_MENU_PLANE_GROUPS, table + (uint16_t)(2u * plane));
}

static inline uint16_t menu_plane_groups(const GameState *g, uint8_t plane) { return menu_plane_word(g, 0x0, plane); }

static inline uint16_t menu_plane_map(const GameState *g, uint8_t plane) { return menu_plane_word(g, 0x6, plane); }

static inline uint16_t menu_plane_tilemap(const GameState *g, uint8_t plane) { return menu_plane_word(g, 0xC, plane); }

/* The menu scripts the map view and the time dial run, and the scripts of
 * the map view's markers: where each starts, `at` bytes into its table. */
static inline uint32_t menu_map_view_script(uint16_t at) { return asset_address(ASSET_MENU_MAP_VIEW_SCRIPTS) + at; }
static inline uint32_t menu_map_view_marker(uint16_t at) { return asset_address(ASSET_MENU_MAP_VIEW_MARKER_SCRIPTS) + at; }
static inline uint32_t menu_time_dial_script(uint16_t at) { return asset_address(ASSET_MENU_TIME_DIAL_SCRIPTS) + at; }

static inline uint32_t menu_scene_stream(const GameState *g, MenuSceneResource kind, uint8_t number)
{
    AssetId streams = kind == MENU_SCENE_GRAPHICS ? ASSET_OVERWORLD_SPRITE_GRAPHICS : ASSET_OVERWORLD_PALETTES;
    uint32_t at = MENU_SCENE_POINTER_BYTES * number;
    return asset_read16(g, streams, at) | (uint32_t)asset_read8(g, streams, at + 2) << 16;
}

/* The way the party faces coming back to the world map from a location, by
 * the way it faced arriving there (the low two bits of the location's flags):
 * turned round ($C2:2509, four bytes). */
static inline uint8_t menu_return_flags(const GameState *g, uint8_t kind)
{
    return asset_read8(g, ASSET_MENU_RETURN_FLAGS, kind);
}

/* The sizes of the waves on the layer over the map ($C0:F900): a byte a
 * step, 1024 steps a turn, of which the second half is read below zero. */
static inline uint8_t menu_wave_size(const GameState *g, uint16_t step) { return asset_read8(g, ASSET_SINES, step); }

static inline int16_t menu_sine(const GameState *g, uint16_t phase)
{
    uint16_t step = phase & (MENU_SINE_STEPS - 1);
    int16_t size = menu_wave_size(g, step);
    return step >= MENU_SINE_STEPS / 2 ? (int16_t)-size : size;
}

static inline int16_t menu_cosine(const GameState *g, uint16_t phase)
{
    return menu_sine(g, (uint16_t)(phase + MENU_SINE_STEPS / 4));
}

static inline uint8_t menu_world_tilt_step(const GameState *g, uint16_t tilt)
{
    return asset_read8(g, ASSET_MENU_WORLD_TILT_STEP, tilt);
}

/* A sine wave of 256 steps a turn ($C0:F800), as signed bytes: the table the
 * field reads as its directions' velocity factors, read whole by the scenes'
 * objects. */
static inline int8_t menu_sine_step(const GameState *g, uint8_t angle)
{
    return (int8_t)asset_read8(g, ASSET_DIRECTION_FACTORS, angle);
}

static inline uint8_t menu_equipment_figure_shown(const GameState *g, unsigned i)
{
    return (uint8_t)(asset_read8(g, ASSET_MENU_EQUIPMENT_FIGURE_SHOWN, i) - MENU_RECORD_FIGURES_PLACE);
}

static inline uint8_t menu_shop_figure_shown(const GameState *g, unsigned i)
{
    return (uint8_t)(asset_read8(g, ASSET_MENU_SHOP_FIGURE_SHOWN, i) - MENU_RECORD_FIGURES_PLACE);
}

static inline uint8_t menu_arctangent(const GameState *g, unsigned down, unsigned across)
{
    return asset_read8(g, ASSET_SLOPE_ANGLES, down * MENU_ARCTANGENT_STEPS + across);
}

static inline uint8_t menu_facing_of_angle(const GameState *g, uint8_t angle)
{
    return asset_read8(g, ASSET_DIRECTION_FACINGS, angle);
}

/* The menus' random bytes ($C0:FE00, 256 of them), which the sprite-object
 * mode's scenes take in turn; the healing draws six from a frame's place,
 * the last few reading on past the table. */
static inline uint8_t menu_random_byte(const GameState *g, unsigned index)
{
    return asset_read8(g, ASSET_RANDOM_BYTES, index);
}

/* The sixteen velocities a scene's object may be given at random ($C6:F640):
 * across, then down, each 16.16 fixed point. */
static inline uint32_t menu_scene_velocity_x(const GameState *g, uint8_t pick)
{
    uint32_t at = 8u * pick;
    return asset_read16(g, ASSET_MENU_SCENE_VELOCITY_X, at) |
           (uint32_t)asset_read16(g, ASSET_MENU_SCENE_VELOCITY_X, at + 2) << 16;
}

static inline uint32_t menu_scene_velocity_y(const GameState *g, uint8_t pick)
{
    uint32_t at = 8u * pick + 4;
    return asset_read16(g, ASSET_MENU_SCENE_VELOCITY_X, at) |
           (uint32_t)asset_read16(g, ASSET_MENU_SCENE_VELOCITY_X, at + 2) << 16;
}

static inline uint8_t menu_map_view_graphics(const GameState *g, uint16_t map)
{
    return asset_read8(g, ASSET_MENU_MAP_VIEW_GRAPHICS, map);
}

static inline uint16_t menu_landing_marker_x(const GameState *g, uint16_t map)
{
    return asset_read16(g, ASSET_MENU_LANDING_MARKER_X, (uint16_t)(2u * map));
}

/* The map view's cursor's path: its zoom across ($C6:F560) and down
 * ($C6:F5A0) at each of 32 steps, words, by the step's byte offset. */
static inline uint16_t menu_cursor_path_zoom_x(const GameState *g, uint16_t at)
{
    return asset_read16(g, ASSET_MENU_CURSOR_PATH_ZOOM_X, at);
}

static inline uint16_t menu_cursor_path_zoom_y(const GameState *g, uint16_t at)
{
    return asset_read16(g, ASSET_MENU_CURSOR_PATH_ZOOM_Y, at);
}

/* The map view's sprites' sixteen colours (after the cursor's tiles, two
 * runs of 32 bytes), and the byte after them. */
static inline uint8_t menu_map_view_colour_byte(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_MAP_VIEW_COLORS, i);
}

static inline uint16_t menu_period_location(const GameState *g, bool later_set, uint8_t period)
{
    enum { LATER_SET = 0x0E };   /* the later set's seven words after the first's */
    return asset_read16(g, ASSET_MENU_PERIOD_LOCATIONS, (later_set ? LATER_SET : 0) + (uint8_t)(period << 1));
}

static inline uint16_t menu_period_angle(const GameState *g, uint8_t period)
{
    return asset_read16(g, ASSET_MENU_PERIOD_ANGLES, 2u * period);
}

/* The gate's triangle: its three corners about the screen's centre, a signed
 * pair each ($C2:6F2D). */
static inline int8_t menu_gate_corner_x(const GameState *g, unsigned corner)
{
    return (int8_t)asset_read8(g, ASSET_MENU_GATE_CORNER_X, 2u * corner);
}

static inline int8_t menu_gate_corner_y(const GameState *g, unsigned corner)
{
    return (int8_t)asset_read8(g, ASSET_MENU_GATE_CORNER_X, 0x1u + 2u * corner);
}

/* The dial's digits, by period: where each stands ($C2:71DD, across then
 * down), and the sprite scripts that show and hide it ($C2:71EB, $C2:71EC).
 * The original doubles the period in a byte. */
static inline uint8_t menu_digit_x(const GameState *g, uint8_t period)
{
    return asset_read8(g, ASSET_MENU_DIGIT_X, (uint8_t)(period << 1));
}

static inline uint8_t menu_digit_y(const GameState *g, uint8_t period)
{
    return asset_read8(g, ASSET_MENU_DIGIT_X, 0x1u + (uint8_t)(period << 1));
}

static inline uint8_t menu_digit_show_script(const GameState *g, uint8_t period)
{
    return asset_read8(g, ASSET_MENU_DIGIT_X, 0xEu + (uint8_t)(period << 1));
}

static inline uint8_t menu_digit_hide_script(const GameState *g, uint8_t period)
{
    return asset_read8(g, ASSET_MENU_DIGIT_X, 0xFu + (uint8_t)(period << 1));
}

/* The time dial's compressed streams ($C2:7220): where each is, in the bank
 * its loader names, and where in bank $7F it goes, a word each. */
static inline uint16_t menu_gate_stream_source(const GameState *g, uint8_t stream)
{
    return asset_read16(g, ASSET_MENU_GATE_STREAM_SOURCE, 4u * stream);
}

static inline uint16_t menu_gate_stream_dest(const GameState *g, uint8_t stream)
{
    return asset_read16(g, ASSET_MENU_GATE_STREAM_SOURCE, 0x2u + 4u * stream);
}

/* The gate's tiles go to sixteen rows of VRAM ($C2:72B8, a word a row), and
 * its tilemap's rows are picked from a table of row numbers wherever its
 * pointer puts it (the dial's own at $C2:7321). */
static inline uint16_t menu_gate_tile_row(const GameState *g, unsigned row)
{
    return asset_read16(g, ASSET_MENU_GATE_TILE_ROW, 2u * row);
}

/* Only the row number is used. Keep the time of the discarded high-byte
 * read without loading the byte beyond the last row. */
static inline uint8_t menu_gate_row_pick(const GameState *g, uint16_t row_of_vram)
{
    uint8_t row = asset_read8(g, ASSET_MENU_GATE_TILEMAP_ROWS, row_of_vram);
    charge_rom_accesses((GameState *)g, 1);
    return row;
}

/* The gate's plane: two pixels a byte, a colour in each nibble, the left one
 * in the high nibble ($C6:F700, 1024 bytes). */
static inline uint8_t menu_gate_pixel_pair(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_GATE_PIXEL_PAIR, i);
}

/* The End of Time's gate screen's setup, twelve bytes ($FF:D57A). */
static inline uint8_t menu_gate_screen_setup(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_GATE_SCREEN_SETUP, i);
}

static inline uint16_t menu_sprite_set_place(const GameState *g, uint8_t set_doubled)
{
    return asset_read16(g, ASSET_MENU_OPENING_RECORD_TILE, 0x14u + set_doubled);
}

static inline uint8_t menu_sprite_assignment_byte(const GameState *g, uint16_t at)
{
    return asset_read8_at_place(g, ASSET_FIELD_SPRITE_RECORDS, at);
}

/* The graphics and the frames of a sprite: a 24-bit address each, three
 * bytes a sprite in the tables $E4:FFE0 and $E4:FFE2 place; `at` is the entry's
 * place, the table's own offset added. */
static inline uint16_t menu_sprite_graphics_table(const GameState *g)
{
    return asset_read16(g, ASSET_SPRITE_RESOURCE_BASES, 0);
}

static inline uint16_t menu_sprite_frames_table(const GameState *g)
{
    return asset_read16(g, ASSET_SPRITE_RESOURCE_BASES, 0x2u);
}

static inline uint32_t menu_sprite_place(const GameState *g, uint16_t at)
{
    return (uint32_t)rom_read16_in_bank_of(g, ASSET_SPRITE_RESOURCE_BASES, at) |
           (uint32_t)rom_read8_in_bank_of(g, ASSET_SPRITE_RESOURCE_BASES, (uint16_t)(at + 2u)) << 16;
}

/* A sprite's animations: where their frames and their frame times start in
 * bank $E4, a word a sprite in the tables $E4:FFE4 and $E4:FFE6 place (the
 * next sprite's frames' start ends this one's). */
static inline uint16_t menu_sprite_animation_frames_table(const GameState *g)
{
    return asset_read16(g, ASSET_SPRITE_RESOURCE_BASES, 0x4u);
}

static inline uint16_t menu_sprite_animation_times_table(const GameState *g)
{
    return asset_read16(g, ASSET_SPRITE_RESOURCE_BASES, 0x6u);
}

static inline uint16_t menu_sprite_table_word(const GameState *g, uint16_t at)
{
    return rom_read16_in_bank_of(g, ASSET_SPRITE_RESOURCE_BASES, at);
}

/* Where a place in the sprites' tables' bank leads, which a record keeps. */
static inline uint32_t menu_sprite_tables_at(uint16_t place) { return asset_place_address(ASSET_SPRITE_RESOURCE_BASES, place); }

/* A sprite's colours: twelve colours (24 bytes) at the place the table
 * $E4:FFE8 gives, a word a sprite; its second set $200 bytes on. */
static inline uint16_t menu_sprite_colours_table(const GameState *g)
{
    return asset_read16(g, ASSET_SPRITE_RESOURCE_BASES, 0x8u);
}

static inline uint8_t menu_sprite_colour_byte(const GameState *g, uint16_t at)
{
    return asset_read8_at_place(g, ASSET_FIELD_SPRITE_PALETTES, at);
}

/* The blocks a sprite's size class has ($C2:F562), and the bytes a frame of
 * that many blocks takes in its frames ($C2:F755: 40 a block). */
static inline uint8_t menu_sprite_blocks(const GameState *g, uint8_t size)
{
    return asset_read8(g, ASSET_MENU_OPENING_RECORD_TILE, 0x1Au + (size & 3u));
}

static inline uint8_t menu_sprite_frame_bytes(const GameState *g, uint8_t blocks)
{
    return asset_read8(g, ASSET_MENU_SPRITE_FRAME_BYTES, blocks);
}

/* A byte of a sprite's graphics, frames or animation lists, wherever its
 * record's pointer puts it. */
static inline uint8_t menu_sprite_byte(const GameState *g, uint32_t at) { return rom_read8(g, at & 0xFFFFFFu); }

static inline uint16_t menu_opening_record_tile(const GameState *g, unsigned record)
{
    return (uint16_t)(asset_read8(g, ASSET_MENU_OPENING_RECORD_TILE, 2u * record) |
                      asset_read8(g, ASSET_MENU_OPENING_RECORD_TILE, 0x1u + 2u * record) << 8);
}

static inline uint16_t menu_last_record_tile(const GameState *g)
{
    return asset_read16(g, ASSET_MENU_OPENING_RECORD_TILE, 0xEu);
}

/* A byte of pixels mirrored left to right ($C0:FD00). */
static inline uint8_t menu_mirrored_pixels(const GameState *g, uint8_t b)
{
    return asset_read8(g, ASSET_MIRRORED_BYTES, b);
}

/* ---- The menus' input and portraits -------------------------------------------------------- */

/* The frames held buttons wait before they repeat, by the pacing's phase
 * ($C2:84D0: 0 quick, 1 before the first repeat). */
static inline uint8_t menu_repeat_delay(const GameState *g, uint8_t phase)
{
    return asset_read8(g, ASSET_MENU_REPEAT_DELAY, phase);
}

static inline uint8_t menu_large_portrait_first_tile(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_MENU_LARGE_PORTRAIT_FIRST_TILE, character);
}

static inline uint16_t menu_large_portrait_colours(const GameState *g, unsigned slot)
{
    return asset_read16(g, ASSET_MENU_LARGE_PORTRAIT_COLOURS, 2u * slot);
}

static inline uint16_t menu_large_portrait_colour(const GameState *g, uint16_t colours, unsigned i)
{
    return asset_read16_at_place(g, ASSET_MENU_LARGE_PORTRAIT_PALETTES, (uint16_t)(colours + 2u * i));
}

static inline MenuElement menu_character_element(const GameState *g, uint8_t character)
{
    return (MenuElement)(asset_read8(g, ASSET_MENU_CHARACTER_ELEMENT, character) / 2u);
}

/* An element's label, six tiles by two beside a member's portrait: its first
 * tile word ($FF:CE88), and its sixteen colours (where in bank $FF they are,
 * $FF:CE92). */
static inline uint16_t menu_element_label_tile(const GameState *g, MenuElement element)
{
    return asset_read16(g, ASSET_MENU_ELEMENT_LABEL_TILE, 2u * element);
}

static inline uint16_t menu_element_label_colour(const GameState *g, MenuElement element, unsigned i)
{
    uint16_t colours = asset_read16(g, ASSET_MENU_ELEMENT_LABEL_COLOUR, 2u * element);
    return rom_read16_in_bank_of(g, ASSET_MENU_COLORS, (uint16_t)(colours + 2u * i));
}

static inline uint32_t menu_slot_label(const GameState *g, uint8_t slot)
{
    (void)g;
    return asset_address(ASSET_MENU_SLOT_LABELS) + (uint32_t)(slot * MENU_SLOT_LABEL_BYTES);
}

static inline uint8_t menu_slot_record_place(const GameState *g, uint8_t slot)
{
    return asset_read8(g, ASSET_MENU_SLOT_RECORD_PLACE, slot);
}

static inline uint8_t menu_slots_bands_at_rest_byte(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_SLOTS_BANDS_AT_REST_BYTE, i);
}

static inline uint8_t menu_tech_group_first(const GameState *g, uint8_t group)
{
    return asset_read8(g, ASSET_MENU_TECH_GROUP_FIRST, group);
}

/* Each kind of tech's groups (the characters' own, the doubles, the triples):
 * its first group and how many it has ($C2:BD65, two bytes a kind). */
static inline uint8_t menu_tech_kind_first_group(const GameState *g, uint8_t kind)
{
    return asset_read8(g, ASSET_MENU_TECH_GROUP_FIRST, 0x25u + 2u * kind);
}

static inline uint8_t menu_tech_kind_groups(const GameState *g, uint8_t kind)
{
    return asset_read8(g, ASSET_MENU_TECH_GROUP_FIRST, 0x26u + 2u * kind);
}

/* A group's members, a bit a character from Crono's bit 7 ($CC:2963); and a
 * character's bit in such a byte ($FF:F9BB, nine entries, the last 0). */
static inline uint8_t menu_tech_group_members(const GameState *g, uint8_t group)
{
    return asset_read8(g, ASSET_MENU_TECH_GROUP_MEMBERS, group);
}

static inline uint8_t menu_character_bit(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_MENU_CHARACTER_BIT, character);
}

static inline uint8_t menu_tech_effect_byte(const GameState *g, uint8_t tech, unsigned n)
{
    return asset_read8(g, ASSET_PARTY_EFFECT_BYTES, (uint32_t)(tech * MENU_TECH_EFFECT_BYTES) + n);
}

/* Who a tech is used on: the first of its two bytes at $CC:1ACB, bit 0 set
 * for every member at once. */
static inline uint8_t menu_tech_targets(const GameState *g, uint8_t tech)
{
    return asset_read8(g, ASSET_PARTY_TECH_AIM, 2u * tech);
}

/* What each member's part of a tech costs in MP ($CC:253B), by the part's
 * number: a single tech's is the tech itself. */
static inline uint8_t menu_tech_part_mp(const GameState *g, uint16_t part)
{
    /* the table's place in its bank wraps round the bank, as the original's word does */
    enum { COSTS_IN_THEIR_BANK = 0x253B };
    return asset_read8(g, ASSET_PARTY_EFFECT_MP_COST, (uint32_t)(uint16_t)(COSTS_IN_THEIR_BANK + part) - COSTS_IN_THEIR_BANK);
}

/* The techs screen's member rows: the sprite beside each row stands this far
 * down the screen ($C2:BB1A, a byte a row: the three rows, then the row a
 * slide brings in from above and the one from below). */
static inline uint8_t menu_tech_row_sprite_y(const GameState *g, uint16_t row)
{
    return asset_read8(g, ASSET_MENU_TECH_ROW_SPRITE_Y, row);
}

/* The window of members a tech is used on: each member's sprite's place
 * ($C2:BF2F, a row's Y then its X's low byte). */
static inline MenuSpritePlace menu_tech_target_sprite_place(const GameState *g, uint8_t member)
{
    return (MenuSpritePlace){ asset_read8(g, ASSET_MENU_TECH_TARGET_SPRITE_PLACE, 2u * member),
                              asset_read8(g, ASSET_MENU_TECH_TARGET_SPRITE_PLACE, 0x1u + 2u * member) };
}

/* The frame a character's sprite holds while it uses a healing tech
 * ($C2:B519, a byte a character). */
static inline uint8_t menu_tech_casting_frame(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_MENU_TECH_CASTING_FRAME, character);
}

static inline uint16_t menu_exchange_row_at(const GameState *g, unsigned place)
{
    return asset_read16(g, ASSET_MENU_EXCHANGE_ROW_AT, 2u * place);
}

static inline MenuSpritePlace menu_exchange_sprite_place(const GameState *g, unsigned place)
{
    return (MenuSpritePlace){ asset_read8(g, ASSET_MENU_EXCHANGE_SPRITE_PLACE, 2u * place),
                              asset_read8(g, ASSET_MENU_EXCHANGE_SPRITE_PLACE, 0x1u + 2u * place) };
}

/* ---- The menus' display ------------------------------------------------------------------- */

/* The colors the menus' color math ramps step through ($C2:8771, sixteen
 * words), copied into work RAM before each table is built. */
static inline uint16_t menu_math_ramp_color(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_MATH_RAMP_COLOR, 2u * i);
}

/* The values of the window line table's sixteen groups ($FF:FC57). */
static inline uint8_t menu_window_line_value(const GameState *g, unsigned group)
{
    return asset_read8(g, ASSET_MENU_WINDOW_LINE_VALUE, group);
}

static inline uint8_t menu_saved_runs_byte(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_SAVED_RUNS_BYTE, i);
}

static inline MenuSavedRun menu_saved_run(const GameState *g, unsigned run)
{
    uint32_t at = run * MENU_SAVED_RUN_BYTES;
    return (MenuSavedRun){ (uint32_t)asset_read16(g, ASSET_MENU_SAVED_RUNS_BYTE, at) |
                               (uint32_t)asset_read8(g, ASSET_MENU_SAVED_RUNS_BYTE, at + 2) << 16,
                           asset_read16(g, ASSET_MENU_SAVED_RUNS_BYTE, at + 3) };
}

static inline uint8_t menu_carried_runs_byte(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_CARRIED_RUNS_BYTE, i);
}

static inline MenuCarriedRun menu_carried_run(const GameState *g, unsigned run)
{
    uint32_t at = run * MENU_CARRIED_RUN_BYTES;
    return (MenuCarriedRun){ asset_read16(g, ASSET_MENU_CARRIED_RUNS_BYTE, at),
                             (uint32_t)asset_read16(g, ASSET_MENU_CARRIED_RUNS_BYTE, at + 2) |
                                 (uint32_t)asset_read8(g, ASSET_MENU_CARRIED_RUNS_BYTE, at + 4) << 16,
                             asset_read16(g, ASSET_MENU_CARRIED_RUNS_BYTE, at + 5) };
}

/* Where each slot's game is in save memory, as the original addresses it on
 * the bus ($FF:FBE1, a word a slot). */
static inline uint16_t menu_saved_game_on_the_bus(const GameState *g, uint8_t slot)
{
    return asset_read16(g, ASSET_MENU_SAVED_GAME_ON_THE_BUS, 2u * slot);
}

static inline uint8_t menu_starting_record_byte(const GameState *g, uint8_t character, uint8_t at)
{
    return asset_read8(g, ASSET_CHARACTER_START_RECORDS, (uint32_t)(character * MENU_STARTING_RECORD_BYTES) + at);
}

static inline uint8_t menu_starting_equipment_place(const GameState *g, unsigned which)
{
    return asset_read8(g, ASSET_MENU_CARRIED_RUNS_BYTE, 0xEu + which);
}

static inline uint8_t menu_new_game_record_byte(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_CHARACTER_START_RECORDS, i);
}

static inline uint8_t menu_new_game_setting(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_NEW_GAME_SETTING, 0xD8u + i);
}

static inline uint8_t menu_weapon_colour_set(const GameState *g, uint8_t weapon)
{
    return asset_read8(g, ASSET_SETUP_MEMBER_COLOR_SET, weapon);
}

static inline uint16_t menu_weapon_colour(const GameState *g, uint8_t set, unsigned i)
{
    return asset_read16(g, ASSET_BATTLE_MEMBER_COLORS, (uint32_t)(set * MENU_WEAPON_COLOURS + i) * 2u);
}

/* ---- The techs as the menus open ---------------------------------------------------------- */

/* The double techs' characters: two for each of the fifteen groups
 * ($CC:24B3, three bytes a group, the characters in the second and third). */
static inline uint8_t menu_double_tech_character(const GameState *g, uint8_t group, unsigned which)
{
    return asset_read8(g, ASSET_MENU_DOUBLE_TECH_CHARACTER, 3u * group + which);
}

/* Each character's part in each of a double group's three techs ($CC:28DB,
 * six bytes a group: a tech's first character's part, then its second's). */
static inline uint8_t menu_double_tech_part(const GameState *g, uint8_t group, unsigned tech, unsigned which)
{
    return asset_read8(g, ASSET_MENU_DOUBLE_TECH_PART, 6u * group + 2u * tech + which);
}

/* The triple techs' parts, one after another: each tech's characters' in the
 * order of their bits from Crono's ($CC:2935). */
static inline uint8_t menu_triple_tech_part(const GameState *g, unsigned n)
{
    return asset_read8(g, ASSET_MENU_TRIPLE_TECH_PART, n);
}

static inline uint8_t menu_rock_tech_needs(const GameState *g, unsigned rock, unsigned which)
{
    return asset_read8(g, ASSET_MENU_ROCK_TECH_NEEDS, 3u * rock + which);
}

/* How many of a character's single techs show in the menus before its
 * locked ones are unlocked ($FF:F951, a byte a character; $FF none locked). */
static inline uint8_t menu_techs_before_the_lock(const GameState *g, uint8_t character)
{
    return asset_read8(g, ASSET_MENU_TECHS_BEFORE_THE_LOCK, character);
}

static inline uint8_t menu_small_digit_byte(const GameState *g, unsigned digit, unsigned at)
{
    return asset_read8(g, ASSET_MENU_SMALL_DIGIT_BYTE, digit * MENU_DIGIT_BYTES + at);
}

static inline uint8_t menu_amount_digit_byte(const GameState *g, unsigned digit, unsigned at)
{
    return asset_read8(g, ASSET_SMALL_FONT, 0x530u + digit * MENU_DIGIT_BYTES + at);
}

static inline uint8_t menu_settings_option_cell(const GameState *g, uint8_t option)
{
    return asset_read8(g, ASSET_MENU_SETTINGS_OPTION_CELL, option);
}

static inline uint8_t menu_settings_first_value_text(const GameState *g, uint8_t option)
{
    return asset_read8(g, ASSET_MENU_SETTINGS_OPTION_CELL, 0xCu + option);
}

static inline uint16_t menu_settings_option_icon(const GameState *g, uint8_t option)
{
    return asset_read16(g, ASSET_MENU_SETTINGS_OPTION_ICON, 2u * option);
}

static inline uint16_t menu_settings_box_place(const GameState *g, uint8_t box)
{
    return asset_read16(g, ASSET_MENU_SETTINGS_BOX_PLACE, 2u * box);
}

static inline bool menu_settings_box_ticked_for_off(const GameState *g, uint8_t box)
{
    return (asset_read8(g, ASSET_MENU_SETTINGS_BOX_PLACE, 0x14u + box) & 1u) != 0;
}

static inline uint8_t menu_settings_button_choice(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_SETTINGS_BUTTON_CHOICE, i);
}

static inline uint8_t menu_default_button_assignment(const GameState *g, unsigned i)
{
    return asset_read8(g, ASSET_MENU_NEW_GAME_SETTING, 0xDBu + i);
}

static inline uint16_t menu_button_glyph(const GameState *g, unsigned bit_from_the_top)
{
    return asset_read16(g, ASSET_MENU_BUTTON_GLYPH, 2u * bit_from_the_top);
}

static inline uint16_t menu_settings_button_row_place(const GameState *g, unsigned row)
{
    return asset_read16(g, ASSET_MENU_SETTINGS_BUTTON_ROW_PLACE, 2u * row);
}

static inline uint8_t menu_settings_row_assignment(const GameState *g, unsigned row)
{
    return asset_read8(g, ASSET_MENU_SETTINGS_ROW_ASSIGNMENT, row);
}

/* The assignments a button given to one is taken from ($C2:C2C5, the place
 * of a list in bank C2 for each of the eight: a count, then the
 * assignments). The table has seven lists, so the eighth's place is the
 * first list's first two bytes, and its list lies in bank C2's code. */
static inline uint16_t menu_settings_conflicts_place(const GameState *g, unsigned assignment)
{
    return asset_read16(g, ASSET_MENU_SETTINGS_ROW_ASSIGNMENT, 0x8u + 2u * assignment);
}

static inline uint8_t menu_settings_conflicts_count(const GameState *g, unsigned assignment)
{
    return asset_read8_at_place(g, ASSET_MENU_SETTINGS_ROW_ASSIGNMENT, menu_settings_conflicts_place(g, assignment));
}

static inline uint8_t menu_settings_conflict(const GameState *g, unsigned assignment, unsigned i)
{
    return asset_read8_at_place(g, ASSET_MENU_SETTINGS_ROW_ASSIGNMENT,
                                (uint16_t)(menu_settings_conflicts_place(g, assignment) + 1u + i));
}

static inline uint8_t menu_shop_good(const GameState *g, uint8_t shop, unsigned i)
{
    uint16_t list = asset_read16(g, ASSET_MENU_SHOP_GOOD, 2u * shop);
    return asset_read8_at_place(g, ASSET_MENU_SHOP_GOOD, (uint16_t)(list + i));
}

/* Where each member's sprite stands along the shop's bottom window
 * ($C2:DC74, an X each). */
static inline uint8_t menu_shop_member_x(const GameState *g, unsigned member)
{
    return asset_read8(g, ASSET_MENU_SHOP_MEMBER_X, member);
}

/* The place in a character's record of the slot an item of each category
 * goes in ($C2:DE1D; the ROM has a copy at $C2:894F): the weapon, the
 * armour, the helmet, the accessory. The other items' category reads the
 * byte past the table, the first of the routine after it in both copies, 8,
 * a byte of the record that is no slot. */
static inline uint8_t menu_category_slot_place(const GameState *g, uint8_t category)
{
    return asset_read8(g, ASSET_MENU_CATEGORY_SLOT_PLACE, category);
}

/* The quantity window's list of what an item of each category shows beside
 * its count ($C2:DFC5, a window list in bank $FF each). */
static inline uint16_t menu_shop_quantity_windows(const GameState *g, uint8_t category)
{
    return asset_read16(g, ASSET_MENU_SHOP_QUANTITY_WINDOWS, 2u * category);
}

static inline uint16_t menu_gate_row_first_tile(const GameState *g, unsigned row)
{
    return asset_read16(g, ASSET_MENU_GATE_ROW_FIRST_TILE, 2u * row);
}

static inline uint16_t menu_gate_palette_colour(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_GATE_ROW_FIRST_TILE, 0x14u + 2u * i);
}

static inline uint16_t menu_gate_colour(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_GATE_ROW_FIRST_TILE, 0xCu + 2u * i);
}

static inline uint16_t menu_gate_wave(const GameState *g, unsigned i)
{
    return asset_read16(g, ASSET_MENU_GATE_WAVE, 2u * i);
}

static inline uint8_t menu_screen_opened_on(const GameState *g, uint8_t opening)
{
    return asset_read8(g, ASSET_MENU_SCREEN_OPENED_ON, opening);
}

static inline uint16_t menu_screen_step_list(const GameState *g, uint8_t screen)
{
    return asset_read16(g, ASSET_MENU_SCREEN_STEP_LIST, 2u * screen);
}

/* Four rows of tiles repeated across the scene's small Mode 7 picture. */
static inline uint8_t menu_scene_pattern_tile(const GameState *g, unsigned tile)
{
    return asset_read8(g, ASSET_MENU_SCENE_PATTERN_TILE, tile);
}

/* The travelling scroll effect uses the cartridge's initial code bytes as
 * its irregular pattern. Keep that resource choice behind this accessor;
 * a converted asset can replace it without exposing a code bank to the effect. */
static inline uint8_t menu_scene_scroll_pattern_byte(const GameState *g, unsigned byte)
{
    return asset_read8(g, ASSET_MENU_SCENE_SCROLL_PATTERN, byte);
}

/* Menu graphics selected by an upload record, including RAM-built tilemaps. */
static inline PfData menu_upload_data(uint32_t source, uint32_t bytes) { return rom_data(source, bytes); }

/* Font tiles, replacement tiles and window designs copied into screen buffers. */
static inline PfData menu_screen_graphics_data(AssetId graphics, uint32_t bytes)
{
    return rom_data(asset_address(graphics), bytes);
}

/* The two tiles marking the Epoch's map view. */
static inline PfData menu_map_view_tiles(bool second)
{
    return rom_data(asset_address(ASSET_MENU_MAP_VIEW_TILES) + (second ? 32u : 0u), 32);
}

static inline uint16_t menu_string_offset(const GameState *g, uint32_t strings, uint8_t string)
{
    return rom_read16(g, (strings + 2u * string) & 0xFFFFFFu);
}

#endif
