#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "ti84.h"

/* flash command state machine (AMD-style) */
enum { F_READ, F_AA, F_55, F_PROGRAM, F_ERASE, F_ERASE_AA, F_ERASE_55,
       F_AUTOSELECT, F_FASTMODE, F_FASTMODE_EXIT, F_FASTMODE_PROG, F_ERROR };

static const double TIMER_FREQ[4] = { 1.0 / 560.0, 1.0 / 248.0, 1.0 / 170.0, 1.0 / 118.0 };
#define LCD_DELAY  60                      /* t-states the driver stays busy */
#define BASE_LEVEL 24                      /* contrast offset for the 83+/84+ */

/* ---------------- memory mapping ---------------- */

/* Banks 0..3 map 0000/4000/8000/C000. In boot-mapped mode the 4000 and 8000
 * banks come from port 6 (even/odd) and C000 from port 7, per update_bootmap_pages. */
static bank_t eff_bank(calc_t *c, int n) {
    if (!c->boot_mapped) return c->normal[n];
    bank_t b;
    switch (n) {
        case 0: return c->normal[0];
        case 1: b = c->normal[1]; b.page &= 0xFE; b.read_only = 0; return b;
        case 2: b = c->normal[1]; b.read_only = 0; return b;
        default: b = c->normal[2]; b.read_only = 0; return b;
    }
}

static uint8_t *bank_base(calc_t *c, bank_t b) {
    if (b.is_ram) return c->ram + (b.page % RAM_PAGES) * TI_PAGE_SIZE;
    return c->flash + (b.page % FLASH_PAGES) * TI_PAGE_SIZE;
}

static uint8_t mem_read_raw(calc_t *c, uint16_t a) {
    bank_t b = eff_bank(c, a >> 14);
    return bank_base(c, b)[a & 0x3FFF];
}

static void change_page(calc_t *c, int bank, int page, int is_ram) {
    c->normal[bank].is_ram = is_ram;
    c->normal[bank].page = page;
    c->normal[bank].read_only = (!is_ram && page == FLASH_PAGES - 1);
}

static void update_bank1(calc_t *c) {
    if (c->port06 & 0x80) change_page(c, 1, c->port06 & (RAM_PAGES - 1), 1);
    else change_page(c, 1, ((c->port06 & 0x7F) | (c->port0E << 7)) & (FLASH_PAGES - 1), 0);
}
static void update_bank2(calc_t *c) {
    if (c->port07 & 0x80) change_page(c, 2, c->port07 & (RAM_PAGES - 1), 1);
    else change_page(c, 2, ((c->port07 & 0x7F) | (c->port0F << 7)) & (FLASH_PAGES - 1), 0);
}

static int is_privileged_page(calc_t *c) {
    bank_t b = eff_bank(c, c->cpu.pc >> 14);
    if (b.is_ram) return 0;
    int max = FLASH_PAGES;
    return (b.page >= max - 4 && b.page != max - 2) || b.page == max - 0x11;
}

/* ---------------- flash ---------------- */

static void endflash(calc_t *c) { if (c->flash_step != F_ERROR) c->flash_step = F_READ; }

static uint8_t flash_autoselect(calc_t *c, uint16_t a) {
    int off = a & 0x3FFF;
    if (off == 0) return 1;                /* AMD */
    if (off == 2) return 0xDA;             /* 1 MB part */
    if (off == 4) return 0;
    endflash(c);
    return 0;
}

static uint8_t flash_read(calc_t *c, uint16_t a) {
    if (c->flash_error) {
        uint8_t v = (uint8_t)((~c->flash_wb & 0x80) | 0x20 | c->flash_toggles);
        c->flash_toggles ^= 0x40;
        c->flash_error = 0;
        return v;
    }
    if (c->flash_step == F_READ || c->flash_step == F_FASTMODE) return mem_read_raw(c, a);
    if (c->flash_step == F_AUTOSELECT) return flash_autoselect(c, a);
    endflash(c);
    return mem_read_raw(c, a);
}

static void flash_write_byte(calc_t *c, uint16_t a, uint8_t data) {
    bank_t b = eff_bank(c, a >> 14);
    uint8_t *p = bank_base(c, b) + (a & 0x3FFF);
    *p &= data;                            /* flash can only clear bits */
    c->flash_wb = data;
    if (*p != data) c->flash_error = 1;
    c->flash_step = F_READ;
}

