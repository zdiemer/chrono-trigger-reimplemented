/* Battle: the per-frame work the NMI handler runs. */
#include "battle/entries.h"

#include "platform/input.h"
#include "platform/video.h"
#include "port/machine.h"
#include "ram.h"

static uint8_t abs8(Snes *s, uint16_t addr) { return m_read8(s, m_abs(s, addr)); }
static void set_abs8(Snes *s, uint16_t addr, uint8_t v) { m_write8(s, m_abs(s, addr), v); }
static uint8_t dp8(Snes *s, uint8_t cell) { return m_read8(s, m_dp(s, cell)); }

/* INC/DEC on an absolute byte, setting N and Z. */
static uint8_t step_abs8(Snes *s, uint16_t addr, int delta)
{
    uint8_t v = (uint8_t)(abs8(s, addr) + delta);
    set_abs8(s, addr, v);
    m_set_nz8(s, v);
    return v;
}

static bool widths_ok(const Snes *s) { return !s->cpu.e && (s->cpu.p & (CPU_FLAG_M | CPU_FLAG_X)) == CPU_FLAG_M; }

/* JSR/JSL out; false if the callee didn't come back with 8-bit A and 16-bit X
 * (the port then stops at the return address and the original carries on). */
static bool jsr(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr(s, site, target);
    return widths_ok(s);
}

static bool jsl(Snes *s, uint32_t site, uint32_t target)
{
    m_jsl(s, site, target);
    return widths_ok(s);
}

/* LDA #$7E : PHA : PLB (or TDC : PHA : PLB for bank 0) */
static void set_data_bank(Snes *s, uint8_t bank)
{
    m_push8(s, bank);
    s->cpu.db = m_pull8(s);
    m_set_nz8(s, s->cpu.db);
}

// $CD:0C95
/* The battle NMI's per-frame work: dispatch on the battle mode ($7E) through
 * the table at $CD:0C83. Returns with RTL. */
void battle_frame_dispatch(Snes *s)
{
    uint8_t mode = m_asl8(s, m_lda8(s, dp8(s, 0x7E)));
    m_set_a8(s, mode);
    m_tax(s);
    m_jsr(s, 0xCD0C99, 0xCD0000 | m_jump_table(s, 0xCD, 0x0C83, s->cpu.x));
    m_rtl(s);
}

// $CD:3EB4
/* Run a queued VRAM transfer ($CA26 set) with $CD:3E8E: source $CA27 in bank
 * $CA29, VRAM address $CA2A, size $CA2C; then clear the request. */
void battle_flush_vram_transfer(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0xCA26)) == 0) {
        m_rts(s);
        return;
    }
    m_ldx16(s, m_read16(s, m_abs(s, 0xCA2C)));
    m_write16(s, m_dp(s, 0x45), c->x);
    m_ldx16(s, m_read16(s, m_abs(s, 0xCA27)));
    m_set_index(s, &c->y, m_read16(s, m_abs(s, 0xCA2A)));
    m_lda8(s, abs8(s, 0xCA29));
    if (!jsr(s, 0xCD3EC7, 0xCD3E8E))
        return;
    set_abs8(s, 0xCA26, 0x00);
    m_rts(s);
}

// $CD:05B2
/* Step the sliding HDMA window ($CA1E: 0 idle, positive sliding one way, $FF
 * starting the other way, other negative values sliding back; $CA1F frames
 * left): each frame move it ($CD:053C or $CD:0577) and, when done, request
 * the follow-up ($CA24/$CA25) and clear $CA1E. */
void battle_step_window_slide(Snes *s)
{
    uint8_t state = m_lda8(s, abs8(s, 0xCA1E));
    if (state == 0) {
        m_rts(s);
        return;
    }
    bool finish = false;
    if (!(state & 0x80)) {
        if (m_lda8(s, abs8(s, 0xCA1F)) == 0) {
            finish = true;
        } else {
            if (!jsr(s, 0xCD05BE, 0xCD053C))
                return;
            if (step_abs8(s, 0xCA1F, -1) == 0) {
                set_abs8(s, 0xCA25, m_lda8(s, 0x02));
                step_abs8(s, 0xCA24, +1);
            }
        }
    } else {
        m_cmp8(s, state, 0xFF);
        if (state == 0xFF) {
            set_abs8(s, 0xCA25, 0x00);
            m_lda8(s, abs8(s, 0xCA22));
            if (!jsl(s, 0xCD05DA, 0xCEEE6E))
                return;
            set_abs8(s, 0xCA1E, m_lda8(s, 0x01));
        } else {
            if (!jsr(s, 0xCD05E5, 0xCD0577))
                return;
            if (step_abs8(s, 0xCA1F, -1) == 0) {
                set_abs8(s, 0xCA25, 0x00);
                step_abs8(s, 0xCA24, +1);
                finish = true;
            }
        }
    }
    if (finish) {
        if (!(m_lda8(s, abs8(s, 0xCA1E)) & 0x80))
            set_abs8(s, 0xCE16, 0x00);
        set_abs8(s, 0xCA1E, 0x00);
    }
    m_rts(s);
}

// $CD:06B4
/* Jump to the scroll table routine for the battle's scroll mode ($CA92)
 * through the table at $CD:06BC. */
void battle_update_scroll_dispatch(Snes *s)
{
    m_set_a8(s, m_asl8(s, m_lda8(s, abs8(s, 0xCA92))));
    m_tax(s);
    s->cpu.pc = m_jump_table(s, 0xCD, 0x06BC, s->cpu.x);
}

// $CD:0ADE
/* Unless palette animation slot 0 is counting ($0524), count $CDC9 down
 * through 0 and back to 13. */
void battle_step_cdc9(Snes *s)
{
    if (m_lda8(s, abs8(s, 0x0524)) == 0 && (step_abs8(s, 0xCDC9, -1) & 0x80))
        set_abs8(s, 0xCDC9, m_lda8(s, 0x0D));
    m_rts(s);
}

/* LDA $CA21 : INC A : AND #$01 : STA $CA21 : STA $EC -- toggle the battle's
 * alternating-frame flag. */
static void toggle_ca21(Snes *s)
{
    uint8_t v = m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0xCA21)) + 1));
    v = m_lda8(s, v & 0x01);
    set_abs8(s, 0xCA21, v);
    m_write8(s, m_dp(s, 0xEC), v);
}

