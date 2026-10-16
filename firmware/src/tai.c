/* SPDX-License-Identifier: GPL-3.0-only */
/* TAI: one short sample, four coupled organs that keep it alive and will not do as they are told. This is the first slice: the
 * body (tai/organs.h) and its bedside monitor. No DSP is wired to the body yet: the only sound is a short tone under the keys,
 * so a hand on the body is heard. T1 PULSE (tai/pulse.h) is wired: a built-in sample, started on each heartbeat. docs/SPEC.md says what each part is meant to become.
 *
 *   KNOB 1..4        PRESSURE, VENT, TENSION, STASIS: the four organs (0..127, 2 a detent, from 64 at power-on)
 *   SELECT           COMPLIANCE: stiff .. soft tissue           (these three are kept in flash)
 *   PRESETS          METABOLISM: how fast things clear
 *   ALGORITHM        ELECTROLYTE: the resonator's scale pull (not wired yet)
 *   OCT- OCT+        the monitor's sweep speed (kept)
 *   keys             low third: PACE (a forced beat), middle: OCCLUDE (held: the couplings cut; let go: a rebound),
 *                    high third: COOL (the ringing and the breathing drop)
 *   USB-MIDI         notes in act as the keys (and sound)
 *
 * Integers only (docs/RULES.md). tests/tai/sim.c runs the body alone and writes CSV. */
#ifndef APP_NAME
#define APP_NAME "TAI"
#endif
#ifndef SETTINGS_MAGIC
#define SETTINGS_MAGIC 0x54414931u              /* "TAI1" */
#endif
#define APP_STEPPED
#include "platform.c"
#include "quad.h"
#include "tai/organs.h"
#include "tai/pulse.h"

/* ---- the sound: a tone under each key (until the DSP is wired: docs/SPEC.md) */
#define NV 4u
static struct {
    volatile uint32_t inc;
    volatile int32_t gate;
    int32_t amp;
    uint32_t ph, age;
    uint8_t note;
} voice[NV];

static pul_voice pulse;
static volatile uint32_t snap_beats;            /* the body's beat count and attack, copied after each step (1 writer: the main loop) */
static volatile int32_t snap_attack;

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t v, i;
    pul_run(&pulse, out, n, snap_beats, snap_attack);
    for (v = 0; v < NV; v++) {
        uint32_t ph = voice[v].ph, step = voice[v].inc;
        int32_t amp = voice[v].amp, g = voice[v].gate;
        if (!amp && !g)
            continue;
        for (i = 0; i < n; i++) {
            int32_t s;
            if (amp < g) { amp += 48; if (amp > g) amp = g; }
            else if (amp > g) { amp -= 12; if (amp < g) amp = g; }
            ph += step;
            s = (pul_sine(ph) * amp) >> 15;
            out[2u * i] += s;
            out[2u * i + 1u] += s;
        }
        voice[v].ph = ph;
        voice[v].amp = amp;
    }
}

static const uint32_t INC_C9[12] = {
    815363807u, 863847862u, 915214929u, 969636441u, 1027294024u, 1088380105u,
    1153098554u, 1221665363u, 1294309365u, 1371273005u, 1452813141u, 1539201906u};
static uint32_t ages;
static void play(uint32_t note, uint32_t vel)
{
    uint32_t v, pick = 0, best = 0xFFFFFFFFu;
    if (note > 127u || !vel) return;
    for (v = 0; v < NV; v++) {
        uint32_t score = voice[v].note == note && voice[v].gate ? 0u
                       : voice[v].gate ? 0x80000000u + voice[v].age : (uint32_t)voice[v].amp + 1u;
        if (score < best) { best = score; pick = v; }
    }
    voice[pick].note = (uint8_t)note;
    voice[pick].age = ++ages;
    voice[pick].inc = INC_C9[note % 12u] >> (10u - note / 12u);
    voice[pick].gate = (int32_t)(vel * 60u);
}
static void stop(uint32_t note)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].note == note) voice[v].gate = 0;
}
static int quiet(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (voice[v].gate || voice[v].amp) return 0;
    return 1;
}