static void flash_write(calc_t *c, uint16_t a, uint8_t data) {
    if (data == 0xF0 && c->flash_step != F_PROGRAM && c->flash_step != F_FASTMODE_PROG) {
        endflash(c);
        return;
    }
    c->flash_error = 0;
    switch (c->flash_step) {
    case F_READ:      c->flash_step = ((a & 0xFFF) == 0xAAA && data == 0xAA) ? F_AA : F_READ; break;
    case F_AA:        c->flash_step = ((a & 0xFFF) == 0x555 && data == 0x55) ? F_55 : F_READ; break;
    case F_55:
        if ((a & 0xFFF) == 0xAAA) {
            switch (data) {
                case 0xA0: c->flash_step = F_PROGRAM; break;
                case 0x80: c->flash_step = F_ERASE; break;
                case 0x20: c->flash_step = F_FASTMODE; break;
                case 0x90: c->flash_step = F_AUTOSELECT; break;
                default: endflash(c); break;
            }
        } else endflash(c);
        break;
    case F_PROGRAM:   flash_write_byte(c, a, data); endflash(c); break;
    case F_ERASE:     c->flash_step = ((a & 0xFFF) == 0xAAA && data == 0xAA) ? F_ERASE_AA : F_READ; break;
    case F_ERASE_AA:  c->flash_step = ((a & 0xFFF) == 0x555 && data == 0x55) ? F_ERASE_55 : F_READ; break;
    case F_ERASE_55: {
        if ((a & 0xFFF) == 0xAAA && data == 0x10) {
            memset(c->flash, 0xFF, FLASH_SIZE);
        } else if (data == 0x30) {         /* sector erase */
            bank_t b = eff_bank(c, a >> 14);
            int spage = (b.page << 1) + ((a >> 13) & 1);
            int total = FLASH_PAGES * 2, start, end;
            if (spage < total - 8)      { start = (spage & 0x1FF) * 0x2000; end = start + TI_PAGE_SIZE * 4; }
            else if (spage < total - 4) { start = (FLASH_PAGES - 4) * TI_PAGE_SIZE; end = (FLASH_PAGES - 2) * TI_PAGE_SIZE; }
            else if (spage < total - 3) { start = (FLASH_PAGES - 2) * TI_PAGE_SIZE; end = start + TI_PAGE_SIZE / 2; }
            else if (spage < total - 2) { start = (FLASH_PAGES - 2) * TI_PAGE_SIZE + TI_PAGE_SIZE / 2; end = (FLASH_PAGES - 1) * TI_PAGE_SIZE; }
            else                        { start = (FLASH_PAGES - 1) * TI_PAGE_SIZE; end = FLASH_SIZE; }
            memset(c->flash + start, 0xFF, (size_t)(end - start));
        }
        endflash(c);
        break;
    }
    case F_FASTMODE:
        if (data == 0x90) c->flash_step = F_FASTMODE_EXIT;
        else if (data == 0xA0) c->flash_step = F_FASTMODE_PROG;
        else endflash(c);
        break;
    case F_FASTMODE_EXIT: c->flash_step = F_FASTMODE; break;
    case F_FASTMODE_PROG: flash_write_byte(c, a, data); c->flash_step = F_FASTMODE; break;
    default: endflash(c); break;
    }
}

static int flash_write_valid(calc_t *c, int page) {
    return !c->flash_locked && ((page != 0x3F && page != 0x2F) || (c->model_bits & 3));
}

/* ---------------- CPU bus callbacks ---------------- */

static uint8_t cb_fetch(void *ctx, uint16_t a) {
    calc_t *c = (calc_t *)ctx;
    int n = a >> 14;
    bank_t b = eff_bank(c, n);
    /* the boot page starts mapped at 0000; the first execution out of a port-6
     * page swaps bank 0 to flash page 0 (WabbitEmu CPU_opcode_fetch) */
    if (!c->changed_page0 && !b.is_ram && (n == 1 || (c->boot_mapped && n == 2))) {
        change_page(c, 0, 0, 0);
        c->changed_page0 = 1;
    }
    if (!b.is_ram && c->flash_step != F_READ) endflash(c);
    return mem_read_raw(c, a);
}

