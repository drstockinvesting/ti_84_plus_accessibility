/* Z80 core. Timing follows the decomposed machine-cycle model: 4 t-states per
 * opcode fetch, 3 per memory byte, 4 per I/O byte, plus the documented internal
 * cycles added explicitly at each site. That reproduces the standard per-opcode
 * t-state table without a 4x256 entry table to mistype.
 */
#include "z80.h"

#define SF 0x80
#define ZF 0x40
#define YF 0x20
#define HF 0x10
#define XF 0x08
#define PF 0x04
#define NF 0x02
#define CF 0x01

static uint8_t sz53[256], sz53p[256], parity[256];

void z80_init(z80_t *z) {
    for (int i = 0; i < 256; i++) {
        int p = 0;
        for (int b = 0; b < 8; b++) p ^= (i >> b) & 1;
        parity[i] = p ? 0 : PF;
        sz53[i] = (uint8_t)((i & (SF | YF | XF)) | (i ? 0 : ZF));
        sz53p[i] = sz53[i] | parity[i];
    }
    z80_reset(z);
}

void z80_reset(z80_t *z) {
    z->a = z->f = 0xFF;
    z->b = z->c = z->d = z->e = z->h = z->l = 0;
    z->a2 = z->f2 = z->b2 = z->c2 = z->d2 = z->e2 = z->h2 = z->l2 = 0;
    z->ixh = z->ixl = z->iyh = z->iyl = 0xFF;
    z->pc = 0; z->sp = 0xFFFF; z->wz = 0;
    z->i = z->r = 0;
    z->iff1 = z->iff2 = 0; z->im = 0;
    z->halted = 0; z->ei_pending = 0; z->irq = 0;
}

/* ---- bus helpers (each adds its machine cycle) ---- */
static inline void tick(z80_t *z, int n) { z->t += n; }

static inline uint8_t op_fetch(z80_t *z) {
    z->t += 4;
    uint8_t v = z->fetch(z->ctx, z->pc++);
    z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F));
    return v;
}
static inline uint8_t rd(z80_t *z, uint16_t a) { z->t += 3; return z->read(z->ctx, a); }
static inline void wr(z80_t *z, uint16_t a, uint8_t v) { z->t += 3; z->write(z->ctx, a, v); }
static inline uint8_t imm(z80_t *z) { return rd(z, z->pc++); }
static inline uint16_t imm16(z80_t *z) { uint16_t v = imm(z); return (uint16_t)(v | (imm(z) << 8)); }
static inline uint8_t io_in(z80_t *z, uint16_t p) { z->t += 4; return z->in(z->ctx, p); }
static inline void io_out(z80_t *z, uint16_t p, uint8_t v) { z->t += 4; z->out(z->ctx, p, v); }

static inline void push(z80_t *z, uint16_t v) {
    wr(z, --z->sp, (uint8_t)(v >> 8));
    wr(z, --z->sp, (uint8_t)v);
}
static inline uint16_t pop(z80_t *z) {
    uint16_t v = rd(z, z->sp++);
    return (uint16_t)(v | (rd(z, z->sp++) << 8));
}

/* ---- 16-bit register pair access ---- */
static uint16_t get_rp(z80_t *z, int p, int idx) {
    switch (p) {
        case 0: return (uint16_t)Z80_BC(z);
        case 1: return (uint16_t)Z80_DE(z);
        case 2: return idx == 0 ? (uint16_t)Z80_HL(z) : (idx == 1 ? (uint16_t)Z80_IX(z) : (uint16_t)Z80_IY(z));
        default: return z->sp;
    }
}
static void set_rp(z80_t *z, int p, int idx, uint16_t v) {
    switch (p) {
        case 0: z->b = (uint8_t)(v >> 8); z->c = (uint8_t)v; break;
        case 1: z->d = (uint8_t)(v >> 8); z->e = (uint8_t)v; break;
        case 2:
            if (idx == 0) { z->h = (uint8_t)(v >> 8); z->l = (uint8_t)v; }
            else if (idx == 1) { z->ixh = (uint8_t)(v >> 8); z->ixl = (uint8_t)v; }
            else { z->iyh = (uint8_t)(v >> 8); z->iyl = (uint8_t)v; }
            break;
        default: z->sp = v; break;
    }
}
static uint16_t get_rp2(z80_t *z, int p, int idx) {   /* AF instead of SP */
    if (p == 3) return (uint16_t)((z->a << 8) | z->f);
    return get_rp(z, p, idx);
}
static void set_rp2(z80_t *z, int p, int idx, uint16_t v) {
    if (p == 3) { z->a = (uint8_t)(v >> 8); z->f = (uint8_t)v; }
    else set_rp(z, p, idx, v);
}

