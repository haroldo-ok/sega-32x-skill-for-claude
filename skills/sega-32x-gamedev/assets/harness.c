/*
 * harness.c - headless libretro host to drive a Sega 32X ROM inside PicoDrive.
 *
 * Boots the ROM, runs a scripted controller sequence, and dumps frames as PPM
 * so run_tests.py can assert on them (black-screen guard, transitions, crops).
 *
 * Build (see tests/Makefile):
 *   cc -O2 -std=c11 -o harness harness.c -ldl
 *
 * Run:
 *   ./harness <core.so> <rom.32x> <script.txt> <outdir>
 *
 * Script language (one command per line, '#' = comment):
 *   run <n>            run n frames
 *   press <btn> <n>    hold btn for n frames, then release
 *   hold <btn>         press and keep held
 *   release <btn>      release a held button
 *   port <0|1>         choose which pad subsequent commands drive
 *   shot <name>        write <outdir>/<name>.ppm from the latest frame
 * Buttons: up down left right a b c start x y z mode
 *
 * This is a minimal, self-contained libretro frontend: it declares only the
 * subset of the libretro ABI it needs, so no libretro.h is required.
 * NOTE: the Genesis/32X button->libretro-id mapping is core-dependent; adjust
 * the BTN table below if a press does not register with your PicoDrive build.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- minimal libretro ABI ------------------------------------------------ */
struct retro_game_info { const char *path; const void *data; size_t size; const char *meta; };
struct retro_game_geometry { unsigned base_width, base_height, max_width, max_height; float aspect_ratio; };
struct retro_system_timing { double fps; double sample_rate; };
struct retro_system_av_info { struct retro_game_geometry geometry; struct retro_system_timing timing; };

typedef bool (*env_t)(unsigned cmd, void *data);
typedef void (*video_t)(const void *data, unsigned w, unsigned h, size_t pitch);
typedef void (*audio_t)(int16_t l, int16_t r);
typedef size_t (*audio_batch_t)(const int16_t *data, size_t frames);
typedef void (*poll_t)(void);
typedef int16_t (*input_t)(unsigned port, unsigned device, unsigned index, unsigned id);

#define ENV_GET_CAN_DUPE            3
#define ENV_GET_SYSTEM_DIRECTORY    9
#define ENV_SET_PIXEL_FORMAT        10
#define ENV_GET_VARIABLE            15
#define ENV_GET_VARIABLE_UPDATE     17
#define ENV_SET_SUPPORT_NO_GAME     18
#define ENV_GET_SAVE_DIRECTORY      31
#define ENV_GET_INPUT_BITMASKS      51
#define ENV_GET_CORE_OPTIONS_VERSION 52

#define DEVICE_JOYPAD 1
/* libretro joypad ids */
#define ID_B 0
#define ID_Y 1
#define ID_START 3
#define ID_UP 4
#define ID_DOWN 5
#define ID_LEFT 6
#define ID_RIGHT 7
#define ID_A 8
#define ID_X 9
#define ID_L 10
#define ID_R 11

/* name -> libretro id. Genesis mapping is core-dependent; tweak if needed. */
static const struct { const char *name; unsigned id; } BTN[] = {
    {"up", ID_UP}, {"down", ID_DOWN}, {"left", ID_LEFT}, {"right", ID_RIGHT},
    {"a", ID_A}, {"b", ID_B}, {"c", ID_Y}, {"start", ID_START},
    {"x", ID_X}, {"y", ID_L}, {"z", ID_R}, {"mode", 2 /*SELECT*/},
};

/* ---- core symbols -------------------------------------------------------- */
static void  (*retro_init)(void);
static void  (*retro_deinit)(void);
static void  (*retro_set_environment)(env_t);
static void  (*retro_set_video_refresh)(video_t);
static void  (*retro_set_audio_sample)(audio_t);
static void  (*retro_set_audio_sample_batch)(audio_batch_t);
static void  (*retro_set_input_poll)(poll_t);
static void  (*retro_set_input_state)(input_t);
static bool  (*retro_load_game)(const struct retro_game_info *);
static void  (*retro_run)(void);
static void  (*retro_unload_game)(void);
static void  (*retro_get_system_av_info)(struct retro_system_av_info *);