// $CD:088D
/* The per-frame battle work in battle modes 1 and 2, run from the NMI handler:
 * a queued VRAM transfer, the window registers ($2123-$2125, $212A-$212B)
 * from their shadows at $C861, HDMA setup, the window slide (twice), then
 * either, when paused ($CD02), the brightness (halved unless $CD:3FFF is set),
 * or the full frame: the scroll tables, the alternating-frame flag, the OAM
 * buffer, the battle's own per-frame routines and palette animation, most of
 * them as call-outs. */
void battle_frame_work(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    if (m_lda8(s, m_read8(s, 0x7ECE0F)) != 0) {
        if (!jsl(s, 0xCD0893, 0xCD09CE))
            return;
        m_lda8(s, 0x7E);
        set_data_bank(s, 0x7E);
        if (!jsr(s, 0xCD089B, 0xCD3EB4))
            return;
    } else {
        m_lda8(s, 0x7E);
        set_data_bank(s, 0x7E);
        if (m_lda8(s, abs8(s, 0xCA1E)) == 0 && !jsr(s, 0xCD08A9, 0xCD0607))
            return;
        if (!jsr(s, 0xCD08AC, 0xCD3EB4))
            return;
        if (m_lda8(s, abs8(s, 0xCD02)) == 0 && !jsl(s, 0xCD08B4, 0xC10009))
            return;
    }

    /* $CD:08B8: the window shadows $C861-$C865 */
    static const struct { uint32_t field, site; } windows[5] = {
        { VID_WINDOWS_BG1_BG2, 0xCD08BB }, { VID_WINDOWS_BG3_BG4, 0xCD08C2 }, { VID_WINDOWS_OBJ_BACK, 0xCD08C9 },
        { VID_WINDOWS_BG_LOGIC, 0xCD08D0 }, { VID_WINDOWS_OBJ_LOGIC, 0xCD08D7 },
    };
    VidWindows w = { 0 };
    for (int i = 0; i < 5; i++) {
        vid_windows_decode(&w, windows[i].field, m_lda8(s, abs8(s, (uint16_t)(0xC861 + i))));
        vid_set_windows(pf, PF_AT(windows[i].site), &w, windows[i].field);
    }
    set_abs8(s, 0xBE93, m_lda8(s, abs8(s, 0xC866)));
    m_write16(s, m_abs(s, 0xC151), m_ldx16(s, m_read16(s, m_abs(s, 0xC155))));
    m_write16(s, m_abs(s, 0xC153), m_ldx16(s, m_read16(s, m_abs(s, 0xC157))));
    m_push8(s, c->db);
    m_tdc(s);
    set_data_bank(s, (uint8_t)c->a);
    if (!jsr(s, 0xCD08F1, 0xCD0AEE))
        return;
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    if (!jsr(s, 0xCD08F5, 0xCD05B2) || !jsr(s, 0xCD08F8, 0xCD05B2))
        return;
    vid_enable_line_effects(pf, PF_AT(0xCD08FE), m_lda8(s, abs8(s, 0xCA1D)));

    if (m_lda8(s, abs8(s, 0xCD02)) != 0) {
        uint8_t brightness;
        if (m_lda8(s, m_read8(s, 0xCD3FFF)) != 0) {
            brightness = m_lda8(s, abs8(s, 0xBB00));
        } else {
            brightness = m_lsr8(s, m_lda8(s, abs8(s, 0xBB00)));
            m_set_a8(s, brightness);
        }
        VidDisplay paused = vid_display_decode(brightness);
        vid_set_display(pf, PF_AT(0xCD0916), &paused);
        if (!jsl(s, 0xCD091A, 0xC28002) || !jsr(s, 0xCD091E, 0xCD0340))
            return;
        m_rts(s);
        return;
    }

    /* $CD:0922 */
    VidDisplay display = vid_display_decode(m_lda8(s, abs8(s, 0xBB00)));
    vid_set_display(pf, PF_AT(0xCD0925), &display);
    if (!jsr(s, 0xCD0929, 0xCD06B4) || !jsr(s, 0xCD092C, 0xCD0B51))
        return;
    uint8_t frame = m_lda8(s, dp8(s, 0x7C));
    if (m_lda8(s, frame & 0x01) == 0 && m_lda8(s, abs8(s, 0xCA1E)) == 0) {
        uint8_t ec = m_lda8(s, dp8(s, 0xEC));
        uint8_t ca21 = abs8(s, 0xCA21);
        m_cmp8(s, ec, ca21);
        if (ec != ca21)
            toggle_ca21(s);
        /* $CD:094C */
        bool clear = true;
        if (m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0xCD27)) & 0x20)) == 0 &&
            m_lda8(s, (uint8_t)(m_lda8(s, dp8(s, 0xF8)) & 0x01)) != 0) {
            clear = false;
            if (m_lda8(s, abs8(s, 0xCA23)) == 0) {
                toggle_ca21(s);
                set_abs8(s, 0xCA23, m_lda8(s, 0x01));
            }
        }
        if (clear)
            set_abs8(s, 0xCA23, 0x00);
    }

    /* $CD:0973 */
    if (m_lda8(s, (uint8_t)(m_lda8(s, abs8(s, 0x2A21)) & 0x01)) != 0) {
        if (!jsl(s, 0xCD097A, 0xCEE18E))
            return;
        uint8_t v = m_lda8(s, dp8(s, 0x7C));
        if (m_lda8(s, (uint8_t)(v & abs8(s, 0xCE13))) == 0 && !jsl(s, 0xCD0985, 0xD1EA4B))
            return;
    }
    if (m_lda8(s, abs8(s, 0xCD2D)) == 0 && m_lda8(s, abs8(s, 0xA028)) == 0 && !jsl(s, 0xCD0993, 0xCEF066))
        return;
    if (!jsl(s, 0xCD0997, 0xC1000C))
        return;
    if (m_lda8(s, abs8(s, 0xCD2E)) == 0 && m_lda8(s, abs8(s, 0xA013)) != 0 && m_lda8(s, abs8(s, 0xA028)) == 0) {
        m_lda8(s, 0x40);
        if (!jsr(s, 0xCD09AC, 0xCD3B82))
            return;
    }
    if (!jsr(s, 0xCD09AF, 0xCD1609))
        return;
    set_abs8(s, 0xA028, m_lda8(s, abs8(s, 0xCD29)));
    if (!jsr(s, 0xCD09B8, 0xCD0B1E) || !jsl(s, 0xCD09BB, 0xD1F426) || !jsl(s, 0xCD09BF, 0xFDFFF7) ||
        !jsr(s, 0xCD09C3, 0xCD0ADE) || !jsl(s, 0xCD09C6, 0xC28002) || !jsr(s, 0xCD09CA, 0xCD0340))
        return;
    m_rts(s);
}