/* ---- the body and the hands */
#define KEY0 53u                                /* key 0 = F3 */
static org_body body;
static uint32_t held_occlude;                   /* middle-band keys (and MIDI notes) held: bit = key */

static void hand(uint32_t k, int down)          /* key k (0..26) down or up: the three bands */
{
    if (k < 9u) { if (down) org_pace(&body); }
    else if (k < 18u) {
        if (down) held_occlude |= 1u << k; else held_occlude &= ~(1u << k);
        org_occlude(&body, held_occlude != 0);
    } else if (down) org_cool(&body);
}

/* ---- the monitor, laid out like an operating-room bedside monitor: black ground, one colour per signal (ECG green, respiration
 * yellow, the pleth-like muscle cyan, venous pressure blue), the trace on the left, its number big on the right in the same colour,
 * red for the alarm. Not the palette tokens (UI.md 5.1 keeps the track-colour exception; the black and greys are fixed too).
 *
 * Who moves a value (the two things a knob and a neighbour can do look alike on a trace), shown on every lane:
 *   the bar under the number: SOLID = what the knob alone explains; AMBER = pushed up by the others; a hollow outline = held down
 *   the tag at the top right: KNOB (white, for 2 s after you turn it) or <HEART / <LUNG / <MUSCLE / <FLUID / <HAND / <TIDE (amber:
 *   the biggest other push, when it is big enough to see) */
#define TW 150u                                 /* the traces: x 0..149 */
#define LANE 50u                                /* 4 lanes, y 0..199 */
#define NX 153u                                 /* the numbers: x 153..239 */
#define BAR 202u                                /* the encoders: y 202..239 */
#define BLACK C_BLACK
static const uint16_t TRACE[4] = {RGB(64, 240, 96), RGB(255, 226, 60), RGB(70, 224, 238), RGB(74, 140, 255)};
static const uint16_t AMBER = RGB(255, 160, 40), ALARM = RGB(255, 56, 56), WHITE = RGB(246, 246, 250), GREY = RGB(132, 134, 146);
static const uint16_t CVP_FILL = RGB(14, 30, 70);
static const char *const LEAD[4] = {"ECG II", "RESP", "EMG", "CVP"};
static const char *const SRC_NAME[7] = {"", "<HEART", "<LUNG", "<MUSCLE", "<FLUID", "<HAND", "<TIDE"};
static const uint16_t SWEEP_MS[3] = {60, 30, 15};       /* per pixel */
static const char *const SWEEP_NAME[3] = {"12.5", "25", "50"};
static uint32_t turned_ms[4];                   /* when each knob was last turned */

static int32_t lo[4], hi[4], last_y[4];         /* this column's extent per trace, in pixels; the last column's end */
static uint32_t col, col_ms, rng_emg = 0x51u;

static int32_t ecg(uint32_t phase, int32_t amp)  /* one beat: P, QRS, T. -1..1 (Q15) */
{
    int32_t p = (int32_t)(phase >> 17), v = 0;  /* 0..32767 over the beat */
    if (p < 3000) v = q15_mul(q15_smooth(p, 0, 1500) - q15_smooth(p, 1500, 3000), Q15(0.12));          /* P */
    else if (p < 3700) v = -q15_mul(q15_smooth(p, 3000, 3350) - q15_smooth(p, 3350, 3700), Q15(0.15));  /* Q */
    else if (p < 4900) v = q15_smooth(p, 3700, 4300) - q15_smooth(p, 4300, 4900);                        /* R */
    else if (p < 5700) v = -q15_mul(q15_smooth(p, 4900, 5300) - q15_smooth(p, 5300, 5700), Q15(0.3));   /* S */
    else if (p > 9000 && p < 15000) v = q15_mul(q15_smooth(p, 9000, 12000) - q15_smooth(p, 12000, 15000), Q15(0.25));  /* T */
    return q15_mul(v, amp);
}
static int32_t capno(uint32_t rphase, int32_t co2)   /* one breath: the CO2 plateau out, the baseline in. 0..1 */
{
    int32_t p = (int32_t)(rphase >> 17);
    int32_t up = q15_smooth(p, 0, 2500), down = q15_smooth(p, 15000, 17000);
    int32_t slope = q15_mul(p < 15000 ? p : 15000, Q15(0.2));             /* the plateau leans up */
    return q15_mul(q15_clamp(up - down), q15_clamp(q15_mul(co2, Q15(1.2) + slope)));
}
static int32_t emg(void)                         /* the muscle: hiss by its Q, spike bursts while it rings. -1..1 */
{
    int32_t n = q15_noise(&rng_emg), v = q15_mul(n, Q15(0.05) + q15_mul(body.q, Q15(0.25)));
    if (body.osc > Q15(0.05) && (q15_noise(&rng_emg) & 0xFF) < (body.osc >> 8))
        v += n > 0 ? body.osc : -body.osc;
    return v < -32767 ? -32767 : v > 32767 ? 32767 : v;
}