/* 8-bit register pointer; r==6 means memory and returns NULL.
 * idx selects H/L vs IXH/IXL vs IYH/IYL. */
static uint8_t *reg8(z80_t *z, int r, int idx) {
    switch (r) {
        case 0: return &z->b;
        case 1: return &z->c;
        case 2: return &z->d;
        case 3: return &z->e;
        case 4: return idx == 0 ? &z->h : (idx == 1 ? &z->ixh : &z->iyh);
        case 5: return idx == 0 ? &z->l : (idx == 1 ? &z->ixl : &z->iyl);
        case 6: return 0;
        default: return &z->a;
    }
}

/* effective address for the (HL) / (IX+d) operand; adds the d fetch + 5 internal */
static uint16_t ea_hl(z80_t *z, int idx) {
    if (idx == 0) return (uint16_t)Z80_HL(z);
    int8_t d = (int8_t)imm(z);
    tick(z, 5);
    z->wz = (uint16_t)((idx == 1 ? Z80_IX(z) : Z80_IY(z)) + d);
    return z->wz;
}

/* ---- ALU ---- */
static void add_a(z80_t *z, uint8_t v, uint8_t carry) {
    uint16_t r = (uint16_t)(z->a + v + carry);
    uint8_t res = (uint8_t)r;
    z->f = (uint8_t)((res & (SF | YF | XF)) | (res ? 0 : ZF)
        | ((z->a ^ v ^ res) & HF)
        | ((((z->a ^ v ^ 0x80) & (v ^ res)) >> 5) & PF)
        | ((r >> 8) & CF));
    z->a = res;
}
static void sub_a(z80_t *z, uint8_t v, uint8_t carry) {
    uint16_t r = (uint16_t)(z->a - v - carry);
    uint8_t res = (uint8_t)r;
    z->f = (uint8_t)((res & (SF | YF | XF)) | (res ? 0 : ZF)
        | ((z->a ^ v ^ res) & HF) | NF
        | ((((z->a ^ v) & (z->a ^ res)) >> 5) & PF)
        | ((r >> 8) & CF));
    z->a = res;
}
static void cp_a(z80_t *z, uint8_t v) {
    uint16_t r = (uint16_t)(z->a - v);
    uint8_t res = (uint8_t)r;
    z->f = (uint8_t)((res & SF) | (res ? 0 : ZF) | (v & (YF | XF))
        | ((z->a ^ v ^ res) & HF) | NF
        | ((((z->a ^ v) & (z->a ^ res)) >> 5) & PF)
        | ((r >> 8) & CF));
}
static void and_a(z80_t *z, uint8_t v) { z->a &= v; z->f = (uint8_t)(sz53p[z->a] | HF); }
static void xor_a(z80_t *z, uint8_t v) { z->a ^= v; z->f = sz53p[z->a]; }
static void or_a(z80_t *z, uint8_t v)  { z->a |= v; z->f = sz53p[z->a]; }