/* The battle's uploads run on DMA channel 7. The step lists are the orders the
 * original programs the channel in. */
enum { BATTLE_DMA = 7, DMA_ONE_REGISTER = 0x00, DMA_WORDS = 0x01, DMA_WORDS_FIXED_SOURCE = 0x09 };

static const uint8_t steps_full[] = {
    VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
    VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_START, VID_STEP_END,
};
/* OAM and CGRAM have a one-byte address register. */
static const uint8_t steps_full_short_target[] = {
    VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
    VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_TARGET_LO, VID_STEP_START, VID_STEP_END,
};
/* A second upload reusing the first one's mode, destination and bank. */
static const uint8_t steps_same_channel[] = {
    VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI,
    VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_START, VID_STEP_END,
};
static const VidSnesXfer dma_words = { BATTLE_DMA, DMA_WORDS, steps_full, NULL };
static const VidSnesXfer dma_words_again = { BATTLE_DMA, DMA_WORDS, steps_same_channel, NULL };

/* A 16-bit DMA size as a length: 0 means 65536. */
static uint32_t dma_length(uint16_t size) { return size ? size : 0x10000; }

/* The four queued VRAM uploads' table ($CC:F957): per slot, two uploads' source
 * addresses in bank $7E, VRAM addresses and sizes, each field four words
 * (the second upload's eight bytes after the first's). */
enum { QUEUED_SOURCE = 0xF957, QUEUED_VRAM = 0xF967, QUEUED_SIZE = 0xF977, QUEUED_SECOND = 8, QUEUED_SLOTS = 4,
       OBJECT_UPLOAD_SECOND = 11 * 2, KINDS_UPLOADED = 3, FRAMES_TILEMAP_BYTES = 0x0180,
       FRAMES_TILEMAP_VRAM = 0x7A00, PALETTE_COLORS = 256,
       TARGET_VRAM_ADDRESS_LOW = 0x16 /* the escape names the VRAM address register by its low byte */ };
static uint16_t queued_upload(Snes *s, uint16_t field, uint16_t slot_x2) { return m_read16(s, 0xCC0000u + field + slot_x2); }

/* One of the queued VRAM uploads (`slot_x2` twice the slot): two uploads from
 * bank $7E, the second only when its size is nonzero. `sites`: the first
 * writes of each. */
static void upload_queued_vram(Snes *s, uint16_t slot_x2, uint32_t first_site, uint32_t second_site)
{
    Platform *pf = m_platform(s);
    uint32_t bank = RAM_OAM_BUFFER & 0xFF0000u;
    vid_upload_vram(pf, PF_AT(first_site), queued_upload(s, QUEUED_VRAM, slot_x2),
                    PF_GAME(bank | queued_upload(s, QUEUED_SOURCE, slot_x2)),
                    dma_length(queued_upload(s, QUEUED_SIZE, slot_x2)), &dma_words);
    uint16_t second = (uint16_t)(slot_x2 + QUEUED_SECOND);
    uint16_t size = queued_upload(s, QUEUED_SIZE, second);
    /* an empty second upload only programs its source address */
    vid_upload_vram(pf, PF_AT(second_site), size != 0 ? queued_upload(s, QUEUED_VRAM, second) : 0,
                    PF_GAME(bank | queued_upload(s, QUEUED_SOURCE, second)), size, &dma_words_again);
}

// $CF:E300
/* The battle's vblank uploads over DMA channel 7: the OAM buffer; the
 * transfer described at battle_pending_transfer when one is pending; the
 * window frames' tilemap when asked for; the working palette; one of the four
 * rotating queued VRAM uploads while they run and no effects do; the graphics
 * object's two uploads when asked for (objects of the first three kinds); and
 * queued upload 0 again when its countdown ends. Returns with RTL. */
static void upload_vram(Snes *s)
{
    Platform *pf = m_platform(s);
    static const VidSnesXfer oam = { BATTLE_DMA, DMA_ONE_REGISTER, steps_full_short_target, NULL };
    vid_upload_oam(pf, PF_AT(0xCFE301), PF_GAME(RAM_OAM_BUFFER & 0xFFFFu), &oam);

    if (m_read8(s, RAM_BATTLE_TRANSFER_PENDING)) {
        /* the channel's own values, from every sixth byte */
        uint8_t v[9];
        for (uint16_t i = 0; i < 9; i++)
            v[i] = m_read8(s, RAM_BATTLE_PENDING_TRANSFER + i * RAM_BATTLE_PENDING_TRANSFER_STRIDE);
        VidSnesXfer x = { BATTLE_DMA, v[0], steps_full, NULL };
        VidSnesTransfer t = { .dest = v[1], .source = (uint32_t)v[4] << 16 | v[3] << 8 | v[2],
                              .size = (uint16_t)(v[6] << 8 | v[5]), .target_reg = TARGET_VRAM_ADDRESS_LOW,
                              .target = (uint16_t)(v[8] << 8 | v[7]) };
        vid_snes_transfer(pf, PF_AT(0xCFE33B), &t, &x);
        m_write8(s, RAM_BATTLE_TRANSFER_PENDING, 0);
    }
    if (m_read8(s, RAM_BATTLE_FRAMES_UPLOAD_REQUEST)) {
        vid_upload_vram(pf, PF_AT(0xCFE386), FRAMES_TILEMAP_VRAM, PF_GAME(RAM_BATTLE_WINDOW_FRAMES_TILEMAP & 0xFFFFu),
                        FRAMES_TILEMAP_BYTES, &dma_words);
        m_write8(s, RAM_BATTLE_FRAMES_UPLOAD_REQUEST, 0);
    }
    static const VidSnesXfer palette = { BATTLE_DMA, DMA_ONE_REGISTER, steps_full_short_target, NULL };
    vid_upload_palette(pf, PF_AT(0xCFE3C5), 0, PF_GAME(RAM_PALETTE_WORKING), PALETTE_COLORS, &palette);

    // $CF:E3F8
    if (!m_read8(s, RAM_BATTLE_EFFECTS_RUNNING) && m_read8(s, RAM_BATTLE_QUEUED_UPLOADS_ON)) {
        uint8_t phase = m_read8(s, RAM_BATTLE_FRAME_PHASE);
        upload_queued_vram(s, (uint8_t)(phase << 1), 0xCFE40A, 0xCFE449);
        phase = (uint8_t)(m_read8(s, RAM_BATTLE_FRAME_PHASE) + 1);
        m_write8(s, RAM_BATTLE_FRAME_PHASE, phase == QUEUED_SLOTS ? 0 : phase);
    }

    // $CF:E481
    if (m_read8(s, RAM_BATTLE_GRAPHICS_UPLOAD_REQUEST) == 1) {
        m_write8(s, RAM_BATTLE_GRAPHICS_UPLOAD_REQUEST, 0);
        uint8_t slot = m_read8(s, RAM_BATTLE_GRAPHICS_OBJECT);
        if (m_read8(s, RAM_BATTLE_OBJECT_KIND + slot) < KINDS_UPLOADED) {
            uint16_t w = (uint8_t)(slot << 1);
            static const uint32_t sites[2] = { 0xCFE4A0, 0xCFE4E3 };
            for (uint16_t u = 0; u < 2; u++) {
                uint16_t k = (uint16_t)(w + u * OBJECT_UPLOAD_SECOND);
                vid_upload_vram(pf, PF_AT(sites[u]), m_read16(s, RAM_BATTLE_UPLOAD_VRAM_ADDRESS + k),
                                PF_GAME((RAM_OAM_BUFFER & 0xFF0000u) | m_read16(s, RAM_BATTLE_UPLOAD_SOURCE + k)),
                                dma_length(m_read16(s, RAM_BATTLE_UPLOAD_SIZE + k)), u ? &dma_words_again : &dma_words);
            }
        }
    }

    // $CF:E510
    uint8_t left = m_read8(s, RAM_BATTLE_REUPLOAD_COUNTDOWN);
    if (left) {
        m_write8(s, RAM_BATTLE_REUPLOAD_COUNTDOWN, --left);
        if (!left)
            upload_queued_vram(s, 0, 0xCFE51C, 0xCFE55B);
    }
}