static void sample(void)                         /* after each body step: widen this column's extents */
{
    int32_t v[4], i, y;
    v[0] = body.arrest_ms ? 0 : ecg(body.phase, Q15(0.4) + q15_mul(body.attack, Q15(0.6)));
    v[1] = capno(body.rphase, body.co2) * 2 - Q1;
    v[2] = emg();
    v[3] = body.vol * 2 - Q1;
    for (i = 0; i < 4; i++) {
        y = (int32_t)(LANE * (uint32_t)i) + 25 - ((v[i] * 22) >> 15);
        if (y < lo[i]) lo[i] = y;
        if (y > hi[i]) hi[i] = y;
    }
}
static void column(void)                         /* draw the column at col, clear one ahead */
{
    int32_t i, a, b;
    lcd_fill((col + 6u) % TW, 0, 1, 4u * LANE, BLACK);
    for (i = 0; i < 4; i++) {
        a = lo[i] < last_y[i] ? lo[i] : last_y[i];
        b = hi[i] > last_y[i] ? hi[i] : last_y[i];
        if (i == 3) {                            /* the fluid: a level, filled below */
            a = b = hi[i];
            lcd_fill(col, (uint32_t)a + 1u, 1, LANE * 4u - 2u - (uint32_t)a, CVP_FILL);
        }
        if (col == 0u) a = b = hi[i];            /* (no line back across the screen) */
        lcd_fill(col, (uint32_t)a, 1, (uint32_t)(b - a + 1), TRACE[i]);
        last_y[i] = hi[i];
        lo[i] = 1 << 20;
        hi[i] = -(1 << 20);
    }
    col = (col + 1u) % TW;
}

static void num(char *s, uint32_t v)            /* v (0..999) as text */
{
    char t[4];
    uint32_t i = 0, j = 0;
    do { t[i++] = (char)('0' + v % 10u); v /= 10u; } while (v && i < 3u);
    while (i) s[j++] = t[--i];
    s[j] = 0;
}
static int32_t shown_of(uint32_t lane)          /* what the number says, and the bar's length: Q15 */
{
    return lane == 0 ? q15_clamp((body.hr * 182) >> 8) : lane == 1 ? q15_clamp(body.vent) : lane == 2 ? body.q : body.vol;
}
static uint32_t lane_tag(uint32_t lane)         /* 0 none, 1 KNOB, 2 + source: an other push */
{
    int32_t p = body.push[lane];
    if (fm1_ms - turned_ms[lane] < 2000u) return 1u;
    if ((p < 0 ? -p : p) > Q15(0.07) && body.src[lane]) return 2u + body.src[lane];
    return 0;
}
static void draw_lane(uint32_t lane, uint32_t blink)
{
    char s[4];
    uint32_t v, tag = lane_tag(lane), flat = lane == 0 && body.arrest_ms != 0;
    int32_t val = shown_of(lane), base = body.base[lane], w, bw;
    v = lane == 0 ? (uint32_t)body.hr >> 8 : (uint32_t)val * (lane == 1 ? 30u : 127u) >> 15;
    num(s, v);
    cv_begin(240u - NX, LANE, BLACK);
    cv_text(2, 2, &AF_S, LEAD[lane], TRACE[lane]);
    if (flat) { if (blink) cv_text(2, 15, &AF_S, "ASYSTOLE", ALARM); }
    else if (tag == 1u) cv_text(86 - text_w(&AF_S, "KNOB") - 2, 2, &AF_S, "KNOB", WHITE);
    else if (tag) cv_text(86 - text_w(&AF_S, SRC_NAME[tag - 2u]) - 2, 2, &AF_S, SRC_NAME[tag - 2u], AMBER);
    cv_text_in(0, 14, 86, &AF_L, flat ? "---" : s, flat ? ALARM : TRACE[lane], BLACK);
    w = 82;                                     /* the bar: 82 px = the whole scale */
    bw = (val * w) >> 15;
    cv_rect(2, 44, w, 4, RGB(28, 28, 34));
    if (base > val) base = val;
    if (!flat) {
        int32_t kb = (base * w) >> 15;
        cv_rect(2, 44, kb, 4, TRACE[lane]);                          /* the knob's share */
        if (bw > kb) cv_rect(2 + kb, 44, bw - kb, 4, AMBER);         /* pushed up by the others */
        {
            int32_t want = (body.base[lane] * w) >> 15;              /* held down: where the knob alone would have put it */
            if (want > bw) { cv_rect(2 + bw, 44, want - bw, 1, TRACE[lane]); cv_rect(2 + bw, 47, want - bw, 1, TRACE[lane]); cv_rect(1 + want, 44, 1, 4, TRACE[lane]); }
        }
    }
    cv_blit(NX, lane * LANE);
}

