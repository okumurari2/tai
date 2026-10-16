// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// The browser build (build/emu/APP.wasm, web/emu/web_host.c) in Node, as the worklet runs it. After X0X's tests/host/emu_test.mjs
// (charlesvestal/fm1-x0x, GPL-3.0). It checks the exports the page needs (docs/EMULATOR.md), not what a firmware does with them,
// so it is the same for every firmware built on niwa: it boots and draws, a key makes sound and lights its LED, a button and a knob
// reach the firmware, MIDI in sounds, MASTER scales, the flash sectors kept as the page keeps them come back in a fresh instance,
// and the same gestures twice give the same audio.
//   node web/emu/emu_test.mjs build/emu/niwa.wasm
import fs from "fs";
import { storeAreas, storeSectors, restoreSectors } from "./worklet.js";

const bytes = fs.readFileSync(process.argv[2] || "build/emu/niwa.wasm");
const B = { OCTDN: 12, OCTUP: 13 };               // the page's labels (index.html B)
const SELF = !!process.env.EMU_SELF_SOUND;     // a firmware that sounds by itself (a body that keeps beating): the two silence checks are skipped
const EN = { SELECT: 0, PRESETS: 2 };
let fails = 0;
const check = (what, ok) => { console.log(`emu: ${what.padEnd(78)} ${ok ? "ok" : "FAIL"}`); if (!ok) fails++; };

async function device(saved) {
  const { instance } = await WebAssembly.instantiate(bytes, {});
  const ex = instance.exports, mem = ex.memory;
  if (ex._initialize) ex._initialize();
  const areas = storeAreas(ex);
  ex.web_nor_erase();
  if (saved) restoreSectors(new Uint8Array(mem.buffer, ex.web_nor(), ex.web_nor_size()), saved, areas);
  ex.web_boot();
  const d = { ex, mem, areas, peak: 0, sig: 0, rec: false };
  d.render = (ms) => {
    const n = Math.round(ms * 44.1);
    d.peak = 0;
    for (let k = 0; k < n; k += 128) {
      const m = Math.min(128, n - k);
      ex.web_render(m);
      const l = new Float32Array(mem.buffer, ex.web_out_l(), m), r = new Float32Array(mem.buffer, ex.web_out_r(), m);
      for (let i = 0; i < m; i++) {
        d.peak = Math.max(d.peak, Math.abs(l[i]), Math.abs(r[i]));
        if (d.rec) d.sig = (d.sig * 31 + Math.round(l[i] * 32768) + 7 * Math.round(r[i] * 32768)) % 1000000007;
      }
    }
  };
  d.tap = (b) => { ex.web_buttons(1 << b); d.render(60); ex.web_buttons(0); d.render(60); };
  d.turn = (role, n) => { for (let i = 0; i < Math.abs(n); i++) { ex.web_enc(role, Math.sign(n)); d.render(40); } };
  d.screen = () => new Uint16Array(mem.buffer, ex.web_screen(), 240 * 240).slice();
  return d;
}
const colours = (fb) => new Set(fb).size;
const same = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);

// ---- the contract: every export the page and the worklet call
const a = await device(null);
const need = ["web_render", "web_out_l", "web_out_r", "web_nor", "web_nor_size", "web_nor_erase", "web_flash_writes_count", "web_boot",
              "web_buttons", "web_keys", "web_enc", "web_master", "web_midi", "web_lit_buttons", "web_lit_keys", "web_screen",
              "web_screen_draws", "web_now_ms", "web_sample_rate"];
check("exports: " + need.length + " functions, and the memory", need.every((n) => typeof a.ex[n] === "function") && a.mem instanceof WebAssembly.Memory);
check("web_sample_rate: 44100", a.ex.web_sample_rate() === 44100);
check("web_store_areas: the settings sector, 4 KiB aligned", a.areas.length >= 1 && a.areas.every(([x, y]) => x % 4096 === 0 && y % 4096 === 0 && y <= a.ex.web_nor_size()));

// ---- boot, screen, silence
a.render(300);
const idle = a.screen();
check(`boots: the screen is drawn (${colours(idle)} colours), the draw count moved`, colours(idle) > 3 && a.ex.web_screen_draws() > 0);
check("silent at rest" + (SELF ? " (skipped: EMU_SELF_SOUND)" : ""), SELF || a.peak === 0);
check("the clock follows the audio: 300 ms rendered, web_now_ms within a block of it", Math.abs(a.ex.web_now_ms() - 300) <= 6);

