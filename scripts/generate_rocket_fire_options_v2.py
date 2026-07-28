"""Generate five full-bodied, original rocket launcher fire sounds."""

from __future__ import annotations

import math
import random
import struct
import wave
from pathlib import Path


RATE = 48_000
ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "rocket_sound_options_v2"

OPTIONS = [
    ("rocket_fire_v2_01_arena_body.wav", 0.48, 7001, 1.00, 1.00, 0.82, 1.00),
    ("rocket_fire_v2_02_heavy_pressure.wav", 0.64, 7002, 1.28, 0.86, 0.72, 1.16),
    ("rocket_fire_v2_03_aggressive_bark.wav", 0.50, 7003, 0.94, 1.22, 1.18, 1.08),
    ("rocket_fire_v2_04_compact_punch.wav", 0.40, 7004, 1.04, 1.10, 0.94, 0.86),
    ("rocket_fire_v2_05_big_launcher.wav", 0.72, 7005, 1.36, 0.96, 0.78, 1.30),
]


def lowpass(samples: list[float], cutoff: float) -> list[float]:
    alpha = 1.0 - math.exp(-2.0 * math.pi * cutoff / RATE)
    state = 0.0
    out = [0.0] * len(samples)
    for i, sample in enumerate(samples):
        state += alpha * (sample - state)
        out[i] = state
    return out


def highpass(samples: list[float], cutoff: float) -> list[float]:
    low = lowpass(samples, cutoff)
    return [sample - bass for sample, bass in zip(samples, low)]


def bandpass(samples: list[float], low: float, high: float) -> list[float]:
    return highpass(lowpass(samples, high), low)


def smoothstep(edge0: float, edge1: float, value: float) -> float:
    x = max(0.0, min(1.0, (value - edge0) / (edge1 - edge0)))
    return x * x * (3.0 - 2.0 * x)


def build(
    seconds: float,
    seed: int,
    low_weight: float,
    mid_weight: float,
    bite_weight: float,
    tail_weight: float,
) -> list[float]:
    rng = random.Random(seed)
    count = round(seconds * RATE)
    white_a = [rng.uniform(-1.0, 1.0) for _ in range(count)]
    white_b = [rng.uniform(-1.0, 1.0) for _ in range(count)]

    sub_noise = bandpass(white_a, 42.0, 180.0)
    low_roar = bandpass(white_a, 90.0, 480.0)
    mid_roar = bandpass(white_b, 320.0, 2100.0)
    bite = bandpass(white_a, 1700.0, 7600.0)
    air = highpass(white_b, 5800.0)

    samples = [0.0] * count
    pressure_phase = 0.0
    motor_phase = 0.0

    for i in range(count):
        t = i / RATE

        # The first 180 ms carries the launch pressure. Its falling pitch
        # reads as gas expansion rather than as a struck object.
        pressure_pos = min(1.0, t / 0.18)
        pressure_hz = 172.0 * (58.0 / 172.0) ** pressure_pos
        pressure_phase += 2.0 * math.pi * pressure_hz / RATE
        pressure_env = smoothstep(0.0, 0.0025, t) * math.exp(-t * 10.5)
        pressure = (
            math.sin(pressure_phase)
            + 0.38 * math.sin(2.0 * pressure_phase)
            + 0.13 * math.sin(3.0 * pressure_phase)
        ) * pressure_env

        # A short, dense low-mid plateau gives the shot weight after its peak.
        hold = smoothstep(0.0, 0.006, t)
        hold *= 1.0 - smoothstep(seconds * 0.34, seconds * 0.88, t)
        exhaust_env = hold * math.exp(-max(0.0, t - seconds * 0.20) * 4.4)
        exhaust = (
            sub_noise[i] * 2.3 * low_weight
            + low_roar[i] * 1.40 * low_weight
            + mid_roar[i] * 1.55 * mid_weight
        ) * exhaust_env

        # Broad ignition bite, with no narrow ringing tone.
        ignition_env = math.exp(-t * 34.0) * smoothstep(0.0, 0.0008, t)
        ignition = (
            bite[i] * 1.55 * bite_weight
            + air[i] * 0.25 * bite_weight
        ) * ignition_env

        # A rough jet pulse adds motion to the sustained roar.
        motor_hz = 37.0 + 12.0 * math.exp(-t * 8.0)
        motor_phase += 2.0 * math.pi * motor_hz / RATE
        pulse = 0.76 + 0.24 * math.sin(motor_phase)
        tail_env = smoothstep(0.018, 0.055, t)
        tail_env *= math.exp(-max(0.0, t - 0.07) * (6.2 / tail_weight))
        jet = (low_roar[i] * 0.78 + mid_roar[i] * 0.58) * pulse * tail_env

        # Sparse hot-gas pops live inside the roar, not above it.
        pop = 0.0
        if 0.012 < t < seconds * 0.55 and rng.random() < 0.0045:
            pop = rng.uniform(-0.20, 0.20) * math.exp(-t * 4.0)

        samples[i] = (
            pressure * 1.08 * low_weight
            + exhaust
            + ignition
            + jet * tail_weight
            + pop
        )

    # Broad saturation creates midrange harmonics and keeps the body present
    # on small speakers. Two very short room returns add size without a ring.
    dry = samples.copy()
    for delay_s, gain in ((0.007, 0.105), (0.016, 0.065), (0.029, 0.038)):
        delay = round(delay_s * RATE)
        for i in range(delay, count):
            samples[i] += dry[i - delay] * gain

    samples = highpass(samples, 30.0)
    samples = [math.tanh(sample * 2.05) for sample in samples]

    fade = min(round(0.055 * RATE), count)
    for i in range(count - fade, count):
        samples[i] *= (count - i - 1) / fade

    peak = max(abs(sample) for sample in samples) or 1.0
    gain = (10.0 ** (-0.8 / 20.0)) / peak
    return [sample * gain for sample in samples]


def write_wav(path: Path, samples: list[float]) -> None:
    pcm = bytearray()
    for sample in samples:
        pcm.extend(struct.pack("<h", round(max(-1.0, min(1.0, sample)) * 32767.0)))
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(RATE)
        wav.writeframes(pcm)


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, seconds, seed, low, mid, bite, tail in OPTIONS:
        path = OUT_DIR / name
        write_wav(path, build(seconds, seed, low, mid, bite, tail))
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
