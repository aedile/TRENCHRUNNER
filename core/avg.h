/*
 * avg.h - Atari Analog Vector Generator (Star Wars variant)
 *
 * Port of MAME's avgdvg.cpp (BSD-3-Clause, Mathis Rosenhauer et al.) to plain C.
 * The AVG is a small sequencer driven by a 256-byte state PROM. It walks the
 * vector RAM/ROM (CPU addresses 0x0000-0x3FFF) and emits beam positions.
 */
#ifndef AVG_H
#define AVG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AVG_MAX_POINTS 2560

/* Beam coordinate space: 0..AVG_XMAX horizontally, 0..AVG_YMAX vertically (MAME visarea) */
#define AVG_XMAX 250
#define AVG_YMAX 280

typedef struct {
    int32_t x, y;        /* 16.16 fixed point, beam space */
    uint8_t color;       /* 4-bit color; bits 2,1,0 = red, green, blue (MAME vector color111) */
    uint8_t intensity;   /* 0 = move only, else brightness 0..255 */
} avg_point_t;

typedef struct {
    /* configuration */
    const uint8_t *prom;                       /* 256-byte state PROM */
    const uint8_t *ram;                        /* vector RAM, CPU 0x0000-0x2FFF */
    const uint8_t *rom;                        /* vector ROM, CPU 0x3000-0x3FFF */

    /* state (kept in a small struct so avg_go can work on a register copy) */
    struct avg_state {
        uint16_t pc;
        uint8_t  sp;
        uint16_t dvx, dvy;
        uint16_t stack[4];
        uint16_t data;
        uint8_t  state_latch;
        uint8_t  scale;
        uint8_t  intensity;
        uint8_t  op;
        uint8_t  halt;
        int32_t  xpos, ypos;
        uint8_t  dvy12;
        uint16_t timer;
        uint8_t  int_latch;
        uint8_t  bin_scale;
        uint8_t  color;
    } st;

    /* output */
    avg_point_t points[AVG_MAX_POINTS];
    int npoints;
    int overflow;
    uint32_t steps;      /* state machine iterations in the last list */
} avg_t;

void avg_init(avg_t *avg, const uint8_t *prom, const uint8_t *vector_ram, const uint8_t *vector_rom);
void avg_reset(avg_t *avg);          /* VGRST */
/* VGGO: run the display list to completion. Returns AVG clock cycles consumed
 * (12.096 MHz), so the caller can decide when VG_HALT becomes visible. */
uint32_t avg_go(avg_t *avg);
/* Same result without stepping the PROM (much faster); verified against avg_go on the host. */
uint32_t avg_go_fast(avg_t *avg);

#ifdef __cplusplus
}
#endif

#endif
