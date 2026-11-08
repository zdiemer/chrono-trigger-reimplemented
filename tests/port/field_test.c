/* Field state shared by drawing, collision and event scripts. Run against
 * the build without scaffolding: these effects must survive retirement. */
#include <stdio.h>

#include "field/object.h"
#include "field/menu_compat.h"
#include "field/machine_forms.h"
#include "field/field.h"
#include "field/display.h"
#include "field/main_loop.h"
#include "field/screen_setup.h"
#include "port/vars_ram.h"
#include "snes/system.h"
#include "vars.h"

static Snes machine;
static int failures;
#define CHECK(condition) do { if (!(condition)) { \
    printf("field_test:%d: %s\n", __LINE__, #condition); failures++; \
} } while (0)

static void tail_leaves(GameState *g, FieldObject predecessor, FieldLink end)
{
    const FieldObject tail = 3;
    set_field_object_row_mark(g, tail, 0);
    set_field_object_row_link_previous(g, tail, field_object_offset(predecessor));
    set_field_object_row_link_next(g, tail, field_link_byte(end));
    set_field_object_row_link_next(g, predecessor, field_object_offset(tail));
    object_leave_row(g, tail);
    CHECK(field_same_link(object_next_in_row(g, predecessor), end));
    CHECK(!field_link_is_object(object_next_in_row(g, tail)));
}

int main(void)
{
    static uint8_t rom[0x10000];
    snes_init(&machine, rom, sizeof rom);
    GameState *g = state_of(&machine);

    object_set_in_view(g, 0, true);
    tail_leaves(g, 2, field_no_link());
    CHECK(object_view(g, 0) == PRESENT_WITHOUT_UPDATES);
    CHECK(!object_in_view(g, 0));
    CHECK(object_view(g, 0) != OUT_OF_VIEW);
    /* A later visibility update restores the ordinary state. */
    object_set_in_view(g, 0, true);
    CHECK(object_in_view(g, 0));
    tail_leaves(g, 0, field_no_link());
    CHECK(object_view(g, 0) == OUT_OF_VIEW);

    /* Preserve the admitted end markers, including the last table slot. */
    object_set_in_view(g, 63, true);
    tail_leaves(g, 2, field_link_stored(0xFE));
    CHECK(object_view(g, 63) == PRESENT_WITHOUT_UPDATES);
    CHECK(object_view(g, 0) == OUT_OF_VIEW);

    /* Removing a middle object links its neighbours without changing view. */
    object_set_in_view(g, 0, true);
    set_field_object_row_mark(g, 3, 0);
    set_field_object_row_link_previous(g, 3, field_object_offset(2));
    set_field_object_row_link_next(g, 3, field_object_offset(4));
    object_leave_row(g, 3);
    CHECK(field_object_row_link_previous(g, 4) == field_object_offset(2));
    CHECK(object_in_view(g, 0));

    set_event_scratch_byte(g, 8, 0x35);
    set_event_scratch_byte(g, 9, 0x46);
    set_event_scratch_byte(g, 10, 0x57);
    set_event_scratch_byte(g, 11, 0x68);
    field_leaves_menu_wave_low(g, 0x79);
    CHECK(event_scratch_byte(g, 8) == 0x35);
    CHECK(event_scratch_byte(g, 9) == 0x79);
    CHECK(event_scratch_byte(g, 10) == 0x57);
    CHECK(menu_wave_phase(g) == 0x5779);
    field_leaves_menu_wave_high(g, 0x8A);
    CHECK(event_scratch_byte(g, 9) == 0x79);
    CHECK(event_scratch_byte(g, 10) == 0x8A);
    CHECK(event_scratch_byte(g, 11) == 0x68);
    CHECK(menu_wave_phase(g) == 0x8A79);

    /* The retired contracts keep every original width and decimal mask. */
    machine.cpu.e = false;
    machine.cpu.p = CPU_FLAG_M;
    machine.cpu.d = 0x100;
    machine.cpu.db = 0;
    CHECK(field_machine_form_takes(&machine, field_fade_in_entry));
    machine.cpu.p |= CPU_FLAG_D;
    CHECK(!field_machine_form_takes(&machine, field_fade_in_entry));
    CHECK(field_machine_form_takes(&machine, field_enter));
    machine.cpu.p = CPU_FLAG_M | CPU_FLAG_X;
    CHECK(!field_machine_form_takes(&machine, field_fade_in_entry));
    machine.cpu.p = CPU_FLAG_M;
    machine.cpu.d = 0;
    CHECK(!field_machine_form_takes(&machine, field_fade_in_entry));
    machine.cpu.d = 0x100;
    machine.cpu.db = 0x7F;
    CHECK(!field_machine_form_takes(&machine, field_fade_in_entry));
    machine.cpu.db = 0x80;
    CHECK(field_machine_form_takes(&machine, field_fade_in_entry));
    set_location_id(g, 0x8000);
    CHECK(!field_machine_form_takes(&machine, field_enter));
    CHECK(!field_enter_the_location(g, NULL));
    set_location_id(g, 0);
    /* Interrupt entries accept the interrupted program's P, page and bank. */
    machine.cpu.p = CPU_FLAG_D | CPU_FLAG_X;
    CHECK(field_machine_form_takes(&machine, field_nmi));
    CHECK(field_machine_form_takes(&machine, field_irq));
    machine.cpu.e = true;
    CHECK(!field_machine_form_takes(&machine, field_nmi));

    unsigned count;
    const VarsRangeFault *faults = vars_range_faults(&count);
    for (unsigned i = 0; i < count; i++)
        CHECK(faults[i].count == 0);
    printf("field_test: %d failures\n", failures);
    return failures != 0;
}
