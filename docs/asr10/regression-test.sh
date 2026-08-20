#!/bin/sh
# ASR-10 Lua regression tests. Observation-only tests run via -autoboot_script.
#
# boot:    V3.50 reaches FILE 1 from reset.
# display: FILE 1 renders as a complete 22-character VFD line.
# button:  BTN_0A through the panel ioport moves FILE 1 -> FILE 2.
# button_upper: BTN_23 through the upper panel ioport reaches firmware as two RHRB bytes.
# nodisk:  no mounted disk produces PLEASE INSERT DISK.
# file_loaded: BTN_0A/23/02 loads JM DIGI SYN -- FILE LOADED *and* the IDMA
#              channel moves the full measured 172544-byte payload.
# mc68302_guards: a clean boot + load produces zero unexpected exception
#                 vectors, zero uninventoried SIB-register hits, zero
#                 IDMA (SAPR/CMR/BCR) anomalies.
# note_audio:  boots, loads, selects the instrument (BTN_02 from idle --
#              keyboard-and-sample-bridge-5.md), plays a note via MIDI,
#              and verifies real, non-silent, correctly-pitched audio
#              in the -wavwrite capture (not just register writes --
#              keyboard-and-sample-bridge-6.md found register writes
#              alone insufficient evidence of sound).

set -u

MAME=${MAME:-./mame}
IMAGE=${ASR10_V350_IMAGE:-floppies/asr10booth/V350.img}
PYTHON=${PYTHON:-python3}
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

run_test_audio() {
	name=$1
	script=$2
	image=$3
	log="/tmp/asr10-regression-${name}.log"
	wav="/tmp/asr10-regression-${name}.wav"

	rm -f "$wav"
	SDL_VIDEODRIVER=dummy "$MAME" asr10booth -flop1 "$image" $COMMON \
		-seconds_to_run 60 -autoboot_script "$script" -wavwrite "$wav" >"$log" 2>&1

	if ! grep -q "^PASS ${name}" "$log"; then
		if grep -q "^FAIL ${name}" "$log"; then
			grep "^FAIL ${name}" "$log"
		else
			echo "FAIL ${name} no PASS/FAIL line; see ${log}"
		fi
		return 1
	fi
	grep "^PASS ${name}" "$log"

	onset=$(grep "^NOTE_AUDIO_ONSET " "$log" | sed -n 's/.*t=\([0-9.]*\).*/\1/p')
	if [ -z "$onset" ]; then
		echo "FAIL ${name} no NOTE_AUDIO_ONSET timestamp in ${log}"
		return 1
	fi

	if ! "$PYTHON" docs/asr10/lua/check_note_audio.py "$wav" "$onset"; then
		return 1
	fi
	return 0
}

failures=0

run_test boot docs/asr10/lua/boot.lua "$IMAGE" || failures=$((failures + 1))
run_test display docs/asr10/lua/display.lua "$IMAGE" || failures=$((failures + 1))
run_test button docs/asr10/lua/button.lua "$IMAGE" || failures=$((failures + 1))
run_test button_upper docs/asr10/lua/button_upper.lua "$IMAGE" || failures=$((failures + 1))
run_test nodisk docs/asr10/lua/nodisk.lua "" || failures=$((failures + 1))
run_test file_loaded docs/asr10/lua/file_loaded.lua "$IMAGE" || failures=$((failures + 1))
run_test mc68302_guards docs/asr10/lua/mc68302_guards.lua "$IMAGE" || failures=$((failures + 1))
run_test_audio note_audio docs/asr10/lua/note_audio.lua "$IMAGE" || failures=$((failures + 1))

if [ "$failures" -ne 0 ]; then
	echo "FAIL regression failures=${failures}"
	exit 1
fi

echo "PASS regression"
