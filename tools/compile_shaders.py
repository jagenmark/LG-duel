"""Compile or verify LG Duel GLSL shaders and checked-in SPIR-V.

The script uses shaderc from VULKAN_SDK when present, then the shaderc library
shipped with Blender. It expands local ``#include \"...\"`` files at build time;
the renderer still loads only checked-in SPIR-V. Run it from the repository root:

  python tools/compile_shaders.py
  python tools/compile_shaders.py --check
"""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHADER_DIR = ROOT / "assets" / "shaders"
INCLUDE_PATTERN = re.compile(r'^\s*#include\s+"([^"\\]+)"\s*$')


def shaderc_sdk_candidates(sdk_root: Path) -> tuple[Path, ...]:
    """Return shaderc library paths used by the supported SDK layouts."""
    return (
        sdk_root / "Bin" / "shaderc_shared.dll",
        sdk_root / "Lib" / "shaderc_shared.dll",
        sdk_root / "lib" / "libshaderc_shared.so",
        sdk_root / "lib" / "libshaderc_shared.so.1",
        sdk_root / "lib" / "libshaderc_shared.dylib",
    )


def shaderc_library() -> Path | str:
    candidates: list[Path] = []
    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates.extend(shaderc_sdk_candidates(Path(sdk)))
    candidates.extend(sorted(
        Path("C:/Program Files/Blender Foundation").glob(
            "Blender */blender.shared/shaderc_shared.dll"
        ),
        reverse=True,
    ))
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    system_library = ctypes.util.find_library("shaderc_shared")
    if system_library:
        return system_library
    raise RuntimeError(
        "shaderc shared library was not found. Install the Vulkan SDK, "
        "system shaderc, or Blender, then rerun tools/compile_shaders.py."
    )


def expand_shader_source(
    source_path: Path,
    include_stack: tuple[Path, ...] = (),
) -> str:
    """Expand checked-in shader includes without allowing paths outside shaders."""
    shader_root = SHADER_DIR.resolve()
    resolved_source = source_path.resolve()
    try:
        resolved_source.relative_to(shader_root)
    except ValueError as error:
        raise RuntimeError(
            f"Shader include escapes {SHADER_DIR}: {source_path}"
        ) from error
    if resolved_source in include_stack:
        chain = " -> ".join(
            path.relative_to(shader_root).as_posix()
            for path in (*include_stack, resolved_source)
        )
        raise RuntimeError(f"Shader include cycle: {chain}")
    if not resolved_source.is_file():
        raise RuntimeError(f"Shader source is missing: {resolved_source}")

    expanded: list[str] = []
    for line in resolved_source.read_text(encoding="utf-8").splitlines():
        match = INCLUDE_PATTERN.fullmatch(line)
        if match is None:
            expanded.append(line)
            continue
        include_path = (resolved_source.parent / match.group(1)).resolve()
        expanded.append(
            expand_shader_source(include_path, (*include_stack, resolved_source))
        )
    return "\n".join(expanded) + "\n"


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--check",
        action="store_true",
        help="fail when a checked-in SPIR-V binary differs from its source",
    )
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
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
    stale_outputs: list[Path] = []
    try:
        for source_path in sorted(SHADER_DIR.glob("*.*")):
            kind = {
                ".vert": 0,   # shaderc_glsl_vertex_shader
                ".frag": 1,   # shaderc_glsl_fragment_shader
                ".comp": 2,   # shaderc_glsl_compute_shader
            }.get(source_path.suffix)
            if kind is None:
                continue
            source = expand_shader_source(source_path).encode("utf-8")
            result = library.shaderc_compile_into_spv(
                compiler,
                source,
                len(source),
                kind,
                source_path.name.encode(),
                b"main",
                options,
            )
            if not result:
                raise RuntimeError(f"shaderc did not return a result for {source_path}")
            try:
                if library.shaderc_result_get_compilation_status(result) != 0:
                    error = library.shaderc_result_get_error_message(result)
                    raise RuntimeError(
                        f"{source_path.relative_to(ROOT)}: "
                        f"{error.decode(errors='replace')}"
                    )
                length = library.shaderc_result_get_length(result)
                data = ctypes.string_at(
                    library.shaderc_result_get_bytes(result),
                    length,
                )
                output_path = source_path.with_suffix(source_path.suffix + ".spv")
                if arguments.check:
                    if not output_path.is_file() or output_path.read_bytes() != data:
                        stale_outputs.append(output_path)
                    else:
                        print(f"Verified {output_path.relative_to(ROOT)}")
                else:
                    output_path.write_bytes(data)
                    print(f"Wrote {output_path.relative_to(ROOT)}")
            finally:
                library.shaderc_result_release(result)
    finally:
        library.shaderc_compile_options_release(options)
        library.shaderc_compiler_release(compiler)
    if stale_outputs:
        names = "\n  ".join(
            path.relative_to(ROOT).as_posix() for path in stale_outputs
        )
        raise RuntimeError(
            "Checked-in SPIR-V is stale or missing:\n  "
            f"{names}\nRun: python tools/compile_shaders.py"
        )


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(f"Shader compilation failed: {error}", file=sys.stderr)
        raise SystemExit(1) from error
