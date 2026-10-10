/* ct_oracle: headless libretro frontend that runs the original ROM from an
 * input movie and records per-frame state traces. See docs/ORACLE.md. */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dynlib.h"
#include "libretro.h"
#include "media.h"
#include "movie.h"
#include "ppu_state.h"
#include "trace.h"

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define make_dir(p) mkdir(p, 0755)
#endif

#define MAX_OPTIONS 256

typedef struct {
    void (*init)(void);
    void (*deinit)(void);
    unsigned (*api_version)(void);
    void (*get_system_info)(struct retro_system_info *);
    void (*get_system_av_info)(struct retro_system_av_info *);
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*set_controller_port_device)(unsigned, unsigned);
    void (*run)(void);
    bool (*load_game)(const struct retro_game_info *);
    void (*unload_game)(void);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
    size_t (*serialize_size)(void);
    bool (*serialize)(void *, size_t);
} core_api;

static struct {
    core_api api;
    const char *work_dir;
    bool verbose;
    enum retro_pixel_format pixel_format;

    struct {
        char *key, *value;
    } options[MAX_OPTIONS];
    int option_count;
    char *overrides[MAX_OPTIONS];
    int override_count;

    uint16_t input;  /* JOY1 order */
    uint16_t input2; /* the second controller's, JOY2 */

    uint32_t *fb;
    unsigned fb_width, fb_height, fb_cap;

    ct_wav wav;
} g = {.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555};

/* ---- environment ------------------------------------------------------ */

static void log_cb(enum retro_log_level level, const char *fmt, ...) {
    if (level == RETRO_LOG_DEBUG && !g.verbose)
        return;
    static const char *names[] = {"debug", "info", "warn", "error"};
    fprintf(stderr, "[core %s] ", level <= RETRO_LOG_ERROR ? names[level] : "?");
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static char *dup_str(const char *s) {
    size_t n = strlen(s) + 1;
    return memcpy(malloc(n), s, n);
}

static void set_option_default(const char *key, const char *value) {
    for (int i = 0; i < g.option_count; i++) {
        if (strcmp(g.options[i].key, key) == 0)
            return;
    }
    if (g.option_count == MAX_OPTIONS)
        return;
    g.options[g.option_count].key = dup_str(key);
    g.options[g.option_count].value = dup_str(value);
    g.option_count++;
}

static const char *get_option(const char *key) {
    size_t key_len = strlen(key);
    for (int i = 0; i < g.override_count; i++) {
        if (strncmp(g.overrides[i], key, key_len) == 0 && g.overrides[i][key_len] == '=')
            return g.overrides[i] + key_len + 1;
    }
    for (int i = 0; i < g.option_count; i++) {
        if (strcmp(g.options[i].key, key) == 0)
            return g.options[i].value;
    }
    return NULL;
}

/* Legacy variables: value is "Description; default|other|...". */
static void register_variables(const struct retro_variable *vars) {
    for (; vars->key; vars++) {
        const char *semi = strchr(vars->value, ';');
        if (!semi)
            continue;
        const char *start = semi + 1;
        while (*start == ' ')
            start++;
        const char *end = strchr(start, '|');
        size_t len = end ? (size_t)(end - start) : strlen(start);
        char *value = malloc(len + 1);
        memcpy(value, start, len);
        value[len] = 0;
        set_option_default(vars->key, value);
        free(value);
    }
}

static void print_memory_maps(const struct retro_memory_map *map) {
    fprintf(stderr, "memory maps: %u descriptors\n", map->num_descriptors);
    for (unsigned i = 0; i < map->num_descriptors; i++) {
        const struct retro_memory_descriptor *d = &map->descriptors[i];
        fprintf(stderr, "  [%2u] flags=%08llx start=%06zx select=%06zx disconnect=%06zx len=%06zx offset=%06zx name=%s\n",
                i, (unsigned long long)d->flags, d->start, d->select, d->disconnect, d->len, d->offset,
                d->addrspace ? d->addrspace : "");
    }
}

static bool environment_cb(unsigned cmd, void *data) {
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        enum retro_pixel_format fmt = *(const enum retro_pixel_format *)data;
        if (fmt != RETRO_PIXEL_FORMAT_XRGB8888 && fmt != RETRO_PIXEL_FORMAT_RGB565 &&
            fmt != RETRO_PIXEL_FORMAT_0RGB1555)
            return false;
        g.pixel_format = fmt;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = g.work_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0; /* legacy SET_VARIABLES only */
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        register_variables(data);
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *var = data;
        var->value = get_option(var->key);
        if (g.verbose)
            fprintf(stderr, "option %s = %s\n", var->key, var->value ? var->value : "(unset)");
        return var->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        if (g.verbose)
            print_memory_maps(data);
        return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        fprintf(stderr, "[core message] %s\n", ((const struct retro_message *)data)->msg);
        return true;
    case RETRO_ENVIRONMENT_GET_OVERSCAN:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
        return true;
    default:
        if (g.verbose)
            fprintf(stderr, "environment: unhandled command %u\n", cmd & 0xFFFF);
        return false;
    }
}

