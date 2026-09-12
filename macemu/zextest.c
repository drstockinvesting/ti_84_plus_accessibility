/* CP/M harness for the ZEX instruction-exerciser suites (CPU validation only). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "z80.h"

static uint8_t mem[0x10000];
static uint8_t rb(void *c, uint16_t a) { (void)c; return mem[a]; }
static void   wb(void *c, uint16_t a, uint8_t v) { (void)c; mem[a] = v; }
static uint8_t pin(void *c, uint16_t p) { (void)c; (void)p; return 0xFF; }
static void   pout(void *c, uint16_t p, uint8_t v) { (void)c; (void)p; (void)v; }

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: zextest FILE.com\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fread(mem + 0x100, 1, 0xFF00, f);
    fclose(f);

    z80_t z;
    memset(&z, 0, sizeof z);
    z.read = rb; z.fetch = rb; z.write = wb; z.in = pin; z.out = pout;
    z80_init(&z);
    z.pc = 0x100; z.sp = 0xF000;
    mem[0x0005] = 0xC9;                       /* BDOS entry: RET, intercepted below */
    mem[0x0000] = 0x76;                       /* HALT = exit */

    long long instr = 0;
    for (;;) {
        if (z.pc == 0x0005) {                 /* CP/M BDOS call */
            if (z.c == 2) putchar(z.e);
            else if (z.c == 9) {
                for (uint16_t a = (uint16_t)((z.d << 8) | z.e); mem[a] != '$'; a++) putchar(mem[a]);
            }
            fflush(stdout);
        }
        if (z.pc == 0x0000) break;
        z80_step(&z);
        instr++;
    }
    printf("\n%lld instructions, %lld t-states\n", instr, (long long)z.t);
    return 0;
}
