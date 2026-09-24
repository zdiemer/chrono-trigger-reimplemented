#include "sound_compare.h"

#include <stdio.h>
#include <string.h>

enum { ENVX = 0x8, OUTX = 0x9, ENDX = 0x7C, KON = 0x4C, KOFF = 0x5C };

bool sound_register_compared(uint8_t reg)
{
    reg &= 0x7F;
    if (reg == ENDX)
        return false;
    uint8_t low = reg & 0x0F;
    return low != ENVX && low != OUTX;
}

void sound_side_clear_writes(SoundSide *side)
{
    side->key_writes = 0;
    side->has_status = false;
}

void sound_side_key_write(SoundSide *side, uint8_t reg, uint8_t value)
{
    if (side->key_writes < SOUND_KEY_WRITES)
        side->keys[side->key_writes] = (SoundKeyWrite){ reg, value };
    if (side->key_writes < UINT16_MAX)
        side->key_writes++;
}

static bool differ(SoundDivergence *first, SoundDifference kind, uint16_t at, uint8_t reg, int16_t want, int16_t got)
{
    *first = (SoundDivergence){ kind, at, reg, want, got };
    return true;
}

static bool compare_keys(const SoundSide *r, const SoundSide *p, SoundDivergence *first)
{
    uint16_t n = r->key_writes > p->key_writes ? r->key_writes : p->key_writes;
    for (uint16_t i = 0; i < n && i < SOUND_KEY_WRITES; i++) {
        bool have_r = i < r->key_writes, have_p = i < p->key_writes;
        if (have_r && have_p && r->keys[i].reg == p->keys[i].reg && r->keys[i].value == p->keys[i].value)
            continue;
        uint8_t reg = have_r ? r->keys[i].reg : p->keys[i].reg;
        return differ(first, SOUND_KEYS_DIFFER, i, reg, have_r ? r->keys[i].value : -1,
                      have_p && p->keys[i].reg == reg ? p->keys[i].value : -1);
    }
    if (r->key_writes != p->key_writes)
        return differ(first, SOUND_KEYS_DIFFER, SOUND_KEY_WRITES, 0, (int16_t)r->key_writes, (int16_t)p->key_writes);
    return false;
}

bool sound_compare(const SoundSide *reference, const SoundSide *port, SoundCompareLevel level, SoundDivergence *first)
{
    *first = (SoundDivergence){ SOUND_SAME, 0, 0, 0, 0 };
    if (reference->has_status != port->has_status)
        return differ(first, SOUND_STATUS_DIFFERS, 4, 0, reference->has_status, port->has_status);
    if (reference->has_status) {
        for (uint16_t i = 0; i < 4; i++)
            if (reference->status[i] != port->status[i])
                return differ(first, SOUND_STATUS_DIFFERS, i, 0, reference->status[i], port->status[i]);
    }
    if (level >= SOUND_COMPARE_KEYS && compare_keys(reference, port, first))
        return true;
    if (level < SOUND_COMPARE_DSP)
        return false;
    for (uint16_t reg = 0; reg < 128; reg++) {
        if (!sound_register_compared((uint8_t)reg) || reference->regs[reg] == port->regs[reg])
            continue;
        return differ(first, SOUND_REGISTER_DIFFERS, reg, (uint8_t)reg, reference->regs[reg], port->regs[reg]);
    }
    for (uint16_t i = 0; i < SOUND_COMPARED_MEMORY; i++)
        if (reference->memory[i] != port->memory[i])
            return differ(first, SOUND_MEMORY_DIFFERS, i, 0, reference->memory[i], port->memory[i]);
    return false;
}

/* A register's name: a voice's own ($V0-$V9), or one of the globals. */
static void register_name(uint8_t reg, char *out, size_t size)
{
    static const char *voice[10] = { "VOLL", "VOLR", "PITCHL", "PITCHH", "SRCN", "ADSR1", "ADSR2", "GAIN", "ENVX", "OUTX" };
    static const char *column_c[8] = { "MVOLL", "MVOLR", "EVOLL", "EVOLR", "KON", "KOFF", "FLG", "ENDX" };
    static const char *column_d[8] = { "EFB", "$1D", "PMON", "NON", "EON", "DIR", "ESA", "EDL" };
    uint8_t low = reg & 0x0F, high = (reg >> 4) & 7;
    if (low < 10)
        snprintf(out, size, "voice %u %s", high, voice[low]);
    else if (low == 0x0C)
        snprintf(out, size, "%s", column_c[high]);
    else if (low == 0x0D)
        snprintf(out, size, "%s", column_d[high]);
    else if (low == 0x0F)
        snprintf(out, size, "FIR%u", high);
    else
        snprintf(out, size, "$%02X", reg);
}

void sound_divergence_describe(const SoundDivergence *d, char *out, size_t size)
{
    switch (d->kind) {
    case SOUND_SAME:
        snprintf(out, size, "same");
        break;
    case SOUND_STATUS_DIFFERS:
        if (d->at == 4)
            snprintf(out, size, "status %s by the original, %s by the port", d->want ? "shown" : "not shown",
                     d->got ? "shown" : "not shown");
        else
            snprintf(out, size, "status port %u: original $%02X, port $%02X", d->at, d->want, d->got);
        break;
    case SOUND_KEYS_DIFFER:
        if (d->at == SOUND_KEY_WRITES)
            snprintf(out, size, "key writes: original %d, port %d", d->want, d->got);
        else
            snprintf(out, size, "key write %u (%s): original %s%02X, port %s%02X", d->at,
                     d->reg == KON ? "KON" : d->reg == KOFF ? "KOFF" : "?", d->want < 0 ? "none " : "$",
                     d->want < 0 ? 0 : d->want, d->got < 0 ? "none " : "$", d->got < 0 ? 0 : d->got);
        break;
    case SOUND_REGISTER_DIFFERS: {
        char name[24];
        register_name(d->reg, name, sizeof name);
        snprintf(out, size, "%s ($%02X): original $%02X, port $%02X", name, d->reg, d->want, d->got);
        break;
    }
    case SOUND_MEMORY_DIFFERS:
        snprintf(out, size, "the driver's own sample, byte %u: original $%02X, port $%02X", d->at, d->want, d->got);
        break;
    case SOUND_STATE_DIFFERS:
        snprintf(out, size, "%u fields of the drivers' state", d->at);
        break;
    }
}