/* Guarded on the battle page and data bank $7E: the original loads the OAM
 * address and the palette's first entry with TDC. Leaves Y and V. */
void battle_upload_vram(Snes *s)
{
    upload_vram(s);
    m_rtl(s);
}

/* ---- Requests for the described transfer ------------------------------------------ *
 *
 * battle_upload_vram makes the transfer described at battle_pending_transfer
 * when battle_transfer_pending is set. These three describe the upload of
 * three rows of the menu window's tilemap to VRAM, from one of three places.
 * The description is the DMA channel's own values -- words to the VRAM data
 * port, source, size and VRAM address -- which the upload hands to the
 * SNES-only transfer escape as they are. */
enum { WINDOW_ROWS_BYTES = 0x0180, WINDOW_ROWS_VRAM = 0x7820, WINDOW_ROWS_LOWER = 0x01C0, DESCRIPTION_STRIDE = 6,
       DEST_VRAM_PORT = 0x18, TRANSFER_PENDING = 0x80 };

static void request_window_rows(Snes *s, uint32_t source)
{
    const uint8_t values[9] = {
        DMA_WORDS, DEST_VRAM_PORT, (uint8_t)source, (uint8_t)(source >> 8), (uint8_t)(source >> 16),
        (uint8_t)WINDOW_ROWS_BYTES, (uint8_t)(WINDOW_ROWS_BYTES >> 8), (uint8_t)WINDOW_ROWS_VRAM,
        (uint8_t)(WINDOW_ROWS_VRAM >> 8),
    };
    for (uint16_t i = 0; i < 9; i++)
        m_write8(s, RAM_BATTLE_PENDING_TRANSFER + i * DESCRIPTION_STRIDE, values[i]);
    m_write8(s, RAM_BATTLE_TRANSFER_PENDING, TRANSFER_PENDING);
}

/* What the three leave: X the direct page (TDC : TAX, 0 on the battle page)
 * and A the pending flag. */
static void finish_request(Snes *s)
{
    s->cpu.x = s->cpu.d;
    s->cpu.a = (uint16_t)((s->cpu.d & 0xFF00) | TRANSFER_PENDING);
    m_rtl(s);
}

// $CF:FD02
/* The window tilemap's first three rows (from bank $00, its work RAM mirror). */
void battle_request_window_rows(Snes *s)
{
    request_window_rows(s, (uint16_t)RAM_BATTLE_WINDOW_TILEMAP);
    finish_request(s);
}

// $CF:FD36
/* The three rows the menu saved. */
void battle_request_saved_window_rows(Snes *s)
{
    request_window_rows(s, RAM_BATTLE_MENU_TILEMAP);
    finish_request(s);
}

// $CF:FD6A
/* The window tilemap's rows further down. */
void battle_request_lower_window_rows(Snes *s)
{
    request_window_rows(s, (uint16_t)(RAM_BATTLE_WINDOW_TILEMAP + WINDOW_ROWS_LOWER));
    finish_request(s);
}

/* The palette colors the frame update animates and rotates, and the cells it
 * rotates them through (the battle page's, from $C6). */
enum { ANIMATED_COLOR = 0x13C, ANIMATED_COLOR_ALT = 0x1BC, ROTATED_COLORS = 0x180, ROTATED_LAST = 0x16,
       COLOR_PHASES = 0x3E, BLINK_PERIOD = 0x0C, PALETTE_BYTES = 0x20, CYCLE_ROW_MOVES = 0x0E,
       CELL_KEPT = 0x00, CELL_MOVES = 0x02, CELL_START = 0x04, SPRITE_PRIORITY_KEPT = 0xF1, CURSOR_SPRITES = 4,
       BUTTON_L_R = 0x30, RUN_HOLD_FRAMES = 0x78, RUN_NOT_ALLOWED = 0x80, DEMO_BATTLE = 0x20,
       GRAPHICS_UPLOAD_NEXT_FRAME = 2 };

/* The animated color's two bytes by phase * 2 ($D1:5980). */
static uint8_t battle_animated_color_byte(Snes *s, uint16_t at) { return m_read8(s, 0xD15980u + at); }

static uint16_t palette_word(Snes *s, uint16_t at) { return m_read16(s, RAM_PALETTE_WORKING + at); }
static void set_palette_word(Snes *s, uint16_t at, uint16_t v) { m_write16(s, RAM_PALETTE_WORKING + at, v); }
/* The word above `at`: past $FFFF it runs on into bank $7F, as the original's
 * indexed store does. */
static void set_palette_word_above(Snes *s, uint16_t at, uint16_t v) { m_write16(s, RAM_PALETTE_WORKING + 2u + at, v); }
static uint16_t frame_cell(Snes *s, uint8_t c) { return m_read16(s, RAM_BATTLE_EFFECT_CELLS + c); }
static void set_frame_cell(Snes *s, uint8_t c, uint16_t v) { m_write16(s, RAM_BATTLE_EFFECT_CELLS + c, v); }

