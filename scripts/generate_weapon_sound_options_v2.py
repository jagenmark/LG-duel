"""Build the heavier, less bright weapon sound audition set."""

from __future__ import annotations

import math
import wave
from pathlib import Path

import numpy as np
from scipy.signal import butter, resample_poly, sosfilt


ROOT = Path(__file__).resolve().parents[1]
RATE = 48_000
TARGET_PEAK = 10.0 ** (-1.0 / 20.0)
RECORDED_DIR = ROOT / "tmp" / "weapon_sound_sources" / "recorded" / "sounds" / "sounds"
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
    samples = np.frombuffer(raw, dtype="<i2").reshape(-1, channels)
    result = samples.astype(np.float64).mean(axis=1) / 32768.0
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


def band_filter(samples: np.ndarray, low: float | None = None, high: float | None = None) -> np.ndarray:
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
    return sosfilt(butter(4, cutoff, btype=kind, fs=RATE, output="sos"), samples)


def envelope(count: int, decay: float, attack: float = 0.0005) -> np.ndarray:
    time = np.arange(count, dtype=np.float64) / RATE
    result = np.exp(-time / max(decay, 0.001))
    if attack > 0:
        result *= np.minimum(1.0, time / attack)
    return result


def noise_burst(count: int, seed: int, low: float, high: float, decay: float, attack: float = 0.0005) -> np.ndarray:
    noise = np.random.default_rng(seed).standard_normal(count)
    return band_filter(noise, low, high) * envelope(count, decay, attack)


def boom(count: int, start_hz: float, end_hz: float, decay: float, seed: int, noise_gain: float = 0.65) -> np.ndarray:
    time = np.arange(count, dtype=np.float64) / RATE
    span = max(time[-1], 1.0 / RATE)
    phase = 2.0 * np.pi * (start_hz * time + (end_hz - start_hz) * time * time / (2.0 * span))
    tone = np.sin(phase) * envelope(count, decay, 0.001)
    rumble = noise_burst(count, seed, 38.0, 620.0, decay * 1.2, 0.001)
    return tone * 0.55 + rumble * noise_gain


def place(destination: np.ndarray, source: np.ndarray, start: float, gain: float) -> None:
    offset = max(0, round(start * RATE))
    if offset >= len(destination):
        return
    count = min(len(source), len(destination) - offset)
    destination[offset : offset + count] += source[:count] * gain


def trim(samples: np.ndarray, seconds: float, peak: int, lead: float = 0.003, fade: float = 0.14) -> np.ndarray:
    count = round(seconds * RATE)
    start = max(0, peak - round(lead * RATE))
    result = np.zeros(count, dtype=np.float64)
    chunk = samples[start : start + count]
    result[: len(chunk)] = chunk
    fade_count = min(count, round(fade * RATE))
    if fade_count:
        result[-fade_count:] *= np.linspace(1.0, 0.0, fade_count, endpoint=True)
    return result


def source_at(path: Path, approx_time: float, seconds: float, high_cut: float) -> np.ndarray:
    samples = read_wav(path)
    center = round(approx_time * RATE)
    window = round(0.16 * RATE)
    start = max(0, center - window)
    end = min(len(samples), center + window)
    peak = start + int(np.argmax(np.abs(samples[start:end])))
    result = trim(samples, seconds, peak)
    # Keep the natural report and room tail, but remove the bright synthetic edge.
    return band_filter(result, high=high_cut)


def rocket_source(name: str, seconds: float) -> np.ndarray:
    samples = read_wav(ROCKET_DIR / name)
    peak = int(np.argmax(np.abs(samples)))
    return band_filter(trim(samples, seconds, peak, lead=0.006, fade=0.16), high=1500.0)


ROCKET_OPTIONS = [
    ("rocket_impact_v2_01_deep_arena.wav", "rocket_fire_v4_01_clean_launch.wav", 0.68, 0.76, 0.78, 0.26, 0.10, 810),
    ("rocket_impact_v2_02_low_heavy_boom.wav", "rocket_fire_v4_02_heavy_launch.wav", 0.78, 0.70, 0.86, 0.22, 0.08, 820),
    ("rocket_impact_v2_03_short_concussion.wav", "rocket_fire_v4_03_sharp_launch.wav", 0.58, 0.62, 0.70, 0.34, 0.14, 830),
    ("rocket_impact_v2_04_rolling_thump.wav", "rocket_fire_v4_04_round_launch.wav", 0.88, 0.74, 0.92, 0.20, 0.07, 840),
    ("rocket_impact_v2_05_big_ground_boom.wav", "rocket_fire_v4_05_full_launch.wav", 0.96, 0.78, 1.00, 0.18, 0.06, 850),
]