static uint8_t cb_read(void *ctx, uint16_t a) {
    calc_t *c = (calc_t *)ctx;
    bank_t b = eff_bank(c, a >> 14);
    if (b.is_ram) return mem_read_raw(c, a);
    return flash_read(c, a);
}

static void cb_write(void *ctx, uint16_t a, uint8_t v) {
    calc_t *c = (calc_t *)ctx;
    bank_t b = eff_bank(c, a >> 14);
    if (b.is_ram) {
        if (!b.read_only) bank_base(c, b)[a & 0x3FFF] = v;
    } else if (flash_write_valid(c, b.page)) {
        flash_write(c, a, v);
    }
}

/* ---------------- LCD ---------------- */

#define LCD_OFF(col, row, z) ((((row) + (z)) % LCD_H) * LCD_MEM_W + ((col) % LCD_MEM_W))

static int lcd_busy(calc_t *c) { return (c->cpu.t - c->lcd.last_tstate) < LCD_DELAY; }

static void lcd_enqueue(calc_t *c) {
    lcd_t *l = &c->lcd;
    if (l->front == 0) l->front = LCD_SHADES;
    l->front--;
    for (int row = 0; row < LCD_H; row++)
        for (int col = 0; col < LCD_MEM_W; col++)
            l->queue[l->front][LCD_OFF(col, row, LCD_H - l->z)] = l->display[LCD_OFF(col, row, 0)];
}

static void lcd_advance(lcd_t *l) {
    switch (l->cursor_mode) {
        case 1: l->x = (l->x + 1) % LCD_H; break;                 /* X_UP */
        case 0: l->x = (l->x + LCD_H - 1) % LCD_H; break;         /* X_DOWN */
        case 3: { l->y++; if (l->y >= (l->word_len ? 15 : 19)) l->y = 0; break; }
        default: l->y = (l->y <= 0) ? (l->word_len ? 14 : 18) : l->y - 1; break;
    }
}

static void lcd_command(calc_t *c, uint8_t v) {
    lcd_t *l = &c->lcd;
    c->dbg_cmd++;
    if (lcd_busy(c)) { c->dbg_dropped++; return; }
    l->last_tstate = c->cpu.t;
    if ((v & 0xFE) == 0x02)      { l->active = v & 1; lcd_enqueue(c); }
    else if ((v & 0xFE) == 0x00) { l->word_len = v & 1; }
    else if ((v & 0xFC) == 0x04) { l->cursor_mode = v & 3; }
    else if ((v & 0xF8) == 0x18) { }                              /* test mode */
    else if ((v & 0xF8) == 0x10) { }                              /* op-amp 1 */
    else if ((v & 0xF8) == 0x08) { }                              /* op-amp 2 */
    else if ((v & 0xE0) == 0x20) { l->y = v & 0x1F; }
    else if ((v & 0xC0) == 0x40) { l->z = v & 0x3F; lcd_enqueue(c); }
    else if ((v & 0xC0) == 0x80) { l->x = v & 0x3F; }
    else if ((v & 0xC0) == 0xC0) { l->contrast = (v & 0x3F) - BASE_LEVEL; }
}

static uint8_t lcd_status(calc_t *c) {
    lcd_t *l = &c->lcd;
    if (lcd_busy(c)) return 0x80;
    return (uint8_t)((l->word_len << 6) | (l->active << 5) | l->cursor_mode);
}

static uint8_t *lcd_cursor(calc_t *c, unsigned *shift) {
    lcd_t *l = &c->lcd;
    if (l->word_len) { *shift = 0; return &l->display[LCD_OFF(l->y, l->x, 0)]; }
    unsigned ny = (unsigned)l->y * 6;
    *shift = 10 - (ny % 8);
    return &l->display[LCD_OFF(ny / 8, l->x, 0)];
}