static uint8_t inc8(z80_t *z, uint8_t v) {
    uint8_t r = (uint8_t)(v + 1);
    z->f = (uint8_t)((z->f & CF) | (r & (SF | YF | XF)) | (r ? 0 : ZF)
        | ((r & 0x0F) ? 0 : HF) | (r == 0x80 ? PF : 0));
    return r;
}
static uint8_t dec8(z80_t *z, uint8_t v) {
    uint8_t r = (uint8_t)(v - 1);
    z->f = (uint8_t)((z->f & CF) | (r & (SF | YF | XF)) | (r ? 0 : ZF)
        | (((r & 0x0F) == 0x0F) ? HF : 0) | (r == 0x7F ? PF : 0) | NF);
    return r;
}
static uint16_t add16(z80_t *z, uint16_t a, uint16_t b) {
    uint32_t r = (uint32_t)a + b;
    z->wz = (uint16_t)(a + 1);
    z->f = (uint8_t)((z->f & (SF | ZF | PF)) | (((a ^ b ^ r) >> 8) & HF)
        | ((r >> 16) & CF) | ((r >> 8) & (YF | XF)));
    tick(z, 7);
    return (uint16_t)r;
}
static void adc16(z80_t *z, uint16_t b) {
    uint16_t a = (uint16_t)Z80_HL(z);
    uint32_t r = (uint32_t)a + b + (z->f & CF);
    uint16_t res = (uint16_t)r;
    z->wz = (uint16_t)(a + 1);
    z->f = (uint8_t)(((res >> 8) & (SF | YF | XF)) | (res ? 0 : ZF)
        | (((a ^ b ^ res) >> 8) & HF)
        | ((((a ^ b ^ 0x8000) & (b ^ res)) >> 13) & PF)
        | ((r >> 16) & CF));
    z->h = (uint8_t)(res >> 8); z->l = (uint8_t)res;
    tick(z, 7);
}
static void sbc16(z80_t *z, uint16_t b) {
    uint16_t a = (uint16_t)Z80_HL(z);
    uint32_t r = (uint32_t)a - b - (z->f & CF);
    uint16_t res = (uint16_t)r;
    z->wz = (uint16_t)(a + 1);
    z->f = (uint8_t)(((res >> 8) & (SF | YF | XF)) | (res ? 0 : ZF)
        | (((a ^ b ^ res) >> 8) & HF) | NF
        | ((((a ^ b) & (a ^ res)) >> 13) & PF)
        | ((r >> 16) & CF));
    z->h = (uint8_t)(res >> 8); z->l = (uint8_t)res;
    tick(z, 7);
}
static void daa(z80_t *z) {
    uint8_t corr = 0, c = (uint8_t)(z->f & CF), before = z->a;
    if ((z->f & HF) || (z->a & 0x0F) > 9) corr |= 0x06;
    if (c || z->a > 0x99) { corr |= 0x60; c = CF; }
    if (z->f & NF) z->a = (uint8_t)(z->a - corr); else z->a = (uint8_t)(z->a + corr);
    z->f = (uint8_t)(sz53p[z->a] | c | (z->f & NF) | ((before ^ z->a) & HF));
}

/* ---- CB rotates/shifts ---- */
static uint8_t cb_op(z80_t *z, int y, uint8_t v) {
    uint8_t r = 0, c;
    switch (y) {
        case 0: c = (uint8_t)(v >> 7); r = (uint8_t)((v << 1) | c); break;                 /* RLC */
        case 1: c = (uint8_t)(v & 1);  r = (uint8_t)((v >> 1) | (c << 7)); break;          /* RRC */
        case 2: c = (uint8_t)(v >> 7); r = (uint8_t)((v << 1) | (z->f & CF)); break;       /* RL  */
        case 3: c = (uint8_t)(v & 1);  r = (uint8_t)((v >> 1) | ((z->f & CF) << 7)); break;/* RR  */
        case 4: c = (uint8_t)(v >> 7); r = (uint8_t)(v << 1); break;                       /* SLA */
        case 5: c = (uint8_t)(v & 1);  r = (uint8_t)((v >> 1) | (v & 0x80)); break;        /* SRA */
        case 6: c = (uint8_t)(v >> 7); r = (uint8_t)((v << 1) | 1); break;                 /* SLL */
        default: c = (uint8_t)(v & 1); r = (uint8_t)(v >> 1); break;                       /* SRL */
    }
    z->f = (uint8_t)(sz53p[r] | c);
    return r;
}

static int cc_true(z80_t *z, int y) {
    switch (y) {
        case 0: return !(z->f & ZF);
        case 1: return  (z->f & ZF) != 0;
        case 2: return !(z->f & CF);
        case 3: return  (z->f & CF) != 0;
        case 4: return !(z->f & PF);
        case 5: return  (z->f & PF) != 0;
        case 6: return !(z->f & SF);
        default: return (z->f & SF) != 0;
    }
}