def make_rocket(name: str, source_name: str, seconds: float, source_gain: float, boom_gain: float, concussion_gain: float, crack_gain: float, seed: int) -> None:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)
    place(result, noise_burst(round(0.07 * RATE), seed, 45.0, 420.0, 0.065, 0.001), 0.0, 0.50)
    place(result, rocket_source(source_name, seconds - 0.020), 0.020, source_gain)
    place(result, boom(count, 72.0, 29.0, 0.62, seed + 1, 0.80), 0.016, boom_gain)
    place(result, noise_burst(count, seed + 2, 55.0, 850.0, 0.25, 0.0007), 0.014, concussion_gain)
    place(result, noise_burst(round(0.032 * RATE), seed + 3, 500.0, 2100.0, 0.022, 0.0002), 0.022, crack_gain)
    write_wav(ROOT / "rocket_impact_sound_options_v2" / name, result)


SNIPER_OPTIONS = [
    ("sniper_v2_01_mosin_boom.wav", 0.4633, 0.94, 0.82, 0.54, 0.18, 910),
    ("sniper_v2_02_mosin_crack.wav", 3.6429, 0.88, 0.72, 0.62, 0.24, 920),
    ("sniper_v2_03_heavy_mosin.wav", 6.0184, 0.86, 0.96, 0.58, 0.14, 930),
    ("sniper_v2_04_open_field_report.wav", 8.8645, 0.82, 0.90, 0.46, 0.20, 940),
    ("sniper_v2_05_tight_rifle_boom.wav", 12.2497, 0.92, 0.76, 0.60, 0.18, 950),
]


def make_sniper(name: str, approx_time: float, source_gain: float, seconds: float, concussion_gain: float, crack_gain: float, seed: int) -> None:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)
    place(result, source_at(RECORDED_DIR / "mosin.wav", approx_time, seconds, 5200.0), 0.0, source_gain)
    place(result, boom(count, 102.0, 38.0, 0.42, seed, 0.72), 0.0, 0.72)
    place(result, noise_burst(count, seed + 1, 55.0, 1250.0, 0.23, 0.0007), 0.0, concussion_gain)
    place(result, noise_burst(round(0.034 * RATE), seed + 2, 650.0, 3400.0, 0.022, 0.0002), 0.002, crack_gain)
    write_wav(ROOT / "sniper_sound_options_v2" / name, result)


REVOLVER_OPTIONS = [
    ("revolver_v2_01_cz52_heavy_report.wav", 0.2952, 0.92, 0.42, 0.52, 0.16, 1010),
    ("revolver_v2_02_cz52_dry_boom.wav", 2.8278, 0.90, 0.36, 0.62, 0.20, 1020),
    ("revolver_v2_03_cz52_wide_report.wav", 4.0706, 0.84, 0.54, 0.56, 0.15, 1030),
    ("revolver_v2_04_cz52_hard_shot.wav", 5.5468, 0.95, 0.32, 0.48, 0.24, 1040),
    ("revolver_v2_05_cz52_handcannon.wav", 0.2952, 0.82, 0.58, 0.78, 0.12, 1050),
]


def make_revolver(name: str, approx_time: float, source_gain: float, seconds: float, concussion_gain: float, crack_gain: float, seed: int) -> None:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)
    place(result, source_at(RECORDED_DIR / "cz.wav", approx_time, seconds, 4300.0), 0.0, source_gain)
    place(result, boom(count, 148.0, 60.0, 0.24, seed, 0.62), 0.0, 0.50)
    place(result, noise_burst(count, seed + 1, 70.0, 1450.0, 0.15, 0.0005), 0.0, concussion_gain)
    place(result, noise_burst(round(0.026 * RATE), seed + 2, 650.0, 3200.0, 0.018, 0.0002), 0.001, crack_gain)
    write_wav(ROOT / "revolver_sound_options_v2" / name, result)


def main() -> None:
    if not RECORDED_DIR.exists():
        raise SystemExit(f"Missing recorded source directory: {RECORDED_DIR}")
    for option in ROCKET_OPTIONS:
        make_rocket(*option)
    for option in SNIPER_OPTIONS:
        make_sniper(*option)
    for option in REVOLVER_OPTIONS:
        make_revolver(*option)


if __name__ == "__main__":
    main()
