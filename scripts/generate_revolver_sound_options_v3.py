"""Build revolver options with the full recorded report and room tail."""

from __future__ import annotations

from pathlib import Path

import numpy as np

import generate_weapon_sound_options_v2 as base


ROOT = base.ROOT
RATE = base.RATE
RECORDED = base.RECORDED_DIR / "cz.wav"
OUT_DIR = ROOT / "revolver_sound_options_v3"


def full_source(approx_time: float, seconds: float, high_cut: float = 3800.0) -> np.ndarray:
    samples = base.read_wav(RECORDED)
    center = round(approx_time * RATE)
    window = round(0.16 * RATE)
    search_start = max(0, center - window)
    search_end = min(len(samples), center + window)
    peak = search_start + int(np.argmax(np.abs(samples[search_start:search_end])))
    count = round(seconds * RATE)
    start = max(0, peak - round(0.003 * RATE))
    result = np.zeros(count, dtype=np.float64)
    chunk = samples[start : start + count]
    result[: len(chunk)] = chunk
    # Keep the full tail, then use a long fade so the file never stops hard.
    fade_count = min(count, round(0.28 * RATE))
    result[-fade_count:] *= np.linspace(1.0, 0.0, fade_count, endpoint=True)
    return base.band_filter(result, high=high_cut)


def make_option(
    name: str,
    approx_time: float,
    seconds: float,
    source_gain: float,
    boom_gain: float,
    concussion_gain: float,
    crack_gain: float,
    seed: int,
) -> None:
    count = round(seconds * RATE)
    result = np.zeros(count, dtype=np.float64)
    base.place(result, full_source(approx_time, seconds), 0.0, source_gain)
    base.place(result, base.boom(count, 148.0, 52.0, 0.33, seed, 0.72), 0.0, boom_gain)
    base.place(result, base.noise_burst(count, seed + 1, 70.0, 1350.0, 0.22, 0.0005), 0.0, concussion_gain)
    base.place(result, base.noise_burst(round(0.028 * RATE), seed + 2, 600.0, 3000.0, 0.020, 0.0002), 0.001, crack_gain)
    base.write_wav(OUT_DIR / name, result)


OPTIONS = [
    ("revolver_v3_01_full_heavy_report.wav", 0.2952, 1.05, 0.92, 0.48, 0.42, 0.14, 1110),
    ("revolver_v3_02_full_dry_boom.wav", 2.8278, 0.96, 0.90, 0.46, 0.45, 0.17, 1120),
    ("revolver_v3_03_full_wide_report.wav", 4.0706, 1.12, 0.84, 0.52, 0.38, 0.13, 1130),
    ("revolver_v3_04_full_hard_shot.wav", 5.5468, 0.92, 0.95, 0.42, 0.43, 0.20, 1140),
    ("revolver_v3_05_full_handcannon.wav", 0.2952, 1.20, 0.82, 0.62, 0.50, 0.10, 1150),
]


def main() -> None:
    if not RECORDED.exists():
        raise SystemExit(f"Missing recorded source: {RECORDED}")
    for option in OPTIONS:
        make_option(*option)


if __name__ == "__main__":
    main()
