# Makes the weather sounds Aedir has no recording of, from noise, so they can be built again at any time.
# Usage: python MakeWeatherSounds.py <output folder>
# Writes Rain_Loop.wav: steady rain on open ground, a seamless twelve second stereo loop.

import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, sosfilt

RATE = 44100
SECONDS = 12.0
CROSSFADE = 1.0


def band(signal, low, high):
    sos = butter(4, [low / (RATE / 2), high / (RATE / 2)], btype="band", output="sos")
    return sosfilt(sos, signal)


def make_rain(random):
    length = int((SECONDS + CROSSFADE) * RATE)
    channels = []
    for _ in range(2):
        # The hiss of drops too many and too far to tell apart, breathing a little as the shower thickens and thins.
        hiss = band(random.standard_normal(length), 350.0, 9000.0)
        swell = 0.85 + 0.15 * np.interp(np.arange(length), np.linspace(0, length, 9), random.uniform(0.0, 1.0, 9))
        out = hiss * swell * 0.35

        # Single drops close by: a short tick of bright noise, many small ones and now and then a heavy one.
        ticks = np.zeros(length)
        for _ in range(int(380 * (SECONDS + CROSSFADE))):
            start = random.integers(0, length - 2000)
            heavy = random.uniform() < 0.06
            span = random.integers(600, 1500) if heavy else random.integers(120, 500)
            decay = np.exp(-np.linspace(0.0, 6.0, span))
            ticks[start:start + span] += random.standard_normal(span) * decay * random.lognormal(-1.2, 0.6) * (2.2 if heavy else 1.0)
        out += band(ticks, 1500.0, 12000.0) * 0.5 + band(ticks, 500.0, 2500.0) * 0.25
        channels.append(out)

    stereo = np.stack(channels, axis=1)
    # The end fades into the beginning, so the loop has no seam.
    fade = int(CROSSFADE * RATE)
    body = stereo[: length - fade].copy()
    ramp = np.linspace(0.0, 1.0, fade)[:, None]
    body[:fade] = body[:fade] * ramp + stereo[length - fade:] * (1.0 - ramp)
    body /= np.max(np.abs(body))
    return (body * 0.7 * 32767).astype(np.int16)


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(folder, exist_ok=True)
    random = np.random.default_rng(11)
    wavfile.write(os.path.join(folder, "Rain_Loop.wav"), RATE, make_rain(random))
    print("Wrote Rain_Loop.wav to", folder)


if __name__ == "__main__":
    main()
