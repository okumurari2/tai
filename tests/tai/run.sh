#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# TAI's checks: the body alone (tests/tai/sim.c -> build/tai/*.csv), the app on the computer (tests/run_host.sh), the browser build.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/tai
cc -O2 -Wall -Ifirmware/src -o build/tai/sim tests/tai/sim.c && build/tai/sim build/tai
cc -O2 -Wall -Ifirmware/src -o build/tai/pulse_test tests/tai/pulse_test.c && build/tai/pulse_test
APP=tai sh tests/run_host.sh > /dev/null && echo "tai: host build ok (build/host/tai.wav, tai-*.png)"
if command -v node >/dev/null 2>&1 && command -v wasm-ld >/dev/null 2>&1 && clang --target=wasm32 -c -x c /dev/null -o /dev/null 2>/dev/null; then
    APP=tai sh web/emu/build.sh
    EMU_SELF_SOUND=1 node web/emu/emu_test.mjs build/emu/tai.wasm
else
    echo "== skip the browser build (needs node, wasm-ld and clang with the wasm32 target)"
fi