static struct { uint8_t comp, metab, elyte, sweep; } set = {64, 64, 64, 1};
static uint8_t knob4[4] = {64, 64, 64, 64};
static uint32_t prev, t_change, dirty, shown_nums = ~0u, shown_bar = ~0u, last_ms, nums_at;
static uint8_t held[27];

static void draw_bar(void)
{
    static const char *const NAME[4] = {"COMPL", "METAB", "ELYTE", "SWEEP"};
    uint32_t i, v[3] = {set.comp, set.metab, set.elyte};
    char s[6];
    for (i = 0; i < 4u; i++) {
        cv_begin(60, 240u - BAR, BLACK);
        cv_text(4, 2, &AF_S, NAME[i], GREY);
        if (i < 3u) num(s, v[i]);
        cv_text(4, 18, &AF_M, i < 3u ? s : SWEEP_NAME[set.sweep], WHITE);
        cv_blit(i * 60u, BAR);
    }
}

static int32_t knob(int32_t v, uint32_t enc)
{
    v += fm1_enc_take(enc) * 2;
    return v < 0 ? 0 : v > 127 ? 127 : v;
}
static int32_t q7(uint32_t v) { return (int32_t)(v * 32767u / 127u); }

static void app_init(void)
{
    uint32_t i;
    settings_load(&set, sizeof set);
    if (set.comp > 127u || set.metab > 127u || set.elyte > 127u || set.sweep > 2u) { set.comp = set.metab = set.elyte = 64; set.sweep = 1; }
    org_init(&body, 0x7A1u);
    pul_make(&pulse);
    snap_beats = body.beats;
    pulse.seen = body.beats;
    for (i = 0; i < 4u; i++) { last_y[i] = (int32_t)(LANE * i + 25u); lo[i] = 1 << 20; hi[i] = -(1 << 20); }
    palette_set(UI_DEFAULT_PALETTE);
    lcd_fill(0, 0, 240, 240, BLACK);
    lcd_fill(0, BAR - 2u, 240, 1, RGB(48, 50, 58));
    lcd_fill(TW + 1u, 0, 1, BAR - 2u, RGB(48, 50, 58));
    for (i = 1; i < 4u; i++) lcd_fill(0, i * LANE - 1u, TW, 1, RGB(26, 28, 34));
    last_ms = col_ms = fm1_ms;
}