static void exec_cb(z80_t *z, int idx) {
    uint16_t ea = 0;
    uint8_t op, v;
    int x, y, r;
    if (idx == 0) {
        op = op_fetch(z);
    } else {
        int8_t d = (int8_t)imm(z);
        op = imm(z);                  /* not an M1 cycle: R is not incremented */
        tick(z, 2);
        ea = (uint16_t)((idx == 1 ? Z80_IX(z) : Z80_IY(z)) + d);
        z->wz = ea;
    }
    x = op >> 6; y = (op >> 3) & 7; r = op & 7;

    if (idx == 0 && r != 6) {
        uint8_t *p = reg8(z, r, 0);
        v = *p;
        if (x == 0) { *p = cb_op(z, y, v); }
        else if (x == 1) {
            uint8_t res = (uint8_t)(v & (1 << y));
            z->f = (uint8_t)((z->f & CF) | HF | (res ? (res & SF) : (ZF | PF)) | (v & (YF | XF)));
        }
        else if (x == 2) *p = (uint8_t)(v & ~(1 << y));
        else *p = (uint8_t)(v | (1 << y));
        return;
    }

    if (idx == 0) ea = (uint16_t)Z80_HL(z);
    v = rd(z, ea);
    if (x == 1) {
        uint8_t res = (uint8_t)(v & (1 << y));
        tick(z, 1);
        z->f = (uint8_t)((z->f & CF) | HF | (res ? (res & SF) : (ZF | PF))
            | ((z->wz >> 8) & (YF | XF)));
        return;
    }
    tick(z, 1);
    if (x == 0) v = cb_op(z, y, v);
    else if (x == 2) v = (uint8_t)(v & ~(1 << y));
    else v = (uint8_t)(v | (1 << y));
    wr(z, ea, v);
    if (idx != 0 && r != 6) *reg8(z, r, 0) = v;   /* undocumented copy-back */
}

static void exec_block(z80_t *z, int y, int r);

static void exec_ed(z80_t *z) {
    uint8_t op = op_fetch(z);
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7, p = y >> 1, q = y & 1;

    if (x == 2) {                                  /* block group; the rest are NOPs */
        if (y >= 4 && r <= 3) exec_block(z, y, r);
        return;
    }
    if (x != 1) return;                            /* ED NOPs */

    switch (r) {
        case 0: {                                  /* IN r,(C) */
            uint8_t v = io_in(z, (uint16_t)Z80_BC(z));
            z->wz = (uint16_t)(Z80_BC(z) + 1);
            z->f = (uint8_t)(sz53p[v] | (z->f & CF));
            if (y != 6) *reg8(z, y, 0) = v;
            break;
        }
        case 1:                                    /* OUT (C),r */
            io_out(z, (uint16_t)Z80_BC(z), y == 6 ? 0 : *reg8(z, y, 0));
            z->wz = (uint16_t)(Z80_BC(z) + 1);
            break;
        case 2:
            if (q) adc16(z, get_rp(z, p, 0)); else sbc16(z, get_rp(z, p, 0));
            break;
        case 3: {                                  /* LD (nn),rr / LD rr,(nn) */
            uint16_t nn = imm16(z);
            if (q) {
                uint16_t v = rd(z, nn);
                v = (uint16_t)(v | (rd(z, (uint16_t)(nn + 1)) << 8));
                set_rp(z, p, 0, v);
            } else {
                uint16_t v = get_rp(z, p, 0);
                wr(z, nn, (uint8_t)v);
                wr(z, (uint16_t)(nn + 1), (uint8_t)(v >> 8));
            }
            z->wz = (uint16_t)(nn + 1);
            break;
        }
        case 4: {                                  /* NEG */
            uint8_t v = z->a;
            z->a = 0;
            sub_a(z, v, 0);
            break;
        }
        case 5:                                    /* RETN / RETI */
            z->iff1 = z->iff2;
            z->pc = pop(z);
            z->wz = z->pc;
            break;
        case 6:                                    /* IM 0/0/1/2 */
            z->im = (uint8_t)((int[]){0, 0, 1, 2}[y & 3]);
            break;
        default:
            switch (y) {
                case 0: tick(z, 1); z->i = z->a; break;                       /* LD I,A */
                case 1: tick(z, 1); z->r = z->a; break;                       /* LD R,A */
                case 2:                                                       /* LD A,I */
                    tick(z, 1); z->a = z->i;
                    z->f = (uint8_t)((z->f & CF) | sz53[z->a] | (z->iff2 ? PF : 0));
                    break;
                case 3:                                                       /* LD A,R */
                    tick(z, 1); z->a = z->r;
                    z->f = (uint8_t)((z->f & CF) | sz53[z->a] | (z->iff2 ? PF : 0));
                    break;
                case 4: {                                                     /* RRD */
                    uint16_t hl = (uint16_t)Z80_HL(z);
                    uint8_t v = rd(z, hl);
                    tick(z, 4);
                    wr(z, hl, (uint8_t)((v >> 4) | (z->a << 4)));
                    z->a = (uint8_t)((z->a & 0xF0) | (v & 0x0F));
                    z->f = (uint8_t)((z->f & CF) | sz53p[z->a]);
                    z->wz = (uint16_t)(hl + 1);
                    break;
                }
                case 5: {                                                     /* RLD */
                    uint16_t hl = (uint16_t)Z80_HL(z);
                    uint8_t v = rd(z, hl);
                    tick(z, 4);
                    wr(z, hl, (uint8_t)((v << 4) | (z->a & 0x0F)));
                    z->a = (uint8_t)((z->a & 0xF0) | (v >> 4));
                    z->f = (uint8_t)((z->f & CF) | sz53p[z->a]);
                    z->wz = (uint16_t)(hl + 1);
                    break;
                }
                default: break;                                               /* ED 77 / 7F: NOP */
            }
            break;
    }
}

