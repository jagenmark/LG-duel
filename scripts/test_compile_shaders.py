from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools import compile_shaders


class CompileShaderTests(unittest.TestCase):
    def test_vulkan_sdk_candidates_cover_supported_library_layouts(self) -> None:
        sdk_root = Path("C:/VulkanSDK")
        self.assertEqual(
            compile_shaders.shaderc_sdk_candidates(sdk_root),
            (
                sdk_root / "Bin" / "shaderc_shared.dll",
                sdk_root / "Lib" / "shaderc_shared.dll",
                sdk_root / "lib" / "libshaderc_shared.so",
                sdk_root / "lib" / "libshaderc_shared.so.1",
                sdk_root / "lib" / "libshaderc_shared.dylib",
            ),
        )

    def test_loader_library_name_is_used_after_path_candidates(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(
            compile_shaders.Path,
            "glob",
            return_value=[],
        ), mock.patch.object(
            compile_shaders.ctypes.util,
            "find_library",
            return_value="libshaderc_shared.so.1",
        ):
            self.assertEqual(
                compile_shaders.shaderc_library(),
                "libshaderc_shared.so.1",
            )

    def test_ubuntu_shaderc_library_is_used_as_fallback(self) -> None:
        with mock.patch.dict(os.environ, {}, clear=True), mock.patch.object(
            compile_shaders.Path,
            "glob",
            return_value=[],
        ), mock.patch.object(
            compile_shaders.ctypes.util,
            "find_library",
            side_effect=(None, "libshaderc.so.1"),
        ) as find_library:
            self.assertEqual(
                compile_shaders.shaderc_library(),
                "libshaderc.so.1",
            )
            self.assertEqual(
                find_library.call_args_list,
                [mock.call("shaderc_shared"), mock.call("shaderc")],
            )

    def test_vulkan_sdk_linux_library_is_preferred(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            sdk_root = Path(temporary)
            library = sdk_root / "lib" / "libshaderc_shared.so"
            library.parent.mkdir()
            library.write_bytes(b"shaderc")
            with mock.patch.dict(
                os.environ,
                {"VULKAN_SDK": str(sdk_root)},
                clear=True,
            ), mock.patch.object(
                compile_shaders.Path,
                "glob",
                return_value=[],
            ):
                self.assertEqual(compile_shaders.shaderc_library(), library)


if __name__ == "__main__":
    unittest.main()
