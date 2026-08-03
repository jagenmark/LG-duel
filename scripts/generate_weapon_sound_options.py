"""Build audition options for rocket impacts, sniper shots, and revolver shots.

The source pack in tmp/weapon_sound_sources/gunshots is the CC-BY 3.0
"Gunshots!" pack by dklon. The rocket launch WAVs were built earlier from the
CC0 OpenGameArt rocket engine and bang packs already kept in this project.
"""

from __future__ import annotations

import math
import wave
from pathlib import Path

import numpy as np
from scipy.signal import butter, resample_poly, sosfilt


ROOT = Path(__file__).resolve().parents[1]
RATE = 48_000
TARGET_PEAK = 10.0 ** (-1.0 / 20.0)

GUNSHOT_DIR = ROOT / "tmp" / "weapon_sound_sources" / "gunshots"
ROCKET_DIR = ROOT / "rocket_sound_options_v4"


def read_wav(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as handle:
        rate = handle.getframerate()
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        frames = handle.getnframes()
        raw = handle.readframes(frames)

    if width != 2:
        raise ValueError(f"{path} is not a 16-bit WAV")
    data = np.frombuffer(raw, dtype="<i2").reshape(-1, channels)
    result = data.astype(np.float64).mean(axis=1) / 32768.0
    if rate != RATE:
        factor = math.gcd(rate, RATE)
        result = resample_poly(result, RATE // factor, rate // factor)
    result -= float(np.mean(result))
    return result


def write_wav(path: Path, samples: np.ndarray) -> None:
    samples = np.asarray(samples, dtype=np.float64)
    samples -= float(np.mean(samples))
    peak = float(np.max(np.abs(samples))) or 1.0
    samples = np.clip(samples * (TARGET_PEAK / peak), -1.0, 1.0)
    encoded = np.round(samples * 32767.0).astype("<i2").tobytes()
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(encoded)


def filter_audio(samples: np.ndarray, low: float | None = None, high: float | None = None) -> np.ndarray:
    if low is not None and high is not None:
        kind = "bandpass"
        cutoff: float | tuple[float, float] = (low, high)
    elif low is not None:
        kind = "highpass"
        cutoff = low
    elif high is not None:
        kind = "lowpass"
        cutoff = high
    else:
        return samples.copy()
    sos = butter(4, cutoff, btype=kind, fs=RATE, output="sos")
    return sosfilt(sos, samples)


def exp_envelope(count: int, decay: float, attack: float = 0.0004) -> np.ndarray:
    time = np.arange(count, dtype=np.float64) / RATE
    envelope = np.exp(-time / max(decay, 0.001))
    if attack > 0:
        envelope *= np.minimum(1.0, time / attack)
    return envelope


def noise_burst(
    count: int,
    seed: int,
    low: float,
    high: float,
    decay: float,
    attack: float = 0.0004,
) -> np.ndarray:
    rng = np.random.default_rng(seed)
    noise = rng.standard_normal(count)
    return filter_audio(noise, low=low, high=high) * exp_envelope(count, decay, attack)


def low_body(
    count: int,
    f_start: float,
    f_end: float,
    decay: float,
    seed: int,
    noise_gain: float = 0.35,
) -> np.ndarray:
    time = np.arange(count, dtype=np.float64) / RATE
    phase = 2.0 * np.pi * (f_start * time + (f_end - f_start) * time * time / (2.0 * max(time[-1], 1.0 / RATE)))
    tone = np.sin(phase) * exp_envelope(count, decay, 0.001)
    rumble = noise_burst(count, seed, 35.0, 700.0, decay * 1.2, 0.001)
    return tone + rumble * noise_gain


def metal_tick(count: int, f_start: float, f_end: float, decay: float) -> np.ndarray:
    time = np.arange(count, dtype=np.float64) / RATE
    phase = 2.0 * np.pi * (f_start * time + (f_end - f_start) * time * time / (2.0 * max(time[-1], 1.0 / RATE)))
    tone = np.sin(phase) * exp_envelope(count, decay, 0.0001)
    overtone = np.sin(phase * 1.93) * exp_envelope(count, decay * 0.62, 0.0001)
    return tone * 0.75 + overtone * 0.25


def extract_peak(samples: np.ndarray, seconds: float, lead: float = 0.004, fade: float = 0.08) -> np.ndarray:
    count = round(seconds * RATE)
    peak = int(np.argmax(np.abs(samples)))
    start = max(0, peak - round(lead * RATE))
    result = np.zeros(count, dtype=np.float64)
    chunk = samples[start : start + count]
    result[: len(chunk)] = chunk
    fade_count = min(len(result), round(fade * RATE))
    if fade_count:
        result[-fade_count:] *= np.linspace(1.0, 0.0, fade_count, endpoint=True)
    return result


def put(destination: np.ndarray, source: np.ndarray, start: float = 0.0, gain: float = 1.0) -> None:
    offset = max(0, round(start * RATE))
    if offset >= len(destination):
        return
    count = min(len(source), len(destination) - offset)
    destination[offset : offset + count] += source[:count] * gain


def source_layer(path: Path, seconds: float, lead: float = 0.004) -> np.ndarray:
    return extract_peak(read_wav(path), seconds, lead=lead)


def make_rocket_option(
    source_name: str,
    seconds: float,
    source_gain: float,
    body_start: float,
    body_end: float,
    body_decay: float,
    crack_gain: float,
    seed: int,
) -> np.ndarray:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)

    # A small ground hit comes first; the main blast starts a little later.
    put(result, low_body(round(0.085 * RATE), 105.0, 62.0, 0.075, seed + 10, 0.18), 0.0, 0.40)
    put(result, metal_tick(round(0.020 * RATE), 900.0, 260.0, 0.013), 0.004, 0.08)

    source = source_layer(ROCKET_DIR / source_name, seconds - 0.022, lead=0.006)
    put(result, source, 0.022, source_gain)

    put(result, low_body(count, body_start, body_end, body_decay, seed + 20, 0.42), 0.018, 0.36)
    put(result, noise_burst(count, seed + 30, 65.0, 1850.0, body_decay * 0.72, 0.0007), 0.018, 0.24)
    put(result, noise_burst(round(0.034 * RATE), seed + 40, 2200.0, 10500.0, 0.010, 0.00015), 0.024, crack_gain)
    put(result, metal_tick(round(0.045 * RATE), 3600.0, 1100.0, 0.031), 0.052, 0.06)
    return result


ROCKET_OPTIONS = [
    ("rocket_impact_01_arena_thunder.wav", "rocket_fire_v4_01_clean_launch.wav", 0.62, 0.65, 82.0, 39.0, 0.34, 0.38, 101),
    ("rocket_impact_02_heavy_boom.wav", "rocket_fire_v4_02_heavy_launch.wav", 0.70, 0.61, 68.0, 31.0, 0.42, 0.30, 202),
    ("rocket_impact_03_sharp_burst.wav", "rocket_fire_v4_03_sharp_launch.wav", 0.55, 0.58, 96.0, 48.0, 0.26, 0.58, 303),
    ("rocket_impact_04_rolling_blast.wav", "rocket_fire_v4_04_round_launch.wav", 0.77, 0.67, 61.0, 28.0, 0.49, 0.28, 404),
    ("rocket_impact_05_full_detonation.wav", "rocket_fire_v4_05_full_launch.wav", 0.86, 0.70, 74.0, 34.0, 0.57, 0.36, 505),
]


SNIPER_OPTIONS = [
    ("sniper_01_long_report.wav", 17, 0.72, 0.62, 116.0, 55.0, 0.26, 0.34, 601),
    ("sniper_02_dense_boom.wav", 23, 0.80, 0.66, 82.0, 42.0, 0.38, 0.25, 602),
    ("sniper_03_sharp_crack.wav", 14, 0.58, 0.55, 132.0, 64.0, 0.20, 0.58, 603),
    ("sniper_04_arena_echo.wav", 19, 0.90, 0.62, 98.0, 46.0, 0.32, 0.29, 604),
    ("sniper_05_heavy_precision.wav", 21, 0.69, 0.68, 91.0, 38.0, 0.44, 0.26, 605),
]


REVOLVER_OPTIONS = [
    ("revolver_01_crisp_357.wav", 9, 0.36, 0.62, 164.0, 82.0, 0.16, 0.52, 701, 0.030, 0.10),
    ("revolver_02_wide_report.wav", 3, 0.48, 0.58, 138.0, 65.0, 0.24, 0.34, 702, 0.048, 0.08),
    ("revolver_03_hard_bark.wav", 10, 0.33, 0.64, 188.0, 92.0, 0.13, 0.58, 703, 0.024, 0.12),
    ("revolver_04_heavy_handcannon.wav", 12, 0.52, 0.66, 118.0, 52.0, 0.30, 0.26, 704, 0.056, 0.07),
    ("revolver_05_dry_arcade.wav", 15, 0.41, 0.57, 176.0, 78.0, 0.19, 0.46, 705, 0.034, 0.15),
]


def make_sniper_option(source_index: int, seconds: float, source_gain: float, body_start: float, body_end: float, body_decay: float, crack_gain: float, seed: int) -> np.ndarray:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)
    source = source_layer(GUNSHOT_DIR / f"gunshot_{source_index}.wav", seconds, lead=0.001)
    put(result, source, 0.0, source_gain)
    put(result, low_body(count, body_start, body_end, body_decay, seed + 1, 0.48), 0.0, 0.34)
    put(result, noise_burst(count, seed + 2, 90.0, 1600.0, body_decay * 0.9, 0.0005), 0.0, 0.22)
    put(result, noise_burst(round(0.030 * RATE), seed + 3, 2800.0, 11500.0, 0.013, 0.00012), 0.002, crack_gain)
    put(result, metal_tick(round(0.055 * RATE), 2500.0, 800.0, 0.040), 0.035, 0.045)
    return result


