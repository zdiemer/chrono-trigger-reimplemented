/* The video service's SNES escapes (platform/video.h), as a host without the
 * SNES makes them (runtime/platform/native/host.h): each is a call whose
 * meaning is the SNES's own, so this is where a host says what it makes of
 * the SNES's model, and the backends in runtime/platform/native/ don't
 * (tests/test_native_render.py). Where the meaning is one a host can follow,
 * it does; the rest it counts, keeping the first site for the driver's
 * report (docs/TODO.md, "The native program"). */
#include "platform/native/host.h"

/* What a host makes of an escape it has no meaning for: nothing, counted, and
 * the first site kept for the driver's report. */
static void unmodelled(Platform *pf, PfOrigin o)
{
    VidHost *v = &pf->video;
    if (!v->escapes++)
        v->first_escape = o.site;
}

/* The transfers whose meaning a host knows: the multiplier's product copied
 * over memory, which is how the field clears it (with a product of 0); and
 * work RAM's data port filled, which is how the menus clear their buffers and
 * copy tables into them. The rest -- transfers into VRAM programmed from the
 * game's tables -- the observer follows only with the SNES's channel
 * settings, which a host doesn't have. */
/* A transfer into VRAM's data port whose mode the game's data gives (the
 * scene programs' loaders' and uploads'): the observer follows it, with the
 * port it sets. */
static bool into_vram_by_its_data(const VidSnesTransfer *t)
{
    return t->mode_from_data && (t->dest == VID_SNES_VRAM_DATA_LOW || t->dest == VID_SNES_VRAM_DATA_HIGH);
}

void vid_snes_copy(Platform *pf, PfOrigin o, const VidSnesTransfer *t)
{
    VidHost *v = &pf->video;
    if (into_vram_by_its_data(t)) {
        if (t->port)
            vid_obs_vram_port(v->observer, t->port);
        vid_obs_snes_transfer(v->observer, t, t->mode);
        return;
    }
    uint32_t bytes = t->size ? t->size : 0x10000u;
    if (t->dest == VID_SNES_WRAM_DATA) {
        /* work RAM's port steps through both banks, and wraps round them */
        enum { WORK_RAM = 0x7E0000, WORK_RAM_SPAN = 0x20000 };
        for (uint32_t i = 0; i < bytes; i++) {
            uint8_t b = v->read(v->ctx, t->repeats ? t->source : (t->source & 0xFF0000u) | (uint16_t)(t->source + i));
            v->write(v->ctx, WORK_RAM + (t->wram_at + i) % WORK_RAM_SPAN, b);
        }
        return;
    }
    if (t->dest != VID_SNES_MULTIPLIER_PRODUCT) {
        unmodelled(pf, o);
        return;
    }
    uint8_t product = (uint8_t)(v->multiplicand * v->multiplier);
    for (uint32_t i = 0; i < bytes; i++)
        v->write(v->ctx, (t->source & 0xFF0000u) | (uint16_t)(t->source + i), product);
}

void vid_snes_transfer(Platform *pf, PfOrigin o, const VidSnesTransfer *t, const VidSnesXfer *x)
{
    (void)t, (void)x;
    unmodelled(pf, o);
}

/* The sprites' sizes and their tiles' place, which the game's scripts set by
 * the register's address, are the one register a host takes: as the setting
 * it is. */
enum { SNES_OBJ_CONFIG = 0x2101 };

bool vid_snes_takes_register(uint16_t address) { return address == SNES_OBJ_CONFIG; }

void vid_snes_write_register(Platform *pf, PfOrigin o, uint16_t address, uint8_t v)
{
    if (address != SNES_OBJ_CONFIG) {
        unmodelled(pf, o);
        return;
    }
    VidObjConfig c = vid_obj_config_decode(v);
    vid_set_obj_config(pf, o, &c);
}

/* What the SNES would send nowhere (its writes land in memory, not in the
 * registers), a host sends: the palette. */
void vid_snes_send_palette_into_memory(Platform *pf, PfOrigin o, uint8_t bank, uint8_t first, PfData src, uint16_t count)
{
    (void)bank;
    vid_send_palette(pf, o, first, src, count);
}