/* ---- state --------------------------------------------------------------- */
static int      pixfmt = 0;            /* 0=0RGB1555, 1=XRGB8888, 2=RGB565 */
static uint32_t pad[2];                /* held button bitmask per port     */
static char     sysdir[] = ".";

#define MAXW 512
#define MAXH 512
static uint8_t  rgb[MAXW * MAXH * 3];
static unsigned fw, fh;
static int      have_frame;

static bool cb_env(unsigned cmd, void *data)
{
    switch (cmd & 0xFFFF) {
    case ENV_SET_PIXEL_FORMAT:
        pixfmt = *(const int *)data;
        return pixfmt >= 0 && pixfmt <= 2;
    case ENV_GET_CAN_DUPE:
        *(bool *)data = true; return true;
    case ENV_GET_SYSTEM_DIRECTORY:
    case ENV_GET_SAVE_DIRECTORY:
        *(const char **)data = sysdir; return true;
    case ENV_GET_VARIABLE:
        return false;                 /* use each option's default */
    case ENV_GET_VARIABLE_UPDATE:
        *(bool *)data = false; return true;
    case ENV_GET_INPUT_BITMASKS:
        return false;
    case ENV_GET_CORE_OPTIONS_VERSION:
        *(unsigned *)data = 0; return true;
    case ENV_SET_SUPPORT_NO_GAME:
        return true;
    default:
        /* accept SET_* notifications, reject unknown GETs */
        return false;
    }
}

static void store_frame(const void *src, unsigned w, unsigned h, size_t pitch)
{
    if (!src || w > MAXW || h > MAXH) return;
    const uint8_t *base = src;
    for (unsigned y = 0; y < h; y++) {
        for (unsigned x = 0; x < w; x++) {
            unsigned r, g, b;
            if (pixfmt == 1) {                     /* XRGB8888 */
                uint32_t v = ((const uint32_t *)(base + y * pitch))[x];
                r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255;
            } else {
                uint16_t v = ((const uint16_t *)(base + y * pitch))[x];
                if (pixfmt == 2) {                 /* RGB565 */
                    r = ((v >> 11) & 31) * 255 / 31;
                    g = ((v >> 5) & 63) * 255 / 63;
                    b = (v & 31) * 255 / 31;
                } else {                           /* 0RGB1555 */
                    r = ((v >> 10) & 31) * 255 / 31;
                    g = ((v >> 5) & 31) * 255 / 31;
                    b = (v & 31) * 255 / 31;
                }
            }
            uint8_t *p = &rgb[(y * w + x) * 3];
            p[0] = r; p[1] = g; p[2] = b;
        }
    }
    fw = w; fh = h; have_frame = 1;
}

static void cb_video(const void *data, unsigned w, unsigned h, size_t pitch)
{
    if (data && data != (const void *)-1) store_frame(data, w, h, pitch);
}
static void cb_audio(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t cb_audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void cb_poll(void) {}
static int16_t cb_input(unsigned port, unsigned dev, unsigned idx, unsigned id)
{
    if (port > 1 || dev != DEVICE_JOYPAD || idx != 0) return 0;
    return (pad[port] >> id) & 1;
}

static void *load_sym(void *h, const char *n)
{
    void *p = dlsym(h, n);
    if (!p) { fprintf(stderr, "missing symbol %s\n", n); exit(2); }
    return p;
}

static void write_ppm(const char *dir, const char *name)
{
    if (!have_frame) { fprintf(stderr, "shot %s: no frame yet\n", name); exit(3); }
    char path[1024];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(3); }
    fprintf(f, "P6\n%u %u\n255\n", fw, fh);
    fwrite(rgb, 1, (size_t)fw * fh * 3, f);
    fclose(f);
}