def make_revolver_option(source_index: int, seconds: float, source_gain: float, body_start: float, body_end: float, body_decay: float, snap_gain: float, seed: int, tick_time: float, tick_gain: float) -> np.ndarray:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)
    source = source_layer(GUNSHOT_DIR / f"gunshot_{source_index}.wav", seconds, lead=0.001)
    put(result, source, 0.0, source_gain)
    put(result, low_body(count, body_start, body_end, body_decay, seed + 1, 0.30), 0.0, 0.33)
    put(result, noise_burst(count, seed + 2, 180.0, 3500.0, body_decay * 0.72, 0.00025), 0.0, 0.14)
    put(result, noise_burst(round(0.024 * RATE), seed + 3, 3200.0, 12500.0, 0.009, 0.00008), 0.001, snap_gain)
    put(result, metal_tick(round(0.032 * RATE), 4200.0, 1800.0, 0.018), tick_time, tick_gain)
    return result


def main() -> None:
    if not GUNSHOT_DIR.exists():
        raise SystemExit(f"Missing source directory: {GUNSHOT_DIR}")

    for name, source, seconds, source_gain, start, end, decay, crack, seed in ROCKET_OPTIONS:
        write_wav(
            ROOT / "rocket_impact_sound_options" / name,
            make_rocket_option(source, seconds, source_gain, start, end, decay, crack, seed),
        )

    for name, source, seconds, source_gain, start, end, decay, crack, seed in SNIPER_OPTIONS:
        write_wav(
            ROOT / "sniper_sound_options" / name,
            make_sniper_option(source, seconds, source_gain, start, end, decay, crack, seed),
        )

    for name, source, seconds, source_gain, start, end, decay, snap, seed, tick_time, tick_gain in REVOLVER_OPTIONS:
        write_wav(
            ROOT / "revolver_sound_options" / name,
            make_revolver_option(source, seconds, source_gain, start, end, decay, snap, seed, tick_time, tick_gain),
        )


if __name__ == "__main__":
    main()
