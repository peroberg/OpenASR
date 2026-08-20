#!/usr/bin/env python3
"""8th regression test's audio-level gate (keyboard-and-sample-bridge-7.md).

note_audio.lua only proves the register-level preconditions (MIDI
received, ES5506 voice registers written, instrument selected). This
script checks the actual -wavwrite capture for real, non-silent,
correctly-pitched audio -- the thing every earlier "voice writes
happened but it was still silent" turn in this series had to learn the
hard way not to assume.

Usage: check_note_audio.py <wav_path> <onset_seconds>
Exit 0 on pass, 1 on fail, with a one-line PASS/FAIL message on stdout
matching the sh regression harness's own convention.
"""
import sys
import wave
import array
import math

NAME = "note_audio_wav"

PEAK_WINDOW_START = 0.10   # seconds after onset
PEAK_WINDOW_END = 0.50
PEAK_THRESHOLD = 500       # out of 32767; well above any DC/noise floor

FREQ_WINDOW_START = 0.15
FREQ_WINDOW_END = 0.35
FREQ_MIN_HZ = 100.0
FREQ_MAX_HZ = 180.0
# Autocorrelation search range: wider than the pass band so a
# reported failure shows the actual pitch, not just "out of range",
# but still narrow enough to stay fast in pure Python (no numpy
# dependency in this checker).
FREQ_MIN_LIMIT_HZ = 80.0
FREQ_MAX_LIMIT_HZ = 260.0


def autocorr_pitch(chan, rate, s0, s1, fmin, fmax):
    seg = chan[s0:s1]
    n = len(seg)
    if n == 0:
        return None
    mean = sum(seg) / n
    seg = [x - mean for x in seg]
    if max(abs(x) for x in seg) == 0:
        return None
    lag_min = int(rate / fmax)
    lag_max = min(int(rate / fmin), n - 1)
    best_lag, best_val = None, -1.0
    for lag in range(lag_min, lag_max):
        acc = 0.0
        for i in range(n - lag):
            acc += seg[i] * seg[i + lag]
        if acc > best_val:
            best_val = acc
            best_lag = lag
    if best_lag is None or best_lag == 0:
        return None
    return rate / best_lag


def main():
    if len(sys.argv) != 3:
        print(f"FAIL {NAME} usage: check_note_audio.py <wav_path> <onset_seconds>")
        return 1
    path, onset_s = sys.argv[1], float(sys.argv[2])

    try:
        w = wave.open(path, "rb")
    except Exception as e:
        print(f"FAIL {NAME} cannot_open_wav error={e}")
        return 1

    ch = w.getnchannels()
    rate = w.getframerate()
    n_frames = w.getnframes()
    if ch < 2:
        print(f"FAIL {NAME} unexpected_channel_count channels={ch}")
        return 1

    data = w.readframes(n_frames)
    a = array.array("h", data)
    chan1 = a[1::ch]  # SPEAKER left, per asr10_boot.cpp's add_route(0, "speaker", 1.0, 0)

    p0 = int((onset_s + PEAK_WINDOW_START) * rate)
    p1 = int((onset_s + PEAK_WINDOW_END) * rate)
    if p1 > len(chan1):
        print(f"FAIL {NAME} wav_too_short frames={len(chan1)} needed={p1}")
        return 1
    seg = chan1[p0:p1]
    peak = max(abs(x) for x in seg) if seg else 0

    if peak < PEAK_THRESHOLD:
        print(f"FAIL {NAME} peak={peak} threshold={PEAK_THRESHOLD} window=[{PEAK_WINDOW_START},{PEAK_WINDOW_END}]s_after_onset")
        return 1

    f0_s = int((onset_s + FREQ_WINDOW_START) * rate)
    f0_e = int((onset_s + FREQ_WINDOW_END) * rate)
    freq = autocorr_pitch(chan1, rate, f0_s, f0_e, FREQ_MIN_LIMIT_HZ, FREQ_MAX_LIMIT_HZ)
    if freq is None:
        print(f"FAIL {NAME} peak_ok={peak} but_freq_undetectable")
        return 1

    freq_ok = FREQ_MIN_HZ <= freq <= FREQ_MAX_HZ
    if not freq_ok:
        print(f"FAIL {NAME} peak={peak} freq={freq:.1f}Hz expected=[{FREQ_MIN_HZ},{FREQ_MAX_HZ}]Hz")
        return 1

    print(f"PASS {NAME} peak={peak} freq={freq:.1f}Hz")
    return 0


if __name__ == "__main__":
    sys.exit(main())
