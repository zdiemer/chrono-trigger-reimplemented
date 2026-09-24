/* Checks the accessors generated from re/ram.toml (game/vars.h, backed by
 * runtime/port/vars_ram.c) on a machine with no ROM:
 *
 * - an ordinary accessor reads and writes the bytes the original's code does,
 *   low byte first (the controller cells, on the page the reader sets);
 * - an accessor on a page kind reaches the cells of the instance it is given:
 *   a menu record, the scene's own page or a sequence channel's, and nothing
 *   of the others;
 * - <page>_page_of turns each declared D into its instance, refuses every
 *   other D, and counts the refusals it made (runtime/port/vars_pages.h);
 * - an accessor given an index past what the map declares -- an element, a
 *   byte, a row or column, an instance -- still reaches the byte the address
 *   arithmetic names, and is counted against its variable with the index,
 *   the limit and which accessor it was (CT_ACCESSOR_CHECKS, which this test
 *   is built with); an index in range is not.
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

static void check_scene_program(GameState *g)
{
    SceneProgramPage page;
    CHECK(scene_program_page_of(0x0000, &page) && page.kind == SCENE_PROGRAM_OWN, "D $0000 is the scene's page");
    CHECK(scene_program_page_of(0x0A00, &page) && page.kind == SCENE_PROGRAM_CHANNEL && page.index == 0,
          "D $0A00 is channel 0");
    CHECK(scene_program_page_of(0x0B00, &page) && page.kind == SCENE_PROGRAM_CHANNEL && page.index == 1,
          "D $0B00 is channel 1");
    uint16_t last = 0;
    unsigned long before = refused("scene_program", &last);
    CHECK(!scene_program_page_of(0x0C00, &page), "D $0C00 is past the channels");
    CHECK(!scene_program_page_of(0x0A80, &page), "D $0A80 is inside a channel's page");
    CHECK(!scene_program_page_of(0x0100, &page), "D $0100 is the field's page");
    CHECK(refused("scene_program", &last) == before + 3 && last == 0x0100, "the three refusals are counted");

    set_scene_call_second(g, scene_program_own(), 0x1111);
    set_scene_call_second(g, scene_program_channel(0), 0x2222);
    set_scene_call_second(g, scene_program_channel(1), 0x3333);
    CHECK(wram(0x7E00F3) == 0x11 && wram(0x7E0AF3) == 0x22 && wram(0x7E0BF3) == 0x33,
          "each page's second operand is at $F2 of that page");
    CHECK(scene_call_second(g, scene_program_channel(1)) == 0x3333, "channel 1's reads back");
    set_scene_call_after_the_product(g, scene_program_channel(0), 0x44);
    CHECK(wram(0x7E0AF8) == 0x44 && (scene_call_quotient(g, scene_program_channel(0)) & 0xFF) == 0x44,
          "a field overlapping another reads the same bytes");
}

static const VarsRangeFault *fault(const char *var)
{
    unsigned n;
    const VarsRangeFault *r = vars_range_faults(&n);
    for (unsigned i = 0; i < n; i++)
        if (!strcmp(r[i].var, var))
            return &r[i];
    return NULL;
}

static bool hit(const VarsRangeHit *h, const char *accessor, const char *bound, unsigned index, unsigned limit)
{
    return !strcmp(h->accessor, accessor) && !strcmp(h->bound, bound) && h->index == index && h->limit == limit;
}

static void check_range(GameState *g)
{
    vars_range_reset();
    set_input_mapped(g, 2, 0x0101);
    (void)field_layer_maps(g, 2, 63, 63);
    (void)scene_call_second(g, scene_program_channel(1));
    unsigned n;
    const VarsRangeFault *r = vars_range_faults(&n);
    unsigned long total = 0;
    for (unsigned i = 0; i < n; i++)
        total += r[i].count;
    CHECK(n > 0 && total == 0, "no index in range is counted");

    /* An element past the count: the word after the array's, still written. */
    set_input_mapped(g, 3, 0xCAFE);
    CHECK(wram(0x7E00FC) == 0xFE && wram(0x7E00FD) == 0xCA, "input_mapped(3) still writes the word at $00FC");
    CHECK(input_mapped(g, 3) == 0xCAFE, "and reads it back");
    const VarsRangeFault *f = fault("input_mapped");
    CHECK(f && f->count == 2 && f->kept == 2, "input_mapped is counted twice");
    CHECK(f && hit(&f->first[0], "set_input_mapped", "count", 3, 3) && hit(&f->first[1], "input_mapped", "count", 3, 3),
          "with the accessor, the index and the count");

    /* A byte past a buffer, a row past a table, an instance past a page kind. */
    (void)oam_high_table_byte(g, OAM_HIGH_TABLE_BYTES);
    f = fault("oam_high_table");
    CHECK(f && f->count == 1 && hit(&f->first[0], "oam_high_table_byte", "bytes", 32, 32), "a byte past the buffer");
    (void)field_layer_maps(g, 0, FIELD_LAYER_MAPS_ROWS + 1, 0);
    (void)field_layer_maps(g, 0, 0, FIELD_LAYER_MAPS_COLUMNS);
    f = fault("field_layer_maps");
    CHECK(f && f->count == 2 && hit(&f->first[0], "field_layer_maps", "rows", 65, 64)
              && hit(&f->first[1], "field_layer_maps", "columns", 64, 64), "a row and a column past the table");
    (void)scene_call_second(g, scene_program_channel(2));
    f = fault("scene_call");
    CHECK(f && f->count == 1 && hit(&f->first[0], "scene_call_second", "instances", 2, 2), "a channel past the two");

    /* Only the first few are kept; every one is counted. */
    for (unsigned k = 0; k < 10; k++)
        (void)input_mapped(g, 4 + k);
    f = fault("input_mapped");
    CHECK(f && f->count == 12 && f->kept == VARS_RANGE_HITS_KEPT, "12 counted, %d kept", VARS_RANGE_HITS_KEPT);
    vars_range_reset();
    f = fault("input_mapped");
    CHECK(f && f->count == 0 && f->kept == 0, "reset clears the counts");
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
    check_scene_program(g);
    check_range(g);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("vars_test: all passed\n");
    return 0;
}
