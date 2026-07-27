"""Compile LG Duel GLSL shaders to checked-in SPIR-V.

The script uses shaderc from VULKAN_SDK when present, then the shaderc library
shipped with Blender. Run it from the repository root:

  python tools/compile_shaders.py
"""

from __future__ import annotations

import ctypes
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "assets" / "shaders"


def shaderc_library() -> Path:
    candidates: list[Path] = []
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates.append(Path(sdk) / "Bin" / "shaderc_shared.dll")
    candidates.extend(
        Path("C:/Program Files/Blender Foundation").glob(
            "Blender */blender.shared/shaderc_shared.dll"
        )
    )
    for candidate in sorted(candidates, reverse=True):
        if candidate.is_file():
            return candidate
    raise RuntimeError("shaderc_shared.dll was not found")


def main() -> None:
    library = ctypes.CDLL(str(shaderc_library()))
    library.shaderc_compiler_initialize.restype = ctypes.c_void_p
    library.shaderc_compiler_release.argtypes = [ctypes.c_void_p]
    library.shaderc_compile_options_initialize.restype = ctypes.c_void_p
    library.shaderc_compile_options_release.argtypes = [ctypes.c_void_p]
    library.shaderc_compile_into_spv.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_size_t,
        ctypes.c_int,
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.c_void_p,
    ]
    library.shaderc_compile_into_spv.restype = ctypes.c_void_p
    library.shaderc_result_get_compilation_status.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_compilation_status.restype = ctypes.c_int
    library.shaderc_result_get_error_message.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_error_message.restype = ctypes.c_char_p
    library.shaderc_result_get_length.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_length.restype = ctypes.c_size_t
    library.shaderc_result_get_bytes.argtypes = [ctypes.c_void_p]
    library.shaderc_result_get_bytes.restype = ctypes.c_void_p
    library.shaderc_result_release.argtypes = [ctypes.c_void_p]

    compiler = library.shaderc_compiler_initialize()
    options = library.shaderc_compile_options_initialize()
    if not compiler or not options:
        raise RuntimeError("shaderc failed to initialize")
    try:
        for source_path in sorted(SHADER_DIR.glob("*.*")):
            kind = 0 if source_path.suffix == ".vert" else (
                1 if source_path.suffix == ".frag" else None
            )
            if kind is None:
                continue
            source = source_path.read_bytes()
            result = library.shaderc_compile_into_spv(
                compiler,
                source,
                len(source),
                kind,
                source_path.name.encode(),
                b"main",
                options,
            )
            try:
                if library.shaderc_result_get_compilation_status(result) != 0:
                    error = library.shaderc_result_get_error_message(result)
                    raise RuntimeError(error.decode(errors="replace"))
                length = library.shaderc_result_get_length(result)
                data = ctypes.string_at(
                    library.shaderc_result_get_bytes(result),
                    length,
                )
                output_path = source_path.with_suffix(source_path.suffix + ".spv")
                output_path.write_bytes(data)
                print(f"Wrote {output_path.relative_to(ROOT)}")
            finally:
                library.shaderc_result_release(result)
    finally:
        library.shaderc_compile_options_release(options)
        library.shaderc_compiler_release(compiler)


if __name__ == "__main__":
    main()