static void lcd_data_out(calc_t *c, uint8_t v) {
    lcd_t *l = &c->lcd;
    c->dbg_data++;
    if (lcd_busy(c)) { c->dbg_dropped++; return; }
    unsigned shift;
    uint8_t *cur = lcd_cursor(c, &shift);

    double delay = c->elapsed - l->write_last;
    if (l->write_avg == 0.0) l->write_avg = delay;
    l->write_last = c->elapsed;
    l->last_tstate = c->cpu.t;
    /* a write much later than the running average means the previous frame ended */
    if (delay < l->write_avg * 100.0) l->write_avg = l->write_avg * 0.9 + delay * 0.1;
    else { l->ufps_last = c->elapsed; lcd_enqueue(c); l->time = c->elapsed; }

    if (l->word_len) {
        cur[0] = v;
    } else {
        uint16_t data = (uint16_t)(v << shift), mask = (uint16_t)~(0x3F << shift);
        cur[0] = (uint8_t)((cur[0] & (mask >> 8)) | (data >> 8));
        cur[1] = (uint8_t)((cur[1] & (mask & 0xFF)) | (data & 0xFF));
    }
    lcd_advance(l);
}

static uint8_t lcd_data_in(calc_t *c) {
    lcd_t *l = &c->lcd;
    if (lcd_busy(c)) return 0;
    unsigned shift;
    uint8_t *cur = lcd_cursor(c, &shift);
    uint8_t out = (uint8_t)l->last_read;
    if (l->word_len) l->last_read = cur[0];
    else l->last_read = (uint16_t)((((cur[0] << 8) | cur[1]) >> shift) & 0x3F);
    l->last_tstate = c->cpu.t;
    lcd_advance(l);
    return out;
}

/* Frames are normally enqueued when the OS pauses between LCD writes, which is what
 * makes greyscale work. A static screen never pauses "again", so fall back to
 * sampling at 30 Hz (LCD_image in wabbitemu does the same). */
void calc_frame(calc_t *c) {
    lcd_t *l = &c->lcd;
    if (l->time > c->elapsed) l->time = c->elapsed;
    else if (c->elapsed - l->time > 2.0 / 30.0) l->time = c->elapsed - 2.0 / 30.0;
    if (c->elapsed - l->time >= 1.0 / 30.0) { lcd_enqueue(c); l->time += 1.0 / 30.0; }
}

void calc_lcd_gray_ex(calc_t *c, uint8_t out[LCD_W * LCD_H], int apply_contrast) {
    lcd_t *l = &c->lcd;
    int alpha, contrast_color = 255;

    if (!l->active) { memset(out, 0, LCD_W * LCD_H); return; }
    if (l->contrast < 20) { alpha = 98 - (l->contrast % 20) * 100 / 20; contrast_color = 0; }
    else { alpha = (l->contrast % 20) * (l->contrast % 20) / 3; if (alpha > 100) alpha = 100; }
    int overlay = alpha * contrast_color / 100, inv = 100 - alpha;
    if (!apply_contrast) { overlay = 0; inv = 100; }

    for (int row = 0; row < LCD_H; row++) {
        for (int col = 0; col < LCD_W / 8; col++) {
            int cnt[8] = {0, 0, 0, 0, 0, 0, 0, 0};
            for (int i = 0; i < LCD_SHADES; i++) {
                uint8_t u = l->queue[i][row * LCD_MEM_W + col];
                for (int b = 0; b < 8; b++) cnt[7 - b] += (u >> b) & 1;
            }
            uint8_t *p = out + row * LCD_W + col * 8;
            for (int b = 0; b < 8; b++) {
                int v = cnt[b] * 255 / LCD_SHADES;
                p[b] = (uint8_t)(overlay + v * inv / 100);
            }
        }
    }
}

void calc_lcd_gray(calc_t *c, uint8_t out[LCD_W * LCD_H]) { calc_lcd_gray_ex(c, out, 1); }

/* ---------------- timers and interrupts ---------------- */

static void xtal_tick(calc_t *c) {
    uint64_t ticks = (uint64_t)(c->elapsed * 32768.0);
    for (int i = 0; i < 3; i++) {
        xtimer_t *t = &c->xt[i];
        if (t->active) {
            int mode = (t->clock & 0xC0) >> 6;
            if (mode == 1) {
                if (t->last_ticks + t->divisor < (double)ticks) {
                    t->last_ticks += t->divisor;
                    if (!--t->count) {
                        if (!t->underflow) { t->count = t->max; if (!t->loop) t->active = 0; }
                        if (t->interrupt) t->generate = 1;
                        t->underflow = 1;
                    }
                }
            } else if (mode >= 2) {
                while (t->last_tstates + (int64_t)t->divisor < c->cpu.t) {
                    t->last_tstates += (int64_t)t->divisor;
                    if (!--t->count) {
                        if (!t->underflow) { t->count = t->max; if (!t->loop) t->active = 0; }
                        if (t->interrupt) t->generate = 1;
                        t->underflow = 1;
                    }
                }
            }
        }
    }
}