/* $CF:E653/$CF:E682: rotate the working palette's colors up one, the color
 * at byte `at` going to the bottom, over the 16-bit count of moves in the
 * cells (0 meaning 65536, as the original's DEC runs). */
static void rotate_colors(Snes *s, uint16_t at)
{
    set_frame_cell(s, CELL_KEPT, palette_word(s, at));
    at = (uint16_t)(at - 2);
    uint16_t left;
    do {
        set_palette_word_above(s, at, palette_word(s, at));
        at = (uint16_t)(at - 2);
        left = (uint16_t)(frame_cell(s, CELL_MOVES) - 1);
        set_frame_cell(s, CELL_MOVES, left);
    } while (left != 0);
    set_palette_word_above(s, at, frame_cell(s, CELL_KEPT));
}

static bool overflow8(uint8_t a, uint8_t b, uint8_t r) { return (~(a ^ b) & (a ^ r) & 0x80) != 0; }
static bool overflow16(uint16_t a, uint16_t b, uint16_t r) { return (~(a ^ b) & (a ^ r) & 0x8000) != 0; }

/* Rotate the colors: every period frames, the twelve at $2382 up one; and
 * with the background cycle on, in the palette its high nibble names the
 * colors from its low nibble on, and the fifteen above color 0 of every
 * later palette. Returns the overflow the last addition leaves (`v` if none). */
static bool rotate_palette_colors(Snes *s, bool v)
{
    if (!m_read8(s, RAM_BATTLE_COLOR_ROTATION_ON))
        return v;
    uint8_t count = (uint8_t)(m_read8(s, RAM_BATTLE_COLOR_ROTATION_COUNT) - 1);
    m_write8(s, RAM_BATTLE_COLOR_ROTATION_COUNT, count);
    if (count)
        return v;
    m_write8(s, RAM_BATTLE_COLOR_ROTATION_COUNT, m_read8(s, RAM_BATTLE_COLOR_ROTATION_PERIOD));
    set_frame_cell(s, CELL_KEPT, palette_word(s, ROTATED_COLORS + ROTATED_LAST + 2));
    for (int16_t x = ROTATED_LAST; x >= 0; x -= 2)
        set_palette_word(s, (uint16_t)(ROTATED_COLORS + 2 + x), palette_word(s, (uint16_t)(ROTATED_COLORS + x)));
    set_palette_word(s, ROTATED_COLORS + 2, frame_cell(s, CELL_KEPT));

    if (!m_read8(s, RAM_BATTLE_BACKGROUND_CYCLE_ON))
        return v;
    uint8_t first = m_read8(s, RAM_BATTLE_BACKGROUND_CYCLE) & 0x0F;
    m_write8(s, RAM_BATTLE_EFFECT_CELLS + CELL_KEPT, first);
    set_frame_cell(s, CELL_MOVES, (uint8_t)(0x0F - first));
    uint16_t at = (uint16_t)((uint8_t)(m_read8(s, RAM_BATTLE_BACKGROUND_CYCLE) | 0x0F) << 1);
    set_frame_cell(s, CELL_START, at);
    rotate_colors(s, at);

    uint8_t top = (uint8_t)(m_read8(s, RAM_BATTLE_BACKGROUND_CYCLE) | 0x0F);
    uint8_t next = (uint8_t)(top + 0x10);
    v = overflow8(top, 0x10, next);
    set_frame_cell(s, CELL_START, (uint16_t)(next << 1));
    uint16_t start;
    do {
        set_frame_cell(s, CELL_MOVES, CYCLE_ROW_MOVES);
        rotate_colors(s, frame_cell(s, CELL_START));
        uint16_t was = frame_cell(s, CELL_START);
        start = (uint16_t)(was + PALETTE_BYTES);
        v = overflow16(was, PALETTE_BYTES, start);
        set_frame_cell(s, CELL_START, start);
    } while ((start & 0xFF00) == 0);
    return v;
}

/* The frame update's call-outs: the callees read A's high byte, the direct
 * page and the data bank, and none of them A's low byte, X, Y or the flags,
 * which nothing after them reads either. False if the callee didn't leave
 * 8-bit A and 16-bit X. */
enum { FRAME_CALL_DEAD = DC_AL | DC_X | DC_Y | DC_FLAGS };
static bool call_on(Snes *s, uint32_t site, uint32_t target)
{
    m_jsr_dc(s, site, target, FRAME_CALL_DEAD);
    return widths_ok(s);
}

// $CF:E58A
/* The battle's per-frame update, run from the main loop's frame wait: the
 * blink timer, the party palette cycle, the animated color (in the palette
 * the cursor's priority bits pick, the other one while the sprite palettes
 * are flipped), the 32-bit frame count, the color rotations, the cursor
 * sprites' priority, a demo battle's exit on any button (to the reset code
 * once the brightness counts down), the L+R hold that makes the party run,
 * the frame ticks, the effect slots, and while effects run the objects'
 * paths, anchors, trails and rising numbers; the formation; and, unless the
 * sprites are frozen, the objects' drawing. Clears the frame's pending flag,
 * puts battle_effect_sprite_attributes back and returns with RTL.
 *
 * Its callees read only A's high byte, which is 0 from the frame count's TDC
 * on (guarded on the battle page); before that the party palette cycle gets
 * the caller's, and what it leaves indexes the animated color. */
