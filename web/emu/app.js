// SPDX-License-Identifier: GPL-3.0-only
// TAI's part of its browser page (web/emu/index.html is niwa's and the same for all; docs/EMULATOR.md). docs/SPEC.md says what TAI is.
export default {
  title: "TAI",
  description: "TAI: four coupled organs keep one sample alive on the FM-1, and will not do as they are told. The body and its monitor, in your browser.",
  wasm: "tai.wasm",
  store: "tai-emu-flash",
  verbs: {},                                       // the ampoules (DRAIN, PRESSOR, RELAX, FREEZE) come later
  help: [
    "KNOB 1–4 (drag, wheel): PRESSURE · VENT · TENSION · STASIS",
    "<kbd>↑</kbd> <kbd>↓</kbd> SELECT = COMPLIANCE · PRESETS = METABOLISM · <kbd>←</kbd> <kbd>→</kbd> ALGORITHM = ELECTROLYTE",
    "keys, low third: PACE · middle (held): OCCLUDE · high third: COOL",
    "<kbd>Z</kbd>–<kbd>'</kbd> <kbd>Q</kbd>–<kbd>O</kbd> keys · <kbd>[</kbd> <kbd>]</kbd> OCT = sweep speed",
    "no sound from the body yet: a short tone under each key only",
  ],
  theme: { bg: "#7f8a86", body: "#121615", tray: "#0b0e0d", part: "#1f2624", edge: "#2f3a37", label: "#d3e0db", bezel: "#070908",
           keyface: "#0a0d0c", "btn-off": "#2b3330", lit: "#c9f5d8", "lit-ink": "#10201a", "slit-off": "#2c3532", "slit-lit": "#dff8e8" },
  credits: 'TAI, built on niwa · <a href="https://github.com/okumurari2/tai">source</a> (GPL-3.0)',
};
