/* Headless driver: boot a ROM, optionally type keys, print or dump the LCD.
 * usage: headless ROM [--secs N] [--keys "2ND,1,+,2,ENTER"] [--pgm out.pgm] [--ascii]
 *                     [--state FILE] [--save FILE]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ti84.h"
#include "keytable.h"

static calc_t calc;

static int find_key(const char *name, int *g, int *b) {
    for (int i = 0; i < TIKEY_COUNT; i++)
        if (!strcasecmp(TIKEYS[i].name, name)) { *g = TIKEYS[i].group; *b = TIKEYS[i].bit; return 1; }
    return 0;
}

static int watching = 0;

static void watch_line(calc_t *c) {
    int nz = 0;
    for (int i = 0; i < LCD_MEM_W * LCD_H; i++) if (c->lcd.display[i]) nz++;
    printf("t=%6.2fs pc=%04X halt=%d int=%02X on=%d lcd.active=%d ctr=%2d wl=%d nz=%4d cmd=%ld data=%ld drop=%ld banks=%d/%d/%d/%d\n",
           c->elapsed, c->cpu.pc, c->cpu.halted, c->int_active, c->on_pressed, c->lcd.active,
           c->lcd.contrast, c->lcd.word_len, nz, c->dbg_cmd, c->dbg_data, c->dbg_dropped,
           c->normal[0].page, c->normal[1].page, c->normal[2].page, c->normal[3].page);
}

static void run_seconds(calc_t *c, double s) {
    /* the CPU speed can change mid-run, so step in small slices */
    double left = s, since = 0;
    while (left > 0) {
        double slice = left > 0.01 ? 0.01 : left;
        calc_run(c, (int64_t)(slice * c->freq));
        calc_frame(c);                       /* as a display refresh would */
        left -= slice;
        since += slice;
        if (watching && since >= 0.25) { since = 0; watch_line(c); }
    }
}

static void print_ascii(calc_t *c) {
    uint8_t px[LCD_W * LCD_H];
    calc_lcd_gray(c, px);
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            int v = px[y * LCD_W + x];
            putchar(v > 190 ? '#' : v > 120 ? '+' : v > 60 ? '.' : ' ');
        }
        putchar('\n');
    }
    printf("lcd: active=%d contrast=%d word_len=%d cursor=%d  pc=%04X t=%lld elapsed=%.2fs\n",
           c->lcd.active, c->lcd.contrast, c->lcd.word_len, c->lcd.cursor_mode,
           c->cpu.pc, (long long)c->cpu.t, c->elapsed);
}

static void write_pgm(calc_t *c, const char *path) {
    uint8_t px[LCD_W * LCD_H];
    calc_lcd_gray(c, px);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P5\n%d %d\n255\n", LCD_W, LCD_H);
    for (int i = 0; i < LCD_W * LCD_H; i++) fputc(255 - px[i], f);   /* dark = ink */
    fclose(f);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: headless ROM [--secs N] [--keys LIST] [--pgm F]\n"); return 2; }
    const char *rom = argv[1], *pgm = 0, *keys = 0, *state = 0, *save = 0;
    double secs = 3.0, after = 0.4;
    int ascii_out = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--secs") && i + 1 < argc) secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) keys = argv[++i];
        else if (!strcmp(argv[i], "--pgm") && i + 1 < argc) pgm = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) state = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--after") && i + 1 < argc) after = atof(argv[++i]);
        else if (!strcmp(argv[i], "--quiet")) ascii_out = 0;
    }
    int rc = calc_init(&calc, rom);
    if (rc) { fprintf(stderr, "cannot load ROM %s (%d)\n", rom, rc); return 1; }
    if (state && calc_load_state(&calc, state) == 0) {
        printf("loaded state %s\n", state);
        memset(calc.dbg_pchist, 0, sizeof calc.dbg_pchist);
        memset(calc.dbg_in, 0, sizeof calc.dbg_in);
        memset(calc.dbg_out, 0, sizeof calc.dbg_out);
    }

    watching = getenv("TI84_WATCH") != 0;
    run_seconds(&calc, secs);

    if (keys) {
        char buf[512];
        snprintf(buf, sizeof buf, "%s", keys);
        for (char *tok = strtok(buf, ","); tok; tok = strtok(0, ",")) {
            int g, b;
            while (*tok == ' ') tok++;
            if (!find_key(tok, &g, &b)) { fprintf(stderr, "unknown key '%s'\n", tok); continue; }
            calc_key(&calc, g, b, 1);
            run_seconds(&calc, 0.10);
            calc_key(&calc, g, b, 0);
            run_seconds(&calc, 0.15);
        }
        run_seconds(&calc, after);
    }

    if (getenv("TI84_HIST")) {
        unsigned best[8] = {0}; int bi[8] = {0};
        for (int i = 0; i < 256; i++)
            for (int k = 0; k < 8; k++)
                if (calc.dbg_pchist[i] > best[k]) {
                    for (int j = 7; j > k; j--) { best[j] = best[j-1]; bi[j] = bi[j-1]; }
                    best[k] = calc.dbg_pchist[i]; bi[k] = i; break;
                }
        for (int k = 0; k < 8; k++) printf("pc %02XXX: %u\n", bi[k], best[k]);
        for (int i = 0; i < 256; i++)
            if (calc.dbg_in[i] || calc.dbg_out[i])
                printf("port %02X: in=%u out=%u\n", i, calc.dbg_in[i], calc.dbg_out[i]);
    }
    if (ascii_out) print_ascii(&calc);
    if (pgm) write_pgm(&calc, pgm);
    if (save && calc_save_state(&calc, save) == 0) printf("saved state %s\n", save);
    return 0;
}