void battle_frame_update(Snes *s)
{
    Cpu *c = &s->cpu;
    Platform *pf = m_platform(s);
    uint8_t blink = (uint8_t)(m_read8(s, RAM_BATTLE_BLINK_TIMER) - 1);
    m_write8(s, RAM_BATTLE_BLINK_TIMER, blink);
    if (!blink) {
        m_write8(s, RAM_BATTLE_BLINK_TIMER, BLINK_PERIOD);
        m_write8(s, RAM_BATTLE_BLINK, (uint8_t)(m_read8(s, RAM_BATTLE_BLINK) ^ 0x01));
    }
    if (m_read8(s, RAM_BATTLE_EFFECT_MIRROR_SLOT3))
        m_write8(s, RAM_BATTLE_OBJECT_PALETTE + 9, m_read8(s, RAM_BATTLE_OBJECT_PALETTE + 3));
    if (!call_on(s, 0xCFE5A7, 0xCFE7D7))
        return;

    uint16_t index_high = c->a & 0xFF00;
    uint8_t phase = (uint8_t)(m_read8(s, RAM_BATTLE_COLOR_PHASE) + 1);
    m_write8(s, RAM_BATTLE_COLOR_PHASE, phase);
    if (phase == COLOR_PHASES) {
        phase = 0;
        index_high = c->d & 0xFF00;
        m_write8(s, RAM_BATTLE_COLOR_PHASE, 0);
    }
    uint16_t at = (uint16_t)(index_high | (uint8_t)(phase << 1));
    uint8_t priority = m_read8(s, RAM_BATTLE_CURSOR_PRIORITY);
    if (m_read8(s, RAM_BATTLE_SPRITE_PALETTES_FLIPPED))
        priority ^= 0x08;
    uint16_t color = (priority & 0x08) ? ANIMATED_COLOR_ALT : ANIMATED_COLOR;
    m_write8(s, RAM_PALETTE_WORKING + color, battle_animated_color_byte(s, at));
    m_write8(s, RAM_PALETTE_WORKING + color + 1, battle_animated_color_byte(s, (uint16_t)(at + 1)));

    // $CF:E5EB
    uint16_t low = m_read16(s, RAM_BATTLE_FRAMES), high = m_read16(s, RAM_BATTLE_FRAMES + 2);
    m_write16(s, RAM_BATTLE_FRAMES, (uint16_t)(low + 1));
    uint16_t carry = low == 0xFFFF;
    m_write16(s, RAM_BATTLE_FRAMES + 2, (uint16_t)(high + carry));
    bool v = overflow16(high, 0, (uint16_t)(high + carry));
    c->a = c->d;   /* TDC: A's high byte 0 from here */

    /* the overflow of the last addition, which the reset reads if a demo
     * battle leaves below */
    m_set_flag(s, CPU_FLAG_V, rotate_palette_colors(s, v));

    // $CF:E6AA
    for (uint16_t k = 0; k < CURSOR_SPRITES; k++) {
        uint32_t attributes = RAM_OAM_BUFFER_ATTRIBUTES + k * 4u;
        m_write8(s, attributes,
                 (uint8_t)((m_read8(s, attributes) & SPRITE_PRIORITY_KEPT) | m_read8(s, RAM_BATTLE_CURSOR_PRIORITY)));
    }
    m_push8(s, m_read8(s, RAM_BATTLE_EFFECT_SPRITE_ATTRIBUTES));
    if (m_read8(s, RAM_BATTLE_SETUP_FLAGS) & DEMO_BATTLE) {
        if (in_read_low(pf, PF_AT(0xCFE6E0), 0) | in_read_high(pf, PF_AT(0xCFE6E4), 0))
            m_write8(s, RAM_BATTLE_DEMO_EXITING, 1);
        if (m_read8(s, RAM_BATTLE_DEMO_EXITING)) {
            uint8_t brightness = (uint8_t)(m_read8(s, RAM_BATTLE_BRIGHTNESS) - 1);
            m_write8(s, RAM_BATTLE_BRIGHTNESS, brightness);
            if (!brightness) {
                m_write8(s, RAM_BATTLE_LEAVING, 0x80);
                /* JML to the reset code, the attribute byte still pushed; the
                 * reset reads Y and the overflow, which the frame update's
                 * callers don't */
                m_stop_at_own(s, 0xC0FF00, DC_A | DC_X | DC_N | DC_Z | DC_C);
                return;
            }
        }
    }

    // $CF:E706
    if (!m_read8(s, RAM_BATTLE_EFFECTS_RUNNING)) {
        if ((in_read_low(pf, PF_AT(0xCFE70B), 0) & BUTTON_L_R) == BUTTON_L_R) {
            uint16_t held = (uint16_t)(m_read16(s, RAM_BATTLE_LR_HELD) + 1);
            m_write16(s, RAM_BATTLE_LR_HELD, held);
            if (held >= RUN_HOLD_FRAMES && !(m_read8(s, RAM_BATTLE_SETUP_FLAGS) & RUN_NOT_ALLOWED))
                m_write16(s, RAM_BATTLE_RUN_COUNTER, (uint16_t)(m_read16(s, RAM_BATTLE_RUN_COUNTER) + 1));
        } else {
            m_write8(s, RAM_BATTLE_LR_STATE, 0);
            m_write16(s, RAM_BATTLE_LR_HELD, 0);
        }
    }

    // $CF:E73B
    uint8_t tick = (uint8_t)(m_read8(s, RAM_BATTLE_FRAME_TICK) + 1);
    m_write8(s, RAM_BATTLE_FRAME_TICK, tick);
    if (!(tick & 0x03))
        m_write8(s, RAM_BATTLE_FRAME_TICK_4, (uint8_t)(m_read8(s, RAM_BATTLE_FRAME_TICK_4) + 1));
    if (!call_on(s, 0xCFE748, 0xCFEFAF))
        return;
    uint8_t running = m_read8(s, RAM_BATTLE_EFFECTS_RUNNING);
    if (running && (!call_on(s, 0xCFE750, 0xCFEC78) || !call_on(s, 0xCFE753, 0xCFECC2) ||
                    !call_on(s, 0xCFE756, 0xCFED18) || !call_on(s, 0xCFE759, 0xCFE781)))
        return;
    uint8_t formation = m_read8(s, RAM_BATTLE_FORMATION);
    if (formation && !call_on(s, 0xCFE761, 0xCFF9D3))
        return;
    if (!m_read8(s, RAM_BATTLE_SPRITES_FROZEN)) {
        uint8_t loaded = m_read8(s, RAM_BATTLE_EFFECTS_RUNNING);
        if (loaded) {
            loaded = m_read8(s, RAM_BATTLE_GRAPHICS_UPLOAD_REQUEST);
            if (loaded == GRAPHICS_UPLOAD_NEXT_FRAME)
                m_write8(s, RAM_BATTLE_GRAPHICS_UPLOAD_REQUEST, GRAPHICS_UPLOAD_NEXT_FRAME - 1);
        }
        if (!call_on(s, 0xCFE778, 0xCFE83A))
            return;
    }
    m_write8(s, RAM_BATTLE_FRAME_PENDING, 0);
    uint8_t attributes = m_pull8(s);
    c->a = (uint16_t)((c->a & 0xFF00) | attributes);
    m_write8(s, RAM_BATTLE_EFFECT_SPRITE_ATTRIBUTES, attributes);
    m_rtl(s);
}