/* ---- video, audio, input ---------------------------------------------- */

static void video_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
    if (!data)
        return; /* dupe: keep previous frame */
    if (width * height > g.fb_cap) {
        g.fb_cap = width * height;
        g.fb = realloc(g.fb, g.fb_cap * sizeof *g.fb);
    }
    g.fb_width = width;
    g.fb_height = height;
    for (unsigned y = 0; y < height; y++) {
        const uint8_t *row = (const uint8_t *)data + y * pitch;
        uint32_t *out = g.fb + y * width;
        for (unsigned x = 0; x < width; x++) {
            uint32_t c;
            if (g.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) {
                uint32_t p;
                memcpy(&p, row + x * 4, 4);
                c = p & 0x00FFFFFF;
            } else {
                uint16_t p;
                memcpy(&p, row + x * 2, 2);
                unsigned r, gr, b;
                if (g.pixel_format == RETRO_PIXEL_FORMAT_RGB565) {
                    r = (p >> 11) & 0x1F;
                    gr = (p >> 5) & 0x3F;
                    b = p & 0x1F;
                    gr = (gr << 2) | (gr >> 4);
                } else {
                    r = (p >> 10) & 0x1F;
                    gr = (p >> 5) & 0x1F;
                    b = p & 0x1F;
                    gr = (gr << 3) | (gr >> 2);
                }
                r = (r << 3) | (r >> 2);
                b = (b << 3) | (b >> 2);
                c = (r << 16) | (gr << 8) | b;
            }
            out[x] = c;
        }
    }
}

static void audio_sample_cb(int16_t left, int16_t right) {
    int16_t s[2] = {left, right};
    ct_wav_write(&g.wav, s, 1);
}

static size_t audio_batch_cb(const int16_t *data, size_t frames) {
    ct_wav_write(&g.wav, data, frames);
    return frames;
}

static void input_poll_cb(void) {}

/* libretro joypad ids B,Y,SELECT,START,UP,DOWN,LEFT,RIGHT,A,X,L,R (0..11)
 * map to JOY1 bits 15..4, i.e. bit = 15 - id. */
static int16_t input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id) {
    (void)index;
    if (port > 1 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD)
        return 0;
    uint16_t input = port ? g.input2 : g.input;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK) {
        int16_t mask = 0;
        for (unsigned i = 0; i < 12; i++) {
            if (input & (1u << (15 - i)))
                mask |= (int16_t)(1 << i);
        }
        return mask;
    }
    return id < 12 ? (input >> (15 - id)) & 1 : 0;
}

/* ---- setup ------------------------------------------------------------ */

static bool load_core(const char *path) {
    ct_dynlib *lib = ct_dynlib_open(path);
    if (!lib) {
        fprintf(stderr, "cannot load core %s: %s\n", path, ct_dynlib_error());
        return false;
    }
#define SYM(field, name)                                                                                           \
    if (!(g.api.field = ct_dynlib_sym(lib, name))) {                                                               \
        fprintf(stderr, "core %s: missing %s\n", path, name);                                                      \
        return false;                                                                                              \
    }
    SYM(init, "retro_init");
    SYM(deinit, "retro_deinit");
    SYM(api_version, "retro_api_version");
    SYM(get_system_info, "retro_get_system_info");
    SYM(get_system_av_info, "retro_get_system_av_info");
    SYM(set_environment, "retro_set_environment");
    SYM(set_video_refresh, "retro_set_video_refresh");
    SYM(set_audio_sample, "retro_set_audio_sample");
    SYM(set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(set_input_poll, "retro_set_input_poll");
    SYM(set_input_state, "retro_set_input_state");
    SYM(set_controller_port_device, "retro_set_controller_port_device");
    SYM(run, "retro_run");
    SYM(load_game, "retro_load_game");
    SYM(unload_game, "retro_unload_game");
    SYM(get_memory_data, "retro_get_memory_data");
    SYM(get_memory_size, "retro_get_memory_size");
    SYM(serialize_size, "retro_serialize_size");
    SYM(serialize, "retro_serialize");
#undef SYM
    return true;
}

static uint8_t *read_file(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n > 0 ? n : 1);
    if (n < 0 || fread(data, 1, n, f) != (size_t)n) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return data;
}