/* block instructions live in their own helper for clarity */
static void exec_block(z80_t *z, int y, int r) {
    uint16_t hl = (uint16_t)Z80_HL(z), de = (uint16_t)Z80_DE(z), bc = (uint16_t)Z80_BC(z);
    int inc = (y & 1) ? -1 : 1;            /* y: 4=I, 5=D, 6=IR, 7=DR */
    int rep = (y >= 6);
    switch (r) {
        case 0: {                          /* LDI/LDD/LDIR/LDDR */
            uint8_t v = rd(z, hl);
            wr(z, de, v);
            tick(z, 2);
            hl = (uint16_t)(hl + inc); de = (uint16_t)(de + inc); bc = (uint16_t)(bc - 1);
            uint8_t n = (uint8_t)(v + z->a);
            z->f = (uint8_t)((z->f & (SF | ZF | CF)) | (bc ? PF : 0)
                | (n & XF) | ((n & 0x02) ? YF : 0));
            if (rep && bc) { tick(z, 5); z->pc -= 2; z->wz = (uint16_t)(z->pc + 1); }
            break;
        }
        case 1: {                          /* CPI/CPD/CPIR/CPDR */
            uint8_t v = rd(z, hl);
            tick(z, 5);
            uint8_t res = (uint8_t)(z->a - v);
            uint8_t hf = (uint8_t)((z->a ^ v ^ res) & HF);
            hl = (uint16_t)(hl + inc); bc = (uint16_t)(bc - 1);
            uint8_t n = (uint8_t)(res - (hf ? 1 : 0));
            z->f = (uint8_t)((z->f & CF) | NF | (res & SF) | (res ? 0 : ZF) | hf
                | (bc ? PF : 0) | (n & XF) | ((n & 0x02) ? YF : 0));
            z->wz = (uint16_t)(z->wz + (inc > 0 ? 1 : -1));
            if (rep && bc && res) { tick(z, 5); z->pc -= 2; }
            break;
        }
        case 2: {                          /* INI/IND/INIR/INDR */
            tick(z, 1);
            uint8_t v = io_in(z, bc);
            wr(z, hl, v);
            z->b = (uint8_t)(z->b - 1);
            hl = (uint16_t)(hl + inc);
            z->wz = (uint16_t)(bc + inc);
            uint16_t k = (uint16_t)(v + ((uint8_t)(z->c + inc)));
            z->f = (uint8_t)(sz53[z->b] | (v & 0x80 ? NF : 0)
                | (k > 255 ? (HF | CF) : 0) | parity[(k & 7) ^ z->b]);
            if (rep && z->b) { tick(z, 5); z->pc -= 2; }
            bc = (uint16_t)Z80_BC(z);
            break;
        }
        default: {                         /* OUTI/OUTD/OTIR/OTDR */
            tick(z, 1);
            uint8_t v = rd(z, hl);
            z->b = (uint8_t)(z->b - 1);
            io_out(z, (uint16_t)Z80_BC(z), v);
            hl = (uint16_t)(hl + inc);
            z->wz = (uint16_t)(Z80_BC(z) + inc);
            uint16_t k = (uint16_t)(v + (hl & 0xFF));
            z->f = (uint8_t)(sz53[z->b] | (v & 0x80 ? NF : 0)
                | (k > 255 ? (HF | CF) : 0) | parity[(k & 7) ^ z->b]);
            if (rep && z->b) { tick(z, 5); z->pc -= 2; }
            bc = (uint16_t)Z80_BC(z);
            break;
        }
    }
    z->h = (uint8_t)(hl >> 8); z->l = (uint8_t)hl;
    z->d = (uint8_t)(de >> 8); z->e = (uint8_t)de;
    if (r == 0 || r == 1) { z->b = (uint8_t)(bc >> 8); z->c = (uint8_t)bc; }
}

