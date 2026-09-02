"""Generate dry, dense rocket fire sounds without tube-like ringing."""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path


RATE = 48_000
ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "art" / "audio" / "auditions" / "rocket" / "fire" / "v3"

OPTIONS = [
    ("rocket_fire_v3_01_solid_arena.wav", 0.46, 8101, 1.00, 1.00, 0.86, 1.00),
    ("rocket_fire_v3_02_dense_heavy.wav", 0.58, 8102, 1.22, 1.10, 0.68, 1.12),
    ("rocket_fire_v3_03_hard_bark.wav", 0.43, 8103, 0.94, 1.25, 1.18, 0.86),
    ("rocket_fire_v3_04_dry_punch.wav", 0.36, 8104, 1.08, 1.16, 0.96, 0.70),
    ("rocket_fire_v3_05_full_thrust.wav", 0.66, 8105, 1.18, 1.04, 0.76, 1.30),
]


def lowpass(samples: list[float], cutoff: float) -> list[float]:
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    state = 0.0
    result = [0.0] * len(samples)
    for index, sample in enumerate(samples):
        state += alpha * (sample - state)
        result[index] = state
    return result


def highpass(samples: list[float], cutoff: float) -> list[float]:
    bass = lowpass(samples, cutoff)
    return [sample - low for sample, low in zip(samples, bass)]


def bandpass(samples: list[float], low: float, high: float) -> list[float]:
    return highpass(lowpass(samples, high), low)


def smoothstep(start: float, end: float, value: float) -> float:
    x = max(0.0, min(1.0, (value - start) / (end - start)))
    return x * x * (3.0 - 2.0 * x)


def compress_clean(samples: list[float]) -> list[float]:
    """Control peaks with a moving gain, without clipping or wave shaping."""
    attack = math.exp(-1.0 / (RATE * 0.0015))
    release = math.exp(-1.0 / (RATE * 0.060))
    threshold = 0.42
    ratio = 3.2
    envelope = 0.0
    result = [0.0] * len(samples)
    for index, sample in enumerate(samples):
        coefficient = attack if abs(sample) > envelope else release
        envelope = coefficient * envelope + (1.0 - coefficient) * abs(sample)
        if envelope > threshold:
            target = threshold + (envelope - threshold) / ratio
            gain = target / envelope
        else:
            gain = 1.0
        result[index] = sample * gain
    return result


def build(
    seconds: float,
    seed: int,
    low_amount: float,
    body_amount: float,
    attack_amount: float,
    tail_amount: float,
) -> list[float]:
    rng = random.Random(seed)
    count = round(seconds * RATE)
    noise_a = [rng.uniform(-1.0, 1.0) for _ in range(count)]
    noise_b = [rng.uniform(-1.0, 1.0) for _ in range(count)]
    noise_c = [rng.uniform(-1.0, 1.0) for _ in range(count)]

    weight = bandpass(noise_a, 48.0, 230.0)
    body = bandpass(noise_b, 120.0, 1050.0)
    presence = bandpass(noise_c, 650.0, 3200.0)
    crack = bandpass(noise_a, 2200.0, 9000.0)
    samples = [0.0] * count

    for index in range(count):
        t = index / RATE

        # One broad pressure pulse adds impact without forming a pitched ring.
        pulse_width = 0.017
        pulse_x = (t - 0.012) / pulse_width
        pressure = (1.0 - 2.0 * pulse_x * pulse_x) * math.exp(
            -pulse_x * pulse_x
        )
        pressure *= 0.46 * low_amount

        onset = smoothstep(0.0, 0.0022, t)
        sustain_end = seconds * 0.24
        exhaust_env = onset * math.exp(-max(0.0, t - sustain_end) * 6.4)
        exhaust = (
            weight[index] * 4.60 * low_amount
            + body[index] * 3.85 * body_amount
            + presence[index] * 2.20 * body_amount
        ) * exhaust_env

        ignition_env = smoothstep(0.0, 0.0006, t) * math.exp(-t * 38.0)
        ignition = (
            crack[index] * 0.92 + presence[index] * 0.84
        ) * ignition_env * attack_amount

        # A second dry gas surge fills the space behind the initial crack.
        surge_env = smoothstep(0.015, 0.038, t)
        surge_env *= math.exp(-max(0.0, t - 0.05) * (7.4 / tail_amount))
        surge = (
            weight[index] * 2.35 + body[index] * 1.84
        ) * surge_env * tail_amount

        samples[index] = pressure + exhaust + ignition + surge

    # Clean gain control keeps the low-mid body intact. No clipping, saturation,
    # delays, or narrow resonances are used.
    samples = highpass(samples, 27.0)
    samples = compress_clean(samples)
    samples = lowpass(samples, 11_500.0)

    fade = min(round(0.045 * RATE), count)
    for index in range(count - fade, count):
        samples[index] *= (count - index - 1) / fade

    peak = max(abs(sample) for sample in samples) or 1.0
    gain = (10.0 ** (-0.8 / 20.0)) / peak
    return [sample * gain for sample in samples]


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
    for name, seconds, seed, low, body, attack, tail in OPTIONS:
        path = OUT_DIR / name
        write_wav(path, build(seconds, seed, low, body, attack, tail))
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