static const char *default_work_dir(void) {
    static char buf[1024];
    const char *tmp = getenv("TMPDIR");
    if (!tmp)
        tmp = getenv("TEMP");
    if (!tmp)
        tmp = getenv("TMP");
    if (!tmp)
        tmp = "/tmp";
    snprintf(buf, sizeof buf, "%s/ct_oracle", tmp);
    return buf;
}

static void usage(void) {
    fprintf(stderr,
            "usage: ct_oracle --core <core> --rom <rom> --frames N [options]\n"
            "\n"
            "  --movie <file.ctm>       input movie (default: no input)\n"
            "  --trace <out.cttr>       write a per-frame state trace\n"
            "  --trace-from F           first frame to record (default 0)\n"
            "  --trace-to F             last frame to record, inclusive (default: last frame)\n"
            "  --shot-every N           write a PNG every N frames (requires --shot-dir)\n"
            "  --shot-dir <dir>         directory for PNG screenshots\n"
            "  --wav <out.wav>          record audio\n"
            "  --sram <save.srm>        start with this cartridge save in save RAM\n"
            "  --option key=value       override a core option (repeatable)\n"
            "  --no-ppu-state           don't extract CGRAM/OAM from savestates\n"
            "  --work-dir <dir>         core system/save directory (default: <temp>/ct_oracle)\n"
            "  --verbose                log core options, memory maps and debug output\n");
}

typedef struct {
    uint8_t id;
    const char *name;
    unsigned libretro_id;
} region_source;

static const region_source region_sources[] = {
    {CT_REGION_WRAM, "WRAM", RETRO_MEMORY_SYSTEM_RAM},
    {CT_REGION_VRAM, "VRAM", RETRO_MEMORY_VIDEO_RAM},
    {CT_REGION_SRAM, "SRAM", RETRO_MEMORY_SAVE_RAM},
};
#define REGION_SOURCES (int)(sizeof region_sources / sizeof region_sources[0])