// ---- a key sounds and lights its LED, then the sound dies
a.ex.web_keys(1 << 7);
a.render(120);
check(`a key: it sounds (peak ${a.peak.toFixed(3)}), its LED is lit`, a.peak > 0.01 && (a.ex.web_lit_keys() >> 7 & 1) === 1);
a.ex.web_keys(0);
a.render(1500);
a.render(100);
check("the key up: silent again, the LED dark" + (SELF ? " (sound skipped)" : ""), (SELF || a.peak < 0.0005) && a.ex.web_lit_keys() === 0);

// ---- MIDI in sounds; MASTER scales it
a.render(100);
check("MIDI in: a note on is taken", a.ex.web_midi(0x90, 69, 100) === 1);
a.render(120);
const p1 = a.peak;
check(`MIDI in sounds (peak ${p1.toFixed(3)})`, p1 > 0.01);
a.ex.web_midi(0x80, 69, 0);
a.render(1500);
a.ex.web_master(300);
a.ex.web_midi(0x90, 69, 100);
a.render(120);
check(`MASTER 300/1023 is quieter (peak ${a.peak.toFixed(3)} < ${p1.toFixed(3)})`, a.peak > 0 && a.peak < p1 * 0.5);
a.ex.web_midi(0x80, 69, 0);
a.ex.web_master(700);
a.render(1500);

// ---- a button and a knob reach the firmware (they change the screen), the page's labels map to the firmware's ids
const before = a.screen();
a.turn(EN.SELECT, -8);
a.render(100);
check("a knob (SELECT): the screen changes", !same(a.screen(), before));
const mid = a.screen();
a.tap(B.OCTUP);
a.render(100);
check("a button (OCT+): the screen changes, its LED lights", !same(a.screen(), mid) && (a.ex.web_lit_buttons() >> B.OCTUP & 1) === 1 && (a.ex.web_lit_buttons() >> B.OCTDN & 1) === 0);

// ---- the flash the page keeps: the settings written by the firmware come back in a fresh instance
const w0 = a.ex.web_flash_writes_count();
a.render(2600);
const saved = storeSectors(new Uint8Array(a.mem.buffer, a.ex.web_nor(), a.ex.web_nor_size()), a.areas);
check(`a change is saved after a quiet moment (${Object.keys(saved).length} sector kept)`, a.ex.web_flash_writes_count() > w0 && Object.keys(saved).length > 0);
const b = await device(saved);
b.render(300);
const c = await device(null);
c.render(300);
const nor = (d) => new Uint8Array(d.mem.buffer, d.ex.web_nor(), d.ex.web_nor_size());
check("a fresh instance with the sectors shows what was saved; without them, the defaults",
      !same(b.screen(), c.screen()) && same(c.screen(), idle) && same(storeSectors(nor(b), b.areas)[a.areas[0][0]] || [], saved[a.areas[0][0]] || [1]));

// ---- deterministic: the same gestures twice, the same audio
async function run() {
  const d = await device(null);
  d.render(200);
  d.rec = true;
  d.ex.web_keys(1 << 7 | 1 << 11);
  d.render(300);
  d.ex.web_midi(0x90, 72, 90);
  d.render(300);
  d.ex.web_keys(0);
  d.render(600);
  return d.sig;
}
const s1 = await run(), s2 = await run();
check(`the same gestures twice give the same audio (${s1})`, s1 === s2 && s1 !== 0);

// ---- the cost: 1 s of two chords against real time
{
  const d = await device(null);
  d.render(100);
  d.ex.web_keys(1 << 7 | 1 << 11 | 1 << 14 | 1 << 19);
  d.render(100);
  const t0 = performance.now();
  d.render(1000);
  const cost = (performance.now() - t0) / 1000, max = +(process.env.BENCH_MAX || 0.5);
  check(`cost: 1 s of audio in ${(cost * 1000).toFixed(0)} ms (${(cost * 100).toFixed(1)} % of real time, max ${max * 100} %)`, cost < max);
}

console.log(fails ? `emu: ${fails} FAILED` : "emu: all ok");
process.exit(fails ? 1 : 0);
