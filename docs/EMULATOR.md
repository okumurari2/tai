# The device in a browser

niwa carries the page that MAKU and t8lab show their firmware in (a drawing of the FM-1: screen, knobs, buttons, the 27 keys; touch,
mouse and the computer keyboard; Web MIDI in; the settings kept in the browser). It is the same page for every firmware built on niwa.
What is the firmware's own is one small file, `web/emu/app.js`.

```
sh tests/run_tests.sh        builds and checks it (when clang has the wasm32 target, wasm-ld and node are there)
web/emu/build.sh             -> build/emu/   (APP=lofi web/emu/build.sh for another app)
python3 -m http.server -d build/emu          then open http://localhost:8000/
```

Published on GitHub Pages by `.github/workflows/emulator.yml` (every push to main; set Settings > Pages > Source to "GitHub Actions"
once). It builds with `APP` (the workflow's `env`), runs `emu_test.mjs` (a failing check stops the deploy; a firmware that sounds by itself sets `EMU_SELF_SOUND=1` in the workflow's `env`, which skips the two silence checks, "silent at rest" and "key up: silent"), and uploads `build/emu`.

## The parts

| file | whose | what |
|---|---|---|
| `web/emu/index.html` | niwa | the panel (an SVG, drawn twice: wide, and tall for a phone), input, screen, LEDs, flash, Web MIDI, "Switch on" |
| `web/emu/worklet.js` | niwa | the AudioWorklet that runs the WebAssembly: 128 frames per quantum, ~30 screen / LED updates a second |
| `web/emu/web_host.c` | niwa | the fake FM-1 for the page: `platform.c` includes it when `NIWA_WEB` is set; the exports below |
| `tests/sim.c` | niwa | the fake FM-1 itself, shared with `tests/host.c` (clock, keys, knobs, screen, settings, MIDI queue) |
| `web/emu/build.sh`, `emu_test.mjs` | niwa | clang (wasm32, wasm-ld; no Emscripten) and a Node test of the exports |
| `web/emu/app.js` | **the firmware** | its name, the logo, the words under the buttons (`verbs`), the help line, the credits, where the browser keeps the settings |

A firmware built on niwa changes only `app.js` (CLAUDE.md). The logo and the tab icon come from `assets/logo.svg` and
`assets/logo-mark.svg`.

## Colours

The page's palette is CSS variables at the top of `index.html`, and `app.js` can set any of them: `theme: { body: "#17151d", lit: "#e6d9ff" }`
(hex, `#rrggbb`; the variable's name without the `--`). `bg fg dim rule` the page and its text · `body tray part edge label` the case, the
wells, a button's face, its rim, the print · `bezel keyface` around the screen, a key · `btn-off lit lit-ink` a button off, lit, and the print on it ·
`slit-off slit-lit` a key's light · `green green-ink rec latch` PLAY's green, REC's print, the Shift-held rim. The breathing, the glow and the
mid level of an LED, and the power-on sweep, are mixes of the off and lit colours, so changing those two is enough. The screen's own colours
are the firmware's (`T_*`, `docs/UI.md`), not the page's. `web/emu/app.js` here is the logo's violet on a dark case; the page's own default
(no `theme`) is a warm white light on charcoal, as MAKU's.

## What a firmware needs

The page runs the firmware's loop from outside, one pass per millisecond of the device's time (WebAssembly has no way to stop
`for (;;)` in the middle). So the app says `#define APP_STEPPED` before `#include "platform.c"` and writes `app_init()` (once, after
boot) and `app_step()` (one pass of the loop) in place of `app_main()`; `platform.c` then supplies `app_main` for the FM-1 and the
computer, as `app_init(); for (;;) { platform_poll(); app_step(); }`. `niwa.c` is written this way. Keep the loop's state in
statics, not in locals of a function that never returns. An app with its own `app_main` still builds for the FM-1 and for
`tests/host.c`; it has no browser build until it is split.

What the browser build leaves out: USB (MIDI out, the audio class, the updater), the battery (unknown, so no notice), and the 0.8 s
power-on swell (`soft.h`). The screen is the same C drawing as on the device (`docs/UI.md`: never a separate one for the page).

## The exports (the contract between the wasm and the page)

MAKU's and t8lab's own browser builds (`web/emu/felucca_web.c`) speak the same one; that is how all three can share this page.
Needed: `web_render(n)` `web_out_l()` `web_out_r()` (the device runs until n frames exist) · `web_boot()` · `web_nor()`
`web_nor_size()` `web_nor_erase()` `web_flash_writes_count()` (the page keeps the flash sectors that change) · `web_buttons(mask)`
`web_keys(mask)` `web_enc(role, n)` `web_master(0..1023)` `web_midi(status, d1, d2)` · `web_lit_buttons()` `web_lit_keys()` ·
`web_screen()` (240 x 240 RGB565, bytes swapped as the panel gets them) `web_screen_draws()` · `web_now_ms()` `web_sample_rate()`.

- Buttons are the page's labels, bit 0..13 = FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+ (`web_host.c` maps them to
  niwa's `BTN_*`); encoder roles are SELECT ALGORITHM PRESETS KNOB1..4 = 0..6.
- Optional: `web_store_areas()` (a pointer to `uint32 {count, start, end, ...}`: the flash areas the page keeps; default: the settings
  sector 0xFC000..0xFFFFF) · `web_dim_buttons` `web_dim_keys` `web_dim_level` `web_breath_buttons` `web_breath_keys` `web_mid_keys` (more LED
  levels) · `web_anim_levels` (the power-on LED sweep) · `web_set_seed(n)` (`#seed=N` in the URL).
- The wasm needs no imports. The worklet is registered as `fm1-emu`.

## MAKU and t8lab

Their `web/emu/index.html` and `worklet.js` are the same page with their words in them. To take niwa's: copy the two files, write
their `app.js` (the `VERB` table, the help and footer lines, `store: "felucca-emu-flash"`, `wasm: "felucca.wasm"`), and add
`web_store_areas()` to `felucca_web.c` with their `STORE_AREAS`. `worklet.js` no longer has the rating-log snapshot (`snap`):
that is MAKU's, and goes next to its own build. The browser build of MAKU / t8lab stays on Emscripten (it compiles the whole of
Felucca); only the page is shared.

Not tried on a device or in all browsers: the page runs in headless Chromium here (boot, key, LED, screen); Safari / Firefox, touch and Web MIDI
are the same code as MAKU's page and are not re-tested.
