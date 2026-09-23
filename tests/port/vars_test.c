/* Checks the accessors generated from re/ram.toml (game/vars.h, backed by
 * runtime/port/vars_ram.c) on a machine with no ROM:
 *
 * - an ordinary accessor reads and writes the bytes the original's code does,
 *   low byte first (the controller cells, on the page the reader sets);
 * - an accessor on a page kind reaches the cells of the instance it is given:
 *   a menu record, the scene's own page or a sequence channel's, and nothing
 *   of the others;
 * - <page>_page_of turns each declared D into its instance, refuses every
 *   other D, and counts the refusals it made (runtime/port/vars_pages.h).
 *
 * Exits non-zero on failure.
 *
 *   vars_test */
#include <stdio.h>
#include <string.h>

#include "port/machine.h"
#include "port/vars_ram.h"
#include "snes/system.h"
#include "vars.h"

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static Snes machine;

static uint8_t wram(uint32_t at) { return m_read8(&machine, at); }

static unsigned long refused(const char *page, uint16_t *last)
{
    unsigned n;
    const VarsPageRefusals *r = vars_page_refusals(&n);
    for (unsigned i = 0; i < n; i++)
        if (!strcmp(r[i].page, page)) {
            *last = r[i].last_d;
            return r[i].refused;
        }
    return 0;
}

static void check_input_cells(GameState *g)
{
    set_input_held(g, 0x1234);
    CHECK(wram(0x7E00F2) == 0x34 && wram(0x7E00F3) == 0x12, "input_held is the word at $00F2");
    set_input_mapped(g, 2, 0xBEEF);
    CHECK(wram(0x7E00FA) == 0xEF && wram(0x7E00FB) == 0xBE, "input_mapped(2) is the word at $00FA");
    CHECK(input_held(g) == 0x1234 && input_mapped(g, 2) == 0xBEEF, "the input cells read back");
}

static void check_menu_records(GameState *g)
{
    static const uint16_t declared[] = { 0x1800, 0x1840, 0x1880, 0x18C0, 0x1900, 0x1940, 0x1980, 0x19C0 };
    for (unsigned k = 0; k < MENU_RECORD_COUNT; k++) {
        unsigned record = 99;
        CHECK(menu_record_page_of(declared[k], &record) && record == k, "D $%04X is record %u", declared[k], k);
    }
    uint16_t last = 0;
    unsigned long before = refused("menu_record", &last);
    static const uint16_t other[] = { 0x17C0, 0x1820, 0x1A00, 0x0000 };
    for (unsigned k = 0; k < sizeof other / sizeof other[0]; k++) {
        unsigned record;
        CHECK(!menu_record_page_of(other[k], &record), "D $%04X is no menu record", other[k]);
    }
    CHECK(refused("menu_record", &last) == before + 4 && last == 0x0000, "the four refusals are counted");

    set_menu_record_y(g, 5, 0x77);
    set_menu_record_x(g, 5, 0x0123);
    CHECK(wram(0x7E1800 + 5 * 0x40 + 0x0E) == 0x77, "menu_record_y(5) is at $194E");
    CHECK(wram(0x7E1800 + 5 * 0x40 + 0x0F) == 0x23 && wram(0x7E1800 + 5 * 0x40 + 0x10) == 0x01,
          "menu_record_x(5) is the word at $194F");
    CHECK(wram(0x7E1800 + 4 * 0x40 + 0x0E) == 0 && menu_record_y(g, 4) == 0, "record 4 is untouched");
}

static void check_scene_math(GameState *g)
{
    SceneMathPage page;
    CHECK(scene_math_page_of(0x0000, &page) && page.kind == SCENE_MATH_SCENE, "D $0000 is the scene's page");
    CHECK(scene_math_page_of(0x0A00, &page) && page.kind == SCENE_MATH_CHANNEL && page.index == 0,
          "D $0A00 is channel 0");
    CHECK(scene_math_page_of(0x0B00, &page) && page.kind == SCENE_MATH_CHANNEL && page.index == 1,
          "D $0B00 is channel 1");
    uint16_t last = 0;
    unsigned long before = refused("scene_math", &last);
    CHECK(!scene_math_page_of(0x0C00, &page), "D $0C00 is past the channels");
    CHECK(!scene_math_page_of(0x0A80, &page), "D $0A80 is inside a channel's page");
    CHECK(!scene_math_page_of(0x0100, &page), "D $0100 is the field's page");
    CHECK(refused("scene_math", &last) == before + 3 && last == 0x0100, "the three refusals are counted");

    set_scene_math_scratch_second(g, scene_math_scene(), 0x1111);
    set_scene_math_scratch_second(g, scene_math_channel(0), 0x2222);
    set_scene_math_scratch_second(g, scene_math_channel(1), 0x3333);
    CHECK(wram(0x7E00F3) == 0x11 && wram(0x7E0AF3) == 0x22 && wram(0x7E0BF3) == 0x33,
          "each page's second operand is at $F2 of that page");
    CHECK(scene_math_scratch_second(g, scene_math_channel(1)) == 0x3333, "channel 1's reads back");
    set_scene_math_scratch_quotient_first_byte(g, scene_math_channel(0), 0x44);
    CHECK(wram(0x7E0AF8) == 0x44 && (scene_math_scratch_quotient(g, scene_math_channel(0)) & 0xFF) == 0x44,
          "a field overlapping another reads the same bytes");
}

int main(void)
{
    static uint8_t rom[0x10000];
    snes_init(&machine, rom, sizeof rom);
    snes_power_on(&machine);
    for (uint32_t a = 0x7E0000; a < 0x7E2000; a++)
        m_write8(&machine, a, 0);
    GameState *g = state_of(&machine);
    check_input_cells(g);
    check_menu_records(g);
    check_scene_math(g);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("vars_test: all passed\n");
    return 0;
}