static void update_interrupts(calc_t *c) {
    int irq = 0;
    xtal_tick(c);

    if (c->int_active & 0x02) {
        if (c->elapsed - c->lastchk1 > c->t1max) irq = 1;
    } else while (c->elapsed - c->lastchk1 > c->t1max) c->lastchk1 += c->t1max;

    if (c->int_active & 0x04) {
        if (c->elapsed - c->lastchk2 > c->t2max) irq = 1;
    } else while (c->elapsed - c->lastchk2 > c->t2max) c->lastchk2 += c->t2max;

    if ((c->int_active & 0x01) && c->on_pressed && !c->on_backup) c->on_latch = 1;
    c->on_backup = c->on_pressed;
    if (c->on_latch) irq = 1;

    for (int i = 0; i < 3; i++)
        if (c->xt[i].generate && !c->cpu.halted) irq = 1;

    /* low-power halt disconnects the LCD, which is how 2nd+OFF blanks the screen */
    if (!(c->int_active & 0x08) && c->cpu.halted) c->lcd.active = 0;

    c->cpu.irq = irq;
}

/* ---------------- ports ---------------- */

static uint8_t keypad_read(calc_t *c) {
    uint8_t map[8] = {0}, bug[8] = {0}, result = 0;
    c->dbg_cmd += 0;
    for (int g = 0; g < 7; g++)
        for (int b = 0; b < 8; b++)
            if (c->keys[g][b]) map[g] |= (uint8_t)(1 << b);
    for (int g = 0; g < 7; g++)
        for (int i = 0; i < 7; i++)
            if (map[g] & map[i]) bug[g] |= (uint8_t)(map[g] | map[i]);
    for (int g = 0; g < 7; g++)
        if (c->key_group & (1 << g)) result |= bug[g];
    if (getenv("TI84_KBD")) {
        static int n = 0;
        if (n++ < 60) fprintf(stderr, "kbd read: group=%02X result=%02X keys=%d%d%d%d%d%d%d\n",
            c->key_group, (uint8_t)~result, map[0],map[1],map[2],map[3],map[4],map[5],map[6]);
    }
    return (uint8_t)~result;
}

static void set_timer_divisor(xtimer_t *t) {
    t->active = 0; t->generate = 0;
    switch ((t->clock & 0xC0) >> 6) {
        case 0: t->divisor = 0.0; break;
        case 1: {
            static const double d[8] = { 3.0, 32.0, 327.0, 3276.0, 1.0, 16.0, 256.0, 4096.0 };
            t->divisor = d[t->clock & 7];
            break;
        }
        default: {
            int mask = 0x20;
            t->divisor = 64.0;
            for (int i = 0; i < 6; i++) {
                if (t->clock & mask) break;
                mask >>= 1;
                t->divisor /= 2.0;
            }
            break;
        }
    }
}

