/* Z80 CPU core. Cycle-counted, documented + common undocumented behaviour.
 * The host supplies memory/IO callbacks; `fetch` is separate from `read` so the
 * TI hardware can do its opcode-fetch-only bookkeeping (page 0 remap, flash state).
 */
#ifndef Z80_H
#define Z80_H
#include <stdint.h>

typedef struct z80 z80_t;
struct z80 {
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a2, f2, b2, c2, d2, e2, h2, l2;
    uint8_t ixh, ixl, iyh, iyl;
    uint16_t sp, pc, wz;
    uint8_t i, r;
    uint8_t iff1, iff2, im;
    uint8_t halted, ei_pending;
    int irq;              /* level-triggered interrupt line, driven by the hardware */
    int64_t t;            /* t-states executed since reset */
    void *ctx;
    uint8_t (*fetch)(void *, uint16_t);            /* M1 opcode fetch */
    uint8_t (*read)(void *, uint16_t);
    void    (*write)(void *, uint16_t, uint8_t);
    uint8_t (*in)(void *, uint16_t);               /* full 16-bit port address */
    void    (*out)(void *, uint16_t, uint8_t);
};

void z80_init(z80_t *z);
void z80_reset(z80_t *z);
int  z80_step(z80_t *z);   /* one instruction (or interrupt ack); returns t-states */

#define Z80_BC(z) (((z)->b << 8) | (z)->c)
#define Z80_DE(z) (((z)->d << 8) | (z)->e)
#define Z80_HL(z) (((z)->h << 8) | (z)->l)
#define Z80_IX(z) (((z)->ixh << 8) | (z)->ixl)
#define Z80_IY(z) (((z)->iyh << 8) | (z)->iyl)
#endif
