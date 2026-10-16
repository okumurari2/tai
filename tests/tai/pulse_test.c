/* SPDX-License-Identifier: GPL-3.0-only */
/* T1 PULSE alone: silent until a beat, sounds after one, stays inside the range, a high attack rises faster than a low one. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "tai/pulse.h"

static pul_voice v;
static int fails;
#define CHECK(c, msg) do { if (!(c)) { printf("FAIL: %s\n", msg); fails++; } } while (0)

static int32_t peak(int32_t *o, uint32_t n)
{
    int32_t m = 0; uint32_t i;
    for (i = 0; i < n; i++) { int32_t a = abs(o[2u * i]); if (a > m) m = a; }
    return m;
}
static int32_t early(int32_t attack)           /* the level in the first 20 frames after a beat */
{
    static int32_t o[2 * 20];
    uint32_t i;
    for (i = 0; i < 40; i++) o[i] = 0;
    pul_make(&v);
    pul_run(&v, o, 20, 1, attack);
    return peak(o, 20);
}

int main(void)
{
    static int32_t o[2 * 4096];
    uint32_t i;
    pul_make(&v);
    for (i = 0; i < 2u * 4096u; i++) o[i] = 0;
    pul_run(&v, o, 4096, 0, Q15(0.5));
    CHECK(peak(o, 4096) == 0, "silent with no beat");
    pul_run(&v, o, 4096, 1, Q15(0.5));
    CHECK(peak(o, 4096) > 1000, "sounds after a beat");
    CHECK(peak(o, 4096) < 32767, "inside the range");
    for (i = 0; i < 2u * 4096u; i++) o[i] = 0;
    pul_run(&v, o, 4096, 1, Q15(0.5));
    pul_run(&v, o, 4096, 1, Q15(0.5));
    CHECK(v.pos >= PUL_LEN, "finishes; no beat, no restart");
    CHECK(early(Q15(1.0)) > early(Q15(0.0)) * 4, "attack: a high one rises faster");
    printf("pulse: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
