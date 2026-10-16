/* SPDX-License-Identifier: GPL-3.0-only */
/* TAI's T1 PULSE and the built-in sample it reads (docs/SPEC.md 2). Pure C (integers only), so the FM-1, the browser and
 * tests/tai/pulse_test.c run this same file. The sample is made at power-on from a few decaying partials and a click: no flash
 * data yet (a recorded one replaces pul_make later; the voice does not change).
 *
 * Each heartbeat (the body's `beats` counter moving) starts the sample from its head. `attack` (0..1, from the body) sets how
 * fast the sound rises (about 45 ms at 0, about 1 ms at 1) and how hard it is driven into the soft clipper. */
#ifndef PULSE_H
#define PULSE_H
#include "core/q15.h"

#define PUL_LEN 8192u                       /* the sample: 8192 frames (0.19 s at 44.1 kHz), 16 kB */
#define PUL_LEVEL Q15(0.35)                 /* the output level: tune by ear */

static int32_t pul_sine(uint32_t ph)        /* 2^32 per cycle -> -32767..32767 (the parabola with a correction: niwa.c's) */
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x, y, ay;
    y = (x * (32768 - ax)) >> 13;
    ay = y < 0 ? -y : y;
    y += ((((y * ay) >> 15) - y) * 7373) >> 15;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

typedef struct {
    int16_t buf[PUL_LEN];
    uint32_t pos;                           /* read position; >= PUL_LEN: finished */
    int32_t env;                            /* the rise, Q15 */
    uint32_t seen;                          /* the last `beats` heard */
} pul_voice;

static void pul_make(pul_voice *p)
{
    static const uint32_t INC[3] = {21474836u, 32212254u, 64424509u};   /* 220, 330, 660 Hz at 44.1 kHz: 2^32 * f / 44100 */
    static const int32_t AMP[3] = {16000, 9000, 4000};
    uint32_t ph[3] = {0, 0, 0}, rng = 0x1234567u, i;
    int32_t j, dec[3] = {32767, 32767, 32767};
    static const int32_t DK[3] = {32690, 32600, 32400};                 /* per-frame decay: slower for the low partial */
    for (i = 0; i < PUL_LEN; i++) {
        int32_t s = 0;
        for (j = 0; j < 3; j++) {
            ph[j] += INC[j];
            s += (pul_sine(ph[j]) * AMP[j] >> 15) * dec[j] >> 15;
            dec[j] = q15_mul(dec[j], DK[j]);
        }
        if (i < 256u)                                                   /* the click at the head */
            s += q15_mul(q15_noise(&rng), (int32_t)(256u - i) * 40);
        p->buf[i] = (int16_t)(s > 32767 ? 32767 : s < -32767 ? -32767 : s);
    }
    p->pos = PUL_LEN;
    p->env = 0;
    p->seen = 0;
}

static int32_t pul_soft(int32_t x)          /* a soft clip: x in Q15, clamped to +-1 first; (3x - x^3) / 2 */
{
    int32_t x3, y;
    x = x > 32767 ? 32767 : x < -32767 ? -32767 : x;
    x3 = (((x * x) >> 15) * x) >> 15;
    y = (3 * x - x3) >> 1;
    return y > 32767 ? 32767 : y < -32767 ? -32767 : y;
}

/* Add n frames (interleaved stereo) to out. beats: the body's counter; attack: Q15. */
static void pul_run(pul_voice *p, int32_t *out, uint32_t n, uint32_t beats, int32_t attack)
{
    uint32_t i, pos;
    int32_t env, rise, gain;
    if (beats != p->seen) {
        p->seen = beats;
        p->pos = 0;
        p->env = 0;
    }
    pos = p->pos;
    if (pos >= PUL_LEN) return;
    env = p->env;
    attack = q15_clamp(attack);
    rise = 32767 / (2000 - ((attack * 1950) >> 15));                    /* 2000 frames (45 ms) .. 50 frames (1 ms) to full */
    gain = 4096 + ((attack * 3 * 4096) >> 15) * 4;                      /* x1 .. x4 (Q12): the drive */
    for (i = 0; i < n && pos < PUL_LEN; i++, pos++) {
        int32_t s = pul_soft((p->buf[pos] * gain) >> 12);
        env += rise;
        if (env > 32767) env = 32767;
        s = q15_mul(q15_mul(s, env), PUL_LEVEL);
        out[2u * i] += s;
        out[2u * i + 1u] += s;
    }
    p->pos = pos;
    p->env = env;
}
#endif