static void app_step(void)
{
    uint32_t keys, down, up, b, k, rel, lit = 0;
    uint8_t st, d1, d2;

    keys = fm1_in.notes;
    down = keys & ~prev;
    up = prev & ~keys;
    prev = keys;
    for (k = 0; k < 27u; k++) {
        if (down >> k & 1u) { held[k] = (uint8_t)(KEY0 + k); play(held[k], 100); midi_send(0x90, held[k], 100); hand(k, 1); }
        if (up >> k & 1u) { stop(held[k]); midi_send(0x80, held[k], 0); hand(k, 0); }
    }
    while (midi_get(&st, &d1, &d2)) {           /* a note in acts as the key of the same pitch (outside the keys: the nearest band) */
        k = d1 < KEY0 ? 0u : d1 - KEY0 > 26u ? 26u : d1 - KEY0;
        if ((st & 0xF0u) == 0x90u && d2) { play(d1, d2); hand(k, 1); }
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) { stop(d1); hand(k, 0); }
    }

    b = fm1_input_edges(&rel);
    if (b & (1u << BTN_OCT_DOWN) && set.sweep > 0u) { set.sweep--; dirty = 1; t_change = fm1_ms; }
    if (b & (1u << BTN_OCT_UP) && set.sweep < 2u) { set.sweep++; dirty = 1; t_change = fm1_ms; }
    k = (uint32_t)knob(set.comp, ENC_SELECT);
    if (k != set.comp) { set.comp = (uint8_t)k; dirty = 1; t_change = fm1_ms; }
    k = (uint32_t)knob(set.metab, ENC_PRESETS);
    if (k != set.metab) { set.metab = (uint8_t)k; dirty = 1; t_change = fm1_ms; }
    k = (uint32_t)knob(set.elyte, ENC_ALGORITHM);
    if (k != set.elyte) { set.elyte = (uint8_t)k; dirty = 1; t_change = fm1_ms; }
    for (k = 0; k < 4u; k++) {
        uint8_t was = knob4[k];
        knob4[k] = (uint8_t)knob(knob4[k], ENC_KNOB1 + k);
        if (knob4[k] != was) turned_ms[k] = fm1_ms;
        body.knob[k] = q7(knob4[k]);
    }
    body.compliance = q7(set.comp);
    body.metabolism = q7(set.metab);
    body.electrolyte = q7(set.elyte);

    while ((int32_t)(fm1_ms - last_ms) >= (int32_t)ORG_DT_MS) {          /* the body keeps its own time */
        last_ms += ORG_DT_MS;
        org_step(&body);
        snap_attack = body.attack;
        snap_beats = body.beats;
        sample();
        if ((int32_t)(last_ms - col_ms) >= SWEEP_MS[set.sweep]) { col_ms = last_ms; column(); }
    }

    if ((int32_t)(fm1_ms - nums_at) >= 200) {                            /* the numbers and bars: 5 times a second, when they changed */
        uint32_t blink = fm1_ms / 500u & 1u;
        k = blink;
        for (b = 0; b < 4u; b++) {
            k = quad_mix(k, (uint32_t)shown_of(b) >> 7);
            k = quad_mix(k, (uint32_t)(body.base[b] >> 7));
            k = quad_mix(k, lane_tag(b) + 8u * (body.arrest_ms != 0));
        }
        nums_at = fm1_ms;
        if (k != shown_nums) {
            shown_nums = k;
            for (b = 0; b < 4u; b++) draw_lane(b, blink);
        }
    }
    k = quad_mix(quad_mix(quad_mix(quad_mix(0u, set.comp), set.metab), set.elyte), set.sweep);
    if (k != shown_bar) { shown_bar = k; draw_bar(); }

    for (k = 0; k < NV; k++)
        if (voice[k].gate && voice[k].note >= KEY0 && voice[k].note < KEY0 + 27u)
            lit |= 1u << (voice[k].note - KEY0);
    for (k = 0; k < 27u; k++)
        fm1_led_key(KEY_LED(k), lit >> k & 1u);
    fm1_led_key(BTN_OCT_DOWN, set.sweep < 1u);
    fm1_led_key(BTN_OCT_UP, set.sweep > 1u);

    if (dirty && quiet() && fm1_ms - t_change > 2000u) {
        settings_save(&set, sizeof set);
        dirty = 0;
    }
}