static uint8_t cb_in(void *ctx, uint16_t port) {
    calc_t *c = (calc_t *)ctx;
    int p = port & 0xFF;
    c->dbg_in[p]++;
    switch (p) {
    case 0x00: return 0x03;                                   /* link idle */
    case 0x01: return keypad_read(c);
    case 0x02:
        return (uint8_t)(0xE1 | (c->flash_locked ? 0 : 4) | (lcd_busy(c) ? 0 : 2));
    case 0x03:
        return c->int_active;
    case 0x04: {
        uint8_t r = 0;
        if (c->on_latch) r |= 0x01;
        if ((c->int_active & 0x02) && (c->elapsed - c->lastchk1) > c->t1max) r |= 0x02;
        if ((c->int_active & 0x04) && (c->elapsed - c->lastchk2) > c->t2max) r |= 0x04;
        if (!c->on_pressed) r |= 0x08;
        if (c->xt[0].underflow) r |= 0x20;
        if (c->xt[1].underflow) r |= 0x40;
        if (c->xt[2].underflow) r |= 0x80;
        return r;
    }
    case 0x05: return (uint8_t)c->normal[3].page;
    case 0x06: return (uint8_t)((c->normal[1].is_ram << 7) | (c->normal[1].page & 0x7F));
    case 0x07: return (uint8_t)((c->normal[2].is_ram << 7) | (c->normal[2].page & 0x7F));
    case 0x0E: return c->port0E;
    case 0x0F: return c->port0F;
    case 0x10: return lcd_status(c);
    case 0x11: return lcd_data_in(c);
    case 0x14: return (uint8_t)(c->flash_locked ? 0 : 1);
    case 0x15: return 0x44;                                   /* hardware revision */
    case 0x20: return (uint8_t)(c->freq >= MHZ_15 ? 1 : 0);
    case 0x21: return (uint8_t)(c->model_bits + (c->prot_mode << 4));
    case 0x22: return (uint8_t)(c->flash_lower & 0xFF);
    case 0x23: return (uint8_t)(c->flash_upper & 0xFF);
    case 0x24: return c->port24;
    case 0x30: case 0x33: case 0x36: return c->xt[(p - 0x30) / 3].clock;
    case 0x31: case 0x34: case 0x37: {
        xtimer_t *t = &c->xt[(p - 0x30) / 3];
        return (uint8_t)((t->loop ? 1 : 0) | (t->interrupt ? 2 : 0) | (t->underflow ? 4 : 0));
    }
    case 0x32: case 0x35: case 0x38:
        xtal_tick(c);
        return c->xt[(p - 0x30) / 3].count;
    case 0x08: return 0x80;                                   /* link assist idle */
    case 0x09: case 0x0A: case 0x0D: return 0x00;
    case 0x25: case 0x26: case 0x27: case 0x28: case 0x3A:
        return c->port_misc[p];                               /* latched, not modelled */
    case 0x4A: return (uint8_t)(c->usb_4a | 0x04);            /* no VBUS */
    case 0x4C: return (uint8_t)(0x22 | c->usb_4c);
    case 0x4D: return (uint8_t)(c->usb_line | 0x01);
    case 0x55: return 0x1F;                                   /* no line/protocol interrupt */
    case 0x56: return c->usb_events;
    case 0x57: return c->usb_mask;
    case 0x5B: return (uint8_t)(c->usb_proto_en ? 1 : 0);
    case 0x80: return (uint8_t)(c->usb_addr & 0x7F);
    case 0x40: return (uint8_t)(c->clk_enable & 3);
    case 0x41: case 0x42: case 0x43: case 0x44:
        return (uint8_t)(c->clk_set >> ((p - 0x41) * 8));
    case 0x45: case 0x46: case 0x47: case 0x48: {
        uint32_t v = c->clk_base;
        if (c->clk_enable & 1) v += (uint32_t)(c->elapsed - c->clk_lasttime);
        return (uint8_t)(v >> ((p - 0x45) * 8));
    }
    default:
        return 0xFF;                                          /* no device answers */
    }
}