// $CD:3E8E
/* VRAM transfer over DMA channel 7: $45 bytes from A:X to VRAM address Y. */
void battle_vram_transfer(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push8(s, (uint8_t)c->a);
    m_push8(s, m_lda8(s, 0x00));
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    uint8_t bank = m_pull8(s);
    m_lda8(s, bank);
    uint32_t source = (uint32_t)bank << 16 | c->x;
    uint16_t size = m_ldx16(s, m_read16(s, m_dp(s, 0x45)));
    m_lda8(s, 0x80);
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
        VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START, VID_STEP_END,
    };
    static const VidSnesXfer x = { BATTLE_DMA, DMA_WORDS, steps, NULL };
    vid_upload_vram(m_platform(s), PF_AT(0xCD3E95), c->y, PF_GAME(source), dma_length(size), &x);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

// $CD:0607
/* Per-frame VRAM work: a requested block upload ($CA17: sixteen 32-byte rows
 * from $7E:$CA18 through $D1:E8D9, to the VRAM address for block $CA16 from
 * $CD:0601), skipped when block and source match the last upload
 * ($CD1F/$CD20); then $200 bytes of VRAM at $5800 or $5980 on
 * alternating frames ($CE14): cleared from the zero word at $CD:06B2, or, when
 * $CCEC is $10, copied from $7E:B400 or $7E:B600 through $CD:3E8E. */
void battle_upload_alternating_vram(Snes *s)
{
    Cpu *c = &s->cpu;
    if (m_lda8(s, abs8(s, 0xCA17)) != 0) {
        set_abs8(s, 0xCA17, 0x00);
        uint8_t row = m_asl8(s, m_lda8(s, abs8(s, 0xCA16)));
        m_set_a8(s, row);
        m_push8(s, row);
        set_abs8(s, 0xCA16, 0x00);
        m_tax(s);
        m_rep(s, CPU_FLAG_M);
        m_write16(s, m_dp(s, 0x45), m_lda16(s, m_read16(s, 0xCD05FF + c->x)));
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        row = m_pull8(s);
        m_lda8(s, row);
        uint8_t last = abs8(s, 0xCD1F);
        m_cmp8(s, row, last);
        bool upload = true;
        if (row == last) {
            uint16_t src = m_ldx16(s, m_read16(s, m_abs(s, 0xCA18)));
            uint16_t last_src = m_read16(s, m_abs(s, 0xCD20));
            m_cmp16(s, src, last_src);
            if (src == last_src)
                upload = false;
            else
                m_write16(s, m_abs(s, 0xCD20), src);
        } else {
            set_abs8(s, 0xCD1F, row);
            m_write16(s, m_abs(s, 0xCD20), m_ldx16(s, m_read16(s, m_abs(s, 0xCA18))));
        }
        if (upload) {
            m_ldx16(s, m_read16(s, m_abs(s, 0xCA18)));
            m_lda8(s, 0x7E);
            if (!jsl(s, 0xCD0644, 0xD1E8D9))
                return;
            step_abs8(s, 0xA01F, +1);
        }
    }

    /* $CD:064B */
    m_write16(s, m_dp(s, 0x45), m_ldx16(s, 0x0200));
    step_abs8(s, 0xCE14, +1);
    uint8_t mode = m_lda8(s, abs8(s, 0xCCEC));
    m_cmp8(s, mode, 0x10);
    if (mode == 0x10) {
        bool odd = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, 0x7ECE14)) & 0x01)) != 0;
        m_ldx16(s, odd ? 0xB400 : 0xB600);
        m_set_index(s, &c->y, odd ? 0x5800 : 0x5980);
        m_lda8(s, 0x7E);
        c->pc = 0x3E8E;
        return;
    }
    m_push8(s, c->db);
    set_data_bank(s, m_lda8(s, 0x00));
    bool odd = m_lda8(s, (uint8_t)(m_lda8(s, m_read8(s, 0x7ECE14)) & 0x01)) != 0;
    m_ldx16(s, 0x0200);
    m_lda8(s, 0x80);
    /* from the zero byte at $CD:06B2, a fixed-source transfer */
    static const uint8_t steps[] = {
        VID_STEP_TARGET_LO, VID_STEP_TARGET_HI, VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_LO,
        VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK, VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_START, VID_STEP_END,
    };
    static const VidSnesXfer x = { BATTLE_DMA, DMA_WORDS_FIXED_SOURCE, steps, NULL };
    vid_fill_vram(m_platform(s), PF_AT(0xCD066F), odd ? 0x5800 : 0x5980, PF_GAME(0xCD06B2), 0x0200, &x);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_rts(s);
}

/* JSR $0B16: ADC $7C : TAX : LDA $C0:FE00,X -- a pseudo-random byte from
 * the table at $C0:FE00, indexed by A plus the frame counter (and carry). */
static uint8_t shake_random(Snes *s, uint16_t site)
{
    m_push16(s, (uint16_t)(site + 2));
    uint8_t v = m_adc8(s, (uint8_t)s->cpu.a, dp8(s, 0x7C));
    m_set_a8(s, v);
    m_tax(s);
    v = m_lda8(s, m_read8(s, 0xC0FE00 + s->cpu.x));
    m_pull16(s);
    return v;
}

/* One axis of $CD:0B51: while enabled (`on`), pick a new random magnitude every
 * `period` frames and step the amplitude toward its target every `rate`
 * frames (disabling the axis when it decays to 0); the offset for this frame
 * is the high byte of magnitude * amplitude, stored in `out`. Disabled, the
 * three layers' offsets `offsets` are cleared instead. */
static void shake_axis(Snes *s, uint16_t base, const uint16_t offsets[3], uint8_t random_base, uint16_t site, uint8_t out)
{
    Cpu *c = &s->cpu;
    uint16_t on = base, magnitude = (uint16_t)(base + 1), count = (uint16_t)(base + 2),
             period = (uint16_t)(base + 3), rate_count = (uint16_t)(base + 4), rate = (uint16_t)(base + 5),
             amplitude = (uint16_t)(base + 6), target = (uint16_t)(base + 7);
    if (m_lda8(s, abs8(s, on)) == 0) {
        m_tax(s);
        for (int i = 0; i < 3; i++)
            m_write16(s, m_abs(s, offsets[i]), c->x);
        m_write8(s, m_dp(s, out), (uint8_t)c->a);
        return;
    }
    step_abs8(s, count, +1);
    uint8_t n = m_lda8(s, abs8(s, count));
    m_cmp8(s, n, abs8(s, period));
    if (m_flag(s, CPU_FLAG_C)) {
        set_abs8(s, count, 0x00);
        if (random_base != 0)
            m_lda8(s, random_base);
        set_abs8(s, magnitude, shake_random(s, site));
    }
    m_write8(s, 0x004202, m_lda8(s, abs8(s, magnitude)));
    step_abs8(s, rate_count, +1);
    n = m_lda8(s, abs8(s, rate_count));
    m_cmp8(s, n, abs8(s, rate));
    if (m_flag(s, CPU_FLAG_C)) {
        set_abs8(s, rate_count, 0x00);
        uint8_t amp = m_lda8(s, abs8(s, amplitude));
        uint8_t goal = abs8(s, target);
        m_cmp8(s, amp, goal);
        if (amp > goal) {
            if (step_abs8(s, amplitude, -1) == 0)
                set_abs8(s, on, 0x00);
        } else if (amp < goal) {
            step_abs8(s, amplitude, +1);
        }
    }
    m_write8(s, 0x004203, m_lda8(s, abs8(s, amplitude)));
    m_write8(s, m_dp(s, out), m_lda8(s, m_read8(s, 0x004217)));
}

