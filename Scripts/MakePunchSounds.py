# Makes the sounds of a fist landing on a body, from noise and tones, so they can be built again at any time.
# Usage: python MakePunchSounds.py <output folder>
# Writes PunchSkin_Mid_<n>.wav, PunchSkin_Fast_<n>.wav, PunchCloth_Mid_<n>.wav and PunchCloth_Fast_<n>.wav: a fist on bare
# skin slaps as well as thuds, one on clothing thuds duller under a rustle of the cloth; a fast one is harder, lower and
# sharper than one thrown at a middling pace. Each take is a little different.

import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

RATE = 44100
SECONDS = 0.45
TAKES = 6


def band(signal, low, high):
    sos = butter(4, [low / (RATE / 2), min(high, RATE / 2 - 100) / (RATE / 2)], btype="band", output="sos")
    return sosfilt(sos, signal)


def decay(length, seconds, attack=0.0015):
    t = np.arange(length) / RATE
    rise = np.clip(t / max(attack, 1e-4), 0.0, 1.0)
    return rise * np.exp(-t / seconds)


def thump(random, length, start_hz, end_hz, seconds, amount):
    # The body itself: a short low drop of pitch, the chest or belly giving under the fist.
    t = np.arange(length) / RATE
    sweep = end_hz + (start_hz - end_hz) * np.exp(-t / 0.035)
    phase = 2.0 * np.pi * np.cumsum(sweep) / RATE
    tone = np.sin(phase + random.uniform(0, np.pi))
    meat = band(random.standard_normal(length), 120.0, 1300.0)
    return (tone * 0.9 + meat * 0.45) * decay(length, seconds) * amount


def slap(random, length, low, high, seconds, amount, offset=0):
    # Skin on skin: a bright, very short crack of noise.
    burst = np.zeros(length)
    span = length - offset
    burst[offset:] = band(random.standard_normal(span), low, high) * decay(span, seconds, 0.0004)
    return burst * amount


def rustle(random, length, low, high, seconds, amount):
    # Cloth struck and dragged: a soft-edged hiss, a little ragged.
    noise = band(random.standard_normal(length), low, high)
    ragged = 0.6 + 0.4 * np.interp(np.arange(length), np.linspace(0, length, 24), random.uniform(0, 1, 24))
    return noise * ragged * decay(length, seconds, 0.004) * amount


def finish(signal, peak):
    fade = np.ones(len(signal))
    tail = int(0.04 * RATE)
    fade[-tail:] = np.linspace(1.0, 0.0, tail)
    signal = signal * fade
    signal = signal / max(np.max(np.abs(signal)), 1e-6) * peak
    return (signal * 32767).astype(np.int16)


def make(kind, fast, random):
    length = int(SECONDS * RATE)
    u = random.uniform
    if kind == "Skin":
        if fast:
            out = thump(random, length, u(160, 200), u(55, 70), u(0.055, 0.075), 1.25)
            out += slap(random, length, u(1800, 2600), u(8000, 10500), u(0.008, 0.013), u(0.9, 1.15))
            out += slap(random, length, u(700, 1100), u(3500, 5000), u(0.02, 0.03), 0.35)
        else:
            out = thump(random, length, u(130, 165), u(70, 90), u(0.04, 0.055), 1.0)
            out += slap(random, length, u(1500, 2100), u(6000, 8000), u(0.006, 0.01), u(0.55, 0.75))
            out += slap(random, length, u(1500, 2100), u(5000, 7000), 0.004, 0.2, int(u(0.006, 0.012) * RATE))
        return finish(out, 0.89 if fast else 0.8)
    if fast:
        out = thump(random, length, u(150, 185), u(55, 68), u(0.06, 0.08), 1.25)
        out = band(out, 40.0, 2200.0)
        out += rustle(random, length, u(900, 1300), u(4200, 5500), u(0.025, 0.04), u(0.45, 0.6))
        out += rustle(random, length, u(400, 700), u(1800, 2500), u(0.06, 0.09), 0.25)
    else:
        out = thump(random, length, u(115, 145), u(65, 85), u(0.045, 0.06), 1.0)
        out = band(out, 40.0, 1600.0)
        out += rustle(random, length, u(700, 1100), u(3000, 4000), u(0.04, 0.07), u(0.3, 0.42))
    return finish(out, 0.85 if fast else 0.75)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(folder, exist_ok=True)
    random = np.random.default_rng(1977)
    for kind in ("Skin", "Cloth"):
        for fast in (False, True):
            for take in range(TAKES):
                name = "Punch%s_%s_%d.wav" % (kind, "Fast" if fast else "Mid", take)
                wavfile.write(os.path.join(folder, name), RATE, make(kind, fast, random))
                print("wrote", name)


if __name__ == "__main__":
    main()