static unsigned btn_id(const char *name)
{
    for (size_t i = 0; i < sizeof BTN / sizeof BTN[0]; i++)
        if (!strcmp(name, BTN[i].name)) return BTN[i].id;
    fprintf(stderr, "unknown button: %s\n", name); exit(4);
}

int main(int argc, char **argv)
{
    if (argc != 5) {
        fprintf(stderr, "usage: %s core.so rom.32x script outdir\n", argv[0]);
        return 2;
    }
    const char *core_path = argv[1], *rom_path = argv[2];
    const char *script = argv[3], *outdir = argv[4];

    void *h = dlopen(core_path, RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }

    retro_init          = load_sym(h, "retro_init");
    retro_deinit        = load_sym(h, "retro_deinit");
    retro_set_environment = load_sym(h, "retro_set_environment");
    retro_set_video_refresh = load_sym(h, "retro_set_video_refresh");
    retro_set_audio_sample = load_sym(h, "retro_set_audio_sample");
    retro_set_audio_sample_batch = load_sym(h, "retro_set_audio_sample_batch");
    retro_set_input_poll = load_sym(h, "retro_set_input_poll");
    retro_set_input_state = load_sym(h, "retro_set_input_state");
    retro_load_game     = load_sym(h, "retro_load_game");
    retro_run           = load_sym(h, "retro_run");
    retro_unload_game   = load_sym(h, "retro_unload_game");
    retro_get_system_av_info = load_sym(h, "retro_get_system_av_info");

    retro_set_environment(cb_env);
    retro_set_video_refresh(cb_video);
    retro_set_audio_sample(cb_audio);
    retro_set_audio_sample_batch(cb_audio_batch);
    retro_set_input_poll(cb_poll);
    retro_set_input_state(cb_input);
    retro_init();

    FILE *rf = fopen(rom_path, "rb");
    if (!rf) { perror(rom_path); return 2; }
    fseek(rf, 0, SEEK_END); long sz = ftell(rf); fseek(rf, 0, SEEK_SET);
    void *rom = malloc(sz);
    if (fread(rom, 1, sz, rf) != (size_t)sz) { perror("read rom"); return 2; }
    fclose(rf);

    struct retro_game_info gi = { rom_path, rom, (size_t)sz, NULL };
    if (!retro_load_game(&gi)) { fprintf(stderr, "core rejected ROM\n"); return 2; }
    struct retro_system_av_info av; retro_get_system_av_info(&av);

    FILE *sf = fopen(script, "r");
    if (!sf) { perror(script); return 2; }
    char line[256]; unsigned cur_port = 0;
    while (fgets(line, sizeof line, sf)) {
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        char cmd[32], arg[32]; int n;
        int k = sscanf(line, "%31s %31s %d", cmd, arg, &n);
        if (k <= 0) continue;
        if (!strcmp(cmd, "run") && k >= 2) {
            int frames = atoi(arg);
            for (int i = 0; i < frames; i++) retro_run();
        } else if (!strcmp(cmd, "press") && k >= 3) {
            unsigned id = btn_id(arg);
            pad[cur_port] |= 1u << id;
            for (int i = 0; i < n; i++) retro_run();
            pad[cur_port] &= ~(1u << id);
        } else if (!strcmp(cmd, "hold") && k >= 2) {
            pad[cur_port] |= 1u << btn_id(arg);
        } else if (!strcmp(cmd, "release") && k >= 2) {
            pad[cur_port] &= ~(1u << btn_id(arg));
        } else if (!strcmp(cmd, "port") && k >= 2) {
            cur_port = (unsigned)atoi(arg) & 1;
        } else if (!strcmp(cmd, "shot") && k >= 2) {
            write_ppm(outdir, arg);
        } else {
            fprintf(stderr, "bad script line: %s", line); return 4;
        }
    }
    fclose(sf);

    retro_unload_game();
    retro_deinit();
    free(rom);
    return 0;
}