int main(int argc, char **argv) {
    const char *core_path = NULL, *rom_path = NULL, *movie_path = NULL, *trace_path = NULL;
    const char *shot_dir = NULL, *wav_path = NULL, *sram_path = NULL;
    long frames = -1, trace_from = 0, trace_to = -1, shot_every = 0;
    bool ppu_state = true;
    g.work_dir = default_work_dir();

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define ARG(name) (strcmp(a, name) == 0 && v && (i++, 1))
        if (ARG("--core"))
            core_path = v;
        else if (ARG("--rom"))
            rom_path = v;
        else if (ARG("--frames"))
            frames = strtol(v, NULL, 10);
        else if (ARG("--movie"))
            movie_path = v;
        else if (ARG("--trace"))
            trace_path = v;
        else if (ARG("--trace-from"))
            trace_from = strtol(v, NULL, 10);
        else if (ARG("--trace-to"))
            trace_to = strtol(v, NULL, 10);
        else if (ARG("--shot-every"))
            shot_every = strtol(v, NULL, 10);
        else if (ARG("--shot-dir"))
            shot_dir = v;
        else if (ARG("--wav"))
            wav_path = v;
        else if (ARG("--sram"))
            sram_path = v;
        else if (ARG("--work-dir"))
            g.work_dir = v;
        else if (ARG("--option")) {
            if (!strchr(v, '=') || g.override_count == MAX_OPTIONS) {
                usage();
                return 2;
            }
            g.overrides[g.override_count++] = (char *)v;
        } else if (strcmp(a, "--verbose") == 0)
            g.verbose = true;
        else if (strcmp(a, "--no-ppu-state") == 0)
            ppu_state = false;
        else {
            fprintf(stderr, "unknown or incomplete argument: %s\n", a);
            usage();
            return 2;
        }
#undef ARG
    }
    if (!core_path || !rom_path || frames < 0 || (shot_every > 0 && !shot_dir)) {
        usage();
        return 2;
    }
    if (trace_to < 0)
        trace_to = frames - 1;

    ct_movie movie = {0};
    if (movie_path && ct_movie_load(&movie, movie_path) != 0)
        return 1;

    size_t rom_len;
    uint8_t *rom = read_file(rom_path, &rom_len);
    if (!rom) {
        fprintf(stderr, "cannot read ROM %s\n", rom_path);
        return 1;
    }
    size_t header = rom_len % 0x8000 == 0x200 ? 0x200 : 0;
    uint32_t rom_crc = ct_crc32(rom + header, rom_len - header);

    make_dir(g.work_dir);
    if (shot_dir)
        make_dir(shot_dir);

    if (!load_core(core_path))
        return 1;
    core_api *api = &g.api;
    if (api->api_version() != RETRO_API_VERSION) {
        fprintf(stderr, "core API version %u, expected %u\n", api->api_version(), RETRO_API_VERSION);
        return 1;
    }

    api->set_environment(environment_cb);
    api->init();
    api->set_video_refresh(video_cb);
    api->set_audio_sample(audio_sample_cb);
    api->set_audio_sample_batch(audio_batch_cb);
    api->set_input_poll(input_poll_cb);
    api->set_input_state(input_state_cb);

    struct retro_system_info sys = {0};
    api->get_system_info(&sys);
    struct retro_game_info game = {.path = rom_path, .data = rom, .size = rom_len};
    if (!api->load_game(&game)) {
        fprintf(stderr, "core rejected ROM %s\n", rom_path);
        return 1;
    }
    api->set_controller_port_device(0, RETRO_DEVICE_JOYPAD);

    /* A cartridge save goes into the core's save RAM before the first frame,
     * the way a frontend loads a .srm. */
    if (sram_path) {
        size_t sram_len;
        uint8_t *sram = read_file(sram_path, &sram_len);
        uint8_t *dest = api->get_memory_data(RETRO_MEMORY_SAVE_RAM);
        size_t dest_len = api->get_memory_size(RETRO_MEMORY_SAVE_RAM);
        if (!sram) {
            fprintf(stderr, "cannot read %s\n", sram_path);
            return 1;
        }
        if (!dest || sram_len != dest_len) {
            fprintf(stderr, "%s is %zu bytes; the core's save RAM is %zu bytes\n", sram_path, sram_len, dest_len);
            return 1;
        }
        memcpy(dest, sram, sram_len);
        free(sram);
    }
    if (movie.has_sram) {
        const uint8_t *sram = api->get_memory_data(RETRO_MEMORY_SAVE_RAM);
        size_t sram_len = api->get_memory_size(RETRO_MEMORY_SAVE_RAM);
        if (!sram_path || !sram || ct_crc32(sram, sram_len) != movie.sram_crc) {
            fprintf(stderr, "%s starts from SRAM with CRC-32 %08X; pass that save with --sram\n", movie_path,
                    (unsigned)movie.sram_crc);
            return 1;
        }
    }

    struct retro_system_av_info av = {0};
    api->get_system_av_info(&av);
    fprintf(stderr, "core %s %s, rom crc32 %08X, %.4f fps, %.0f Hz audio\n", sys.library_name,
            sys.library_version, rom_crc, av.timing.fps, av.timing.sample_rate);

    if (wav_path && ct_wav_open(&g.wav, wav_path, (uint32_t)(av.timing.sample_rate + 0.5)) != 0) {
        fprintf(stderr, "cannot open %s\n", wav_path);
        return 1;
    }

    ct_region_desc regions[REGION_SOURCES + 2];
    unsigned region_libretro[REGION_SOURCES + 2];
    int region_count = 0;
    for (int i = 0; i < REGION_SOURCES; i++) {
        size_t size = api->get_memory_size(region_sources[i].libretro_id);
        if (!size || !api->get_memory_data(region_sources[i].libretro_id)) {
            fprintf(stderr, "warning: core does not expose %s; omitted from trace\n", region_sources[i].name);
            continue;
        }
        if (g.verbose)
            fprintf(stderr, "region %s: %zu bytes\n", region_sources[i].name, size);
        regions[region_count] = (ct_region_desc){region_sources[i].id, region_sources[i].name, (uint32_t)size};
        region_libretro[region_count] = region_sources[i].libretro_id;
        region_count++;
    }

    /* CGRAM and OAM come from savestates; see ppu_state.h. */
    static uint8_t cgram[CT_CGRAM_SIZE], oam[CT_OAM_SIZE];
    uint8_t *state = NULL;
    size_t state_cap = 0;
    ct_ppu_state_kind ppu_kind = ppu_state && trace_path ? ct_ppu_state_detect(sys.library_name) : CT_PPU_STATE_NONE;
    if (ppu_kind != CT_PPU_STATE_NONE) {
        regions[region_count] = (ct_region_desc){CT_REGION_CGRAM, "CGRAM", CT_CGRAM_SIZE};
        region_libretro[region_count++] = ~0u;
        regions[region_count] = (ct_region_desc){CT_REGION_OAM, "OAM", CT_OAM_SIZE};
        region_libretro[region_count++] = ~0u;
    } else if (trace_path) {
        ppu_kind = CT_PPU_STATE_NONE;
        fprintf(stderr, "warning: %s; CGRAM/OAM omitted from trace\n",
                ppu_state ? "no CGRAM/OAM savestate adapter for this core" : "--no-ppu-state given");
    }

    ct_trace_writer *trace = NULL;
    if (trace_path) {
        char producer[256];
        snprintf(producer, sizeof producer, "ct_oracle; core %s %s", sys.library_name, sys.library_version);
        trace = ct_trace_open(trace_path, rom_crc, producer, regions, region_count);
        if (!trace) {
            fprintf(stderr, "cannot open %s\n", trace_path);
            return 1;
        }
    }

    clock_t started = clock();
    const uint8_t *region_data[REGION_SOURCES + 2];
    for (long frame = 0; frame < frames; frame++) {
        g.input = ct_movie_buttons(&movie, (uint32_t)frame);
        g.input2 = ct_movie_buttons2(&movie, (uint32_t)frame);
        api->run();

        if (trace && frame >= trace_from && frame <= trace_to) {
            if (ppu_kind != CT_PPU_STATE_NONE) {
                size_t len = api->serialize_size();
                if (len > state_cap) {
                    state_cap = len;
                    state = realloc(state, state_cap);
                }
                const char *why = "retro_serialize failed";
                if (!api->serialize(state, len) ||
                    ct_ppu_state_extract(ppu_kind, state, len, cgram, oam, &why) != 0) {
                    fprintf(stderr, "frame %ld: CGRAM/OAM extraction failed: %s\n", frame, why);
                    return 1;
                }
            }
            for (int r = 0; r < region_count; r++) {
                if (region_libretro[r] != ~0u)
                    region_data[r] = api->get_memory_data(region_libretro[r]);
                else
                    region_data[r] = regions[r].id == CT_REGION_CGRAM ? cgram : oam;
            }
            uint64_t fb_hash = g.fb ? ct_framebuffer_hash(g.fb, (uint16_t)g.fb_width, (uint16_t)g.fb_height) : 0;
            if (ct_trace_write_frame(trace, (uint32_t)frame, g.input, region_data, (uint16_t)g.fb_width,
                                     (uint16_t)g.fb_height, fb_hash) != 0) {
                fprintf(stderr, "error writing trace\n");
                return 1;
            }
        }
        if (shot_every > 0 && frame % shot_every == 0 && g.fb) {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame_%06ld.png", shot_dir, frame);
            if (ct_png_write(path, g.fb, g.fb_width, g.fb_height) != 0)
                fprintf(stderr, "warning: cannot write %s\n", path);
        }
    }
    double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
    fprintf(stderr, "ran %ld frames in %.2fs (%.0f fps)\n", frames, seconds, seconds > 0 ? frames / seconds : 0.0);

    int rc = 0;
    if (ct_trace_close(trace) != 0) {
        fprintf(stderr, "error closing trace\n");
        rc = 1;
    }
    if (ct_wav_close(&g.wav) != 0) {
        fprintf(stderr, "error closing wav\n");
        rc = 1;
    }
    api->unload_game();
    api->deinit();
    ct_movie_free(&movie);
    free(rom);
    free(state);
    free(g.fb);
    return rc;
}