// $CD:0B51
/* Battle screen shake: compute this frame's horizontal ($CCB0) and vertical
 * ($CCB8) shake offsets, apply them to the layers selected by $CCC0 (bits 7-2:
 * BG1 X/Y, BG2 X/Y, BG3 X/Y at $CCA4-$CCAE), then, when $CCE5 is set, copy
 * $7E:CCD9 (12 bytes, to $0598; bit 7) or $7E:CCC1 (24 bytes, to $0580) and
 * clear that request bit. */
void battle_update_screen_shake(Snes *s)
{
    Cpu *c = &s->cpu;
    static const uint16_t x_offsets[3] = { 0xCCA4, 0xCCA8, 0xCCAC }, y_offsets[3] = { 0xCCA6, 0xCCAA, 0xCCAE };
    shake_axis(s, 0xCCB0, x_offsets, 0x00, 0x0B72, 0x45);
    shake_axis(s, 0xCCB8, y_offsets, 0x40, 0x0BD7, 0x47);

    /* $CD:0C19 */
    m_lda8(s, dp8(s, 0x45));
    m_set_index(s, &c->y, c->a);
    m_lda8(s, dp8(s, 0x47));
    m_tax(s);
    uint8_t layers = m_lda8(s, abs8(s, 0xCCC0));
    if (layers != 0) {
        for (int i = 0; i < 6; i++) {
            layers = m_asl8(s, layers);
            if (m_flag(s, CPU_FLAG_C))
                m_write16(s, m_abs(s, (uint16_t)(0xCCA4 + 2 * i)), (i & 1) ? c->x : c->y);
        }
        m_set_a8(s, layers);
    }

    /* $CD:0C48 */
    uint8_t preset = m_lda8(s, abs8(s, 0xCCE5));
    if (preset != 0) {
        m_rep(s, CPU_FLAG_M);
        bool vertical = preset & 0x80;
        m_ldx16(s, vertical ? 0xCCD9 : 0xCCC1);
        m_set_index(s, &c->y, vertical ? 0x0598 : 0x0580);
        m_lda16(s, vertical ? 0x000B : 0x0017);
        m_mvn(s, 0x7E, 0x7E);
        m_tdc(s);
        m_sep(s, CPU_FLAG_M);
        uint8_t v = m_lda8(s, abs8(s, 0xCCE5));
        set_abs8(s, 0xCCE5, m_lda8(s, (uint8_t)(v & (vertical ? 0x7F : 0xFE))));
    }
    m_rts(s);
}

// $CD:0AEE
/* Set up HDMA channel 7 for the window: indirect mode 4 to $2126 ($4370 = $44,
 * $4371 = $26), tables in $7E, from $C9CF on odd frames ($7C bit 0) and $C9A2
 * on even ones. */
void battle_setup_window_hdma(Snes *s)
{
    uint8_t frame = m_lda8(s, dp8(s, 0x7C));
    uint16_t table = m_lda8(s, frame & 0x01) ? 0xC9CF : 0xC9A2;
    m_ldx16(s, table);
    static const uint8_t steps[] = {
        VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_INDIRECT_BANK, VID_STEP_SOURCE_BANK,
        VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_END,
    };
    const VidLineEffect effect = {
        .target = VID_LINE_WINDOWS, .table = PF_GAME(0x7E0000 | table), .indirect = true, .indirect_bank = 0x7E,
    };
    vid_set_line_effect(m_platform(s), PF_AT(0xCD0AF0), BATTLE_DMA, &effect, steps);
    m_rts(s);
}

// $CD:0C89
/* Call $C0:0002 preserving the data bank, direct page and flags; returns with
 * A = the direct page. */
void battle_call_c00002(Snes *s)
{
    Cpu *c = &s->cpu;
    m_push8(s, c->db);
    m_push16(s, c->d);
    m_push8(s, c->p);
    m_jsl(s, 0xCD0C8C, 0xC00002);
    c->p = m_pull8(s);
    if (c->p & CPU_FLAG_X) {
        c->x &= 0xFF;
        c->y &= 0xFF;
    }
    c->d = m_pull16(s);
    m_set_nz16(s, c->d);
    c->db = m_pull8(s);
    m_set_nz8(s, c->db);
    m_tdc(s);
    m_rtl(s);
}

// $CD:04AA
/* The tail of the battle frame wait: unless paused, $CD:3B5F; the sound
 * driver poll ($CD:3E44); then, unless $CE11 is set, $CD:0453 (with $CCEA set
 * and $A013 clear) and a pending $D1:ECF3 request ($CD44 set, $CD43 clear,
 * value $CD45); and finally $CD:04DF. Returns with RTL. */
void battle_frame_wait_tail(Snes *s)
{
    if (m_lda8(s, abs8(s, 0xCD02)) == 0 && !jsr(s, 0xCD04AF, 0xCD3B5F))
        return;
    if (!jsr(s, 0xCD04B2, 0xCD3E44))
        return;
    if (m_lda8(s, abs8(s, 0xCE11)) == 0) {
        if (m_lda8(s, abs8(s, 0xCCEA)) != 0 && m_lda8(s, abs8(s, 0xA013)) == 0 && !jsr(s, 0xCD04C4, 0xCD0453))
            return;
        if (m_lda8(s, abs8(s, 0xCD44)) != 0 && m_lda8(s, abs8(s, 0xCD43)) == 0) {
            m_lda8(s, abs8(s, 0xCD45));
            if (!jsl(s, 0xCD04D4, 0xD1ECF3))
                return;
            set_abs8(s, 0xCD44, 0x00);
        }
    }
    if (!jsr(s, 0xCD04DB, 0xCD04DF))
        return;
    m_rtl(s);
}