static void cb_out(void *ctx, uint16_t port, uint8_t v) {
    calc_t *c = (calc_t *)ctx;
    int p = port & 0xFF;
    c->dbg_out[p]++;
    c->port_misc[p] = v;
    switch (p) {
    case 0x01:
        c->key_group = (uint8_t)~v;
        break;
    case 0x02:
        if (!(v & 1)) c->on_latch = 0;
        if (!(v & 2)) while (c->elapsed - c->lastchk1 > c->t1max) c->lastchk1 += c->t1max;
        if (!(v & 4)) while (c->elapsed - c->lastchk2 > c->t2max) c->lastchk2 += c->t2max;
        break;
    case 0x03:
        if (!(c->int_active & 0x08) && (v & 0x08)) c->lcd.active = 1;
        if (!(v & 0x01)) c->on_latch = 0;
        c->int_active = v;
        break;
    case 0x04: {
        int f = (v & 6) >> 1;
        c->t1max = TIMER_FREQ[f];
        c->t2max = TIMER_FREQ[f] / 2.0;
        c->lastchk2 = c->lastchk1 + TIMER_FREQ[f] / 4.0;
        c->boot_mapped = v & 1;
        break;
    }
    case 0x05:
        change_page(c, 3, v & (RAM_PAGES - 1), 1);
        break;
    case 0x06: c->port06 = v; update_bank1(c); break;
    case 0x07: c->port07 = v; update_bank2(c); break;
    case 0x0E: c->port0E = v & 3; update_bank1(c); break;
    case 0x0F: c->port0F = v & 3; update_bank2(c); break;
    case 0x10: lcd_command(c, v); break;
    case 0x11: lcd_data_out(c, v); break;
    case 0x14:
        if (is_privileged_page(c)) c->flash_locked = !(v & 1);
        break;
    case 0x20:
        c->freq = (v & 3) ? MHZ_15 : MHZ_6;
        break;
    case 0x21:
        c->model_bits = v & 3;
        c->prot_mode = (v & 0x30) >> 4;
        break;
    case 0x22: c->flash_lower = (c->flash_lower & 0xFF00) | v; break;
    case 0x23: c->flash_upper = (c->flash_upper & 0xFF00) | v; break;
    case 0x24:
        c->port24 = v;
        c->flash_upper = (c->flash_upper & 0xFF) | ((v & 2) << 7);
        c->flash_lower = (c->flash_lower & 0xFF) | ((v & 1) << 8);
        break;
    case 0x30: case 0x33: case 0x36: {
        xtimer_t *t = &c->xt[(p - 0x30) / 3];
        t->clock = v;
        set_timer_divisor(t);
        break;
    }
    case 0x31: case 0x34: case 0x37: {
        xtimer_t *t = &c->xt[(p - 0x30) / 3];
        t->loop = v & 1; t->interrupt = (v & 2) ? 1 : 0;
        t->underflow = 0; t->generate = 0;
        break;
    }
    case 0x32: case 0x35: case 0x38: {
        xtimer_t *t = &c->xt[(p - 0x30) / 3];
        xtal_tick(c);
        t->count = t->max = v;
        if (t->clock & 0xC0) t->active = 1;
        t->last_tstates = c->cpu.t;
        t->last_ticks = (double)(uint64_t)(c->elapsed * 32768.0);
        break;
    }
    case 0x40:
        if (!(c->clk_enable & 2) && (v & 2)) c->clk_base = c->clk_set;
        if (!(c->clk_enable & 1) && (v & 1)) c->clk_lasttime = c->elapsed;
        if ((c->clk_enable & 1) && !(v & 1)) c->clk_base += (uint32_t)(c->elapsed - c->clk_lasttime);
        c->clk_enable = v & 3;
        break;
    case 0x4A: c->usb_4a = v & 0x38; break;
    case 0x4C: c->usb_4c = v & 0x08; break;
    case 0x4D: c->usb_line = v; break;
    case 0x54: c->usb_54 = v & 0xC7; break;
    case 0x56: c->usb_events = v; break;
    case 0x57: c->usb_mask = v; break;
    case 0x5B: c->usb_proto_en = v & 1; break;
    case 0x80: c->usb_addr = v & 0x7F; break;
    case 0x41: case 0x42: case 0x43: case 0x44: {
        int sh = (p - 0x41) * 8;
        c->clk_set = (c->clk_set & ~(0xFFu << sh)) | ((uint32_t)v << sh);
        break;
    }
    default:
        break;
    }
}

/* ---------------- lifecycle ---------------- */

void calc_reset(calc_t *c) {
    z80_reset(&c->cpu);   /* rom_sig and the flash contents survive a reset */
    c->cpu.pc = 0;
    c->cpu.sp = 0;
    c->cpu.im = 1;
    c->boot_mapped = 0;
    c->changed_page0 = 0;
    c->flash_locked = 1;
    c->flash_step = F_READ;
    c->flash_error = 0;
    c->flash_lower = 0x10;
    c->flash_upper = 0x30;
    c->port06 = 0; c->port07 = 0; c->port0E = 0; c->port0F = 0;
    c->model_bits = 0; c->prot_mode = 0;

    c->normal[0] = (bank_t){ FLASH_PAGES - 1, 0, 0 };   /* boot page at 0000 */
    c->normal[1] = (bank_t){ 0, 0, 0 };
    c->normal[2] = (bank_t){ 0, 0, 0 };
    c->normal[3] = (bank_t){ 0, 1, 0 };

    c->freq = MHZ_6;
    c->elapsed = 0.0;
    c->int_active = 0;
    c->t1max = TIMER_FREQ[3];
    c->t2max = TIMER_FREQ[3] / 2.0;
    c->lastchk1 = 0.0;
    c->lastchk2 = TIMER_FREQ[3] / 4.0;
    c->on_latch = 0; c->on_pressed = 0; c->on_backup = 0;
    memset(c->keys, 0, sizeof c->keys);
    c->key_group = 0;

    memset(&c->lcd, 0, sizeof c->lcd);
    c->lcd.contrast = 32;
    c->lcd.cursor_mode = 3;                              /* Y_UP */
    memset(c->xt, 0, sizeof c->xt);
    c->clk_enable = 0; c->clk_base = 0; c->clk_set = 0; c->clk_lasttime = 0;
    memset(c->port_misc, 0, sizeof c->port_misc);
    c->usb_4a = c->usb_4c = c->usb_54 = 0;
    c->usb_line = 0xA5;
    c->usb_events = 0x50;
    c->usb_mask = 0; c->usb_addr = 0; c->usb_proto_en = 0;
}