/* ---- main decode (x/y/z form) ---- */
static void exec_one(z80_t *z, int idx) {
    uint8_t op = op_fetch(z);
    int x = op >> 6, y = (op >> 3) & 7, r = op & 7, p = y >> 1, q = y & 1;

    switch (x) {
    case 0:
        switch (r) {
        case 0:
            if (y == 0) break;                                     /* NOP */
            if (y == 1) {                                          /* EX AF,AF' */
                uint8_t t;
                t = z->a; z->a = z->a2; z->a2 = t;
                t = z->f; z->f = z->f2; z->f2 = t;
                break;
            }
            if (y == 2) {                                          /* DJNZ d */
                tick(z, 1);
                int8_t d = (int8_t)imm(z);
                z->b = (uint8_t)(z->b - 1);
                if (z->b) { tick(z, 5); z->pc = (uint16_t)(z->pc + d); z->wz = z->pc; }
                break;
            }
            {                                                      /* JR d / JR cc,d */
                int8_t d = (int8_t)imm(z);
                if (y == 3 || cc_true(z, y - 4)) {
                    tick(z, 5); z->pc = (uint16_t)(z->pc + d); z->wz = z->pc;
                }
            }
            break;
        case 1:
            if (q) set_rp(z, 2, idx, add16(z, get_rp(z, 2, idx), get_rp(z, p, idx)));
            else set_rp(z, p, idx, imm16(z));
            break;
        case 2:
            if (q == 0) {
                switch (p) {
                case 0: wr(z, (uint16_t)Z80_BC(z), z->a); z->wz = (uint16_t)((z->a << 8) | ((Z80_BC(z) + 1) & 0xFF)); break;
                case 1: wr(z, (uint16_t)Z80_DE(z), z->a); z->wz = (uint16_t)((z->a << 8) | ((Z80_DE(z) + 1) & 0xFF)); break;
                case 2: {
                    uint16_t nn = imm16(z), v = get_rp(z, 2, idx);
                    wr(z, nn, (uint8_t)v); wr(z, (uint16_t)(nn + 1), (uint8_t)(v >> 8));
                    z->wz = (uint16_t)(nn + 1);
                    break;
                }
                default: {
                    uint16_t nn = imm16(z);
                    wr(z, nn, z->a);
                    z->wz = (uint16_t)((z->a << 8) | ((nn + 1) & 0xFF));
                    break;
                }
                }
            } else {
                switch (p) {
                case 0: z->a = rd(z, (uint16_t)Z80_BC(z)); z->wz = (uint16_t)(Z80_BC(z) + 1); break;
                case 1: z->a = rd(z, (uint16_t)Z80_DE(z)); z->wz = (uint16_t)(Z80_DE(z) + 1); break;
                case 2: {
                    uint16_t nn = imm16(z), v = rd(z, nn);
                    v = (uint16_t)(v | (rd(z, (uint16_t)(nn + 1)) << 8));
                    set_rp(z, 2, idx, v);
                    z->wz = (uint16_t)(nn + 1);
                    break;
                }
                default: {
                    uint16_t nn = imm16(z);
                    z->a = rd(z, nn);
                    z->wz = (uint16_t)(nn + 1);
                    break;
                }
                }
            }
            break;
        case 3:
            tick(z, 2);
            set_rp(z, p, idx, (uint16_t)(get_rp(z, p, idx) + (q ? -1 : 1)));
            break;
        case 4:
        case 5: {
            uint8_t v;
            if (y == 6) {
                uint16_t ea = ea_hl(z, idx);
                v = rd(z, ea);
                tick(z, 1);
                wr(z, ea, r == 4 ? inc8(z, v) : dec8(z, v));
            } else {
                uint8_t *pr = reg8(z, y, idx);
                *pr = r == 4 ? inc8(z, *pr) : dec8(z, *pr);
            }
            break;
        }
        case 6:
            if (y == 6) {
                if (idx == 0) {
                    wr(z, (uint16_t)Z80_HL(z), imm(z));
                } else {                                           /* LD (IX+d),n: 19 t-states */
                    int8_t d = (int8_t)imm(z);
                    uint8_t n = imm(z);
                    tick(z, 2);
                    z->wz = (uint16_t)((idx == 1 ? Z80_IX(z) : Z80_IY(z)) + d);
                    wr(z, z->wz, n);
                }
            } else {
                *reg8(z, y, idx) = imm(z);
            }
            break;
        default:
            switch (y) {
            case 0: {                                              /* RLCA */
                uint8_t c = (uint8_t)(z->a >> 7);
                z->a = (uint8_t)((z->a << 1) | c);
                z->f = (uint8_t)((z->f & (SF | ZF | PF)) | c | (z->a & (YF | XF)));
                break;
            }
            case 1: {                                              /* RRCA */
                uint8_t c = (uint8_t)(z->a & 1);
                z->a = (uint8_t)((z->a >> 1) | (c << 7));
                z->f = (uint8_t)((z->f & (SF | ZF | PF)) | c | (z->a & (YF | XF)));
                break;
            }
            case 2: {                                              /* RLA */
                uint8_t c = (uint8_t)(z->a >> 7);
                z->a = (uint8_t)((z->a << 1) | (z->f & CF));
                z->f = (uint8_t)((z->f & (SF | ZF | PF)) | c | (z->a & (YF | XF)));
                break;
            }
            case 3: {                                              /* RRA */
                uint8_t c = (uint8_t)(z->a & 1);
                z->a = (uint8_t)((z->a >> 1) | ((z->f & CF) << 7));
                z->f = (uint8_t)((z->f & (SF | ZF | PF)) | c | (z->a & (YF | XF)));
                break;
            }
            case 4: daa(z); break;
            case 5:                                                /* CPL */
                z->a = (uint8_t)~z->a;
                z->f = (uint8_t)((z->f & (SF | ZF | PF | CF)) | HF | NF | (z->a & (YF | XF)));
                break;
            case 6:                                                /* SCF */
                z->f = (uint8_t)((z->f & (SF | ZF | PF)) | CF | (z->a & (YF | XF)));
                break;
            default:                                               /* CCF */
                z->f = (uint8_t)((z->f & (SF | ZF | PF)) | ((z->f & CF) ? HF : 0)
                    | ((z->f & CF) ^ CF) | (z->a & (YF | XF)));
                break;
            }
            break;
        }
        break;

    case 1:
        if (y == 6 && r == 6) { z->halted = 1; break; }            /* HALT */
        if (y == 6) wr(z, ea_hl(z, idx), *reg8(z, r, 0));
        else if (r == 6) *reg8(z, y, 0) = rd(z, ea_hl(z, idx));
        else *reg8(z, y, idx) = *reg8(z, r, idx);
        break;

    case 2: {
        uint8_t v = (r == 6) ? rd(z, ea_hl(z, idx)) : *reg8(z, r, idx);
        switch (y) {
        case 0: add_a(z, v, 0); break;
        case 1: add_a(z, v, (uint8_t)(z->f & CF)); break;
        case 2: sub_a(z, v, 0); break;
        case 3: sub_a(z, v, (uint8_t)(z->f & CF)); break;
        case 4: and_a(z, v); break;
        case 5: xor_a(z, v); break;
        case 6: or_a(z, v); break;
        default: cp_a(z, v); break;
        }
        break;
    }

    default:
        switch (r) {
        case 0:                                                    /* RET cc */
            tick(z, 1);
            if (cc_true(z, y)) { z->pc = pop(z); z->wz = z->pc; }
            break;
        case 1:
            if (q == 0) set_rp2(z, p, idx, pop(z));
            else switch (p) {
                case 0: z->pc = pop(z); z->wz = z->pc; break;      /* RET */
                case 1: {                                          /* EXX */
                    uint8_t t;
                    t = z->b; z->b = z->b2; z->b2 = t;
                    t = z->c; z->c = z->c2; z->c2 = t;
                    t = z->d; z->d = z->d2; z->d2 = t;
                    t = z->e; z->e = z->e2; z->e2 = t;
                    t = z->h; z->h = z->h2; z->h2 = t;
                    t = z->l; z->l = z->l2; z->l2 = t;
                    break;
                }
                case 2: z->pc = get_rp(z, 2, idx); break;          /* JP (HL) */
                default: tick(z, 2); z->sp = get_rp(z, 2, idx); break;  /* LD SP,HL */
            }
            break;
        case 2: {                                                  /* JP cc,nn */
            uint16_t nn = imm16(z);
            z->wz = nn;
            if (cc_true(z, y)) z->pc = nn;
            break;
        }
        case 3:
            switch (y) {
            case 0: z->pc = imm16(z); z->wz = z->pc; break;         /* JP nn */
            case 1: exec_cb(z, idx); break;
            case 2: {                                              /* OUT (n),A */
                uint8_t n = imm(z);
                io_out(z, (uint16_t)((z->a << 8) | n), z->a);
                z->wz = (uint16_t)((z->a << 8) | ((n + 1) & 0xFF));
                break;
            }
            case 3: {                                              /* IN A,(n) */
                uint8_t n = imm(z);
                z->a = io_in(z, (uint16_t)((z->a << 8) | n));
                z->wz = (uint16_t)(((z->a << 8) | n) + 1);
                break;
            }
            case 4: {                                              /* EX (SP),HL */
                uint16_t v = rd(z, z->sp);
                v = (uint16_t)(v | (rd(z, (uint16_t)(z->sp + 1)) << 8));
                tick(z, 1);
                uint16_t hl = get_rp(z, 2, idx);
                wr(z, (uint16_t)(z->sp + 1), (uint8_t)(hl >> 8));
                wr(z, z->sp, (uint8_t)hl);
                tick(z, 2);
                set_rp(z, 2, idx, v);
                z->wz = v;
                break;
            }
            case 5: {                                              /* EX DE,HL (never IX/IY) */
                uint8_t t;
                t = z->d; z->d = z->h; z->h = t;
                t = z->e; z->e = z->l; z->l = t;
                break;
            }
            case 6: z->iff1 = z->iff2 = 0; break;                  /* DI */
            default: z->iff1 = z->iff2 = 1; z->ei_pending = 1; break;  /* EI */
            }
            break;
        case 4: {                                                  /* CALL cc,nn */
            uint16_t nn = imm16(z);
            z->wz = nn;
            if (cc_true(z, y)) { tick(z, 1); push(z, z->pc); z->pc = nn; }
            break;
        }
        case 5:
            if (q == 0) { tick(z, 1); push(z, get_rp2(z, p, idx)); }
            else switch (p) {
                case 0: {                                          /* CALL nn */
                    uint16_t nn = imm16(z);
                    tick(z, 1);
                    push(z, z->pc);
                    z->pc = nn; z->wz = nn;
                    break;
                }
                case 1: exec_one(z, 1); break;                     /* DD */
                case 2: exec_ed(z); break;                         /* ED */
                default: exec_one(z, 2); break;                    /* FD */
            }
            break;
        case 6: {                                                  /* alu n */
            uint8_t v = imm(z);
            switch (y) {
            case 0: add_a(z, v, 0); break;
            case 1: add_a(z, v, (uint8_t)(z->f & CF)); break;
            case 2: sub_a(z, v, 0); break;
            case 3: sub_a(z, v, (uint8_t)(z->f & CF)); break;
            case 4: and_a(z, v); break;
            case 5: xor_a(z, v); break;
            case 6: or_a(z, v); break;
            default: cp_a(z, v); break;
            }
            break;
        }
        default:                                                   /* RST */
            tick(z, 1);
            push(z, z->pc);
            z->pc = (uint16_t)(y * 8);
            z->wz = z->pc;
            break;
        }
        break;
    }
    (void)q;
}

int z80_step(z80_t *z) {
    int64_t t0 = z->t;

    if (z->irq && z->iff1 && !z->ei_pending) {
        z->halted = 0;
        z->iff1 = z->iff2 = 0;
        z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F));
        tick(z, 7);
        push(z, z->pc);
        if (z->im == 2) {
            uint16_t v = (uint16_t)((z->i << 8) | 0xFF);
            uint16_t lo = rd(z, v);
            z->pc = (uint16_t)(lo | (rd(z, (uint16_t)(v + 1)) << 8));
        } else {
            z->pc = 0x38;                 /* IM 0 on the TI bus reads FF = RST 38h */
        }
        z->wz = z->pc;
        return (int)(z->t - t0);
    }
    z->ei_pending = 0;

    if (z->halted) {
        tick(z, 4);
        z->r = (uint8_t)((z->r & 0x80) | ((z->r + 1) & 0x7F));
        return (int)(z->t - t0);
    }

    exec_one(z, 0);
    return (int)(z->t - t0);
}
