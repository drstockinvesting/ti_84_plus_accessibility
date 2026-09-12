/* TI-84 Plus hardware: memory mapping, ports, T6A04 LCD, keypad, timers, flash.
 * Behaviour follows sputt/wabbitemu (core/core.c, hardware/83phw.c, 83psehw.c,
 * hardware/lcd.c, hardware/keys.c) so a ROM and its OS behave the same here.
 */
#ifndef TI84_H
#define TI84_H
#include <stdint.h>
#include "z80.h"

#define TI_PAGE_SIZE    0x4000
#define FLASH_PAGES  64                    /* TI-84 Plus: 1 MB flash */
#define RAM_PAGES    8                     /* 128 KB RAM */
#define FLASH_SIZE   (FLASH_PAGES * TI_PAGE_SIZE)
#define RAM_SIZE     (RAM_PAGES * TI_PAGE_SIZE)
#define LCD_W        96
#define LCD_H        64
#define LCD_MEM_W    16
#define LCD_SHADES   6                     /* frames averaged for greyscale */
#define MHZ_6        6000000.0
#define MHZ_15       15000000.0

typedef struct { int page, is_ram, read_only; } bank_t;

typedef struct {
    uint8_t clock, count, max;
    int active, loop, interrupt, underflow, generate;
    double divisor, last_ticks;
    int64_t last_tstates;
} xtimer_t;

typedef struct {
    int active;                            /* display on */
    int x, y, z;                           /* row cursor, column cursor, scroll */
    int contrast;                          /* 0..39 */
    int word_len;                          /* 1 = 8-bit, 0 = 6-bit */
    int cursor_mode;                       /* 0 X_DOWN 1 X_UP 2 Y_DOWN 3 Y_UP */
    uint16_t last_read;
    uint8_t display[LCD_MEM_W * LCD_H + 1];
    uint8_t queue[LCD_SHADES][LCD_MEM_W * LCD_H];
    int front;
    int64_t last_tstate;
    double write_avg, write_last, ufps_last, time;
} lcd_t;

typedef struct calc {
    z80_t cpu;
    uint32_t rom_sig;                       /* fingerprint of the ROM this state belongs to */
    uint8_t flash[FLASH_SIZE];
    uint8_t ram[RAM_SIZE];

    bank_t normal[4];
    int boot_mapped, flash_locked, changed_page0;
    uint8_t port06, port07, port0E, port0F, port24, model_bits, prot_mode;
    int flash_lower, flash_upper;
    int flash_step, flash_error;
    uint8_t flash_wb, flash_toggles;
    uint8_t port_misc[256];                /* latched values of ports we only store */

    double freq, elapsed;                  /* CPU Hz, seconds of emulated time */
    uint8_t int_active;                    /* port 3 */
    double t1max, t2max, lastchk1, lastchk2;
    int on_latch;
    uint8_t on_pressed, on_backup;

    uint8_t keys[8][8], key_group;
    lcd_t lcd;
    xtimer_t xt[3];

    long dbg_cmd, dbg_data, dbg_dropped;
    unsigned dbg_pchist[256], dbg_in[256], dbg_out[256];
    /* fake USB (no cable): values taken from wabbitemu's USB_t defaults */
    uint8_t usb_4a, usb_4c, usb_54, usb_line, usb_events, usb_mask, usb_addr, usb_proto_en;
    uint8_t clk_enable;
    uint32_t clk_base, clk_set;
    double clk_lasttime;
} calc_t;

int  calc_init(calc_t *c, const char *rom_path);   /* 0 = ok */
void calc_reset(calc_t *c);
void calc_run(calc_t *c, int64_t tstates);
void calc_key(calc_t *c, int group, int bit, int down);
void calc_release_all(calc_t *c);
/* Call once per display refresh: keeps the greyscale queue moving when the OS
 * has stopped writing (a static screen). */
void calc_frame(calc_t *c);
/* 96x64 greyscale, 0 = background .. 255 = fully dark.
 * apply_contrast models the real panel's contrast setting (washed out / dark
 * overlay); passing 0 gives the full-range image, which is what the high
 * contrast themes want. */
void calc_lcd_gray_ex(calc_t *c, uint8_t out[LCD_W * LCD_H], int apply_contrast);
void calc_lcd_gray(calc_t *c, uint8_t out[LCD_W * LCD_H]);
int  calc_save_state(calc_t *c, const char *path);
int  calc_load_state(calc_t *c, const char *path);
#endif