static void wire(calc_t *c) {
    c->cpu.ctx = c;
    c->cpu.fetch = cb_fetch;
    c->cpu.read = cb_read;
    c->cpu.write = cb_write;
    c->cpu.in = cb_in;
    c->cpu.out = cb_out;
}

int calc_init(calc_t *c, const char *rom_path) {
    memset(c, 0, sizeof *c);
    memset(c->flash, 0xFF, FLASH_SIZE);
    FILE *f = fopen(rom_path, "rb");
    if (!f) return 1;
    size_t n = fread(c->flash, 1, FLASH_SIZE, f);
    fclose(f);
    if (n != FLASH_SIZE) return 2;
    uint32_t sig = 2166136261u;                      /* FNV-1a over the ROM image */
    for (size_t i = 0; i < FLASH_SIZE; i++) { sig ^= c->flash[i]; sig *= 16777619u; }
    c->rom_sig = sig;
    z80_init(&c->cpu);
    wire(c);
    calc_reset(c);
    return 0;
}

void calc_run(calc_t *c, int64_t tstates) {
    int64_t end = c->cpu.t + tstates;
    while (c->cpu.t < end) {
        int64_t before = c->cpu.t;
        z80_step(&c->cpu);
        c->dbg_pchist[c->cpu.pc >> 8]++;
        c->elapsed += (double)(c->cpu.t - before) / c->freq;
        update_interrupts(c);
    }
}

void calc_key(calc_t *c, int group, int bit, int down) {
    if (group < 0 || group > 7 || bit < 0 || bit > 7) return;
    if (group == 5 && bit == 0) {                        /* ON is wired separately */
        c->on_pressed = (uint8_t)(down ? 1 : 0);
        return;
    }
    c->keys[group][bit] = (uint8_t)(down ? 1 : 0);
}

void calc_release_all(calc_t *c) {
    memset(c->keys, 0, sizeof c->keys);
    c->on_pressed = 0;
}

/* ---------------- save states ---------------- */

#define STATE_MAGIC 0x54383453u                          /* "T84S" */
#define STATE_VER   2

int calc_save_state(calc_t *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    uint32_t hdr[3] = { STATE_MAGIC, STATE_VER, (uint32_t)sizeof *c };
    fwrite(hdr, sizeof hdr, 1, f);
    fwrite(c, sizeof *c, 1, f);
    fclose(f);
    return 0;
}

int calc_load_state(calc_t *c, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 1;
    uint32_t hdr[3];
    /* the struct size is part of the header: a state written by a different build
     * of this emulator must be rejected, not loaded as garbage */
    if (fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != STATE_MAGIC || hdr[1] != STATE_VER
        || hdr[2] != (uint32_t)sizeof *c) {
        fclose(f);
        return 2;
    }
    calc_t *tmp = (calc_t *)malloc(sizeof *tmp);
    if (!tmp) { fclose(f); return 3; }
    size_t n = fread(tmp, sizeof *tmp, 1, f);
    fclose(f);
    if (n != 1) { free(tmp); return 4; }
    if (tmp->rom_sig != c->rom_sig) {                /* a state from a different ROM */
        free(tmp);
        return 5;
    }
    memcpy(c, tmp, sizeof *c);
    free(tmp);
    wire(c);                                             /* callbacks are not portable */
    return 0;
}
