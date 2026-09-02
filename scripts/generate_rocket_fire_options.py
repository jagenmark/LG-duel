"""Generate five original, game-ready rocket launcher fire sounds."""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path


RATE = 48_000
ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "art" / "audio" / "auditions" / "rocket" / "fire" / "v1"


OPTIONS = [
    {
        "name": "rocket_fire_01_arena_punch.wav",
        "seconds": 0.205,
        "seed": 3101,
        "body_hz": 108.0,
        "body_drop": 61.0,
        "grit": 0.58,
        "metal_hz": 1120.0,
        "metal": 0.11,
        "tail": 0.16,
        "drive": 1.65,
    },
    {
        "name": "rocket_fire_02_heavy_tube.wav",
        "seconds": 0.285,
        "seed": 3102,
        "body_hz": 82.0,
        "body_drop": 38.0,
        "grit": 0.44,
        "metal_hz": 720.0,
        "metal": 0.08,
        "tail": 0.29,
        "drive": 1.85,
    },
    {
        "name": "rocket_fire_03_dirty_ignition.wav",
        "seconds": 0.235,
        "seed": 3103,
        "body_hz": 126.0,
        "body_drop": 72.0,
        "grit": 0.82,
        "metal_hz": 1580.0,
        "metal": 0.09,
        "tail": 0.12,
        "drive": 2.15,
    },
    {
        "name": "rocket_fire_04_mech_clack.wav",
        "seconds": 0.225,
        "seed": 3104,
        "body_hz": 98.0,
        "body_drop": 54.0,
        "grit": 0.46,
        "metal_hz": 2060.0,
        "metal": 0.24,
        "tail": 0.17,
        "drive": 1.55,
    },
    {
        "name": "rocket_fire_05_deep_whoomp.wav",
        "seconds": 0.340,
        "seed": 3105,
        "body_hz": 67.0,
        "body_drop": 28.0,
        "grit": 0.36,
        "metal_hz": 930.0,
        "metal": 0.07,
        "tail": 0.38,
        "drive": 1.95,
    },
]


def decay(t: float, rate: float) -> float:
    return math.exp(-t * rate)


def lowpass(samples: list[float], cutoff_hz: float) -> list[float]:
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    out: list[float] = []
    state = 0.0
    for sample in samples:
        state += alpha * (sample - state)
        out.append(state)
    return out


def highpass(samples: list[float], cutoff_hz: float) -> list[float]:
    low = lowpass(samples, cutoff_hz)
    return [sample - low_sample for sample, low_sample in zip(samples, low)]


def make_sound(option: dict[str, float | int | str]) -> list[float]:
    rng = random.Random(int(option["seed"]))
    count = round(float(option["seconds"]) * RATE)
    white = [rng.uniform(-1.0, 1.0) for _ in range(count)]
    exhaust = lowpass(white, 4200.0)
    thud_noise = lowpass(white, 260.0)
    crackle = highpass(white, 1900.0)
    output = [0.0] * count

    body_phase = 0.0
    metal_phase = 0.0
    ring_phase = 0.0
    body_hz = float(option["body_hz"])
    body_drop = float(option["body_drop"])
    metal_hz = float(option["metal_hz"])

    for index in range(count):
        t = index / RATE
        attack = min(1.0, t / 0.0012)

        body_freq = max(42.0, body_hz - body_drop * min(1.0, t / 0.085))
        body_phase += 2.0 * math.pi * body_freq / RATE
        body = math.sin(body_phase) * attack * decay(t, 17.0)
        body += 0.34 * math.sin(body_phase * 0.51) * attack * decay(t, 13.0)

        blast_env = attack * decay(t, 25.0)
        blast = exhaust[index] * blast_env * float(option["grit"])
        blast += thud_noise[index] * attack * decay(t, 20.0) * 1.05

        metal_phase += 2.0 * math.pi * (metal_hz * (1.0 - 0.18 * t)) / RATE
        metal = math.sin(metal_phase) * decay(t, 46.0) * float(option["metal"])
        ring_phase += 2.0 * math.pi * (metal_hz * 1.47) / RATE
        metal += math.sin(ring_phase) * decay(t, 72.0) * float(option["metal"]) * 0.42

        click = 0.0
        if t < 0.010:
            click = crackle[index] * (1.0 - t / 0.010) ** 2 * 0.62
        if 0.012 < t < 0.050 and rng.random() < 0.018:
            click += rng.choice((-1.0, 1.0)) * decay(t - 0.012, 34.0) * 0.32

        tail_start = 0.038
        tail = 0.0
        if t > tail_start:
            tail_t = t - tail_start
            tail = exhaust[index] * decay(tail_t, 14.0) * float(option["tail"])

        output[index] = body * 0.92 + blast + metal + click + tail

    # A short reflection adds tube depth without making the cue sound distant.
    reflected = output.copy()
    for delay_seconds, gain in ((0.011, 0.13), (0.019, -0.08), (0.031, 0.055)):
        delay_samples = round(delay_seconds * RATE)
        for index in range(delay_samples, count):
            reflected[index] += output[index - delay_samples] * gain

    # Remove low drift, add firm saturation, and fade the final samples.
    reflected = highpass(reflected, 28.0)
    drive = float(option["drive"])
    shaped = [math.tanh(sample * drive) for sample in reflected]
    fade_samples = min(round(0.028 * RATE), count)
    for index in range(count - fade_samples, count):
        shaped[index] *= (count - index - 1) / fade_samples

    peak = max(abs(sample) for sample in shaped) or 1.0
    gain = (10.0 ** (-1.0 / 20.0)) / peak
    return [sample * gain for sample in shaped]


def write_wav(path: Path, samples: list[float]) -> None:
    pcm = bytearray()
    for sample in samples:
        value = round(max(-1.0, min(1.0, sample)) * 32767.0)
        pcm.extend(struct.pack("<h", value))
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for option in OPTIONS:
        path = OUT_DIR / str(option["name"])
        write_wav(path, make_sound(option))
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
