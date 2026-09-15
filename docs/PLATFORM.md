# Platform abstraction

Game code today talks to the SNES hardware model directly: hand ports write
PPU, DMA and APU registers through `m_write8`, and generated code and the
interpreter do the same through the bus. This document designs the move to
platform services (video, audio, timing, input, saves) that game code calls
instead, with the SNES hardware model as one backend: the reference that every
other backend (a native renderer and mixer, widescreen, a GBA build) is checked
against.

P0 is implemented (`runtime/platform/`, see [P0](#p0-seams-and-the-snes-backend));
the rest is a design. Where P0 had to change the sketches below, the headers
are authoritative and "P0 as built" lists what changed and why.

| Milestone | What | Status |
|---|---|---|
| P0 | Service headers, SNES backend, first hand ports converted | done |
| P1 | Audio behind the API | |
| P2 | Video behind the API, subsystem by subsystem | |
| P3 | Native renderer, pixel-identical to `render.c` | |
| P4 | Widescreen | |
| P5 | GBA feasibility study | |

## Principles

- **Keep SNES semantics, drop SNES mechanics.** The game's data is tiles,
  tilemaps, palettes and sprite entries ([APPROACH.md](APPROACH.md)), and a
  pixel-identical renderer needs mode priorities, window logic and Snes9x's
  color math exactly. What the services hide is how that state gets into the
  PPU: register latches, DMA channel programming, HDMA channel setup, write
  order and the timing of each write.
- **Validation stays incremental.** (1) Hand ports call the API and the SNES
  backend performs the original writes, so shadow mode still compares byte for
  byte. (2) A second backend works from the abstract state and is compared
  pixel for pixel with the SNES backend at 256x224. (3) Only then deliberate
  departures (widescreen, other resolutions), checked on the original 256
  columns and against reference screenshots.
- **Existing seams stay.** `runtime/snes/bus.h` already reaches the PPU and APU
  through callbacks "so their implementation can be swapped", and `render.c`
  is kept separate from `ppu.c` "so a native renderer can replace it"
  ([RUNTIME.md](RUNTIME.md)). The services sit above those seams, where game
  code decides what to show and play.

## Survey: what the game uses

From the hand ports in `game/`, their comments and the docs. Code that isn't
ported yet surely uses more, so treat "not seen" as "not seen in ported code".

### Video

| Group | Registers | Seen in ported code |
|---|---|---|
| Display | `$2100` INIDISP | Field NMI `$C0:EA63` copies the brightness shadow `$0119` (0 becomes forced blank `$80`); event `$F0` (`$C0:3CD0`) only sets that shadow (`$19`, direct page `$0100`) or starts a fade. Battle NMI `$D1:E77C` forces blank while it works; `battle_frame_work` `$CD:088D` restores `$BB00`, halved while paused. Scene NMI `$C3:08B3` uses `$038D`, halved while paused. Battle IRQ tail `$D1:FEBF` forces blank after waiting for hblank. `$C0:0B4E` blanks and stops interrupts, DMA and HDMA |
| BG mode | `$2105` | Field NMI writes `$09` every frame: mode 1 with the BG3 priority bit. [RUNTIME.md](RUNTIME.md) also names mode 7 (attract pendulum, with sub screen math) and mode 2 with offset-per-tile (attract logo); the movie list names Mode 7 for the jet bike race and the Epoch flight. Whether the world map uses Mode 7 isn't established here |
| Scroll | `$210D-$2114` | No direct writes in ported code. Field computes scroll steps and tile map edges (`$C0:91AC`, `$C0:7F9A`, `$C0:2C41`); battle scrolls BG1-BG3 through HDMA tables (below) |
| Mode 7 | `$211A-$2120` | None in ported code; the renderer supports it and the scenes above use it |
| VRAM | `$2115-$2119` | Always DMA channel 7 in ported code: animated tiles `$FD:E022` (six of twelve slots per frame, alternating), battle uploads `$CF:E300` (tile block `$0B40` to VRAM `$7A00`, four rotating queued uploads, object graphics from descriptors at `$A654`-`$A6C3`), `$CD:3E8E` (generic transfer), `$CD:0607` (`$200` bytes at `$5800`/`$5980` on alternating frames, cleared or copied) |
| OAM | `$2101-$2104` | Battle uploads the 544-byte buffer at `$0700` (high table from `$0900`) over DMA with `$2102` = 0. `OBSEL` isn't written by ported code |
| CGRAM | `$2121-$2122` | Battle uploads the working palette `$7E:2200` (512 bytes) over DMA each frame; palette animation (`$FD:E39C`, twelve slots at `$0520`, every mode) works from the copy at `$7E:2000` into `$7E:2200` |
| Windows | `$2123-$2129`, `$212A-$212B` | Field NMI copies shadows `$0BD9-$0BDD` to `$2123-$2125`, `$2128-$2129`; battle copies `$C861-$C865` to `$2123-$2125`, `$212A-$212B`. Battle drives window 1 and 2 positions by HDMA |
| Color math | `$2130-$2132` | Field NMI copies `$0BDE` to `$2130`, writes `$2132` = `$E0` (black) then `$0121`. Event `$F1` (`$C0:3D07`) sets `$21` and writes `$17` or `$97` to `$0BE0`, presumably the `$2131` shadow (layer bits plus subtract) |
| Layers | `$212C-$212F` | Not in ported code |
| Counters | `$2137`, `$213C-$213F` | Battle wait-for-line `$CD:3B92`; battle NMI reads `$213F`. The unported event scheduler `$C0:5A46` and sprite frame queue `$C0:CA76` read them to stop at a scanline limit |

**HDMA.** Channels drive per-line state set up from game tables in WRAM:

| Where | Channel | Drives |
|---|---|---|
| Battle `$CD:0AEE` | 7, indirect mode 4 to `$2126` | Window 1 and 2 edges, table `$C9CF` on odd frames and `$C9A2` on even ones (`$7C` bit 0): two buffers, swapped by frame parity. `$CD:05B2` slides the window |
| Battle `$CD:06C4`, `$CD:076C` | not established | BG1-BG3 scroll tables at `$BC89`, `$BD09`, `$BD89`, refilled every frame from base scroll, shake (`$CD:0B51`) and offsets |
| Field `$C3:0011` | not established | A quadrilateral rasterized into per-line left/right X at `$7F:17C8` or `$7F:1AC8`: the light cone in `late-omen-terramutant`, presumably a window HDMA shape |
| Field NMI, scene NMI | enable shadows `$0128`, `$038C` | Whatever the mode set up; both turn HDMA off during vblank work and restore it |

**Sprites.** Every mode fills the OAM buffer at `$0700` in WRAM and uploads
it in vblank. Field (`$C0:B271`, `$C0:B309`) draws objects bottom row first
into three ranges by priority bits (`$0F80,X & $0C`), through the WRAM port,
hides entries left over from last frame with Y = `$E0`, and culls vertically
in the game code itself (`$C0:B788`: sums leaving the screen become `$E0`, with
wrap rules). X is 9 bits, the carry going to the high table. Battle
(`$CF:E83A`) depth-sorts objects; `$CF:E58A` then ORs priority bits from
`$9F46` into the first four entries, so the buffer is shared state between
routines. Scenes copy sprite lists into it (`$C3:0D0E`). Sprite graphics are
assembled per animation frame in WRAM as 32-byte tiles (field `$C0:E687`,
battle `$C1:1C55`, both with a flip table at `$C0:FD00`) and uploaded to VRAM.

**Graphics built in software.** Battle decodes 2-plane tiles to 4-plane at
`$2D00` (`$CD:1368`); menus draw font glyphs pixel by pixel into tile graphics
(`$C2:5E36`); the jet bike race scales bitmap rows into planar tiles in bank
`$7F` (`$C3:0309`). The decompressor writes all of its output through the
WRAM data port `$2180-$2183`, as do the field sprite tiles, the field sprite
table and the map unpacker (`$C0:A521`). That port is memory access, not a
video service.

### Audio

The CPU side of Square's driver is in bank C7 (`game/core/sound_driver.c`):

- **Boot upload** at `$C7:0000` over the IPL protocol. After a game over the
  driver jumps back to the SPC700 boot ROM and is uploaded again.
- **Commands** through `$C7:0004`: command in `$1E00`, parameters in
  `$1E01-$1E03`. `$18-$2F` and `$80-$FF` are sent directly; `$10`, `$11`,
  `$14`, `$15` play a song (song command, then the missing sample blocks of 16
  slots, three bytes per echo); `$30-$3F` become song `$00-$0F`; `$70` and
  `$71` are special. Known uses: event `$E8` sends `$18` with an effect and
  `$80` (play sound); `$EA` sends `$10` (play song); `$EB` sends `$81` with two
  operands, then `$82` and `$83`; `$EC` sends any command; the scene NMI sends a
  changed song; the scene pause sound is `$18`/`$44`/`$80`. What `$81-$83`,
  `$FC` and `$FE` do is not established.
- **Status reads** straight from the ports: event `$ED` waits for port 1 to
  read 0, `$EE` for port 3's low nibble to clear, the scene program waits on
  port 3 (`$7E:5AAA`), and the battle frame wait polls the driver through
  `$CD:3E44` while paused.

### Timing dependencies found so far

| Dependency | Where | Consequence |
|---|---|---|
| Vblank wait on `$4210` while NMIs are on | `$C0:3B71` (location change) | Depends on where the CPU samples /NMI; hung on a black screen until the runtime modeled it ([RUNTIME.md](RUNTIME.md)) |
| APU handshake | `$C7:0004` and uploads | The SPC700 reads ports 1-3 soon after port 0 changes; ports need `native_sync` and wait loops need `idle_clocks` ([PORTING.md](PORTING.md)). Uploads take frames of CPU time |
| Scanline budgets | `$C0:5A46`, `$C0:CA76` | Which objects run, and which sprite frames get built, in a frame depends on how fast the CPU is |
| Wait for a line, wait for hblank | `$CD:3B82`, `$D1:FEB8` | Battle writes at a chosen line; the IRQ handler forces blank after hblank |
| Frame parity | `$C9A2`/`$C9CF`, `$FD:E022`, `$CD:0607`, battle IRQ odd/even frames | Work split over two frames; the game assumes every frame runs |
| Interrupts clobbering math registers | `$D1:FE71` | Handlers restore `$4202-$4206`; irrelevant to native arithmetic |
| Real-time battles | the ATB fight | Input a frame apart changes the fight ([RECOMP.md](RECOMP.md) results) |
| Auto-joypad busy bit | `$C2:84EC`, `$C3:0A8E` | Input reads wait for `$4212` bit 0 |

**Input** is the auto-joypad read (`$4218/$4219`) in `$C2:84EC`, the scene
NMI, and the battle's pause, demo exit and L+R counter. L+R+Select+Start jumps
to `$C0:FF00`. **Saves** are the 8 KiB SRAM at `$6000-$7FFF` in banks
`$20-$3F` and mirrors; `ct_play` persists it as `.srm`.

## API sketch

The sketches in this section are the design as written before P0. The
implemented subset, with the changes the original code forced (a register per
call where the original writes them at different times, exact encodings, write
orders per call site), is in `runtime/platform/*.h` and summarized under
[P0 as built](#p0-as-built).

### Conventions

```c
/* runtime/platform/platform.h */
typedef struct Platform Platform;

/* Where the call stands in the original: the instruction it replaces, or the
 * JSR/JSL site and target for services the original reaches by calling a
 * routine. The SNES backend uses it for call-outs and the stack bytes they
 * write; other backends ignore it. It keeps `// $BB:AAAA` traceable below the
 * routine level. */
typedef struct { uint32_t site, target; } PfOrigin;
#define PF_AT(addr)             ((PfOrigin){ (addr), 0 })
#define PF_CALL(site, target)   ((PfOrigin){ (site), (target) })

/* The data a call refers to: a 24-bit game address while the WRAM layout is
 * kept, a host pointer once a subsystem owns its memory. */
typedef struct { uint32_t addr; const uint8_t *ptr; } PfData;
#define PF_GAME(a)   ((PfData){ (a), NULL })
```

**Binding.** One backend per build, bound at compile time, so calls cost a
function call. In `ct_dev`, `ct_play` and `ct_fuzz` the backend is the SNES
backend and `Platform` is the machine (`m_platform(s)` in `machine.h`), which
keeps shadow clones working: the native routine's calls act on the clone.
Native consumers (a renderer, a mixer) attach to it as observers. A release
build without the machine binds the native backend.

**Commands, not diffs.** Every setter is a command acted on when called. The
SNES backend performs the writes then, where the original did; a native
backend folds commands into retained state (`VidFrame` below) and renders at
the end of the frame. A retained state compared at vblank can't recover write
order or timing, so the SNES backend never works from it.

### Video

```c
/* runtime/platform/video.h */
typedef enum { VID_BG1, VID_BG2, VID_BG3, VID_BG4, VID_OBJ, VID_BACK } VidLayer;
typedef enum { VID_WIN_OFF, VID_WIN_IN, VID_WIN_OUT } VidWinUse;

typedef struct {
    uint8_t mode;               /* 0-7; the game uses 1, 2 and 7 (see Survey) */
    bool bg3_priority;          /* mode 1 */
    uint8_t brightness;         /* 0-15 */
    bool blank;
} VidDisplay;

typedef struct {
    uint16_t map_base;          /* VRAM word address of the tilemap */
    uint8_t map_w, map_h;       /* 32 or 64 tiles */
    uint16_t char_base;         /* VRAM word address of tile 0 */
    uint8_t mosaic;             /* 1-16 */
    uint16_t hofs, vofs;        /* 10-bit scroll */
} VidBg;

typedef struct {
    uint16_t main, sub;         /* layer bits: screens the layer is on */
    uint16_t win_main, win_sub; /* layer bits: window masks the layer */
    VidWinUse w1[6], w2[6];     /* per VidLayer, VID_BACK is the color window */
    uint8_t logic[6];           /* OR, AND, XOR, XNOR */
    uint8_t w1_left, w1_right, w2_left, w2_right;
} VidWindows;

typedef struct {
    bool subtract, half, sub_screen, direct;
    uint8_t clip_black, math_region;   /* never, outside, inside, always */
    uint16_t layers;            /* layer bits taking part */
    uint16_t fixed;             /* BGR555 */
} VidColorMath;

typedef struct {
    int16_t a, b, c, d, cx, cy, hofs, vofs;
    bool hflip, vflip, extbg;
    uint8_t outside;            /* wrap, transparent, tile 0 */
} VidMode7;

typedef struct {
    int16_t x, y;               /* SNES backend: 9-bit X and 8-bit Y with wrap */
    uint16_t tile;              /* 9-bit name */
    uint8_t palette, priority;  /* 0-7, 0-3 */
    bool hflip, vflip, large;
} VidSprite;

typedef struct {
    uint8_t size_select;        /* OBSEL sizes */
    uint16_t base, name_select; /* VRAM word addresses */
} VidObjConfig;

/* A per-line effect: a table the game keeps in memory, read at the start of
 * every frame, in HDMA's format (line counts with repeat bit, then values),
 * direct or indirect. The target names what it drives; the SNES backend maps
 * it to a channel's $43x0/$43x1. */
typedef enum {
    VID_LINE_WINDOWS,           /* w1_left..w2_right (battle, $CD:0AEE) */
    VID_LINE_WINDOW1,           /* w1_left, w1_right */
    VID_LINE_SCROLL_BG1, VID_LINE_SCROLL_BG2, VID_LINE_SCROLL_BG3,
    VID_LINE_MODE7_ABCD, VID_LINE_COLOR_MATH, VID_LINE_FIXED_COLOR,
    VID_LINE_BRIGHTNESS,
} VidLineTarget;

typedef struct {
    VidLineTarget target;
    PfData table;
    bool indirect;
    uint8_t indirect_bank;
} VidLineEffect;

/* How the SNES backend encodes a transfer: the channel and the order the
 * original wrote the registers in. Other backends ignore it. */
typedef enum { XFER_CHANNEL_THEN_ADDRESS, XFER_ADDRESS_THEN_CHANNEL } VidXferOrder;
typedef struct { uint8_t channel; VidXferOrder order; } VidSnesXfer;

void vid_set_display(Platform *pf, PfOrigin o, const VidDisplay *d);
void vid_set_bg(Platform *pf, PfOrigin o, VidLayer bg, const VidBg *b);
void vid_set_scroll(Platform *pf, PfOrigin o, VidLayer bg, uint16_t hofs, uint16_t vofs);
void vid_set_windows(Platform *pf, PfOrigin o, const VidWindows *w, uint32_t fields);
void vid_set_color_math(Platform *pf, PfOrigin o, const VidColorMath *m, uint32_t fields);
void vid_set_mode7(Platform *pf, PfOrigin o, const VidMode7 *m7, uint32_t fields);
void vid_set_obj_config(Platform *pf, PfOrigin o, const VidObjConfig *c);

void vid_upload_vram(Platform *pf, PfOrigin o, uint16_t vram_addr, PfData src, uint16_t len, const VidSnesXfer *x);
void vid_upload_palette(Platform *pf, PfOrigin o, uint8_t first, PfData src, uint16_t count, const VidSnesXfer *x);
/* The 544-byte OAM image (128 entries, 32-byte high table), as the game builds it. */
void vid_upload_oam(Platform *pf, PfOrigin o, PfData oam, const VidSnesXfer *x);
/* Later form, once a subsystem owns its sprite memory (see Migrating). */
void vid_submit_sprites(Platform *pf, PfOrigin o, const VidSprite *s, uint16_t count);

void vid_set_line_effect(Platform *pf, PfOrigin o, uint8_t slot, const VidLineEffect *e);
void vid_enable_line_effects(Platform *pf, PfOrigin o, uint8_t slots);

/* A write at a known line of the visible frame, for the rare raster writes
 * outside HDMA (the battle IRQ's forced blank). */
void vid_raster_display(Platform *pf, PfOrigin o, uint16_t line, const VidDisplay *d);
```

`fields` is a bit set of the members a call sets, since the original writes
register groups partially (the field NMI writes window 2 positions but not
window 1).

What a renderer consumes:

```c
typedef struct {
    VidDisplay display;
    VidBg bg[4];
    VidWindows windows;
    VidColorMath math;
    VidMode7 mode7;
    VidObjConfig obj;
} VidLineState;

typedef struct {
    uint16_t line;              /* first line (1-224) this state applies to */
    VidLineState state;
    uint16_t palette_changed;   /* nonzero if CGRAM changed before this line */
} VidSpan;

typedef struct {
    const uint8_t *vram;        /* 64 KiB, SNES layout */
    const uint16_t *cgram;      /* 256 BGR555 */
    VidSprite sprites[128];     /* in OAM order: order decides overlap */
    uint8_t sprite_count;
    uint8_t first_sprite;       /* OAM priority rotation */
    VidSpan *spans;             /* spans[0].line == 1 */
    uint16_t span_count;
} VidFrame;
```

**Why the boundary is here.**

- **Register groups, not registers.** Callers set a layer's scroll or the
  window edges; the backend knows `$210D` shares a latch with `$210E` and that
  Mode 7 values share a third ([`ppu.h`](../runtime/snes/ppu.h)). Latches and
  write order are what a native backend should never see.
- **Uploads, not DMA.** The game never uses DMA for anything but copying
  memory into VRAM, CGRAM and OAM in ported code. `VidSnesXfer` carries the
  channel and register order the SNES backend must reproduce: shadow mode
  compares the DMA registers as last written ([PORTING.md](PORTING.md)).
- **VRAM stays a 64 KiB tile store in SNES layout.** Tile graphics, tilemaps
  and the Mode 7 plane are addressed where the game puts them. A native
  renderer keeps decoded tile caches with dirty ranges from uploads. A
  different layout is a GBA question (P5), not a P0-P3 one.
- **Per-line effects are tables with a target.** The game already builds
  HDMA tables in WRAM and rewrites them in place between frames (battle's two
  buffers). A native backend expands the table at the start of each frame, as
  `hdma_init` does, into `VidSpan`s. Naming the target rather than the
  register lets a GBA backend map a window table to an hblank handler.
- **Sprites go through the OAM image first.** Several routines read and
  modify the buffer at `$0700` (battle's `$CF:E58A` sets priority bits in it),
  so it stays game memory until all of them are native. `vid_submit_sprites`
  with signed coordinates is the form widescreen needs, and comes later.

**How the SNES backend reproduces the original.** Each call performs the
writes the original made at that point, through `m_write8`, so bus time, open
bus and every register value are what the interpreter would produce, and
shadow mode's comparison doesn't change. Order matters only where it's
observable: within latched groups (scroll, Mode 7, CGRAM `$2121`/`$2122`, OAM
`$2102-$2104`, the VRAM port), for APU ports, and for the last value in each
register at a checkpoint. Instructions around the writes (the value left in
A, flags, stack bytes) stay in the port: the API replaces bus writes, not the
65816 code that computed them.

**What it can't reproduce, and what to do.**

- **Mid-frame writes.** The SNES backend writes when called, which is when
  the port runs: exact in shadow mode (timing isn't compared) and as
  approximate as today's direct writes in `on` mode. Native backends need the
  line, so raster calls take it explicitly; the SNES backend can check it
  against `s->vline` in `off` and `shadow` runs.
- **Budgets on the beam.** A native backend has no beam. See Timing.
- **Writes a call can't express.** A routine writing a register as a side
  effect of generic code (a table-driven copy into `$21xx`) keeps a raw
  `vid_snes_write(pf, o, reg, v)` escape, allowed only in the SNES backend
  build and flagged by the lint below, until its meaning is known.

### Audio

```c
/* runtime/platform/audio.h */
typedef struct { uint8_t command, p1, p2, p3; } AudioCommand;

/* The $C7:0004 interface: SNES backend writes $1E00-$1E03 and calls out to
 * $C7:0004 from o.site, so the driver's echo waits and uploads run as today
 * (frames pass). Returns when the original's JSL would. */
void au_command(Platform *pf, PfOrigin o, AudioCommand c);

static inline void au_play_song(Platform *pf, PfOrigin o, uint8_t song)
{
    au_command(pf, o, (AudioCommand){ 0x10, song, 0, 0 });
}
static inline void au_play_sound(Platform *pf, PfOrigin o, uint8_t effect, uint8_t p2)
{
    au_command(pf, o, (AudioCommand){ 0x18, effect, p2, 0 });   /* event $E8 passes $80 */
}

/* Driver status the game polls: the byte on APU port 1-3. Named by port until
 * its meaning is known (event $ED: port 1 zero; $EE: port 3 low nibble). */
uint8_t au_status(Platform *pf, PfOrigin o, uint8_t port);

/* The driver upload at $C7:0000, and again after a game over. */
void au_boot(Platform *pf, PfOrigin o);

/* Host side: 32000 Hz stereo, as apu_take_samples. */
size_t au_take_samples(Platform *pf, int16_t *out, size_t max_frames);
```

The boundary is the command interface because every caller already funnels
into one JSL entry, and `$C7:0004` is a hand port: in a build with another
audio backend, its registry entry calls that backend, so interpreted and
generated callers are routed too. Below it is Square's protocol (three bytes
per echo, block slots), which is the SNES backend's business. The APU-direct
backend (P1) still runs Square's driver on the SPC700 and S-DSP cores, fed
without the main CPU's handshake; replacing the driver with a native
sequencer stays optional ([APPROACH.md](APPROACH.md)).

### Timing

```c
/* runtime/platform/timing.h */
typedef void (*PfHandler)(Platform *pf);

/* Wait for the next frame. SNES backend: call out to the mode's wait routine
 * (field $C0:EC60, menu $C2:0346, battle $C1:007E), whose loops are idle
 * entries. Native: yield the game fiber to the host loop. */
void tm_wait_frame(Platform *pf, PfOrigin o);

/* The vblank handler. SNES backend: the JML stub at $0500 (as $C0:0B64). */
void tm_set_vblank_handler(Platform *pf, PfOrigin o, uint32_t handler_addr, PfHandler native);

/* A handler at a line (V-timer IRQ): $4209/$420A, $4200 and the stub at $0504. */
void tm_set_line_handler(Platform *pf, PfOrigin o, uint16_t line, uint32_t handler_addr, PfHandler native);

/* Wait until the beam reaches a line (battle $CD:3B82). */
void tm_wait_line(Platform *pf, PfOrigin o, uint16_t line);

/* The current line, for code that budgets work by the beam ($C0:5A46,
 * $C0:CA76). SNES backend: latch and read the counters as the original does.
 * Native: a model (see Risks). */
uint16_t tm_beam_line(Platform *pf, PfOrigin o);

/* Before I/O another processor watches: native_sync on the SNES backend. */
void tm_sync(Platform *pf);
```

Frame callbacks match how the game is built: each mode's main loop waits for a
flag its NMI handler clears (`$0152` field, `$4B` menu, `$9E` battle), and the
handler does the uploads. The fibers that run hand ports
(`runtime/port/fiber.c`) are what a native host loop needs to suspend a main
loop in `tm_wait_frame`. Line-timed effects are explicit primitives because
there are few of them and each needs a decision per backend.

### Input and saves

```c
/* runtime/platform/input.h */
typedef uint16_t PadBits;   /* JOY1 order: B Y Select Start Up Down Left Right A X L R */

bool in_ready(Platform *pf, PfOrigin o);                 /* SNES: $4212 bit 0 clear */
PadBits in_read(Platform *pf, PfOrigin o, uint8_t pad);   /* SNES: $4218/$4219 */
void in_host_set(Platform *pf, uint8_t pad, PadBits b);   /* host side, once per frame */

/* runtime/platform/save.h */
enum { SAVE_SIZE = 0x2000 };
uint8_t sv_read8(Platform *pf, PfOrigin o, uint16_t offset);
void sv_write8(Platform *pf, PfOrigin o, uint16_t offset, uint8_t v);
const uint8_t *sv_host_bytes(Platform *pf, bool *dirty);  /* host side: persist */
```

These are thin because the game's use is: one controller read per frame and
byte access to 8 KiB. `ct_play`'s `.srm` handling and movie input move behind
the host side unchanged.

**Not services.** The hardware multiplier and divider (`$4202-$4206`,
`$4214-$4217`) are arithmetic. Ports keep writing them while their register
state is compared (a `machine.h` helper can name the pattern), and native code
computes directly once the compatibility layer goes. The WRAM port
`$2180-$2183` is memory access.

## Adopting it in ported code

### Rules for new hand ports

1. No bus access to `$2100-$217F`, `$4200-$421F` (except the math registers)
   or `$4300-$437F` in `game/`. Use the service call. Where none fits, add one
   to the header with its SNES implementation first.
2. Keep every CPU-visible effect as today: registers, flags, stack bytes,
   shadows in WRAM. The call replaces the write, not the `LDA` before it.
3. Pass the origin: `PF_AT` the instruction whose write the call stands for,
   or `PF_CALL` for a service the original reaches through a JSR/JSL.
4. Shadows are game state. The field's `$0BD9` window shadow stays WRAM; the
   call goes where the NMI copies it to the PPU.
5. A write the original makes on a chosen line uses a raster call with the
   line.
6. A unit test in `tests/` scans `game/` for register constants outside the
   services and the allowlist (math registers, WRAM port), so rule 1 holds.

### Shadow validation

Unchanged for the SNES backend: the native routine still runs on a clone, its
calls write the clone's registers, and the machines are compared at call-outs
and return. Two additions make the later stages checkable:

- **An I/O attribution log** (`ct_dev --io-log`): every PPU, APU, DMA and
  HDMA register access with who made it (a registry entry and its API call,
  a generated block, or the interpreter's PC). The bus hooks shadow mode uses
  for generated blocks already record accesses. A subsystem is behind the API
  when its movies show no access outside API calls.
- **An observer on the SNES backend** that folds every call into a
  `VidFrame` (and the audio command stream), so the command-built state can
  be compared with the state decoded from the PPU register layer.

### Generated code and the interpreter

Generated blocks and the interpreter write registers through the bus and stay
on the SNES backend until replaced. Native consumers still see their effects:

- **Video**: a register decoder builds a `VidFrame` from the `Ppu` state at
  each `on_line` callback (lines 1-224, before that line's HDMA writes, as
  `render.c` draws them), merging identical lines into spans. It works whoever
  wrote the registers, so the native renderer (P3) doesn't wait for P2.
- **Audio**: `$C7:0004` is the choke point (above). Direct APU port access
  elsewhere has to be found with the I/O log and ported (P1).

### Migrating an existing port: the sprite path

**Step 1 (exact; done in P0).** The battle's OAM upload in `battle_upload_vram`
(`$CF:E300`, `game/battle/frame.c`) before P0:

```c
put_d8(s, 0x4370);          /* TDC : STA $4370 -- one register, A to B */
put8(s, 0x4371, 0x04);      /* to $2104 */
put8(s, 0x4372, 0x00);
put8(s, 0x4373, 0x07);
put8(s, 0x4374, 0x00);      /* from $00:0700 */
put8(s, 0x4375, 0x20);
put8(s, 0x4376, 0x02);      /* $220 bytes */
put_d8(s, 0x2102);          /* OAM address 0 */
start_dma7(s);              /* LDA #$80 : STA $420B */
```

After (as built):

```c
// $CF:E300
static const uint8_t steps_full_short_target[] = {
    VID_STEP_CONTROL, VID_STEP_DEST, VID_STEP_SOURCE_LO, VID_STEP_SOURCE_HI, VID_STEP_SOURCE_BANK,
    VID_STEP_SIZE_LO, VID_STEP_SIZE_HI, VID_STEP_TARGET_LO, VID_STEP_START, VID_STEP_END,
};
static const VidSnesXfer oam = { BATTLE_DMA, DMA_ONE_REGISTER, steps_full_short_target };
vid_upload_oam(pf, PF_AT(0xCFE301), PF_GAME(0x000700), &oam);
m_tdc(s);
m_lda8(s, 0x80);            /* TDC ... LDA #$80 before STA $420B: A, N and Z as the original leaves them */
```

The SNES backend writes `$4370-$4376`, `$2102` and `$420B` with the same
values in the same order (the `TDC` value is the direct page's low byte, 0 in
battle; a registry guard keeps that true), and the bus model runs the transfer
as before. Shadow mode sees the same machine. The observer decodes the 544
bytes into `VidSprite[128]` for the frame. The builders (`$CF:E83A`, field
`$C0:B271`/`$C0:B309`) don't change at all in this step: they write WRAM.

**Step 2 (between P3 and P4, a deliberate change of representation).** Once every
routine that touches the field's OAM buffer is native (the builder, the
upload, and whatever else the I/O and WRAM access logs show), the field
releases `$0700-$091F` and builds a sprite list. Today's `$C0:B309`, in
outline:

```c
call $C0:B701 (update records); if carry: return
range = priority bits of $0F80,X            /* three OAM ranges */
push high table bytes, write them at the range's high pointer
D = $2100                                   /* STA $80 is the WRAM port */
for each 8-byte record at $7F:4BC0 + $1700,X:
    write X, Y, tile, attribute through $2180
advance the range's next entry by count * 4
```

After (pseudo-C):

```c
// $C0:B309
static void field_draw_object_sprites(Field *f, int slot)
{
    const FieldObject *o = &f->objects[slot];
    if (!field_update_object_sprites(f, slot))
        return;
    VidSpriteList *range = &f->ranges[field_sprite_range(o->priority)];
    for (int i = 0; i < o->record_count; i++) {
        const SpriteRecord *r = &o->records[i];
        vid_sprite_list_push(range, (VidSprite){
            .x = o->screen_x + r->dx, .y = o->screen_y + r->dy,
            .tile = r->tile, .palette = r->palette, .priority = r->priority,
            .hflip = r->hflip, .vflip = r->vflip });
    }
}

// $C0:B271
void field_build_sprite_table(Field *f, Platform *pf)
{
    field_clear_ranges(f);
    for (int row = FIELD_ROWS - 1; row >= 0; row--)
        for (int slot = f->row_head[row]; slot >= 0; slot = f->objects[slot].next_in_row)
            field_draw_object_sprites(f, slot);
    /* OAM order decides overlap: the ranges go out in the order their entries
     * sat in the buffer ($0710, $0770, $08A0) */
    const VidSpriteList *all = field_concat_ranges(f);
    vid_submit_sprites(pf, PF_AT(0xC0B271), all->sprites, all->count);
}
```

What moves: hiding last frame's leftovers disappears (a list has a count);
vertical culling and the 9-bit X wrap move out of game code into the SNES
backend's OAM encoder, which must reproduce `$C0:B788`'s rules exactly
(including sums that wrap to `$E0` or more being kept when the screen Y is
past 255). That is the change widescreen needs: coordinates stay signed until
a backend decides what's visible.

Validation for step 2 can't compare WRAM at `$0700`. Instead shadow mode masks
that range for the entry and compares semantically: the original's buffer,
decoded by the same OAM decoder at the upload, must equal the SNES encoding of
the submitted list, entry for entry and in order. Then P3's pixel comparison
covers the result on screen.

## Backends and validation plan

### P0: seams and the SNES backend

- `runtime/platform/` with the five headers; the SNES backend on the
  development system; `m_platform(s)`; the lint test.
- Convert the ports that touch hardware most directly:
  `display_blank_and_stop_interrupts`, the register copies in `field_nmi`,
  the event sound and screen commands (`$E8`-`$EE`), `battle_nmi`,
  `battle_frame_work`, `battle_upload_vram`, `battle_vram_transfer`,
  `battle_setup_window_hdma`, `tile_animation_upload`, `input_read_joypad`,
  `scene_nmi`.
- `ct_dev --io-log`.

**Done when:** the lint passes with no register constants left in the
converted files; all 21 movies run in shadow mode with 0 mismatches; the
converted routines pass their fuzz runs again; `on` mode's sampled frame
agreement with `off` is the same as before conversion.

#### P0 as built

| Path | What |
|---|---|
| `runtime/platform/platform.h` | `Platform`, `PfOrigin` (`PF_AT`, `PF_CALL`), `PfData` (`PF_GAME`), `PF_API` binding |
| `runtime/platform/video.h` | display, BG mode, windows, color math, fixed color, line effects, VRAM/CGRAM/OAM uploads, decoders for SNES-format shadows, the `VidSnesXfer` step lists, the `vid_snes_transfer` escape |
| `runtime/platform/timing.h` | `tm_enable_interrupts`, `tm_ack_vblank`, `tm_ack_line_irq`, `tm_video_status`, `tm_reset_beam_latch`, `tm_sync` |
| `runtime/platform/audio.h` | `au_command` (`AudioCommand` with a parameter count), `au_play_song`, `au_status`, `au_take_samples` |
| `runtime/platform/input.h` | `in_ready`, `in_read`, `in_read_low`, `in_read_high`, `in_host_set` |
| `runtime/platform/save.h` | `sv_read8`, `sv_write8`, `sv_host_bytes` |
| `runtime/platform/snes/` | the SNES backend: `static inline` definitions writing through `m_write8`, and the service call marker `--io-log` reads |
| `runtime/port/machine.h` | `m_platform(s)` (the machine is the platform), `m_io_bank` |
| `dev/io_log.c` | `ct_dev --io-log` |
| `tests/test_platform_lint.py` | rule 1 over `game/` |

**Binding.** Headers declare every call `PF_API`; without
`CT_PLATFORM_EXTERN` that is `static inline` and the SNES definitions follow,
so a call compiles to the writes it stands for plus two stores of the call
marker. `gato-battle` with `--native on`, six alternating runs each on a shared
machine: 6.99-7.40 s before (median 7.12 s), 7.03-7.32 s after (median 7.18 s),
within run-to-run noise.

**Converted.** Whole files, since the lint works per file:
`game/core/display.c` (`$C0:0B4E`), `game/field/nmi.c` (the register copies of
`$C0:EA63`), `game/event/sound.c` (`$E8`-`$EE`; the screen commands `$E3`-`$E7`,
`$F0`-`$FA` in `screen.c` only write WRAM shadows and needed nothing),
`game/battle/nmi.c` (`battle_nmi`, and with it `battle_irq` and
`battle_irq_after_hblank`), `game/battle/frame.c` (`battle_frame_work`,
`battle_upload_vram`, `battle_vram_transfer`, `battle_setup_window_hdma`, and
with them `battle_upload_alternating_vram` `$CD:0607` and the controller reads of
`battle_frame_update` `$CF:E58A`), `game/core/tile_animation.c`,
`game/core/input.c` (`input_read_joypad`) and `game/scene/nmi.c`. The
multiplier and divider stay direct (not a service).

**What changed from the sketch, and why.**

- **Calls come with their first user.** The SNES encoding of a call (which
  registers, in which order, partial writes) can only be taken from the original
  code that uses it, so the backend implements what the converted ports use.
  Not implemented yet: `vid_set_bg`, `vid_set_scroll`, `vid_set_mode7`,
  `vid_set_obj_config`, `vid_submit_sprites`, `tm_wait_frame`,
  `tm_set_vblank_handler`, `tm_set_line_handler`, `tm_wait_line`,
  `tm_beam_line`, `au_boot`.
- **One call per register group the original writes at a different time.**
  The field NMI writes BG mode at the top and brightness at the bottom, so
  `VidDisplay` holds brightness and blank only and `vid_set_mode` is separate.
  The fixed color register sets chosen channels to an intensity, so it is
  `vid_set_fixed_color(channels, intensity)` rather than a BGR555 member of
  `VidColorMath`. The windows' `fields` bits are one per register.
- **Exact encodings.** Shadow validation compares register bytes and fuzzing
  randomizes the shadows, so every structure encodes back to the byte it was
  decoded from: `VidWinUse` keeps the (enable, invert) bit pair, and
  `VidDisplay`, `VidMode`, `VidWindows` and `VidColorMath` carry the bits the
  hardware ignores (`snes_extra`, `large_tiles`). Ports decode the game's
  SNES-format shadows with `vid_*_decode`.
- **Write order is a step list.** The converted upload sites use six different
  orders and partial reprogramming (a second upload reusing the first one's
  mode, destination and bank; the tile animation's 16-bit `STZ` of the size
  before writing its low byte), so `VidSnesXfer` carries the channel, the
  control byte and a `VID_STEP_*` list instead of an order enum. An upload's
  length is 1-65536 bytes; 0 means empty, and the backend stops at the first
  size step, which is where the battle's queued uploads check.
- **More small calls.** `vid_begin_vram_uploads` (the VRAM port mode, with the
  tile animation's channel setup ahead of its uploads), `vid_fill_vram` (a
  fixed-source transfer), `vid_stop_uploads` (`$420B` = 0), and the timing and
  input reads above. Status reads return the byte as the SNES reads it, with
  named bits, because the ports keep the flags of the original's `LDA`, `CMP`
  and `BIT`.
- **`AudioCommand.count`.** Event `$E8` sets two parameters and the driver sends
  the third as the last command left it; the scene song sets three.
- **Addressing.** Services address registers in bank `$00`. Where the original
  reached them through the data bank, the entry has a registry guard
  (`event_sound_bank_is_system`, `battle_data_bank_is_io`) or the port stops at
  the instruction after a call-out that could have changed the bank. The OAM
  upload's mode and address come from `TDC` in the original, so
  `battle_upload_vram` is guarded on the direct page's low byte being 0. No
  movie call is refused by these guards.
- **One escape.** The battle upload described by the table at `$9EEA` takes its
  mode and destination from game data whose meaning isn't established; it uses
  `vid_snes_transfer`, which the lint allows only there.
- **`vid_raster_display`** takes `VID_AT_LINE_HANDLER`: the line the battle IRQ
  fires on isn't established.

**I/O log.** `ct_dev --io-log out.txt` counts every access to `$2100-$217F`,
`$4016-$4017`, `$4200-$421F` and `$4300-$437F` by origin and register, with the
frame of the first one, and prints a summary. Hand ports are identified with
`native_running_entry()` (the running job, idle entry or generated block) and
the backend's call marker, the interpreter by the instruction's address, HDMA
by the line hook that precedes its writes, and DMA transfer bytes are counted
separately under the origin that started the transfer. It is meaningful with
`--native on`: in shadow mode hand ports run on clones and generated blocks are
replayed off the machine. The log is a table rather than a stream because the
DMA bytes alone run to tens of millions per movie. Summed over the 22 movies
(`on` mode; register accesses, excluding transfer bytes):

| Origin | PPU | APU ports | CPU `$42xx` | multiply/divide | DMA registers |
|---|---|---|---|---|---|
| hand ports, through services | 3130864 | 468 | 3766795 | 0 | 4605113 |
| hand ports, direct | 0 | 2058028 | 0 | 3507500 | 10008 |
| idle entries | 1403421 | 15854144 | 1743344 | 0 | 0 |
| generated blocks | 6089605 | 5106557 | 2994264 | 2404276 | 8118416 |
| interpreter | 1318900 | 21199 | 177068 | 272222 | 1047056 |
| HDMA | 262790127 | | | | |

Direct hand-port access left: the sound driver's APU ports (P1), the
multiplier and divider, and `menu_object_wave_hdma`'s HDMA setup
(`menu/objects.c`). Transfer bytes: 238.9 million started by services, 265.6
million by generated blocks, 64.8 million by the interpreter (2.57 million per
movie is the attract sequence's RAM code).

**Results.**

- Lint: `python3 -m unittest tests.test_platform_lint` passes; the files still
  listed as unconverted are the ones above plus idle entries' polls, the battle
  debug path and two false positives, each noted in the test.
- Shadow: all 22 movies (the six main movies and the 16 save movies) exit 0
  with no mismatch, 1337193299 generated-block checks included. Calls of the
  converted routines, all matched: `input_read_joypad` 266551,
  `battle_irq`/`battle_irq_after_hblank` 108614, `battle_nmi` and
  `battle_setup_window_hdma` 108391, `battle_frame_work`, `battle_upload_vram`
  and `battle_frame_update` 107815, `battle_upload_alternating_vram` 107513,
  `tile_animation_upload` 106647, `field_nmi` 104751, `scene_nmi` 36928,
  `battle_vram_transfer` 36678, `display_blank_and_stop_interrupts` 378, event
  `$EE` 433, `$E8` 148, `$EB` 115, `$EA` 107, `$ED` 107, `$EC` 75.
- `on` mode: every sampled frame (every 100th, 3122 frames) of all 22 movies is
  byte-identical to the build before conversion, and the native reports are the
  same; agreement with `off` is unchanged at 1426 of 3122. The services make
  exactly the original's accesses, so bus time, and with it `on` mode's timing,
  doesn't move.
- Fuzzing: every converted routine matches on every state that completed, 0
  mismatches: `display_blank_and_stop_interrupts` 20000 (from a `field` state);
  event `$ED` and `$EE` 5000 each, `$EA` 4232 of 5000, `$EC` 3351, `$E8` and `$EB`
  2516 (`--range Y=0000-7FFF --keep 7E006D-7E006E`; the rest skipped where the
  driver call-out doesn't return on random RAM, the same counts as the build
  before conversion); `battle_irq`, `battle_vram_transfer`,
  `battle_upload_alternating_vram`, `tile_animation_upload` and
  `input_read_joypad` 30000 each, `battle_frame_work` 29897 of 30000,
  `battle_upload_vram` 10000, `battle_frame_update` 9354 of 10000,
  `battle_nmi` 292 of 2000 (`--interrupt --keep 7E007F-7E007F`), and
  `battle_irq_after_hblank` 30000 and `battle_setup_window_hdma` 20000 with
  `--db 00` (from `battle20000.state`, whose data bank `$7E` their new guards
  refuse; `ct_fuzz --db` sets it); `scene_nmi` 19901 of 20000 (`--interrupt`,
  from the attract sequence). `field_nmi`, not fuzzed before, 2916 of 3000
  (`--interrupt --step-limit 20000`, the same as before conversion).
  `recomp_bus_diff` still reports 0 mismatches.

### P1: audio behind the API

- Inventory APU port access with the I/O log over the movies; route all of it
  through `au_*` (the boot upload at `$C7:0000`, the status waits, the battle
  poll `$CD:3E44`).
- APU-direct backend: the SPC700 and S-DSP run outside the main machine's
  timeline; a command is delivered through the ports and the SPC700 is run
  until it answers, with no main-CPU time passing. The registry entry at
  `$C7:0004` calls it.

**Done when:** the I/O log shows no APU port access outside `au_*` in any of
the 21 movies (generated blocks on); shadow mode stays at 0 mismatches with
the SNES backend; with APU-direct, every movie still reaches its end state
(`gato-battle` won, same rooms), and `tools/audio_compare.py` against the SNES
backend gives spectrum correlation and log-spectral distance at least as good
as the Snes9x comparison in [RUNTIME.md](RUNTIME.md) for `new-game` and
`field`. Frames may shift where uploads no longer take CPU time, so the
comparison allows per-segment lag.

### P2: video behind the API, subsystem by subsystem

Order: shared code (display blank, palette upload, animated tiles), battle
(most of its vblank path is already ported), field (NMI copies, the deferred
uploads flagged in `$1D78-$1D7B`, the sprite upload), scenes (Mode 7,
offset-per-tile), menus. Each subsystem's unported register writers are ported
from their generated drafts ([RECOMP.md](RECOMP.md), M5).

**Done when**, per subsystem: over the movies that cover it, the I/O log shows
no PPU, DMA or HDMA register write outside `vid_*`; shadow mode has 0
mismatches; and the observer's command-built `VidFrame` equals the decoder's
`VidFrame` on every frame (same spans, sprites, palette and VRAM).

### P3: native renderer, pixel-identical

- A frame renderer working from `VidFrame`: whole spans instead of per-pixel
  layer resolution per line, tile caches invalidated by uploads, the Snes9x
  color pipeline (`render_snes9x_color`) as its reference color mode.
- Fed by the decoder, so it runs on every movie from the start; fed by
  commands for subsystems past P2.
- It should also make runs faster: the scanline renderer is 81% of `on` mode's
  time ([RECOMP.md](RECOMP.md)).

**Done when:** every frame of all 21 movies is pixel-identical to `render.c`
at 256x224, including battle windows, split-screen dialogs and the Mode 7
scenes (hi-res frames, if any turn up, compared at `render.c`'s 256-wide
sampling); `ct_play` uses it by default; `on` mode over `gato-battle` is
measurably faster.

### P4: widescreen

- A per-mode policy: widen (field, Mode 7 scenes, and the world map if its
  content allows) or pillarbox (menus, presumably battle and fixed-art scenes).
- Signed sprite coordinates through `vid_submit_sprites`, culling in the
  backend, window edges beyond 0-255, margin content (below).

**Done when:** with widescreen off, output is P3's exactly; with it on, the
center 256 columns are pixel-identical to P3 on every frame of the movies for
widened modes, and pillarboxed modes are identical in full; margins show no
tiles from outside what the map holds (a debug overlay marks margin pixels
drawn from never-uploaded VRAM or unmapped map cells), checked by reviewing
screenshots at chosen frames.

### P5: GBA feasibility study

A study, not a port. It answers, with measurements:

| Question | How |
|---|---|
| CPU | Native game logic per frame on a 16.8 MHz ARM7TDMI: build the native code for ARM, run recorded frames on hardware or an emulator supplied locally, count time |
| Memory | Census of WRAM tables, decompressed data and VRAM use per mode against 32 KiB IWRAM, 256 KiB EWRAM, 96 KiB VRAM, with ROM data read in place |
| Resolution | 240x160 against 256x224: per-mode choice of scaling, camera cropping or re-layout of the UI (dialog boxes, battle window) |
| Video features | Table of `VidFrame` features against GBA hardware: 4 BG layers, 128 sprites, separate BG and OBJ palettes (both BGR555), affine BGs (Mode 7's 128x128 map of 256 8bpp tiles fits an affine BG), two windows and an OBJ window, blending without arbitrary subtraction, hblank interrupts or DMA for per-line tables |
| Audio | Cost of running the SPC700 and S-DSP on ARM (presumably too high) against a native sequencer and sample conversion |
| Assets | A converter that builds the GBA image from the user's ROM locally, never distributed |

**Done when:** the report gives a go or no-go with those numbers, and a
prototype shows one field frame and one battle frame, rendered from recorded
`VidFrame`s, on GBA hardware or an emulator.

## Risks and open questions

- **Instruction-level ports carry the SNES with them.** Until the WRAM layout
  and CPU-register compatibility are dropped, ports keep `A`, flags and stack
  bytes around each call. The API removes hardware mechanics, not 65816
  mechanics; readable game code needs both gone, and the second is a separate
  plan.
- **Beam budgets.** `$C0:5A46` and `$C0:CA76` stop at a scanline, so how many
  objects update and how many sprite frames get built per frame depends on CPU
  speed. A native backend needs a model: a fixed count, or a work-unit budget
  calibrated from SNES backend runs. Either changes behavior against the
  reference; which is acceptable is open.
- **Real-time battles and frame shifts.** Any backend that changes how long
  something takes (APU-direct uploads, no beam) moves input relative to game
  state, which changes fights. Validation past P1 compares end states and
  scenes, not frames.
- **Driver state in WRAM.** The driver keeps slots and song state at
  `$1E00-$1EFF`. Whether other code reads it, and so whether APU-direct must
  maintain it, isn't known.
- **Unknown line-timed uses.** The field IRQ handler `$C0:ECCC`, the line the
  battle IRQ fires on, what the battle does after `$CD:3B82`, and the HDMA
  channels behind the scroll tables and the field's quadrilateral are all in
  unported code. The primitive list may grow.
- **Widescreen exposes assumptions.** Field objects activate only inside a
  window of the screen bounds clamped to 0-255 (`$C0:A88D`), so objects in the
  margins wouldn't exist: widening that window changes when scripts run, and
  keeping it means pop-in at the edges. Background content in VRAM presumably
  covers only what's near the screen (`$C0:7F9A` computes tile map edges for
  uploads), while the whole map is in WRAM (`$C0:A521` unpacks it to
  `$7E:3000` and up), so margins may need to be drawn from the map rather than
  VRAM. Battle bounds clamp at the edges (`$C1:285A`); window positions are
  bytes.
- **Semantic comparison needs tooling.** Masking a WRAM range for one entry
  and comparing decoded state is new to `native.c`; readers of the released
  memory have to be found first, and the WRAM access log that needs isn't
  there yet.
- **Mid-frame VRAM and CGRAM changes.** The decoder must notice palette
  writes during the frame and uploads under forced blank (battle IRQ); spans
  carry palette changes, but a VRAM change inside the visible frame would need
  more.
- **GBA limits beyond the hardware table.** Color math with subtraction and
  sub screen addition has no direct GBA equivalent; per-line effects cost
  interrupt time; the 70% vertical resolution affects every UI layout.
