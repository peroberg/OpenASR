#!/bin/sh
# ASR-10 Lua regression tests. Observation-only tests run via -autoboot_script.
#
# boot:    V3.50 reaches FILE 1 from reset.
# display: FILE 1 renders as a complete 22-character VFD line.
# button:  BTN_0A through the panel ioport moves FILE 1 -> FILE 2.
# button_upper: BTN_23 through the upper panel ioport reaches firmware as two RHRB bytes.
# nodisk:  no mounted disk produces PLEASE INSERT DISK.

set -u

MAME=${MAME:-./mame}
IMAGE=${ASR10_V350_IMAGE:-floppies/asr10booth/V350.img}
COMMON="-video none -sound none -nothrottle -skip_gameinfo -autoboot_delay 0"

run_test() {
	name=$1
	script=$2
	image=$3
	log="/tmp/asr10-regression-${name}.log"

	if [ -n "$image" ]; then
		SDL_VIDEODRIVER=dummy "$MAME" asr10booth -flop1 "$image" $COMMON \
			-seconds_to_run 60 -autoboot_script "$script" >"$log" 2>&1
	else
		SDL_VIDEODRIVER=dummy "$MAME" asr10booth $COMMON \
			-seconds_to_run 60 -autoboot_script "$script" >"$log" 2>&1
	fi

	if grep -q "^PASS ${name}" "$log"; then
		grep "^PASS ${name}" "$log"
		return 0
	fi

	if grep -q "^FAIL ${name}" "$log"; then
		grep "^FAIL ${name}" "$log"
	else
		echo "FAIL ${name} no PASS/FAIL line; see ${log}"
	fi
	return 1
}

failures=0

run_test boot docs/asr10/lua/boot.lua "$IMAGE" || failures=$((failures + 1))
run_test display docs/asr10/lua/display.lua "$IMAGE" || failures=$((failures + 1))
run_test button docs/asr10/lua/button.lua "$IMAGE" || failures=$((failures + 1))
run_test button_upper docs/asr10/lua/button_upper.lua "$IMAGE" || failures=$((failures + 1))
run_test nodisk docs/asr10/lua/nodisk.lua "" || failures=$((failures + 1))

if [ "$failures" -ne 0 ]; then
	echo "FAIL regression failures=${failures}"
	exit 1
fi

echo "PASS regression"
