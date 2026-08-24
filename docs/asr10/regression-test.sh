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
# interrupt_controller: drives the recording chain (Sample-Source Select ->
#              Level Detect -> threshold -> RECORD start -> WAITING -> SCC1
#              RX -> IDMA) and asserts the priority-arbitrated level-4
#              interrupt controller's three load-bearing facts: vector $4D
#              for SCC1, vector $4B for IDMA completion via a real IACK (not
#              polling), and the genuine firmware IMR transient
#              $E480 -> $EC80 -> $E480 around the transfer.
# memory_size: locks in that ROM's own memory-size alias probe
#              ($F8A166-$F8A244) concludes base=$600000/size=$200000 (the
#              stock 2 MB machine) instead of the previous always-maximum
#              ~15.5 MB belief -- memory-size-belief-analysis.md.
# stereo_round_trip: injects two genuinely different byte patterns into
#              SCC1(LEFT)/SCC2(RIGHT) interleaved, waits for both IDMA
#              completions, then reads back each destination and checks
#              it against both patterns -- exact copy, own pattern fits,
#              other pattern does not (the channel-swap/merge check) --
#              investigations/stereo-round-trip-verification.md.
# display_protocol: locks in the display-protocol implementation --
#              underline/cursor rendering for the currently-selected
#              REC SRC field, and the confirmed instrument-select lamp
#              bit toggling 0->1->0 across select/deselect --
#              investigations/display-protocol-inventory.md.
# panel_input: locks in panel input mechanics -- two buttons held
#              simultaneously generate four distinct wire events (not
#              merged/ghosted), and a confirmed navigation button
#              (BTN_0A) genuinely changes REC SRC Field 2 --
#              investigations/panel-button-and-transport-map.md.
# panel_navigation: locks in the confirmed Left Arrow ($10) / Right
#              Arrow ($11) identity -- Left moves the underlined field
#              from REC SRC Field 2 to Field 1, Right moves it back --
#              using the display's own underline output as ground
#              truth. investigations/partial-update-position-probe.md.

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
run_test interrupt_controller docs/asr10/lua/interrupt_controller.lua "$IMAGE" || failures=$((failures + 1))
run_test memory_size docs/asr10/lua/memory_size.lua "$IMAGE" || failures=$((failures + 1))
run_test stereo_round_trip docs/asr10/lua/stereo_round_trip.lua "$IMAGE" || failures=$((failures + 1))
run_test display_protocol docs/asr10/lua/display_protocol.lua "$IMAGE" || failures=$((failures + 1))
run_test panel_input docs/asr10/lua/panel_input.lua "$IMAGE" || failures=$((failures + 1))
run_test panel_navigation docs/asr10/lua/panel_navigation.lua "$IMAGE" || failures=$((failures + 1))

if [ "$failures" -ne 0 ]; then
	echo "FAIL regression failures=${failures}"
	exit 1
fi

echo "PASS regression"
