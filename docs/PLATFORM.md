# Platform abstraction

Game code today talks to the SNES hardware model directly: hand ports write
PPU, DMA and APU registers through `m_write8`, and generated code and the
interpreter do the same through the bus. This document designs the move to
platform services (video, audio, timing, input, saves) that game code calls
instead, with the SNES hardware model as one backend: the reference that every
other backend (a native renderer and mixer, widescreen, a GBA build) is checked
against.

P0 to P3 are implemented (`runtime/platform/`), each with the leftovers its row
and docs/TODO.md name; P4 and P5 are a design, and so is the timing service's
native backend ([Timing without a
beam](#timing-without-a-beam-the-native-backend)). Where a milestone had to
change the sketches below, the headers are authoritative and its "as built"
section lists what changed and why.

| Milestone | What | Status |
|---|---|---|
| P0 | Service headers, SNES backend, first hand ports converted | done |
| P1 | Audio behind the API | done, except `new-game`'s log-spectral distance in the audio comparison (see P1 as built) |
| P2 | Video behind the API, subsystem by subsystem | done: shared code, battle, field, menus and the scenes, the jet bike race included, and `--vid-check` runs from power-on rather than from the frame a subsystem takes over. The Epoch's two Mode 7 screens, the last slice of bank `$C2` that wrote its own registers, went in with Phase 3's menu conversion; what is left is the gate screen the event engine opens, which still writes its own (see P2 as built) |
| P3 | Native renderer, pixel-identical to `render.c` | done, fed by the register decoder: every frame of all 26 movies is identical (see P3 as built) |
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
| Real-time battles | the ATB fight | Input a frame apart changes the fight ([REIMPLEMENTATION.md](REIMPLEMENTATION.md) results) |
| Auto-joypad busy bit | `$C2:84EC`, `$C3:0A8E` | Input reads wait for `$4212` bit 0 |

This table was the survey before the ports; [Timing without a
beam](#timing-without-a-beam-the-native-backend) has the census of every use of
the timing registers in the ROM, and what each is for.

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
without the main CPU's handshake. The driver's C port (Phase 4a) plays through
a second, lower level of the service, the voices ("The voices, as built",
below).

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

The boot (`$FD:C000`, `core/console.c`) brought three more calls, each the
console's start where the game had a register write and no call:
`tm_reset_timers` (the dot and line timers back to 0, which nothing enables
after), `tm_fast_rom` (the cartridge read at its fast speed, a matter of the
SNES backend's timing alone) and, in input, `in_set_io_lines` (the controller
port's programmable lines, all high). A native backend makes none of them do
anything.

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
enum { IN_BUTTON_B = 0x8000, /* ... */ IN_BUTTON_R = 0x0010 };   /* each button by name */

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
`$2180-$2183` is memory access. A port whose logic holds the game's state
rather than the machine (Phase 3a) drives both through `game/cpu_io.h`, which
takes the state, as `game/vars.h` does; the scenes were the first
(docs/DISASSEMBLY.md, "The RAM map").

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
  elsewhere was found with the I/O log and ported in P1.

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
| `runtime/platform/timing.h` | `tm_enable_interrupts`, `tm_set_line`, `tm_ack_vblank`, `tm_ack_line_irq`, `tm_video_status`, `tm_reset_beam_latch`, `tm_odd_frame`, `tm_sync` |
| `runtime/platform/audio.h` | `au_command` (`AudioCommand` with a parameter count), `au_play_song`, `au_status`, `au_take_samples` |
| `runtime/platform/input.h` | `in_ready`, `in_read`, `in_read_low`, `in_read_high`, `in_host_set` |
| `runtime/platform/save.h` | `sv_read8`, `sv_write8`, `sv_peek8` (a guard's look, without the machine's time), `sv_host_bytes` |
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
`game/core/input.c` (`input_read_joypad`) and `game/scene/frame.c`. The
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
  fires on wasn't established then. (It is 211, which the battle's screen
  setup `$D1:F1B0` sets, as the field and the scenes do: [Timing without a
  beam](#timing-without-a-beam-the-native-backend).)

**I/O log.** `ct_dev --io-log out.txt` counts every access to `$2100-$217F`,
`$4016-$4017`, `$4200-$421F` and `$4300-$437F` by origin and register, with the
frame of the first one, and prints a summary. Hand ports are identified with
`native_running_entry()` (the running job, idle entry or generated block) and
the backend's call marker, the interpreter by the instruction's address, HDMA
by the line hook that precedes its writes, and DMA transfer bytes are counted
separately under the origin that started the transfer. It is meaningful with
`--native on`: in shadow mode hand ports run on clones and generated blocks are
replayed off the machine. The log is a table rather than a stream because the
DMA bytes alone run to tens of millions per movie. Each row also counts when
its accesses landed (vblank, forced blank on a picture line, or the display
on), with a per-line histogram of the picture-line ones, for the vblank check
(REIMPLEMENTATION.md, "Where video writes land"); those columns are appended after the
original nine, generated-block rows now carry the block's address as their
site, and HDMA writes made during a generated block's catch-up are counted as
HDMA rather than as the block's. Summed over the 22 movies
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
  `generated_bus_diff` still reports 0 mismatches.

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

#### P1 as built

| Path | What |
|---|---|
| `runtime/platform/audio.h` | `au_driver_write`, `au_driver_read`, `au_driver_run_direct`, `au_driver_call` (the protocol level, below `au_command`; the call-out takes the callee's don't-care set since the $C7 pilot, history/reimplementation-waves.md), and the waits for the SPC700's answer `au_driver_exchange`, `au_driver_wait_echo`, `au_driver_wait_moved`, `au_driver_wait_echo_or_give_up` (below, "The CPU half's waits, as built"); `AuBackend`, `au_select_backend` |
| `runtime/platform/snes/audio.h` | the SNES backend, which hands the protocol-level calls to APU-direct while one of its runs is in progress |
| `runtime/platform/direct/` | APU-direct: synchronous runs of the driver, the APU's lead, the sample buffer |
| `runtime/apu/apu.c` | `Apu.lead`, `apu_run_ahead` |
| `runtime/port/native.c` | `native_hand_entry`, for running registered code synchronously |
| `game/core/sound_driver.c` | the rest of the driver's CPU half: the upload at `$C7:0000`, `$C7:09DA`, `$C7:09EA`, command `$70`'s and `$71`'s paths, retries, data crossing a bank, and every wait loop, all through the service |
| `game/battle/sound.c`, `game/menu/sound.c`, `game/scene/script.c`, `game/scene/sound.c` | the other code that touches the ports or the command buffer around them |
| `dev/io_log.c` | idle entries through services; the port mirrors `$2144-$217F` as their own group |
| `dev/ct_dev.c`, `frontend/ct_play.c` | `--audio snes|direct` |
| `dev/ct_fuzz.c` | `--apu-loopback`; idle entries can be fuzzed |
| `tests/test_platform_lint.py` | `core/sound_driver.c`, `scene/sound.c` and `scene/race.c` converted; `au_driver_*` allowed only in those three |

**Inventory.** `ct_dev --native on --io-log` over the 25 movies (the six main
movies, the 16 save movies and three save movies of the endings, `endings-*`,
which aren't in the repository yet), accesses to
the APU ports `$2140-$2143`:

| Origin | Before P1 | After P1 |
|---|---|---|
| hand ports, through `au_*` | 592 (`au_status`, events `$ED`, `$EE`) | 4401154 |
| idle entries, through `au_*` | | 25455776 |
| hand ports, direct (the sound driver) | 2717574 | 0 |
| idle entries, direct (the driver's four loops, the scene's port 3 wait) | 22531650 | 0 |
| generated blocks (the boot upload, `$C7:09DA`, command `$70`'s path, data crossing a bank, and what ran after the ports stopped there) | 4861081 | 0 |
| interpreter (the scene program's own send at `$7E:377E`, the race at `$7E:4BD4`) | 27729 | 0 |

Accesses to the mirrors `$2144-$217F` (2981364 before, 3353300 after, all from
generated blocks) are counted separately and were left alone, with this
evidence: every one is at `$217F` from the 16-bit `STA $217F` and `LDA $217F`
at 14 sites in `$CD:3810-$CD:3B05`, `$D1:FA4C-$D1:FB5D` and `$FD:DF0E-$FD:DF87`,
whose high byte is the access they're for: the work RAM port `$2180` (the only
instructions in the listing that address `$2144-$217F` at all). The low byte
lands on port 3, as on the hardware; it isn't a message to the driver (the
driver reads port 3 only right after the CPU side has written it as part of a
message), and both backends let it through. Porting them belongs to the video
subsystems that own the WRAM port (P2).

**What was routed.** Everything that touches the ports, reached by a movie or
not, now goes through the service:

- The driver (`core/sound_driver.c`): the upload `$C7:0000` (stops at its wait
  loops, which are idle entries, and entries `$C7:005F`, `$C7:007D`,
  `$C7:00B0`, `$C7:00FB` continue after them; the delay loop at `$C7:0118`
  and the rest of the routine have no port access and stayed in the original
  until the $C7 pilot made the countdown an idle entry, `$C7:011B`, and the
  rest `$C7:011E`),
  the counter send `$C7:09DA` and resend `$C7:09EA`, command `$70`'s echo
  (`$C7:08FF`), command `$71`'s upload (`$C7:07AE`, inlined in the command),
  the resends after a block header, the slot moves or the `$2F00` header gets
  the wrong answer (inlined as loops around a call-out to `$C7:09EA`), and data
  crossing into the next bank (`$C7:054C`, `$C7:06CD`, `$C7:07DF`, inlined).
  Twelve wait loops became idle entries (`$C7:0050`, `0078`, `00AB`, `00DB`,
  `00F6`, `060C`, `08CD`, `08D5`, `08FA`, `0912`, `091A`, `09F2`, with
  `0635`, `0A71`, `09E0`, `0A8B` from before). Every entry but `$C7:0000` and
  `$C7:0004`, which set it themselves, is guarded on a data bank that reaches
  the registers (since the $C7 pilot, on the driver's data bank `$00` and
  direct page `$1E00`, `sound_driver_frame_is_set`, as its work RAM is named
  in bank 0); no movie call is refused.
- The battle: the pause `$CD:3E49` (the "battle poll": `$CD:3E44` tests
  `$CD02` every frame and, while paused, sends `$F5` through `au_command`, and
  `$CD:3E65` sends it again when the pause ends; the test itself stays in the
  original, so the frequent call isn't an entry) and the loops waiting for port
  1 at `$CD:042B`, `$CD:0D9C`, `$CD:0DB7` (`au_status`).
- The menu: the waits around commands `$11` and `$14` at `$C2:CBC1`, `$C2:CC28`
  and the idle entry `$C2:CBE0`; the scene script command's port 2 wait
  `$C3:0C2A` (idle).
- The scene program in RAM (guarded by hash): its own send of the command in
  `$1E00-$1E03` (`$7E:377E`, `$7E:3780`, the echo wait `$7E:379B`), used for
  the pause sound and the race's sounds, and the race's port 1 and 0 writes
  (`$7E:4BD2`), through `au_driver_write`; the port 3 wait `$7E:5AAA` now reads
  `au_status`.

`au_driver_write` and `au_driver_read` are the protocol level: the driver's
CPU half and the scene's send are the SNES backend's implementation of
`au_command`, not game logic a native backend would keep, so the lint allows
them in those two files only. `au_status` stays the call for game code
polling the driver. `au_boot` wasn't added: no port calls the upload; the
registry entry at `$C7:0000` is the upload.

**The CPU half's waits, as built** (REIMPLEMENTATION.md, "Phase 4a, the
shipping mode's fallbacks, chunk 7"). The driver's CPU half waits for the
SPC700 through four more protocol-level calls, which wait where the port
stands rather than handing the machine back at the original's loop:
`au_driver_wait_echo(pf, PF_LOOP(head, exit), value)` until port 0 shows a
value, `au_driver_wait_moved` until it shows another, and
`au_driver_wait_echo_or_give_up(pf, PF_LOOP(head, exit), given_up, value,
tries)`, true where the echo came within the original's count of polls (its
loop leaves at `given_up` otherwise); and `au_driver_exchange(pf,
PF_CALL_DC(site, routine, set), value)`, the end of a message: the value on
port 0 and the wait for its echo, which the original makes by calling
`$C7:09DA` (or `$C7:09EA`, which takes the value from its cell). The SNES
backend waits with `native_wait_loop`, `native_wait_loop_or_give_up` and
`native_wait_call` on a wake for port 0 (`native_wake_port0_shows`,
`_leaves`; `port/native.h`): in the shipping mode the machine steps a pass of
the original's poll at a time (42 master clocks, 66 for a poll that counts
down) and, after an exchange, the time of the routine's return (102,
`native_wait_time`), since the SPC700 reads a message's last two bytes after
it echoes and a next message written sooner corrupted every third byte of a
song's sequence. The port's own time does the rest, and it has to put the
next message's bytes where the original's go: the loads of them through the
original's long pointer, which the port doesn't make, are charged before each
byte (`ACCESSES_A_LOAD`). `ct_dev --apu-port-margins` measures it: at every
echo, how long after the SPC700's read of port 2 and of port 3 the CPU's next
byte there comes. Over the movie library in the shipping mode, of 1,157,232
messages the next byte reached port 3 8-15 SPC700 cycles after the read
in 1,148,655 and 2-3 cycles after (about 63 master clocks, the least) in
182, as the original's do (1,499,256 and 193 of 1,511,199, the least the
same); port 2's least is 5 (the original's 3). Without the charge, port 3's
came 2-3 cycles after in 784,592 of them; in shadow mode and the tools the original's loop or routine
runs, the loop with A (and X) as the original sets them before it. APU-direct
runs the SPC700 ahead a pass at a time instead (`au_direct_wait_port0`).
A backend without an SPC700 never sees them.

**What changed from the plan, and why.**

- **The audio backend is chosen at run time**, not bound at compile time: the
  SNES backend's calls check whether an APU-direct run is in progress. Both
  backends need the same machine and the same ported driver, and validation
  must keep using the SNES backend, so a switch is simpler than two builds.
- **APU-direct runs the ported driver**, rather than the registry entry at
  `$C7:0004` delivering commands through code of its own: the CPU half of the
  protocol (block slots, relocations, the tables sent after a song) keeps its
  state in `$1E00-$1FFF`, which the game may read, so the backend reuses the
  code that is checked against the original and only changes what a wait
  costs. The boot upload and the scene's own send start runs the same way.
- **Protocol-level calls** (`au_driver_*`) were added below `au_command`, for
  the driver's and the scene's port accesses; the plan named only the
  command, status and boot calls.
- **The mirrors at `$217F` stay** (see Inventory).
- **Code no movie reaches was ported too** (the retries, command `$71`, the
  menu and scene script waits), validated only by fuzzing.

**APU-direct.** `ct_dev --audio direct`, `ct_play --audio direct`, with
`--native on` and the ported routines (the default is `--audio snes`):

- The registry entries that start an exchange, `$C7:0000`, `$C7:0004` and
  `$7E:377E`, call `au_driver_run_direct` first. With APU-direct selected it
  runs the code from there synchronously until PC leaves the driver (or the
  scene routine): registered entries directly (the same ports as with the SNES
  backend), idle entries a pass at a time, and instructions without an entry
  on the interpreter, with interrupts held off. JSRs the ports make to wait
  (`au_driver_call`) run the callee the same way instead of calling out.
- No main-CPU time passes: the run's bus clocks are cleared, so the job
  charges nothing. The time the code takes passes on the APU instead, before
  each port access and after each step: an idle pass takes its loop's
  `idle_clocks`, a native entry its bus clocks times its `cost_milli`, an
  interpreted instruction its exact time. So every wait runs the SPC700 until
  it answers, and the SPC700 sees the protocol with the spacing it has on the
  SNES backend (without it, answers read too soon after an echo went wrong and
  the attract sequence's song never started).
- The APU runs ahead of the machine by that time (`Apu.lead`, `apu_run_ahead`),
  and `apu_advance` absorbs the lead from the machine's following advances
  before running the APU again, so it keeps the machine's pace on average.
  Samples produced while running ahead are moved to a buffer outside the
  machine (up to 2^20 frames) so the S-DSP's own 4096 frames don't overflow;
  `au_take_samples` returns them first.
- A run that has had the SPC700 run 10 seconds ahead gives up and leaves the
  code in its wait loop, which then continues on the machine's timeline.
- Status polls (`au_status`) and the port mirrors read and write the APU
  directly, at the machine's time or ahead of it.

Upload and command time over the movies: 1.5-8.4 s of SPC700 time per movie
ran ahead of the machine (the boot upload is about 0.34 s of it); no run gave up.

**Where APU-direct changes behavior.**

- Uploads and commands take no frames: the boot, a new song (up to about 0.55 s
  of SPC700 time) and each command's echo wait. Everything after them runs
  earlier relative to the movie's input, which changes real-time battles and
  can change routes in long movies.
- The driver's timeouts (`$0400` passes) count on the SPC700's time, not the
  CPU's.
- The APU runs ahead after an upload and then pauses while the machine
  catches up; polls in that window see its later state, and in `ct_play` the
  sound arrives in a burst the audio buffer has to absorb.
- A song starts at nearly the same SPC700 time as with the SNES backend (the
  upload's SPC700 time matches), but game code the upload delayed on the SNES
  backend (the title screen's own driver commands after its song, for example)
  runs up to a few frames earlier, while input-driven sounds stay at their
  frames: sounds shift against the music by tens of milliseconds.

**Results.**

- I/O log: no access to `$2140-$2143` outside `au_*` in any of the 25 movies
  (29856930 accesses, all through `au_*`), generated blocks on.
- Lint and unit tests pass; the build has no warnings.
- Shadow (SNES backend): all 25 movies exit 0 with no mismatch, 2357230313
  generated-block checks included; the new entries' counts are in
  [PORTING.md](PORTING.md)'s status table.
- Fuzzing: all 50 routines of the driver and the other port code (idle entries
  included) and events `$ED`/`$EE` pass 20000 states each with `--apu-loopback`
  (constraints in PORTING.md).
- End states with APU-direct, by final frame, against the SNES backend
  (`--native on`) and the original timing (`--native on --generated only`,
  identical to `--native off`). `gato-battle` is won (the robot's "You won"
  dialog on the stage). 18 movies end in the original's room and scene:
  `new-game`, `menu`, `field`, `fair`, `fair-companion`, `gato-battle`, the
  five events, the four menus movies, `battle-tyrano-lair-walls`,
  `battle-magus` (in the Magus fight) and `battle-golem-twins-gameover` (back
  at the title). The SNES backend itself misses the original's end in seven
  long battle movies, where hand ports' estimated time already changes the
  fights ([REIMPLEMENTATION.md](REIMPLEMENTATION.md)); there APU-direct ends in the SNES backend's
  scene in four (`late-lavos` and `endings-bucket-lavos` still fighting
  Lavos, `endings-beyond-time` in the same field, `late-omen-terramutant` in
  the same room, after the fight rather than in it) and elsewhere in three:
  `late-omen-battles` (another Black Omen room; the SNES backend is in a third
  one), `late-omen-zeal` (fighting the Mammon Machine, where the SNES backend
  has lost to it and is back at the file screen and the original is fighting
  Zeal) and `endings-telepod-lavos` (on the 1000 A.D. world map, where the SNES
  backend is in the dark field it also ends in for `endings-beyond-time` and
  the original is fighting Lavos).
- Audio: `tools/audio_compare.py` with APU-direct against the SNES backend
  (`--native on`, same build; 4 s segments):

  | Movie | Audible segments | Waveform correlation (median, >= 0.9) | Spectrum correlation (median) | Log-spectral distance (median) |
  |---|---|---|---|---|
  | `new-game.ctm`, 7200 frames | 24 | 0.916, 16 of 24 | 0.9990 | 0.67 dB |
  | `field.ctm`, 11400 frames | 42 | 0.941, 31 of 42 | 0.9997 | 0.39 dB |

  `field` is as good as the Snes9x comparison on both measures (0.9997,
  0.39 dB), and `new-game`'s spectrum correlation too (0.999), but its
  log-spectral distance isn't (0.52 dB there), so that part of the criterion
  isn't met. The medians move with small timing changes: the same comparison
  before the new entries' `cost_milli` were set from shadow mode gave 0.9994
  and 0.59 dB for `new-game`, 0.9996 and 0.41 dB for `field`. Pure music
  segments are close (0.14-0.36 dB in `new-game`: the same song at a fixed lag,
  with the S-DSP's envelope counter at another phase, as in the Snes9x
  comparison); the distance comes from the title screen and name entry, where
  cursor sounds sit up to 5 ms differently against the music and the title's
  own commands after its song run five frames earlier (see above): 0.85-4.6 dB
  in those segments. The SNES backend against the original timing gives
  0.33 dB for `new-game`. Closing the gap would mean delaying the game by the
  time the SNES backend's uploads take, which is what APU-direct removes.

#### The voices, as built

The C sound driver (`game/sound/`, [SOUND.md](SOUND.md) "The port as built")
reaches the S-DSP through a voice-level part of `platform/audio.h`, added
beside the command interface for it. It has the S-DSP's semantics and none of
its mechanics: eight voices, each a sample from the directory
(`au_voice_sample`) at a pitch in 4096ths of the sample's rate
(`au_voice_pitch`), shaped by an ADSR envelope (`AuEnvelope`) or a gain
(`AuGain`), at a volume on each side; key on and key off by voice set
(`AuVoiceSet`); the master volume; the noise's clock (`au_noise_clock`); the
whole output stopped (`au_output_stopped`: silent, every voice keyed off and
held); noise and pitch modulation by voice set; the
echo's voices, volume, feedback, eight filter taps, delay and writes; a hold
of every envelope (the driver's pause); a voice's current output (the
driver's meter mode reads it); the driver's own sample, defined and
rewritten a byte at a time (`au_sample_define`, `au_sample_write`); and the
game's samples in sound memory, written, moved and given their directory
entries (`au_sample_memory_write`, `au_sample_memory_move`,
`au_sample_place`). No call
names a register or implies a write order. The one escape is
`au_voices_snes_register`, for the command whose meaning is "write this
S-DSP register" (`$FD`), which the game never sends.

The S-DSP keeps the noise clock, the echo's writes and the output's two
switches in one register (FLG), which a single `au_mixer` call first set whole:
the driver's every-tick call for the noise clock also turned the echo's writes
on and the output on without saying so. Each is a call of its own now, and the
S-DSP implementation changes only its own bits of the register. What the
original does is still said: its flush writes FLG whole from its shadow every
tick, so the driver's flush sets all three -- the noise at its clock, the echo
writing, the output on -- undoing whatever changed them since (a `$FD` write),
and the stop for a reload sets them the other way with the clock at 0. The
register file after each tick is the same byte, which is what the lockstep
compares.

The handle, `AuVoices`, is opaque to game code. Host side
(`runtime/platform/voices.h`) it is a table of the calls' implementations
(`AuVoicesOps`) that each implementation's own struct starts with, and
`voices.c` makes each `au_voice*` call through it; a pair (`au_voices_pair`)
plays every call through two implementations at once. Both implementations
keep the original driver's layout of sound memory (`voices.h`): the directory
at `$1E00`, the echo buffer ending at page `$F1`, the driver's own sample at
`$1DC2`. The game's samples are sound memory too, placed by the driver where
the original's CPU half uploaded them (`au_sample_memory_write`,
`au_sample_memory_move` and `au_sample_place`, a directory entry), since what
the echo's buffer runs over depends on where they lie.

The first implementation, `runtime/platform/voices_dsp.{h,c}`, turns each call
into the register writes that make it, through four host callbacks (read a
register, write one, read and write a byte of sound memory). Two hosts give it
callbacks: `ct_dev --sound-lockstep` points them at a copy of the register
file and of sound memory, which are compared with the original driver's; and
`frontend/sound_host.c` (`ct_play`'s default) at the emulated S-DSP, which
then makes the sound. The second is the native mixer (below). The lint test
keeps the S-DSP out of `game/`: no `apu/` or `voices_dsp.h` include and no
`DSP_` name there (`test_no_sound_hardware_in_game`).

#### The native mixer, as built

`runtime/platform/native/mixer.{h,c}` makes the voices into sound itself, with
no S-DSP under it: eight voices playing BRR samples from its own 64 KiB of
sound memory through the four-point Gaussian interpolation, shaped by ADSR
envelopes or gains on the shared rate clock, with noise, pitch modulation of a
voice by the one below, and the echo (a delay line in the same memory, eight
filter taps, feedback) mixed in at its volume under the master volume, 32000
stereo frames a second. Each call sets a named field (a voice's sample,
pitch, volumes, envelope, gain; the echo's voices, volume, feedback, taps,
delay, writes); there is no register file, and nothing is polled from one.
What the S-DSP keeps in one register is the mixer's several fields (the
output silenced, the voices reset, the echo writing, the noise clock), and
the escape (`au_voices_snes_register`, the driver's `$FD`) is taken apart
into them; the registers the S-DSP reports through mean nothing to it, and nor
does the directory's page, which the game never moves.

It keeps the S-DSP's arithmetic to the bit (the 15 bits BRR decodes to, the
Gaussian sum that wraps at 16 bits before its last product, the FIR sum that
wraps before its last tap, every clip in the mix) and the timing of what the
driver asks for: key ons and offs taken every other frame, a note's five
silent frames of start, a block decoded four samples at a time when the voice
reaches them (so a byte that changes under a playing voice -- the echo
writing over a sample, the driver rewriting its own -- is heard from the next
four on, as on the S-DSP), and the echo taking a new place a frame later and
a new size when its offset wraps. Each is cheap, some are audible, and the
game relies on the echo's (SOUND.md, "What playing it found"); together they
make the mixer checkable frame for frame against the S-DSP, which is how it
is proved now and how a change to it is checked. Its sound memory is laid out
as the original driver lays out the SPC700's, sample directory included,
because where a sample lies decides what the echo reads and overwrites
(docs/history/platform-mixer.md has how the first version found that). Its
voices' types come from `platform/voice_types.h`, the voices' part of the
audio service alone, so the mixer includes nothing of the rest of the service
or of the SNES backend under it.

The host (`frontend/sound_host.c`) takes a `SoundMixer`. `SOUND_MIXER_DSP`
keeps the S-DSP, as before (`ct_dev` and `ct_sound_fuzz` default to it, as the
reference; `ct_play` and `ct_drive` default to the mixer). `SOUND_MIXER_NATIVE` hands the driver the mixer and
sets two `Apu` hooks: `host_mix` takes the SPC700 cycles where the S-DSP would
run on them, and the host steps the mixer a frame every 32 cycles, after the
driver's work in them as the S-DSP was stepped; `host_take` hands
`apu_take_samples` the mixer's frames. The S-DSP isn't run. The host's echo
check (`sound_host_samples_into_echo`) and its echo wait ask the mixer where
its echo writes and what its delay is. `SOUND_MIXER_CHECK` hands the driver a
pair, the S-DSP's registers and then the mixer, runs the S-DSP and the mixer
frame by frame, plays the S-DSP's and hands every pair of frames to a
comparison (`sound_host_compare_mixer`). The comparison is a developer tool's,
`dev/mixer_check.{h,c}` (frames compared and sounding, how many differ, the
largest difference, and the difference's power against the signal's, overall
and in octave bands from a Haar split of each 1024 frames), because it works
in floating point and the host `ct_play` ships with stays integer-only. A
saved state of the mixer holds its fields under `sound.mixer.` (its memory
among them) and is a host program of its own
(`SNES_STATE_HOST_C_SOUND_DRIVER_MIXER`), so it loads only with the mixer, as
one played through the S-DSP loads only with that; one played with the check
is a third (`..._MIXER_CHECK`), so the check never compares the mixer with an
S-DSP a state played through the mixer alone left behind (`sound_test`).
`ct_play --mixer dsp|native` picks it (PLAYING.md); `ct_dev`, `ct_drive` and
`ct_sound_fuzz` take `--mixer dsp|native|check`, and with `check` a run in
which a frame differs exits 1. `ct_dev` and `ct_play` refuse `--mixer` on a
run that doesn't play the C driver.

Measured, `ct_dev --native on --sound-driver c --mixer check` over every
movie (the C driver drifts from the movie, so the game is the movie's only
roughly, but the music is the game's): every frame the mixer made is the
S-DSP's, 406 million frames (3.5 hours, 2.5 of them sounding).

| Movies | Frames compared (s) | Sounding (s) | Differing |
|---|---|---|---|
| power-on: title-start, new-game, menu, field, fair, fair-companion, gato-battle | 1352 | 1123 | 0 |
| battle-golem-twins-gameover, battle-magus, battle-tyrano-lair-walls | 649 | 579 | 0 |
| endings-beyond-time, endings-bucket-lavos, endings-telepod-lavos | 7220 | 4490 | 0 |
| events-blackbird, -end-of-time-gate, -jetbike-race, -keepers-dome, -prison-escape | 621 | 521 | 0 |
| late-lavos, late-omen-battles, -terramutant, -zeal | 2219 | 1689 | 0 |
| menus-end-of-time, -epoch, -field-menu, -shop-inn | 631 | 537 | 0 |

`--mixer native` writes the same WAV as `--mixer dsp` byte for byte (new-game,
gato-battle), and runs as fast (late-lavos 8.7-9.2 s against 9.2-9.3 s: the
sound was never the cost). A state saved with the mixer at 3000 frames from
power-on, loaded and run 1000 more, is the same state as 4000 straight.
`ct_sound_fuzz --sound-driver c --mixer check` (100 runs of 400 commands with
the game's songs, and 100 with made-up ones) is identical too.

Two checks hold it there. `dsp_test` (no ROM; CI) plays random calls of every
kind the driver makes, the escape among them, over random BRR samples some of
which lie under the echo's longest buffers, through a pair of the S-DSP's
registers into the S-DSP and the mixer, six seeds of about 230,000 frames:
every frame, every voice's output level and, at the end, every byte of sound
memory must be the same; and a hand-built block of the most negative samples,
played at one sample a frame, makes the Gaussian sum wrap, which none of the
random samples reach (`test_gaussian_wrap`: the voice is heard positive from a
sample all negative, as on the S-DSP). And the gate's hosted `ct_sound_fuzz`
runs play with `--mixer check` (PLAN.md).
`tests/test_native_render.py` holds the mixer to the renderer's rule: it
includes its service's headers and the C library, and names nothing of the
S-DSP's or the APU's.

### P2: video behind the API, subsystem by subsystem

Order: shared code (display blank, palette upload, animated tiles), battle
(most of its vblank path is already ported), field (NMI copies, the deferred
uploads flagged in `$1D78-$1D7B`, the sprite upload), scenes (Mode 7,
offset-per-tile), menus. Each subsystem's unported register writers are ported
from their generated drafts ([REIMPLEMENTATION.md](REIMPLEMENTATION.md), M5).

**Done when**, per subsystem: over the movies that cover it, the I/O log shows
no PPU, DMA or HDMA register write outside `vid_*`; shadow mode has 0
mismatches; and the observer's command-built `VidFrame` equals the decoder's
`VidFrame` on every frame (same spans, sprites, palette and VRAM).

#### P2 as built: shared code, battle, field, menus and the scenes

| Subsystem | State |
|---|---|
| shared | done: display blank (P0), the palette upload `$C0:712E`, the animated tiles (P0), and the register block the boot writes at power-on (`$FD:C036`), which the observer needs before it can be compared from frame 0 |
| battle | done in `gato-battle` and `battle-magus`, where no PPU, DMA or HDMA register write is left outside `vid_*`, and the two frames agree on 2198 of the fight's 2200 frames. `$D1:F4E6`, which `battle-golem-twins-gameover` reaches on its way back to the title, is in as well |
| field | done: over `field.ctm` no routine in banks `$C0` or `$FD` writes a register any more, and with the menus behind the API as well the two frames agree on all 5000 frames sampled from frame 4000 |
| menus | done: both modes of bank `$C2` -- the menus proper at `$C2:800E` and the sprite objects at `$C2:0000`, whose entry `$C2:000F` sets the brightness the mode then runs at -- and the window commands. No PPU, DMA or HDMA register write is left outside `vid_*` from bank `$C2` or from `$FF:FC04` in `field.ctm`, `menus-field-menu` or `gato-battle`. The Epoch's map view and time dial, twenty-five routines between `$C2:631F` and `$C2:7758` that only `menus-epoch` reaches, and the End of Time's gate screen `$C2:CE9D`, went in with Phase 3's conversion of the menus (`menu/epoch.c`, [history/reimplementation-waves.md](history/reimplementation-waves.md), "Phase 3, wave 2"); both movies now agree on every frame |
| scenes | done: the attract sequence, the title screen and the jet bike race. Twenty entries cover the program at `$7E:3000` and eighteen more the race's own code in it, with `$C3:08D0` an entry of its own, so nothing in the scenes writes a PPU, DMA or HDMA register outside the services in any movie. `$7E:59D5`, the fill the graphics loader starts without programming a source, goes through the SNES-only escape, which resolves what the call site leaves unprogrammed from the channel |

**The frame, its two builders and their comparison.**

| Path | What |
|---|---|
| `runtime/platform/video.h` | `VidBg`, `VidMode7`, `VidSprite`, `VidObjConfig`, `VidVideoConfig`, `VidVramPort`, `VidLineState`, `VidSpan`, `VidFrame`, and the calls the ports below needed |
| `runtime/platform/snes/decode.c` | the decoder: `VidLineState` from the `Ppu` registers at every line, sprites from OAM at line 1, spans merged while nothing changes |
| `runtime/platform/observe.c` | the observer: every service call folded into a `VidFrame` semantically, uploads applied to its own video memory, per-line effects expanded from the game's tables |
| `dev/vid_check.c` | `ct_dev --vid-check [out.txt]`: compares the two per frame and reports the differences by kind |

The decoder reads the register layer, so it works whoever wrote the registers
and P3's renderer doesn't wait for P2. The observer never sees a register: each
call hands it the structure the call took, uploads are copied out of game memory
through a read callback, and a per-line effect is expanded from the table the
game keeps, the way a native backend would have to expand it. Its HDMA
bookkeeping (line counts with the repeat bit, indirect addresses, reloads) is
the same state machine the hardware runs, because the table is the game's data
format. The one call it can't take semantically, the SNES-only transfer escape,
it follows byte by byte where the destination is video memory: without that, the
battle's object graphics never reached the observer's VRAM and no frame of the
fight could agree.

**Calls added**, each with its first user: `vid_set_bg_map`, `vid_set_bg_chars`,
`vid_set_scroll`, `vid_set_mosaic`, `vid_set_mode7`, `vid_set_obj_config`,
`vid_set_screens`, `vid_set_video_config`, `vid_set_vram_address`,
`vid_write_vram`, `vid_write_vram_byte`, and four more `VidLineTarget`s for the
battle's per-line slots (`VID_LINE_BG_MAPS`, `VID_LINE_SCREENS`,
`VID_LINE_COLOR_MATH`, `VID_LINE_MODE`) and three for the field's
(`VID_LINE_MATH_LAYERS`, `VID_LINE_WINDOW2_LEFT`, `VID_LINE_WINDOW2_RIGHT`).
`vid_begin_vram_uploads` now takes the port setting and the source bank instead
of assuming both, and `VidSnesXfer` carries a port for `VID_STEP_VRAM_PORT`,
where an upload sets the VRAM port in the middle of its own register sequence
(the field's deferred uploads do).

The menus added three more: `vid_set_line_table`, which moves a slot's table
inside the bank it was programmed with while its target and mode stay as they
are (the menus swap between two tables by frame parity, and rebuild one in
place); `vid_set_palette_address` and `vid_set_oam_address`, which the mode
setups write on their own where every other caller has an upload write them.
Eight more `VidLineTarget`s cover what a window command can ask a slot to drive
one register at a time (`VID_LINE_BG_MODE`, `VID_LINE_MAIN_SCREEN`,
`VID_LINE_MATH_LAYERS_ONLY`, and one for each scroll value the commands use:
`VID_LINE_SCROLL_BG1_H`, `_BG1_V`, `_BG2_V`, `_BG3_H`, `_BG3_V`). The transfer
mode and first register each target drives moved out of the backend into
`vid_line_snes_encoding` in `video.h`, with `vid_line_effect_decode` reading a
target back out of the pair, because the menus' window commands hold the pair as
data rather than naming the target. `vid_mode7_13bit` names what the hardware
keeps of a Mode 7 center or scroll word: a call passes the value as written,
since the registers hold the whole byte, and the observer narrows it to the 13
bits a renderer reads.

The race and the boot added the last few. `VID_M7_MATRIX_B` sets the second
matrix value alone, both bytes, which the scene program's memory clear writes
where `VID_M7_MATRIX_B_HIGH` (the multiplier's 8-bit operand) writes one.
`VID_LINE_MODE7_AB` and `VID_LINE_MODE7_CD` are the race's per-line matrix, a
pair of values to a line; `VID_LINE_WINDOW1_LEFT` is window 1's left edge alone,
which the same table drives; `VID_LINE_BG2_MAP` is BG2's tilemap address
alone, which a scene of the attract sequence drives down the screen (REIMPLEMENTATION.md,
"The last refusals in real play: the attract demo's states"). `VID_LINE_MODE7_CENTER`
is Mode 7's centre down the screen, which the time warp sets a half of the
picture at a time (game/warp/), and `VID_STEP_INDIRECT_BANK_IS_SOURCE` the
indirect bank's register written with a transfer's own bank, as the warp's
vblank uploads write it. `VidLineEffect.snes_spill` with
`VID_STEP_SPILL_SIZE_LO` and `VID_STEP_SPILL_CURSOR_LO` carries the byte a
five-byte descriptor read as a word leaves in the channel's unused size and
table-cursor registers: it is video state nowhere but in what shadow mode
compares, so only the steps that name those registers write it.
`VidSnesTransfer.continue_channel` says a transfer carries on with whatever the
channel already holds, for the graphics loader at `$7E:59D5` (below).

The attract sequence's scenes added three more. `vid_send_oam_fill` sets a run
of the sprite table to one value -- every entry put on one line, or every high
bit cleared -- which the credit pages do before setting their own sprites (the
original fills from a byte of its own holding the value, which the SNES
backend takes from the site's encoding, its `source`); `vid_set_sprite` sets
one entry by its index, for a screen of letters set as sprites once rather
than built in memory each frame (the SNES backend writes the table's address
only where its data port isn't already at the entry, as the original streams
a run of them, and makes the entry's last byte, `vid_oam_attributes`, in
`snes/video.h`, since the bits are the SNES's); and
`VID_STEP_VRAM_READ_FIRST` is the read of VRAM's read port the palace's setup
makes before it reads its map back, which steps the address on a word
(REIMPLEMENTATION.md, "Phase 4a, the native frame: the attract sequence's
pages of credits and the palace rising").

**Ported** (all in [PORTING.md](PORTING.md)'s status table):
`palette_upload` `$C0:712E`; the battle's `$CD:09CE`, `$CD:09F1`, `$CC:E000`,
`$D1:E8D9` and the second vblank handler `$D1:F4C0`; the field's `$FD:C1EE`,
`$C0:ECA3`, `$C0:B1B2`, the twelve deferred uploads `$C0:8445`-`$C0:87BF`,
`$FD:C0D7`, `$C0:A509`, `$C0:2DC8`, `$C0:2DF1`, `$C0:F05E` with its four tile
writers, and the line interrupt `$C0:ECCC`/`$C0:ECEB`/`$C0:ECF2`; and the menus'
`$C2:800E`, `$C2:82C1`, `$C2:832F`, `$C2:842A`, `$C2:85D6`, `$C2:8663`,
`$C2:86A9`, `$C2:86DD`, `$C2:0043`, `$C2:015E`, `$C2:0405`, `$C2:0ADC`,
`$C2:26A8`, `$C2:2D47`, `$C2:2D70`, `$C2:940D`, `$C2:94D5`, `$C2:9512`,
`$C2:9542`, `$C2:F337`, `$C2:000F` and `$FF:FC04`, with `menu_object_wave_hdma`
`$C2:7759` (a P0 port that still set up its HDMA channel itself) converted with
them; the boot's `$FD:C036`; and the scenes' `$C3:08D0` and, in the program at
`$7E:3000`, `$7E:302A`, `$7E:3092`, `$7E:30F9`, `$7E:380E`, `$7E:3843`,
`$7E:388B`, `$7E:38E7`, the five VRAM addresses the graphics loader sets
(`$7E:597B`, `$59A0`, `$59AA`, `$59BC`, `$59CC`) with the fill that follows them
(`$7E:59D5`), `$7E:5A59`, `$7E:5AB6`, `$7E:5C57`, `$7E:5C98`, `$7E:5DA7`,
`$7E:5E54` and `$7E:6212`. The race is eighteen more, in `game/scene/race.c`:
`$7E:36CC`, `$7E:3C09`, `$7E:3CFF`, `$7E:3D8C`, the eight VRAM addresses and
clears between `$7E:3DC8` and `$7E:3EA0`, `$7E:43F2`, `$7E:4643`/`$7E:464D`,
`$7E:5221`, `$7E:5261` and `$7E:8752`.

**Two Mode 7 fields for the multiplier.** `$7E:3843` hides every sprite by
setting the matrix to 1 times -32, whose product the PPU offers at `$2134`, and
copying that byte over the sprite table with a transfer in the other direction.
It writes `$211B` twice and `$211C` once, which `VID_M7_MATRIX` and
`VID_M7_MATRIX_AB` can't express, so `VID_M7_MATRIX_A` (the 16-bit operand) and
`VID_M7_MATRIX_B_HIGH` (the 8-bit one) were added; the observer follows the
latch the Mode 7 registers share, since a single write takes the other byte from
it.

**What the block copies turned out to mean.** The menus keep their display state
as shadows and copy blocks of it out in vblank, which is what P2 had to take
apart. `$C2:85D6` sets the work RAM port to `$7E:0D04` and reads twelve bytes
back through it into `$210D-$2112`: the three layers' scroll, two bytes to each
register, so the port reads a pair and makes one `vid_set_scroll` call per
value. The `MVN` at `$C2:8601` moves sixteen bytes from `$7E:9680` straight into
`$2123-$2132`, which is the window masks, both edges of both windows, the two
logic bytes, the main and sub screens, the two window masks per screen, the
color math control and layers and the fixed color -- in that register order, so
the port names each byte and issues the seven calls that cover them, then leaves
A, X, Y and the data bank as the block move does. The mode at `$C2:0000` keeps
the same state on direct page 0 (`$1E-$49`) and copies it out byte by byte in
`$C2:01CE-$02AF`, including Mode 7's matrix and center, and writes the fixed
color three times, one channel each.

**Deviations, and why.**

- **Write order within a routine.** `field_setup_line_effects` and
  `battle_setup_display` program eight and seven channels; the original writes
  all the controls, then all the destinations, and so on, while the calls write
  each slot's registers together. Neither routine calls anything in between, so
  the only comparison is at its return, where every register holds the same
  byte. The same goes for the reads a port does before a call that writes
  several registers at once.
- **One write still reaches the registers through the data bank.** The second
  battle vblank handler writes the per-line enable register with absolute
  addressing, so with another data bank it lands in memory instead. The port
  stops there when the bank isn't one the registers are mapped in, as
  `field_nmi` does.
- **One more SNES-only escape.** `$C0:2DF1` clears memory by zeroing the Mode 7
  matrix, whose multiplier then offers a product of 0, and copying that *out of*
  a PPU register with a transfer in the other direction. The matrix values go
  through `vid_set_mode7`; the copy uses `vid_snes_transfer`, which the lint now
  allows in `field/display.c` as well.
- **Direct pages on the register block.** Routines that put `$2100` or `$4300`
  in D keep doing it, since D is CPU state; `M_DP_PPU` and `M_DP_DMA` in
  `port/machine.h` name the two values so no register constant appears in
  `game/`. `$C2:85D6` walks the scroll registers with X in the same way, and
  names the two ends of the walk off `M_DP_PPU`. `M_DP_CPU` (`$4200`) joined
  them in Phase 3 for the scenes' divisor loop `$C3:0E29`, which reaches the
  divider through the direct page.
- **The line interrupt's line** (Phase 3, the scene boot `$C3:0000`):
  `tm_set_line` sets the line `TM_INT_LINE` fires at ($4209/$420A), a narrower
  call than the `tm_set_line_handler` sketched under Timing above, since the
  boot installs the handler's stub itself.
- **Script-driven register writes** (Phase 3, the sequence scripts' command
  `$AB` at `$C3:0C5F`): a byte from the script into `$21xx`, xx from the
  script. The scripts write nine registers (Mode 7's flips, window 1's edges
  and the OBJ/color window masks, the main and sub screens, the main-screen
  window mask, the color math layers, the video config), and the port turns
  each into its service call; any other register is left to the original,
  which never meets one.
- **Two more SNES-only escapes, both memory.** `$C2:9512` and `$C2:9542` fill
  and copy work RAM over a DMA channel into the work RAM port. The port is
  memory access rather than video ([the survey](#video) above), but the channel
  is the video service's, so the transfers go through `vid_snes_transfer`, and
  the observer now ignores a transfer whose destination is `$2180-$2183`
  instead of counting it as not modelled.
- **A register a script names by its address.** The menus' object scripts'
  byte commands name their cell by its bank-$00 address, and an ending's sets
  `$2101` (the sprites' sizes and their tiles' place) that way. That one write
  goes through `vid_snes_write_register`, an escape that takes only the
  registers `vid_snes_takes_register` says (the one, so far) and makes the
  write as `vid_set_obj_config`, so the observer and the native renderer see
  the setting; the guard leaves every other register to the original, which
  the game never meets (history/reimplementation-chunks.md, "The refusals in real play, retired").
- **A palette send whose writes land in memory.** The battle's vblank handler
  in bank D1 sends the palette without setting the data bank, and on code
  with work RAM's the original's register writes land in work RAM.
  `vid_snes_send_palette_into_memory` makes the site's own program
  (`encodings.c`, `$CD:09CE`) with the backend's register writes going to the
  data bank's addresses, and sends nothing; a host without the SNES sends the
  palette. The lint can't see such writes in a port: in banks `$7E`-`$7F` the
  registers' mirrors are work RAM the game names (the race's `$7E:4309`, the
  map's `$7E:4319`), so the escape is where they live.
- **An address register written in the other order.** `$7E:380E` writes OAM's
  address high byte before its low one; `vid_set_oam_address` writes the low one
  first, which leaves the same address, the same rotation and the same reload.
  It is the only caller that turns the priority rotation on.
- **A third SNES-only escape in the scenes.** `$7E:5AEB` repeats two bytes into
  the VRAM data port's low half alone, which `vid_fill_vram` would model as
  words, so it goes through `vid_snes_transfer` like `$7E:3843` and `$7E:38E7`
  (whose mode and destination are game data). It also carries on from the VRAM
  address the loop before it left, which the port passes as the address it
  counted rather than one it programs.
- **The scenes' NMI now calls out to its own frame work.** `scene_nmi` inlined
  `$C3:08D0`; with that address an entry of its own, the inlined copy and the
  entry both waited for the same call and neither could be compared, so the
  handler calls out as the original does.
- **The menus' NMI is entered after its stack switch.** `$C2:840E` saves the
  registers and moves the stack from wherever it was interrupted up to `$1DFD`.
  Shadow validation reads a stack pointer above the entry's as a routine that
  has returned, so the port starts at `$C2:842A`, after the switch, and stops at
  `$C2:8466`, before it is undone. The instructions it leaves out touch no
  hardware.
- **The vblank acknowledge's carry is a don't-care.** `$C2:842A` acknowledges
  the interrupt with `CMP $4210`, whose bits 4-6 are open bus: the interpreter's
  instruction fetch leaves the register's own address there and native code
  doesn't, so on about one call in a thousand the two differ in a bit of the
  value and so in the carry. Nothing reads it before it is set again
  (`$C2:8473` saves and restores P around its own work), so it is the entry's
  `dont_care`.
- **Two routines are entered in the middle.** `$C2:2D47` (the palette the
  graphics loader decompressed) and `$C2:94D5` (the memory the video setup
  clears) are reached by falling through, and `$C2:0043` and `$C2:940D` stop
  where their routines move on from the PPU to the CPU registers, which have no
  service. The generator ends a generated block at every registered address,
  so an entry inside a routine still runs.
- **Lines are compared, not spans.** `vid_frame_diff` compares the state each
  line ends up with rather than the span boundaries: an upload that rewrites a
  palette with the same colors starts a span on one side only and changes
  nothing a renderer draws.
- **`--vid-check-from N`.** The observer starts from what the calls have told
  it, so it can only be compared once every writer of every register has been
  behind the API since power-on. With the scenes and the boot in that now holds,
  and `--vid-check` runs from frame 0 over the whole library;
  `--vid-check-from` stays for checking a subsystem from the frame it takes
  over.
- **Power-on video memory is seeded, the registers are not.** From frame 0 the
  calls build every register themselves, but what VRAM, CGRAM and OAM hold before
  the game writes them is the machine's: `snes_power_on` preloads CGRAM with the
  gradient the trace oracle uses, and no call produces it. `vid_obs_seed_memory`
  hands the observer that memory once, at the top of the first frame it compares.
  Without it every frame before the first palette upload differs. Power-on starts
  inside frame 0, so the hook first fires at the top of frame 1 and frame 0 is
  not compared (which is why the counts below are one short of each movie's
  frames); the boot writes no VRAM, CGRAM or OAM data in it, so what the seed
  takes is still what the machine powered on with.
- **The observer's frame starts where HDMA loads.** The hardware loads each
  enabled channel at the top of a frame, before any of that frame's code runs;
  the `on_line` hook the observer was driven from fires 1104 clocks later, at
  line 0's hblank. The scenes' vblank work sometimes runs into that gap and turns
  the per-line effects back on inside it, so the observer started effects the
  hardware had not loaded -- 40 of `events-jetbike-race`'s frames, where it drew
  a Mode 7 frame the hardware never showed. `Snes.on_frame_top` is a hook at the
  load itself, and `vid_check_frame_top` is what the observer is started from;
  with it the race agrees on every frame.
- **A register write that isn't one.** `$D1:F4E6` writes the per-line enable
  register through the data bank, and on `battle-golem-twins-gameover`'s way back
  to the title the bank isn't one the registers are mapped in, so the store lands
  in memory. The port used to hand the rest of the handler back there, which left
  its brightness and scroll writes outside the services; it now makes the memory
  write itself (`M_REG_LINE_EFFECTS` in `port/machine.h`, so no register constant
  appears in `game/`) and carries on.
- **A transfer that carries on from the channel.** `$7E:59D5` starts a fill
  without programming a source, so it repeats from wherever the transfer before
  it left channel 7 -- an address the routine never computes, which no call could
  take and no backend without the channel could resolve. It goes through the
  SNES-only escape with `continue_channel`, which reads back whatever the call
  site's steps leave unwritten (control, destination or source) and hands the
  observer the transfer that will actually run. A new call saying "continue from
  the channel's current state" would have been the same dependency with a
  narrower name, and would have put a DMA channel's retained state into the
  service every other backend has to implement; the escape is where P2 already
  keeps what only the SNES can mean.
- **The race's per-line effects come from a descriptor table.** `$7E:36CC` walks
  five-byte records -- mode and destination register, table address, indirect
  bank -- into channels 0 upwards until a record starting `$FF`. Whether the port
  can express the table is a property of the table, not of the code, so it is
  part of the guard: that walks the whole table (at most eight records, one per
  channel) and refuses one whose mode and destination no `VidLineTarget` names,
  or whose control byte has a bit outside the transfer mode and the indirect flag
  (which is all a `VidLineEffect` carries), and the interpreter programs it
  instead. Stopping halfway through the loop
  instead, which is what the port did first, is not something either validation
  can check -- shadow mode and the fuzzer both look for the original at the
  address the port stopped at, and a loop passes the same address every time, so
  they compared the wrong iteration (both now compare every pass and take the
  one where the machines are the same: REIMPLEMENTATION.md, "Between the waves: the
  validator"). The race's own table needed three targets
  that did not exist (above).
- **Two of the race's uploads write the data port's low half alone.** The column
  at `$7E:4643`/`$7E:464D` and the Mode 7 plane at `$7E:8752` send one byte to
  each VRAM word, which `vid_upload_vram` would model as whole words, so they go
  through the SNES-only escape like `$7E:5AEB` before them. The plane's also
  carries on from the VRAM address the block before it left.

**Results.** Shadow mode: all 22 movies up to `late-omen-zeal` exit 0 with no
mismatch (the four ending runs, 100000 frames and more each, aren't run in
shadow mode); the new entries' call counts are in [PORTING.md](PORTING.md)'s
status table. `--native on --generated only` still traces identically to
`--native off` over `new-game` and `events-jetbike-race`, so the block
boundaries the new entries move change nothing. The lint and the unit tests pass
and the build is warning-free.

I/O log, `--native on`, register accesses (excluding transfer bytes), over
`gato-battle`:

| Origin | PPU before P2 | after the field | after the menus | DMA before P2 | after the field | after the menus |
|---|---|---|---|---|---|---|
| hand ports, through services | 219916 | 301558 | 473457 | 218997 | 1093251 | 1133447 |
| generated blocks | 645950 | 562990 | 18932 | 957689 | 82658 | 26762 |

With the menus in, nothing in bank `$C2` or at `$FF:FC04` writes a PPU, DMA or
HDMA register any more, and the scene program and `$C3:08D0` were all that was
left (18932 PPU and 26762 DMA accesses in `gato-battle` and `field.ctm`, 18981
and 26762 in `menus-field-menu`: nearly all of it the attract sequence, which
every movie runs first). With the scenes in as well, the whole of `field.ctm`
and of `gato-battle` had exactly one register write outside the services: the
seven registers `$7E:59D5` programs, once, for the fill it starts without a
source, which is behind the API now too. Everything else in those logs is
HDMA.
`battle-golem-twins-gameover` had that one and `$D1:F4E6`, on the path back to
the title, and `events-jetbike-race` the race's own display code as well, 19
blocks between `$7E:36DA` and `$7E:8752`.

With all of those in, `events-jetbike-race` has no PPU, DMA or HDMA register
*write* left outside the services at all -- the log's remaining PPU rows are
HDMA and the two beam-counter reads the event scheduler and the sprite frame
queue make (`$C0:5A46`, `$C0:CA76`), which are reads of a latch rather than
video state. That is the strictest form of P2's claim, and it holds over a movie
that runs the attract sequence, the field, the menus, a battle and the Mode 7
race.

`--vid-check`, seeded at the frame the subsystem takes over. These are what P2
reported before it could run from power-on; the table below it is what matters
now:

| Run | Frames equal | Differences |
|---|---|---|
| `field.ctm` from frame 4000 | 5000 of 5000 | none (1 of 5000 before the menus went in) |
| `menus-field-menu` from frame 4000 | 6598 of 6600 | `math.layers` on one frame, `fixed_color` on one |
| `gato-battle` from frame 19400 (the fight) | 2198 of 2200 | `windows.w1_left` on 2 frames |
| `new-game` from frame 1700 (the title screen and name entry) | 1296 of 1300 | `fixed_color` on 4 frames |

The field agrees on every frame now, which is what the menus were holding up,
and the title screen can be compared at all now that the scene program's writes
go through the service. The battle's frames are the shared channel P2 already
had: the transfer channel the battle uploads with is also the slot its window
effect runs on, so a transfer started inside the visible frame leaves the effect
reading from a cleared indirect address. The hardware's channels are one set of
registers for both purposes; the observer keeps a transfer and a per-line effect
apart, as a native backend would.

Seeded at frame 3000 instead, `menus-field-menu` is 7561 of 7600: the frames
between 3000 and 4000, where the save movie leaves the load screen, take a while
to settle after the observer is seeded from the PPU. Which frames those are
moves with `on` mode's timing, so tuning the new entries' `cost_milli` changed
them.

**`--vid-check` from power-on**, which is the number that says whether the
observer can be built from the calls alone. `new-game`'s first 3000 frames went
from 1267 to 2996 as the scene program's last writers went in, and to all but
four with the boot's register block and `$C2:000F`; the whole library now runs
from frame 0:

| Movie | Frames equal | What differs |
|---|---|---|
| `title-start.ctm` | 1799 of 1799 | -- |
| `new-game.ctm` | 7196 of 7199 | `fixed_color` on 3 |
| `menu.ctm` | 8895 of 8899 | `fixed_color` on 4 |
| `field.ctm` | 11396 of 11399 | `fixed_color` on 3 |
| `fair.ctm` | 12446 of 12449 | `fixed_color` on 3 |
| `fair-companion.ctm` | 17896 of 17899 | `fixed_color` on 3 |
| `gato-battle.ctm` | 21596 of 21599 | `fixed_color` on 3 |
| `attract-demo.ctm` | 9379 of 22499 | `vram` on 9840, `bg.map_base` on 1273, `bg.hofs` on 700, `mode.mode` on 566, window 1's edges on 528 and 151, `sprite` on 52 |
| `saves/events-blackbird.ctm` | 4399 of 4399 | -- |
| `saves/events-keepers-dome.ctm` | 4999 of 4999 | -- |
| `saves/events-jetbike-race.ctm` | 9399 of 9399 | -- |
| `saves/events-prison-escape.ctm` | 12899 of 12899 | -- |
| `saves/events-end-of-time-gate.ctm` | 3863 of 5599 | `mode7.d` on 1414, brightness on 271, `mode.mode` on 50 |
| `saves/menus-shop-inn.ctm` | 9701 of 9701 | -- |
| `saves/menus-shop-equip.ctm` | 15512 of 15512 | -- |
| `saves/menus-field-menu.ctm` | 10594 of 10599 | `bg.hofs` on 4, `fixed_color` on 1 |
| `saves/menus-end-of-time.ctm` | 8799 of 8799 | -- |
| `saves/menus-epoch.ctm` | 8799 of 8799 | -- |
| `saves/battle-golem-twins-gameover.ctm` | 12968 of 12968 | -- |
| `saves/battle-tyrano-lair-walls.ctm` | 8041 of 8041 | -- |
| `saves/battle-magus.ctm` | 17999 of 17999 | -- |
| `saves/late-omen-terramutant.ctm` | 17761 of 17761 | -- |
| `saves/late-omen-battles.ctm` | 18411 of 18411 | -- |

Fifteen of the twenty-one agree on every frame, the battles included: the
`windows.w1_left` and `sprite` frames P2 left them with, from the channel a
transfer and a per-line effect share, are gone. `fixed_color` is the handful of
frames P2 already had. The one movie that is not close is
`saves/events-end-of-time-gate`, where everything after frame 3864 differs: the
event's own gate screen drives the Mode 7 matrix and the screen's brightness
itself and none of that code is ported yet, so the observer never hears about it
([TODO.md](TODO.md)). It is not the menus' gate screen -- that movie calls none
of `menu/epoch.c`'s entries -- and the same build of `main` reports the same 3863
of 5599. `attract-demo` is not close either: from frame 2266 the attract
demo's own scenes write the display around the service -- the scene program's
modes 1 and 3, which no other movie runs (VRAM, the tile maps' bases, the mode
and window 1 from 2789 and 8953), and BG2's scroll and window 1's right edge on
two of its world maps ([TODO.md](TODO.md)).

The two menu movies were the milestone's last leftover, and Phase 3's conversion
of bank `$C2` closed it: `menus-epoch` went from 5506 of 8799 to all of them and
`menus-end-of-time` from 8234 to all of them, once the Epoch's map view and time
dial and the End of Time's gate went behind the service
([history/reimplementation-waves.md](history/reimplementation-waves.md), "Phase 3, wave 2"). Two calls were added for them:
`vid_upload_vram_half`, for an upload that writes one byte of each VRAM word and
leaves the other, which is how a Mode 7 plane's tile numbers and pixels are
loaded separately; and the four `VID_LINE_MODE7_A`-`_D` targets, for a matrix
value driven per line on its own rather than in pairs.

Fuzzing found one real bug the movies don't reach: a deferred upload's
transfer, when its size is large enough to read through the stack page, copies
the bytes the routine's own `JSR` left there, so the port has to write them
before the transfer rather than after. It found two more in this round. The VRAM
port's byte has three bits the hardware ignores, which `VidVramPort` dropped, so
a routine that sets the port from a value it was handed (`$7E:59D5`) wrote a
different byte back; `snes_extra` keeps them, as the other decoders already do
for their spare bits. And a port that hands back in the middle of a loop couldn't
be validated at all (above; it can now). The four window tile writers are guarded
on a direct page on the PPU block, which the fuzzer's base states don't have, so
they are validated by shadow mode only.

The menus' ports found two more differences the movies don't reach. Mode 7's
center registers keep the byte written, not the 13 bits the hardware computes
with, so `vid_set_mode7` takes the value as written and the observer narrows it
with `vid_mode7_13bit`; passing the narrowed value would have lost the top three
bits of the high byte. And the window command's channel operand has to be one of
the eight channels' own base addresses for the port to write the registers the
original writes -- with any other value the original writes at whatever offsets
the operand gives -- so that is part of the entry's guard. The per-routine
counts and the constraints each needs are in [PORTING.md](PORTING.md).

`--native on --generated only` still produces traces identical to `--native off`
(3600 frames of `new-game`, no divergence in WRAM, VRAM, SRAM, CGRAM or OAM),
so the block boundaries the new registry entries move change nothing.

#### The services' gaps the reviews found, closed

The Phase 3a reviews of scene, field and the menus found ports spelling out
SNES mechanics because the video service had no call for what they meant:
setups decoding literal register bytes (`vid_mode_decode(0x09)`,
`vid_bg_map_decode(&b, 0x5A)`), a per-call array of the original's write order
for every transfer and line effect (`VID_STEP_*` in a `VidSnesXfer`) with the
DMA channel and mode beside it, line tables built from raw color math and
window bytes, and the sprite table's ninth X bit, 8-bit Y and hide line in
`field/sprites.c`. Four additions close them, all additive: the older forms
stay for the ports not moved yet (below).

- **A whole screen as one value.** `VidScreen` holds what a mode sets up when
  it starts -- the display (or none, to keep it), the sprites' sizes and tiles,
  the BG mode and mosaic, each layer's tilemap, tiles and scroll, the Mode 7
  plane, the windows, the main and sub screens' layers, color math and its
  fixed color (`vid_fixed_color` builds the BGR555 value), the video settings
  and the VRAM port -- and `vid_apply_screen(pf, o, &screen)` sets it. The
  four whole-screen routines are one static value and one call each: the
  power-on register block `$FD:C036` (`core/display.c`), a scene's first
  screen `$7E:30F9` (`scene/screen.c`), and the menus' two, `$C2:0043` and
  `$C2:940D` (`menu/display.c`). The field's `$FD:C0D7` sets part of a
  screen, not all, so it stays a run of setters, which already said what they
  set. Layer sets have names (`VID_BG1_BIT` ... `VID_OBJ_BIT`, `VID_BACK_BIT`,
  `VID_ALL_LAYERS`) and a tilemap its initializer (`VID_TILEMAP`).
- **Uploads and line effects that say what moves where.** `vid_send_vram`,
  `vid_send_vram_half`, `vid_send_vram_fill`, `vid_send_palette`,
  `vid_send_oam`, `vid_start_vram_uploads`, `vid_set_line_slot`, a
  `vid_set_line_table` without steps, and `vid_snes_copy` for the SNES-only
  escape. An upload takes the VRAM port it sets as a value (NULL: as the port
  was left), which is semantic -- a column steps 32 words -- and nothing else
  of the SNES's. `vid_send_vram_in_mode` is the one call that takes a transfer
  mode, for the field's screen transitions, whose mode is game data.
- **Line tables' values from what they set.** A line effect's table is game
  data in the form its target reads, so `vid_line_bg_map`,
  `vid_line_math_layers`, `vid_line_fixed_color` and `vid_line_window_edges`
  give a value that form from what it means; the field's dialog shading, its
  window band's tilemap, its full-line window and its screens now read as
  values (`field/line_effects.c`).
- **The sprite table's form.** `VID_OAM_*` and `vid_oam_x_low`,
  `vid_oam_high_bits`, `vid_oam_x` and `vid_oam_large` turn a signed screen
  position into the table's entry and high bits and back, and
  `VID_OAM_HIDDEN_Y` is the line that shows nowhere; the field's sprites
  (`field/sprites.c`) hold positions as signed coordinates and no longer know
  the ninth bit. Which sprites a placing hides is the game's, not the
  table's: each layout's placing has its own rules, which `field/sprites.c`
  keeps together in one table (an earlier `vid_oam_y` held the 4-sprite
  layout's alone). The table stays game memory: the field still
  builds it where the original did, since other routines read it, and
  `vid_submit_sprites` (the sprite list "Migrating an existing port" sketches)
  waits for all of them.

**Where the write order went.** Shadow mode compares the DMA and HDMA
registers as the original leaves them, and what they are left holding depends
on the order: an empty upload stops at its size, a second upload doesn't
program what the first did, the tile animation clears the size with a 16-bit
store before writing its low byte. That order belongs to the original's code
at each site, not to the game, so it moved to the SNES backend:
`runtime/platform/snes/encodings.c` is a table of the original's sequences by
call site (`PfOrigin.site`, which every call already passes), with the channel
and mode each wrote -- 96 transfer and line-effect sites in 86 entries, the
tile animation's twelve being two runs of copies -- and the four screen setups as ordered lists of
setter calls with each one's instruction. The `vid_send_*` calls look their
site up; one that isn't there, or that names a VRAM port where the original's
sequence sets none (or the other way round), stops the program with the
site's address rather than guess. A second backend never sees the table. The
lint keeps it out of `game/`: `VID_STEP_*` and `VidSnesXfer` appear only in the
files it lists as not moved (`test_write_order_only_where_left`), which may
shrink and not grow.

**Moved:** `core/display.c`, `core/palette.c`, `core/tile_animation.c`,
`field/display.c`, `field/line_effects.c`, `field/window_corners.c` (now in `field/dialog_window.c`),
`field/sprites.c`, `menu/display.c` (whose comments quoting the original's
register writes went with its arrays), `scene/screen.c`, `scene/race.c` and
`scene/title.c`; `scene/scene.h` no longer names a DMA channel or mode. The
menus' queue at `$C2:8663` programmed its channel's mode inside the loop's
first pass; that is a `vid_start_vram_uploads` at the instruction that does
(`$C2:867A`) now, the same writes in the same order. The menus' pass
after it moved the other three menu files (`menu/epoch.c`, `menu/map.c`,
`menu/line_effects.c`): the Epoch's Mode 7 uploads, the map's strip uploads
and every per-line slot the sprite-object mode starts, 25 sites (history/reimplementation-phase3a.md,
"Phase 3a: the menus' follow-ups and the Epoch"). **Not moved:** the battle
(`battle/display.c`, `battle/frame.c`, `battle/nmi.c`), which a rewrite is
changing; docs/TODO.md lists its calls.

**Checked** by the gate (shadow mode over the library, 0 mismatches; the
trace, timing, vblank, route, don't-care and port-style checks), `--px-check`
(every frame identical over the library, as before) and `--vid-check` (every
movie's count the same as before: the observer sees the same calls, now made
by the backend on the game's behalf). The moved entries were fuzzed against
the ports they replace with `tools/fuzz_compare.py` (seed 1, 2000 states each,
200 for the long transfers): 72 routines, every count the same before and
after. The sound driver's change was checked with `ct_dev --sound-lockstep`
over `new-game` (22426 ticks identical, the driver's state included) and 100
runs of `ct_sound_fuzz`, none differing.

### P3: native renderer, pixel-identical

- A frame renderer working from `VidFrame`: whole spans instead of per-pixel
  layer resolution per line, tile caches invalidated by uploads, the Snes9x
  color pipeline (`render_snes9x_color`) as its reference color mode.
- Fed by the decoder, so it runs on every movie from the start; fed by
  commands for subsystems past P2.
- It should also make runs faster: the scanline renderer is 81% of `on` mode's
  time ([GENERATED.md](GENERATED.md)).

**Done when:** every frame of all 21 movies is pixel-identical to `render.c`
at 256x224, including battle windows, split-screen dialogs and the Mode 7
scenes (hi-res frames, if any turn up, compared at `render.c`'s 256-wide
sampling); `ct_play` uses it by default; `on` mode over `gato-battle` is
measurably faster.

#### P3 as built

| Path | What |
|---|---|
| `runtime/platform/native/render.c` | the renderer: one frame from a `VidFrame`, 256x224 XRGB8888, with a line cache |
| `runtime/platform/native/render.h` | `vid_render_frame`, `VidRenderFlags`, `VidRenderStats`, the color helpers |
| `runtime/platform/video.h` | `VidSpriteSet`, and `VidSpan.cgram`, `.vram`, `.sprites`: the memory a span reads |
| `runtime/platform/snes/decode.c` | the snapshots that fill those in, and the hints that keep the decoder cheap |
| `runtime/snes/ppu.h` | `cgram_writes` and `oam_writes` beside `vram_writes` |
| `dev/px_check.c` | `ct_dev --px-check [out.txt]`: the two renderers compared frame by frame |
| `frontend/ct_play.c` | `--renderer native` (the default) or `scanline` |
| `tests/test_native_render.py` | the renderer reads nothing in `runtime/snes/` |

**What it reads, and what it doesn't.** `vid_render_frame(r, frame, out, flags)`
draws lines 1-224 of a `VidFrame` into a 256x224 XRGB8888 buffer. Its only input
is the frame: the lines' state as spans, the sprites in OAM order, and the VRAM,
CGRAM and sprite set each span reads. It includes `platform/video.h` and the C
library and nothing else -- not `Ppu`, not the hardware model's video memory, not
`render.c`'s color helpers, which it has its own copies of. A unit test enforces
that, because a second backend (widescreen, GBA) would otherwise inherit the
hardware model along with the renderer.

The drawing is `render.c`'s, kept identical down to the arithmetic rather than
tidied: the same front-to-back layer order per mode, the same depth merge, the
same window logic, the same Snes9x color pipeline (palette entries scaled by
master brightness, packed RGB565 with green's LSB copied from its top bit, color
math on the packed values). Where the hardware is ambiguous, the scanline
renderer is the definition.

**The gap in the representation, and how it was closed.** `VidFrame` carried one
VRAM pointer, one CGRAM pointer and one set of sprites: whatever they held when
the frame was finished. `VidSpan.palette_changed` said a palette had changed
mid-frame but not what it changed to. That is enough to compare two frames
(P2's `--vid-check`) and not enough to draw one, because the game does change
video memory while the beam is in the picture: the palette animation and fades
write CGRAM between spans, and the battle's line interrupt forces blank, uploads
VRAM and OAM, and unblanks again. A renderer given only the end-of-frame memory
draws the lines before such a change with memory they never saw.

So `VidSpan` gained `cgram`, `vram` and `sprites`, each NULL meaning the frame's
(additive: the observer leaves them NULL and P2's comparison is unchanged). The
decoder fills them in: when memory changes, it keeps the old contents in a
snapshot, hands it to the spans already emitted, and starts a span at that line
so the lines after it read the new contents. It snapshots only for spans that
would show the difference -- not forced-blank lines, and sprites only where OBJ
is on a screen -- which is why the movie library never needs more than two VRAM
snapshots and one of each of the others in a frame (the peak over all 26 movies;
the pools hold 8, 32 and 8, and none was ever lost). `VidFrame`'s own
`sprites` and `first_sprite` are now the set the frame ends with, re-decoded
whenever OAM or the priority rotation changes rather than only at line 1, so they
mean the same thing as its `vram` and `cgram`.

Two further gaps were looked for and turned out not to be gaps. `VidObjConfig`
carries three bits of sprite base address where `render.c` keeps two, but the
third is bit 16 of a byte address the renderer masks to 64 KiB, so the two agree.
`VidBg.char_base` keeps four bits of the character base where `render.c` masks to
three, and the fourth is bit 16 the same way.

**Speed.** The renderer keeps `render.c`'s caches -- the palette as RGB565 with a
generation per entry, VRAM in 32-byte pages with a generation per page, the
per-row sprite sets with a generation per row -- and redraws a line only when the
state it was drawn from, or a CGRAM entry or VRAM page it read, has changed. The
dependency marking is `render.c`'s, so the two draw exactly the same lines:
1152805 of 4455190 over `gato-battle`, the rest kept. Without the cache the
renderer redrew all 224 lines every frame and cost 5.9 s against `render.c`'s
2.7 s, which is the same factor `render.c` gained when it got its cache
([RUNTIME.md](RUNTIME.md)).

What the spans buy is that the caches are brought up to date once per span
rather than once per line -- a span is a run of lines that read the same memory,
which is exactly the question those caches answer -- and that the key a line is
compared against is the span's state masked once, leaving only the line's own
sprite-row generation per line.

One thing had to be learned rather than copied: a forced-blank span's brightness
is the low bits of the forced-blank byte, not a brightness the game means, so
feeding it to the palette cache rebuilt the whole palette twice a frame and no
line was ever kept (78% of lines redrawn instead of 24%). `render.c` returns
before touching the palette on a blank line; the span loop does the same, while
still comparing VRAM, which is what a line interrupt writes under forced blank.

**Deviations, and why.**

- **`$213E`'s sprite flags come at the end of the frame.** `render.c` sets
  range-over and time-over as the beam passes the lines that hit the limits; a
  frame renderer can only know them once the frame is drawn, so `ct_play` sets
  them then. No ported code reads `$213E`; a reader would see them a frame late.
- **The renderer owns its framebuffer between frames.** A line whose inputs are
  unchanged keeps the pixels it has, so `out` must be the same buffer every frame
  and the host must not write to it -- the same contract `render.c` has.
- **Hi-res, pseudo-hires and interlace are what `render.c` makes of them.**
  Modes 5 and 6 are sampled at 256 columns and `$2133`'s interlace and pseudo-
  hires bits are not modeled. They are carried in `VidVideoConfig` so nothing is
  lost, and matching the reference is what P3 is for; a renderer that draws them
  properly is a deliberate departure, like widescreen.
- **The decoder is a bridge, not part of the renderer.** It costs about 0.3 s per
  `gato-battle` run on top of the drawing, because it rebuilds a `VidLineState`
  and compares it at every line. A build where game code feeds the video service
  directly wouldn't decode registers at all; until P2 is finished, every movie
  runs through it.
- **`Ppu` gained two counters.** `cgram_writes` and `oam_writes` join
  `vram_writes` as hints, so the decoder can skip comparing 116 KB of CGRAM and
  122 KB of OAM per frame. As with VRAM, a change that doesn't go through them (a
  state load) is caught by comparing contents at a frame's start.

**Results.**

`ct_dev --px-check --native on` over the whole movie library: **800751 frames,
every one identical to `render.c`'s at 256x224**, not a differing pixel. The
range-over and time-over flags agree on every frame, and no mid-frame change of
video memory ever ran out of snapshots. "Lines drawn" is how many of the frames'
non-blank lines the cache made the renderer redraw, which is the same number
`render.c` redraws.

| Movie | Frames | Identical | Spans per frame | Lines drawn |
|---|---|---|---|---|
| `title-start.ctm` | 1801 | all | 3.0 | 66.4% |
| `new-game.ctm` | 7201 | all | 12.8 | 32.9% |
| `menu.ctm` | 8902 | all | 16.1 | 27.6% |
| `field.ctm` | 11402 | all | 10.3 | 26.4% |
| `fair.ctm` | 12452 | all | 9.6 | 26.6% |
| `fair-companion.ctm` | 17903 | all | 8.9 | 23.7% |
| `gato-battle.ctm` | 21603 | all | 10.3 | 23.8% |
| `attract-demo.ctm` | 22500 | all | 25.9 | 55.0% |
| `saves/battle-tyrano-lair-walls.ctm` | 8043 | all | 19.4 | 32.0% |
| `saves/battle-golem-twins-gameover.ctm` | 12970 | all | 17.3 | 26.2% |
| `saves/battle-magus.ctm` | 18001 | all | 18.8 | 26.9% |
| `saves/events-blackbird.ctm` | 4401 | all | 16.6 | 50.2% |
| `saves/events-keepers-dome.ctm` | 5001 | all | 36.7 | 47.4% |
| `saves/events-end-of-time-gate.ctm` | 5601 | all | 119.5 | 48.5% |
| `saves/events-jetbike-race.ctm` (Mode 7) | 9401 | all | 50.7 | 46.8% |
| `saves/events-prison-escape.ctm` | 12901 | all | 7.3 | 23.0% |
| `saves/menus-end-of-time.ctm` | 8803 | all | 83.1 | 36.6% |
| `saves/menus-epoch.ctm` | 8805 | all | 68.5 | 51.0% |
| `saves/menus-shop-inn.ctm` | 9704 | all | 20.5 | 26.0% |
| `saves/menus-shop-equip.ctm` | 15513 | all | 43.6 | 24.9% |
| `saves/menus-field-menu.ctm` | 10602 | all | 55.2 | 25.3% |
| `saves/late-omen-terramutant.ctm` | 17763 | all | 32.1 | 30.1% |
| `saves/late-omen-battles.ctm` | 18413 | all | 21.7 | 26.0% |
| `saves/late-omen-zeal.ctm` (color math, windows) | 36923 | all | 35.5 | 44.4% |
| `saves/late-lavos.ctm` | 60250 | all | 29.1 | 25.2% |
| `saves/endings-beyond-time.ctm` | 105003 | all | 55.2 | 7.4% |
| `saves/endings-telepod-lavos.ctm` | 132888 | all | 33.4 | 9.9% |
| `saves/endings-bucket-lavos.ctm` | 196001 | all | 25.7 | 52.8% |

The per-line effects are what the span counts show: the battles run about 20
spans a frame, the menus and the Epoch 55-85, the End of Time's gate 120. That
is also what the decoder costs, since it rebuilds and compares a `VidLineState`
at every line whatever happens.

Speed, `ct_play` headless with `--native on --generated only`, on a shared machine,
runs alternating between the two renderers (five pairs for `gato-battle`, three
for the others):

| Movie | Frames | Scanline (median) | Native (median) |
|---|---|---|---|
| `gato-battle.ctm` | 21600 | 11.74 s (11.54-11.85) | 11.79 s (11.75-11.86) |
| `saves/events-jetbike-race.ctm` | 9400 | 5.61 s (5.46-5.64) | 5.55 s (5.39-5.79) |
| `saves/late-omen-zeal.ctm` | 36922 | 20.86 s (20.44-23.80) | 20.55 s (19.30-23.71) |

That is a wash, so P3's "measurably faster" isn't met: the two renderers do the
same drawing on the same lines, and the native one carries the decoder on top.
The 81% figure the milestone quoted was the renderer's share before it had a
cache; against the cached `render.c` there is nothing left to win by rendering
whole spans, and what remains to win is the decoder, which a build fed by service
calls wouldn't run at all.

The unit tests (including the platform lint and the new renderer lint) pass and
the build is warning-free with `-DCT_WERROR=ON`.

**What is left.** The native renderer draws the frame the *decoder* builds. The
frame the *observer* builds from service calls is the same object, so the
renderer will take it as soon as P2 finishes and every register writer is behind
the API; until then `--vid-check` is what says how far that is. The observer
does not fill in the per-span memory pointers, which it will need for the same
reason the decoder does.

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

## The assets: imported once, loaded by name

Phase 4a's "Assets" ([PLAN.md](PLAN.md)): the game's data comes from a store
the driver supplies rather than from a mapped ROM image, so a build with no
emulator, or a target that can't map 4 MiB, has it.

### The assets, as built

**The catalogue** (`re/assets.tsv`, written by `tools/assets.py build`). A row
a resource the ports' accessors read: its name, its type (MD2's, MATCHING.md),
the ROM range the original keeps it in and the codec that decodes it (`raw`,
`lz`, `lz:event`, `text:dialog`, `script:battle-animation`, `gfx:plane-pool`,
...). The rows are regions the matching build extracts ("Build-time
extraction"), chosen three ways: every region holding an address an accessor
names (a constant in a `*_data.h` header; the row is named after the
accessor); every region holding an address `ct_dev --asset-reads` recorded
over the movies (the accessors that follow a pointer the data holds); and,
for a `[[resource]]` table any of those is part of, the table and every target
it names, so a pointer read from it lands in the catalogue whether or not a
movie followed it (the targets are `<table>.<entry>`). Besides the typed
`[[data]]` regions, two kinds the extraction writes as raw blobs: a jump table
the analysis found (`jump-table`), and a run of bytes that is neither code nor
a typed region (`untyped`). And one it doesn't: `code`, an instruction a reader
looks at, where the menus' window command lists are read a byte past their
end. The file holds locations and types, no ROM bytes.
`game/data/asset_catalogue.c` is generated from it and committed, so a build
without a ROM has the table; `tests/test_assets.py` fails when it is stale,
and `tools/assets.py check --rom` (a gate check, `assets`) when a row is no
longer a region the extraction makes.

Built from the 48 movies' reads in shadow mode, the catalogue has **6087
resources, 3,800,908 bytes** (91% of the ROM; nothing the accessors read is
left out):

| Type | Resources | Bytes | Found by |
|---|--:|--:|---|
| `lz` | 1371 | 2354164 | the stream tables (`[[resource]]`) |
| `text` | 3726 | 144144 | the string tables |
| `spc700` | 149 | 294487 | the sound driver's blocks |
| `gfx` | 6 | 334592 | banks CF, D0, D2-D5 |
| `bytes`, `words`, `ptr16`, `ptr24`, `struct` | 759 | 319038 | the accessors' tables |
| `script` | 19 | 86445 | event, battle, enemy and object scripts |
| `mixed`, `undetermined` | 30 | 224269 | regions MD2 hasn't split |
| `untyped` | 11 | 42521 | regions MD2 hasn't typed (`$F6:24B1-7FFF` the largest) |
| `jump-table` | 4 | 1230 | the handler tables the guards and call-outs read |
| `code` | 12 | 18 | a byte read past a command list |

174 rows were found by an accessor's constant, 900 by the reads alone, and
5013 as entries of a table one of those is.

**The check.** Every cartridge read an accessor makes is looked up in the
catalogue (`asset_containing`, `game/data/assets.c`: a binary search, the last
hit kept); one that lands in no resource is counted, and `ct_dev` prints an
`asset outside the catalogue` line, which fails the gate's shadow and
shipping-mode runs (`tools/suite.py`). A read through a pointer into work RAM
(scripts and text the game copies or builds there) isn't the cartridge's and
isn't counted.

**The import** (`frontend/assets.h`, `assets.c`). `asset_pack_import` takes a
ROM image, checks its CRC-32 (`2D206BF7`, Chrono Trigger (USA)) and size, and
makes an asset pack: a header (`CTASSETS`, the format, the count, the
catalogue's version -- a CRC-32 of its rows -- and the ROM's CRC-32), a
directory entry a resource (where its bytes are, how many, its name and its
type) and each resource's original bytes, raw. It reads the ROM through the
same kind of store the game reads from (`asset_store_rom`), so the pack is
whatever that store serves; `asset_pack_build` makes one from any store, which
`assets_test` uses without a ROM. Nothing here opens a file: `ct_play` (its
driver, `frontend/files.c`, `files_asset_pack`) writes the pack as
`<rom name>.assets` beside the saves, on the first launch without one and
again with `ct_play --import <rom>`, which then exits. The pack is ROM-derived
and gitignored wherever it lands. Over this catalogue it is 4,003,655 bytes,
3,800,908 of them the resources.

**Loaded by name** (`game/data/assets.h`, `assets.c`; `game/rom.h`). The driver
gives the game an `AssetStore` with `assets_use`, as it keeps the saves: a
`find` that answers a resource's name and type with its bytes. `game/` never
opens a file or knows a path. The accessors are unchanged: each still says
where the original keeps its data, and that address is now a key. `rom_read8`
looks it up in the catalogue, asks the store for that resource by name and
type the first time (`asset_served` keeps the answer, a slot a row) and reads
the byte at the address's offset in it. What is left of the machine is one
call to the backing, `rom_cartridge_read`: with the machine
(`runtime/port/state_io.c`) it makes the original's access, so the ports' time
estimates, which count their bus accesses, don't move, and checks the store's
byte against the cartridge's, counting a difference (`ct_dev` prints `asset
differs from the cartridge`, which fails the gate); where the store has no copy
-- no store, or an address in work RAM -- the machine's byte is the read's. A
build without a machine gives the store's byte (`tests/frontend/assets_test.c`
is that backing, and `assets_test` reads every resource through a pack built
from made-up bytes, its mirrors among them, with no ROM).

Who supplies which store: `ct_play` reads its pack (`<rom name>.assets`, the
first launch importing it, a pack of another catalogue imported again);
`ct_dev --assets <pack>` does the same, and the gate runs every shadow and
shipping-mode movie that way, from a pack the suite imports first, so each
accessor read is the pack's byte checked against the cartridge's; without
`--assets`, and in `ct_drive`, `ct_fuzz` and the tests, there is no store and
the machine's cartridge serves the reads, as before. `asset_store_rom`
(`frontend/assets.h`) is a store over a ROM image in memory, for a build that
has the ROM but no machine. Over the movies with the pack, shadow mode
compares as before: 0 mismatches, no read outside the catalogue and none that
differs; a pack with a byte in 997 changed makes ports mismatch and prints
both lines.

What still reads the mapped ROM: the original's code (the emulator); the one
port that reads cartridge data on the machine rather than through an accessor,
the decompressor (`data/decompress.c`, on the portstyle backlog), which reads
the streams the catalogue holds by the pointer the loaders leave; and the video
service's transfers whose source is a cartridge address (`PF_GAME` of a ROM
address: the menus' VRAM fills and ROM copy in `menu/display.c`, the battle's
tile blocks in `battle/uploads.c`), which the SNES backend sends by DMA from
the bus. `ct_native` (docs/PLAYING.md, "The native program") reads the game's
data from the pack and maps no ROM: `runtime/port/rom_host.c` is its backing
of `game/rom.h`, the store's byte, and the host's memory where the store has
none.

## Timing without a beam: the native backend

Phase 4a's design ([PLAN.md](PLAN.md)), decided before the backend is
written. For video, audio, input and saves a host backend translates the SNES
one. Timing can't be translated: `tm_set_line`, `tm_enable_interrupts`, the
acknowledges and the status reads are scanline and interrupt concepts, and a
host that calls the game once a frame has neither. So the backend follows from
what the game uses time and the beam *for*, and where no backend can answer,
the answer is a game-side rewrite. This section is that census, the
classification, the model a native build runs on, and the rewrites.

### The census

`python3 tools/timing_census.py` (given the coverage files, it also says which
of them a movie ran) lists every instruction the static analysis finds that
reaches `$4200` (which interrupts are on), `$4207-$420A` (the line interrupt's
dot and line), `$4210`/`$4211` (the vblank and line interrupt flags), `$4212`
(blanking and controller-read status), `$2137` (latch the beam position),
`$213C`/`$213D` (the latched position) or `$213F` (the counters' read toggle
and the frame's field bit), by a long, absolute or direct-page operand, in
each width the instruction was decoded with (a 16-bit access reaches the
register after its operand too). A forward pass follows D
where a routine loads it with `$2100` or `$4200`. Then it lists every wait in
the ROM: a branch back of at most 16 bytes over nothing but reads, tests and
branches, so that only something outside the loop can end it.

What it can't see: the data bank (an absolute operand is taken to reach the
register, which it does only with the bank in `$00-$3F` or `$80-$BF`); a direct
page a routine inherits or computes; indexed operands (the only ones with a
base in the register block are work RAM tables at `$7E:2100-$213F`, reached
with the data bank at `$7E`); and code the analysis doesn't reach. As a check
on those gaps, `ct_dev --native off --io-log` over `fair-companion`,
`gato-battle`, `events-jetbike-race` and `endings-beyond-time` touched these
registers from 78 instructions, every one of them in the census. Six are in
it only because of the width rule: five divider spills, and the second
frame-parity read below.

**117 instructions: 52 in hand ports, 65 still generated.** The ports reach
them through 39 timing service calls, and through direct reads of `$4212` and
the counters in `attract/main_loop.c` and `battle/wait.c`, which predate the
service. By what they are for:

| Kind | What it computes | Ported | Generated | Where |
|---|---|---|---|---|
| The frame tick | which handlers run, and acknowledging them | 31 | 31 | every mode's vblank handler, the three line handlers, mode starts and loads |
| Timer reset | nothing: `$4207-$420A` cleared as a mode starts | 4 | 9 | `$C2:0043`, `$C2:940D`, `$FD:EB13`; the boot `$FD:C000` through `tm_reset_timers` |
| The line handler's line | line 211 | 3 | 1 | field `$C0:0B28`, battle `$D1:F1B0`, scene boot `$C3:0000` |
| Hblank waits in the line handlers | nothing: the blank lands at a line's end | 4 | 0 | `$C0:ECEB`, `$D1:FEB8`, `$C3:0530`, `$C3:0538` |
| Waits for vblank | the frame boundary | 4 | 2 | the races `$C0:3B71`, `$C0:4181`; the attract's `$7E:31B0`/`$7E:31B7`; with the vblank handler off, before turning it on, `$C0:0B28` and `$FD:EADA` |
| Controller read busy | nothing | 2 | 0 | `$C2:84EC`, `$C3:0A8E` |
| The battle's wait for a line | not established | 6 | 0 | `$CD:3B82` |
| Beam budgets | how much deferrable work runs this frame | 0 | 8 | the event scheduler `$C0:5A46`, the sprite frame queue `$C0:CA76` |
| Beam entropy | a random byte | 0 | 4 | `$C3:02DD` |
| Frame parity from the PPU | odd or even frame | 1 | 1 | `$7E:9AB3`, `$7E:9EDF` (scene program; the second through `tm_odd_frame`) |
| Latch reset | nothing | 1 | 0 | the battle's vblank handler, `$D1:E7CF` |
| Divider spill | nothing: a 16-bit divisor write also writes the dot timer | 0 | 5 | the scene program, `$7E:3608` and four more |

Beside them, the waits on memory: 13 on the APU ports (all idle entries, all
deleted by the SPC700 driver's port) and 9 on flags an interrupt handler clears
(5 idle entries; `$CD:044A`, `$CD:3E55`, `$CF:FCFB` and `$FD:EAEB` are
generated). The modes' wait-for-frame routines are called from deep in the
game: the field's `$C0:EC60` from 28 call sites, event commands among them, the
battle's `$C1:007E` from 20. Loops that call something on each pass aren't in
the wait census; the registry's idle entries are the fuller list.

What the census established:

- **Four interrupt settings, ever**: `$00`, `$01` (the controller read alone),
  `$81` (and the vblank handler) and `$A1` (and the line handler). The dot
  interrupt is never enabled; the dot timer is only cleared, or written by
  accident by the divider spill.
- **Every line interrupt is at line 211, and does one thing.** The field
  (`$C0:ECCC`), the battle (`$D1:FE97`) and the scene boot (`$C3:0529`) all
  acknowledge, wait for that line's hblank and force blank. The field and the
  battle also switch the line interrupt and the per-line effects off for the
  rest of the frame, and upload (tile map columns, `$C0:ED15`, or tile
  animation, `$FD:FFFD`; on the battle's even frames `$D1:FE4C`). The menus
  and the gate mode install empty handlers (`$C2:0345` is an `RTI`,
  `$FD:DE87` pulls what it pushed) and never enable them. So there is **no
  raster effect done with an interrupt anywhere in the game**: the picture
  ends after line 211, and the 13 lines below it are more vblank time for
  uploads. Every change of video state inside the picture is a per-line effect
  from a table (HDMA), which the video service already carries.
- **The budgets defer work, they don't drop it.** `$C0:59D9` queues each
  object whose script delay ran out (head and tail at `$0174`/`$0175`, linked
  through `$1080`); `$C0:5A46` runs queued scripts (`$C0:5A93`) while the beam
  is above line 72 (`$0169`, set by `$C0:56A6`) or at line 240 or later, and
  what is left stays queued for the next frame. `$C0:CA76` does the same for
  new animation frames (`field_animation_queue`, `$0176`) against line 120
  (`$016B`, which event command `$47` sets from a script), retrying an object
  whose frame build says it isn't finished. At the frame boundary the event
  queue still held objects on 91 of the 16260 frames of `fair-companion` after
  the attract sequence, the animation queue on 191; on `gato-battle`, 120 and
  230 of 19960. The sprite budget's early exit ran in 36 of 38 coverage files.
  So the budgets bind on about 1% of frames, and when they do, some objects'
  work runs a frame late.
- **Beam randomness and PPU frame parity are the scene program's alone.**
  `$C3:02DD` mixes the latched counters into a seed (`$0386`) for the scene
  program (`$7E:3443`, in the endings) and for the noise effect `$C3:01E4`,
  which indexes a table in ROM with it. `$7E:9AB3` and `$7E:9EDF` read the
  field bit of `$213F` (the second as the high byte of a 16-bit read of
  `$213E`) to move a scroll or a position every other frame. All of it is
  cosmetic.
- **The battle's wait for a line has no established purpose.** `$CD:3B82`
  waits out vblank, then for the beam to reach a line. The battle's frame work
  calls it with line 64 (`$CD:09AC`) before the screen script channels
  (`$CD:1609`) run, while the timed effect slots do; screen script command
  `$C1` (`$CD:1D93`) calls it with a line from the script, and no movie runs
  that command. The channels' handlers write buffers, never a register, so
  dropping the wait changes when their work runs and not what it computes.

### The model

The driver calls the game once per displayed frame, and a frame is:

1. the vblank handler, if it is enabled;
2. the game's main thread, resumed where it last waited, until it waits for
   the next frame;
3. the line handler, if it is enabled;
4. the frame's picture, handed to the video backend.

The picture is the video state the vblank handler left, with the per-line
effects and the calls that name a line (`vid_raster_display`) applied from
their lines. A video call from the main thread applies from the next frame. On
the SNES such a write lands wherever the beam is, and where that is inside the
picture the vblank check lists it as an exception
(`tools/vblank_check_exceptions.txt`); a native build shows it a frame later,
the line or two on a few frames that target 4 accepts.

What follows from it:

- **Handlers never preempt the main thread**, so there are no lag frames.
  Work the original spread over two frames when the main thread overran is done
  in one, and the handlers' still-busy paths (the battle's interrupt-busy
  count, the scene's handler while its frame work runs) don't run.
- **Sections with the interrupts off need nothing.** A load with the handlers
  off (the menu code alone sets the interrupts at 29 places, most of them in
  pairs around a load) runs to completion inside one frame, and the black
  frames the original spent on it are gone.
- **The main thread needs a stack of its own.** Its frame waits are deep in
  its call stack (event commands, fades, battle scripts), so a native build
  runs the main thread as one coroutine through `runtime/port/fiber.h` and
  yields at each wait, or every such caller is rewritten to resume. The first
  is a one-time cost; the per-port fibers and the call-outs go with the
  emulated machine, and this one stays.

### The calls

What the native backend does with each call, and the calls to add (additive;
the SNES backend of each new one makes the original's accesses, so shadow mode
compares what it compared before):

| Call | Native backend |
|---|---|
| `tm_enable_interrupts` | Records which handlers the frame runs. `TM_INT_AUTO_JOYPAD` needs nothing: the driver latches input every frame (`in_host_set`) |
| `tm_set_line` | Records the line handler's line. `vid_raster_display(..., VID_AT_LINE_HANDLER, ...)` resolves to the line after it, where the original's hblank wait puts the write. The picture's height, 211 lines where a mode's handler blanks, becomes a per-mode fact that widescreen and a GBA build can use |
| `tm_ack_vblank`, `tm_ack_line_irq` | The pending bit inside its own handler, 0 elsewhere |
| `tm_video_status` | **None.** A status read in a loop has no constant answer: the scene boot's line handler waits for hblank to end and then for it to start. Each caller is rewritten (below), and until then the call is SNES-only, like `vid_snes_write` |
| `tm_reset_beam_latch` | Nothing |
| `tm_sync` | Nothing; its one user, the sound driver's port I/O, goes with the SPC700 port |
| *new* `tm_wait_frame(pf, o)` | Yields the main thread to the driver. SNES backend: a call-out to the mode's wait routine, whose loop is an idle entry (the API sketch above) |
| *new* `bool tm_frame_time_left(pf, o, line)` | True: a host has time for all of it. SNES backend: latch and read the vertical counter as `$C0:5A46` does, true while it is above `line` or at line 240 or later |
| *new* `tm_wait_line(pf, o, line)` | Returns. SNES backend: `$CD:3B82`'s loops |
| *new* `TmEntropy tm_entropy(pf, o, width)` | Four values of noise, bytes or words, from a host generator the driver seeds, so a replay reproduces them. SNES backend: latch and read the counters as `$C3:02DD` does, a byte or a word at a time (history/reimplementation-chunks.md, "Phase 3, the scene program: the star field, the captions and the random numbers") |
| `bool tm_odd_frame(pf, o)` | The driver's frame count, odd or even. SNES backend: the original's 16-bit read of `$213E`-`$213F`, the field's bit 7 of the high byte, the word kept for scaffolding (`tm_snes_odd_frame_read`; added with the staff roll, REIMPLEMENTATION.md, "Phase 3, the scene program: the staff roll") |
| *new* `bool in_ready(pf, o)` (input) | True. SNES backend: `$4212` bit 0 clear |

`tm_frame_time_left` is a budget, not a model of one. A SNES ROM compiled from
the ports (target 3) needs it more than the original did, since compiled C is
slower, and a GBA build may want a deterministic stand-in (a count of objects
per frame) behind the same call; neither changes the game code.

### The rewrites

The uses no backend can answer, and what each becomes (docs/TODO.md, "Timing
without a beam", tracks them):

| Routine | Now | Becomes |
|---|---|---|
| The event scheduler `$C0:5A46` | generated, beam budget | run the due object scripts while `tm_frame_time_left(pf, o, limit)`; natively, all of them every frame |
| The sprite frame queue `$C0:CA76` | generated, beam budget | the same over the animation queue, with its script-set limit |
| `$C3:02DD` for `$C3:01E4` | generated | the stir as game code (`scene_draw_random`, ported for the star field), over `tm_entropy` |
| The races `$C0:3B71` and `$C0:4181`; the attract's `$7E:31B0`/`$7E:31B7`; the waits before a mode enables its vblank handler, the field's `$C0:0B38` and the time warp's `$FD:EADA` (both reached through `tm_wait_frame_then_enable` now); the generated flag waits `$CD:044A`, `$CD:3E55`, `$CF:FCFB`; every idle entry on a flag a handler clears | idle entries, or generated | `tm_wait_frame` |
| The line handlers' hblank waits `$C0:ECEB`, `$D1:FEB8`, `$C3:0530`/`$C3:0538` | idle entries | gone: the blank is `vid_raster_display` at the handler's line |
| The controller-busy waits in `$C2:84EC` and `$C3:0A8E` | ports that stop at the original's loop | `in_ready` |
| `$CD:3B82` | idle entries reading the registers directly | `tm_wait_line`, or nothing once its purpose is known |
| The timer resets in `$C2:0043`, `$C2:940D` | generated (the boot's, `$FD:C000`, and the time warp's, `$FD:EB81`, are `tm_reset_timers`; the battle's line, `$D1:F1B0`, is `tm_set_line`) | a reset of timers nothing enables is `tm_reset_timers`, which a native backend leaves empty |

**Validation.** A call that re-expresses a use keeps its accesses, so shadow
mode compares it as it compares the ports now. The schedulers are the
exception: their loop runs a routine per object and reads the beam in between,
and a port that reads at a different moment from the original can decide the
budget differently. With the per-object work as a call-out, the clone is
reloaded from the original after each one and the port reads the beam a few
clocks before the original does; a read that falls across a line boundary is
the one disagreement left, and the first port measures how often it happens.
The native backend itself is checked the way target 4 says, by the routes and
gameplay: the routes must reach their outcomes with the budgets drained, the
handlers never preempting and the scene program's randomness from the host.

### Timing without a beam, as built

**The service.** `runtime/platform/timing.h` has the five calls the design
added -- `tm_wait_frame`, `tm_frame_time_left`, `tm_wait_line`, `tm_entropy`
and `tm_odd_frame` -- with `tm_wait_frame_then_enable`, which the field's
screen setup added (a frame's wait with the handlers off, then the handlers
on), `tm_wait_frames` and `tm_wait_for_handlers`, which the battle's waits
added (below), `tm_wait_line_drawn`, with which the field's and the battle's
line handlers wait for their line's horizontal blank before they end the
picture, and `input.h` has `in_ready` as a backend call.
`tm_video_status` and `tm_beam_line` left the portable interface: what they
returned was the hardware's status byte and counter, which no host can answer
(review rubric item 118). The status is SNES-only now (`tm_snes_video_status`,
in `platform/snes/timing.h`), like `vid_snes_write`, and only idle entries
call it (below).

| Call | SNES backend | Host backend |
|---|---|---|
| `tm_wait_frame(pf, PF_CALL_DC(site, routine, set))` | the call of the mode's own wait routine the original made (JSR in its bank, JSL from another) | yields the main thread to the driver |
| `tm_wait_frame_then_enable(pf, PF_CALL_DC(site, routine, set), sources, line)` | the call of the original's wait for the vblank with the handlers off, which then turns them on (the field's screen setup, `$C0:0B38`; the time warp's start, `$FD:EADA`, whose mode runs no line handler: `line` is `TM_LINE_UNCHANGED`) | waits a frame, then `tm_set_line` (unless `TM_LINE_UNCHANGED`) and `tm_enable_interrupts` |
| `tm_frame_time_left(pf, o, line)` | `$213F`, `$2137`, `$213D` twice; true while the line is above `line` or at 240 and below | true |
| `tm_wait_frames(pf, PF_CALL_DC(...), frames)` | the count in A's low byte, and the call of the mode's routine that waits that many (the battle's `$CD:3E3B`) | waits that many frames |
| `tm_wait_for_handlers(pf, PF_LOOP(head, exit))` | the machine waits where the port stands, taking its interrupts there, until one has been taken; no original code runs (`native_wait_loop`, below) | yields the main thread to the driver: the next frame's handlers run |
| `tm_wait_for_handlers_held(pf, PF_LOOP_HELD(head, exit, set))` | the frame waits the main thread is in at nearly every frame's end (the field's, the sprite-object mode's, the menus' uploads): the shipping mode stops the port at the loop's head, where the idle entry waits and goes on as the port would, so a save holds it, and answers false; shadow mode waits through the original's loop and answers true (`native_wait_loop_held`; docs/MOVIES.md, "Suspended routines and owed time") | as `tm_wait_for_handlers`, and true |
| `tm_wait_line_drawn(pf, PF_LOOP(head, exit))` | inside a line handler: the machine steps where the port stands until the line is in its horizontal blank, at least a pass of the shortest such loop into it (`IDLE_EXIT_CLOCKS`) and that pass's tail after it saw the blank (`POLL_TAIL_CLOCKS`), so the writes after land no earlier than the original's; no original code runs (`native_wait_loop`, `NATIVE_WAKE_HBLANK`) | returns: the host runs the line handler once the picture's lines are drawn |
| `tm_wait_line(pf, PF_CALL_DC(...), line)` | the line left in A; the machine waits where the port stands until the picture is at the line, and only shadow mode and the tools call `$CD:3B82` (`native_wait_call`) | returns |
| `tm_entropy(pf, o, width)` | `$2137`, `$213C`, `$213D` twice, a byte or a word at a time as `$C3:02DD` reads them (a word's high bytes are `$2138`, `$213D` and `$213E`), `$2137` reading as the bus's last byte, the compare's operand's `$21`; shadow mode waits for the original's latch | four values from a 32-bit xorshift the driver seeds |
| `in_ready(pf, o)` | `$4212` bit 0 clear | true |

The host backend is `runtime/platform/native/` (`host.h`, `timing.c`,
`input.c`, and `video.c`, `audio.c` and `save.c` for the other services), compiled with `CT_PLATFORM_EXTERN` into `ct_platform_native`. The
driver calls `tm_host_start` with the game's main thread and its two handlers,
then `tm_host_frame` once a displayed frame: the vblank handler if enabled,
the main thread resumed until its `tm_wait_frame`, the line handler if
enabled. The main thread is a fiber (`runtime/port/fiber.h`), and a wait
resumes whichever fiber made it, so the menus' tasks can be fibers the main
thread runs in turn; the
acknowledges answer "pending" inside their own handler; `tm_host_picture_lines`
is where the picture ends (the line handler's line and those above it).
`ct_native` (PLAYING.md, "The native program") links it with the whole of
`game/` and runs the program (`core/program.c`'s main thread and handlers),
so far through the boot and the field's first start; `ct_play` still runs on
the emulated machine, whose interrupts need the SNES backend. `tests/port/timing_test.c` runs a made-up game on it: the
frame's order, a handler switched on or off mid-frame, a wait twenty calls
deep, the acknowledges, the budget, the parity, the generator's replay and
the pads. Of video, the host half folds every call into the observer's
picture state and draws nothing yet; audio plays the C sound driver in the
sound processor's place, and saves keep the driver's save RAM (TODO.md, "The
native program").

**The budgets, measured exactly in shadow mode.** A port that reads the beam
runs on a copy of the machine whose beam stands where the copy was last made:
after a call-out, a few instructions before the original's own latch. The
first port (the frame queue) disagreed with the original on 2 of 11,498 calls
over `fair-companion`, each where the line limit fell in those few
instructions. So the runtime gained `native_await_latch` (`port/native.h`): in
shadow mode the port, having latched the counters on its copy, waits until
the original latches them (a count the PPU keeps, `Ppu.latch_count`) and
reads what the original latched; an interrupt before the original's latch
makes the call interrupted, and an original that returns without latching is
a mismatch. In the shipping mode it lets the port's time so far pass first
(`native_sync`). The budgets have matched on every call of the library since.

**The ports.** The waits real play spent the most instructions in:

- the field's frame work (`field/script_turns.c`, `field/frames.c`): the
  objects counted down to their scripts' turns and queued (`$C0:59D9`), the
  queued turns run while the frame has time (`$C0:5A46`), and the new frames
  drawn the same way (`$C0:CA76`). Each turn and each frame's drawing is a
  call-out to its port (`$C0:5A93`, `$C0:CAD9`), so the time it takes passes
  before the budget is read again; a build with no machine calls them as C.
- the menus' tasks (`menu/tasks.c`): a task's wait for a frame or for some
  frames (`$C2:821E`, `$C2:822B`), its end (`$C2:8216`), the frame's turns
  from the first task (`$C2:81CF`) and the menus' first wait (`$C2:81B9`). The
  scheduler -- which task goes on and how -- is C over the tasks' records; the
  switch of stacks is the entries', through a pair of `machine.h` helpers
  (`m_keep_registers_to_switch`, `m_switch_back`). A host runs each task as a
  coroutine.
- the frame waits that were call-outs to the modes' wait routines (the
  battle's `$C1:007E`, the field's `$C0:EC60`) go through `tm_wait_frame`
  (`battle_wait_frame`, `field_wait_frame`), and the battle's wait for a line
  (`$CD:3B82`, from `$CD:09AC` and screen script command `$C1`) through
  `tm_wait_line`.
- the battle's wait routines themselves (`battle/wait.c`): the frame wait
  (`$C1:007E`), which asks the vblank handler for the frame's update, and the
  return to the field's (`$CF:FCFB`); the frame work's wait for the handler's
  frame (`$CD:3E7D`), with the menu's turn after it (`$CD:3E75`) and a number
  of those (`$CD:3E3B`); the start's and end's wait for their small handler
  (`$CD:044A`); and the pause the waits give a turn (`$CD:3E44`, `$CD:0036`,
  `battle/sound.c`). Each asks the handler, then calls `tm_wait_for_handlers`
  until the handler has answered; what the original did on each pass of its
  loop -- the pause's turn -- the port does between the calls. The battle's
  transfer from the main thread (`$CD:3ED2`, below) waits through them.

**Waiting inside a port.** A port that waits for an interrupt's handler
can't spin: nothing runs the handler until the port gives the machine back.
`native_wait_loop(s, head, exit, wake)` (`port/native.h`) is that giving
back, for a loop of the port's own routine whose head and exit it names. In
the shipping mode the port's fiber is suspended where the machine stands,
its time paid there, and the machine takes its interrupts at that point,
stepping a line at a time between them, until a handler has been taken
(`NATIVE_WAKE_HANDLER`) or the picture is at a line, past the vertical blank;
the original's loop never runs. (The sound driver's CPU half waits the same
way on the SPC700's answer on port 0, a pass of its poll at a time: above,
"The CPU half's waits, as built".) A line handler's wait for its line's
horizontal blank (`NATIVE_WAKE_HBLANK`, `tm_wait_line_drawn`) steps to the
blank instead, and goes on no sooner than a pass of the shortest such loop
into it (`IDLE_EXIT_CLOCKS`), as an idle entry's exit does, so that the
forced blank after it lands where the original's could and not on the line's
last dots; where the blank was seen late, behind the line's HDMA, it goes on
after the pass's tail alone, the `AND #` and `BEQ` the original still runs
after its read (`POLL_TAIL_CLOCKS`, 24; adding the whole pass there took 6% of
the field's forced blanks onto the next line). In shadow mode the original runs its loop: the
port's stretch before the wait is compared where the original first comes to
the loop's head or exit with the port's S (memory, S, the page and the bank;
the port's other registers are its own across the wait and compared where
something reads them), and the port goes on at the first such point after a
handler has run, with the handler's work taken from the original, or where
the original leaves the loop. A stretch a handler fell in is counted
interrupted, as any is; an entry the original reaches in the passes (the
pause's, on every pass) is compared on its own, nested, and the fallback
measure counts it as the port's. `tools/uncompared.py`, which the suite runs,
fails on an entry past half its calls uncompared that
`tools/uncompared_baseline.txt` doesn't list with its reason.
Outside the runtime the original's loop runs in place, and `ct_fuzz
--stop-at-call` with the loop's head ends a run there. `native_wait_call` is
the same for a call of a routine that only waits (the battle's wait for a
line): no call in the shipping mode; a call-out, counted as the port's own
work, in shadow mode.

**A transfer from the main thread.** `vid_send_vram_and_wait(pf, o, vram,
src, len)` (`video.h`) sends a buffer to VRAM from the main thread and
returns once it is there. A host copies it at once, the picture being drawn
after the frame's work (`runtime/platform/native/video.c`, the host
backend's first video call), from a host pointer: a game address, which
has no host memory behind it until the game's state is the host's own,
stops the program with the call's site, and the battlefield's change, the
one caller, passes one. The SNES backend calls the mode's routine that
queues it for the vblank handler and waits (`PF_CALL_DC`), with the arguments
where that routine takes them (`pf_snes_vram_sender`, `encodings.c`: the
battle's `$CD:3ED2` and `$CD:3ECE`, the bank in A, the source in X, VRAM in
Y, the size at `$53`). That routine is a port (`battle/uploads.c`): it waits
for a buffer queued before to go, then queues a piece a frame -- 1 KiB, or
4 KiB while the frame work is cut down to it -- and waits for each, the
vblank handler sending them (`battle_send_queued_transfer`). The
battlefield's change (`battle/battlefield_change.c`) sends its tiles and its
map through it; the original's other callers (the battlefield put back
`$CD:0453`, the effect graphics loader `$D1:ECF3`) reach the port.

**The six status reads.** The controller-busy waits (`$C2:84EC`, `$C3:0A8E`)
call `in_ready`. The other reads are idle entries -- one pass of the
original's loop, run on the machine -- which are the SNES backend's half of
a wait a host doesn't make: the line handlers' hblank waits (`$C0:ECEB`,
`$D1:FEB8`, `$C3:0530`, `$C3:0538`), whose blank `vid_raster_display` at the
handler's line already says; and the location change's and restart's vblank
race (`$C0:3B71`, `$C0:4181`). They read through `tm_snes_video_status`, and
no port does. `$CD:3B82`'s three loops were idle entries too, until
`tm_wait_line` waited in place.

**Measured.** Over the reference play (the 28 movies in shadow mode) the
original's code running in place of hand ports fell from 311.4 million
instructions to 199.0 million; over the shipping mode's movies and the long
routes, from 1,752 million to 431 million (the routes' long stretches in the
field and the menus). The routes reach their outcomes with the budgets drained as a host
drains them (`ct_drive --drain-budgets`, `$CT_DRIVE_DRAIN_BUDGETS` for
`tools/movies/route_check.py`). The battle's waits took the reference play
(35 movies by then) from 60.3 million instructions to 36.2 million; the
battle's frame work's own from 19.9 million to 1.8 million.

## Risks and open questions

- **Instruction-level ports carry the SNES with them.** Until the WRAM layout
  and CPU-register compatibility are dropped, ports keep `A`, flags and stack
  bytes around each call. The API removes hardware mechanics, not 65816
  mechanics; readable game code needs both gone, and the second is a separate
  plan.
- **Beam budgets.** `$C0:5A46` and `$C0:CA76` stop at a scanline, so how many
  objects update and how many sprite frames get built per frame depends on CPU
  speed. Settled ("Timing without a beam, as built"): the budget is
  `tm_frame_time_left`, which a host answers with "all of it", and the routes
  reach their outcomes with the budgets drained so.
- **Real-time battles and frame shifts.** Any backend that changes how long
  something takes (APU-direct uploads, no beam) moves input relative to game
  state, which changes fights. Validation past P1 compares end states and
  scenes, not frames.
- **Driver state in WRAM.** The driver keeps slots and song state at
  `$1E00-$1EFF`. Whether other code reads it, and so whether APU-direct must
  maintain it, isn't known.
- **Unknown line-timed uses.** The line handlers are known now (all three
  blank the picture after line 211; [Timing without a
  beam](#timing-without-a-beam-the-native-backend)), and so is what the battle
  does after `$CD:3B82` (it runs the screen script channels), though not why it
  waits. The HDMA channels behind the battle's scroll tables and the field's
  quadrilateral are still in unported code, so the per-line effect list may
  grow.
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
