import io
import sys
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from tools import generate_worker_material_atlas as atlas


class WorkerMaterialAtlasTests(unittest.TestCase):
    def test_image_hash_ignores_png_compression(self) -> None:
        source = Image.new("RGBA", (2, 1), (17, 34, 51, 255))
        low_compression = io.BytesIO()
        high_compression = io.BytesIO()
        source.save(low_compression, format="PNG", compress_level=0)
        source.save(high_compression, format="PNG", compress_level=9)

        with Image.open(io.BytesIO(low_compression.getvalue())) as low_image, Image.open(
            io.BytesIO(high_compression.getvalue())
        ) as high_image:
            self.assertNotEqual(low_compression.getvalue(), high_compression.getvalue())
            self.assertEqual(atlas.image_hash(low_image), atlas.image_hash(high_image))

    def test_image_hash_rejects_different_pixels(self) -> None:
        expected = Image.new("RGBA", (1, 1), (17, 34, 51, 255))
        changed = Image.new("RGBA", (1, 1), (17, 34, 52, 255))

        self.assertNotEqual(atlas.image_hash(expected), atlas.image_hash(changed))


if __name__ == "__main__":
    unittest.main()
