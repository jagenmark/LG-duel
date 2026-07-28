"""Build clean rocket shots from CC0 field recordings with linear mixing."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LOCAL_DEPS = ROOT / "tmp" / "audio_python_deps"
sys.path.insert(0, str(LOCAL_DEPS))

import numpy as np  # type: ignore  # noqa: E402
import soundfile as sf  # type: ignore  # noqa: E402


RATE = 48_000
SOURCE_DIR = ROOT / "rocket_sound_sources"
BANG_DIR = SOURCE_DIR / "bangs"
OUT_DIR = ROOT / "rocket_sound_options_v4"

OPTIONS = [
    ("rocket_fire_v4_01_clean_launch.wav", "shot_03.ogg", 0.48, 0.76, 0.58, 0.0),
    ("rocket_fire_v4_02_heavy_launch.wav", "bang_04.ogg", 0.58, 0.68, 0.72, 1.2),
    ("rocket_fire_v4_03_sharp_launch.wav", "shot_01.ogg", 0.44, 0.82, 0.48, 2.4),
    ("rocket_fire_v4_04_round_launch.wav", "bang_05.ogg", 0.54, 0.62, 0.70, 3.6),
    ("rocket_fire_v4_05_full_launch.wav", "bang_09.ogg", 0.66, 0.66, 0.78, 4.8),
]


def read_mono(path: Path) -> np.ndarray:
    data, rate = sf.read(path, dtype="float64", always_2d=True)
    if rate != RATE:
        raise ValueError(f"{path} uses {rate} Hz; expected {RATE} Hz")
    return np.mean(data, axis=1)


def slice_around_peak(samples: np.ndarray, length: int) -> np.ndarray:
    peak = int(np.argmax(np.abs(samples)))
    start = max(0, peak - round(0.008 * RATE))
    result = np.zeros(length, dtype=np.float64)
    source = samples[start : start + length]
    result[: len(source)] = source
    return result


def fade_tail(samples: np.ndarray, seconds: float) -> None:
    count = min(len(samples), round(seconds * RATE))
    if count:
        samples[-count:] *= np.linspace(1.0, 0.0, count, endpoint=True)


def make_option(
    bang_name: str,
    seconds: float,
    bang_gain: float,
    engine_gain: float,
    engine_offset: float,
    engine: np.ndarray,
) -> np.ndarray:
    count = round(seconds * RATE)
    bang = slice_around_peak(read_mono(BANG_DIR / bang_name), count)
    fade_tail(bang, min(0.16, seconds * 0.36))

    engine_start = round(engine_offset * RATE)
    engine_layer = engine[engine_start : engine_start + count].copy()
    if len(engine_layer) < count:
        engine_layer = np.pad(engine_layer, (0, count - len(engine_layer)))

    # The engine fades in behind the recorded launch attack and then falls
    # away. No clipping, distortion, synthetic noise, delay, or reverb.
    attack = min(round(0.010 * RATE), count)
    engine_layer[:attack] *= np.linspace(0.0, 1.0, attack, endpoint=True)
    fade_tail(engine_layer, min(0.22, seconds * 0.44))

    result = bang * bang_gain + engine_layer * engine_gain
    result -= float(np.mean(result))
    peak = float(np.max(np.abs(result))) or 1.0
    result *= (10.0 ** (-1.0 / 20.0)) / peak
    return result


def main() -> None:
    engine = read_mono(SOURCE_DIR / "cc0_rocket_engine.wav")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for name, bang, seconds, bang_gain, engine_gain, offset in OPTIONS:
        result = make_option(
            bang, seconds, bang_gain, engine_gain, offset, engine
        )
        path = OUT_DIR / name
        sf.write(path, result, RATE, subtype="PCM_16")
        print(path.relative_to(ROOT))


if __name__ == "__main__":
    main()
