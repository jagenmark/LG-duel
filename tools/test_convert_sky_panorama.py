import importlib.util
import pathlib
import tempfile
import unittest

from PIL import Image


MODULE_PATH = pathlib.Path(__file__).with_name("convert_sky_panorama.py")
SPEC = importlib.util.spec_from_file_location("convert_sky_panorama", MODULE_PATH)
assert SPEC and SPEC.loader
sky = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sky)


class SkyPanoramaConverterTests(unittest.TestCase):
    def test_rejects_non_two_to_one_source(self):
        with self.assertRaisesRegex(ValueError, "exactly 2:1"):
            sky.validate_panorama(Image.new("RGB", (10, 6)))

    def test_face_centres_match_cube_cardinals(self):
        expected = {
            "posx": (1.0, 0.0, 0.0),
            "negx": (-1.0, 0.0, 0.0),
            "posy": (0.0, 1.0, 0.0),
            "negy": (0.0, -1.0, 0.0),
            "posz": (0.0, 0.0, 1.0),
            "negz": (0.0, 0.0, -1.0),
        }
        for face, direction in expected.items():
            with self.subTest(face=face):
                self.assertEqual(
                    sky.normalized_face_direction(face, 1, 1, 3),
                    direction,
                )

    def test_equirectangular_x_wrap_is_bilinear(self):
        image = Image.new("RGBA", (4, 2))
        image.putdata(
            [(255, 0, 0, 255), (0, 0, 0, 255), (0, 0, 0, 255), (0, 0, 255, 255)]
            * 2
        )
        pixels = sky.image_pixels(image)
        positive_seam = sky.sample_equirect_bilinear(
            pixels, 4, 2, (-1.0, 0.0, 0.0)
        )
        negative_seam = sky.sample_equirect_bilinear(
            pixels, 4, 2, (-1.0, -0.0, 0.0)
        )
        self.assertEqual(positive_seam, (128, 0, 128, 255))
        self.assertEqual(negative_seam, positive_seam)

    def test_output_is_deterministic_and_rgba(self):
        panorama = Image.new("RGB", (16, 8))
        panorama.putdata(
            [
                ((x * 17) % 256, (y * 31) % 256, ((x + y) * 13) % 256)
                for y in range(8)
                for x in range(16)
            ]
        )
        faces = sky.build_cube_faces(panorama, 8)
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            first_hashes = sky.write_cube_faces(faces, pathlib.Path(first))
            second_hashes = sky.write_cube_faces(faces, pathlib.Path(second))
        self.assertEqual(first_hashes, second_hashes)
        self.assertTrue(all(image.mode == "RGBA" for image in faces.values()))

    def test_all_edges_and_corners_are_continuous(self):
        panorama = Image.new("RGB", (32, 16))
        panorama.putdata(
            [
                ((x * 7) % 256, (y * 11) % 256, ((x * 3 + y * 5) % 256))
                for y in range(16)
                for x in range(32)
            ]
        )
        audit = sky.continuity_audit(sky.build_cube_faces(panorama, 9))
        self.assertEqual(audit["edge_pair_count"], 12)
        self.assertEqual(audit["corner_count"], 8)
        self.assertEqual(audit["edge_max_channel_delta"], 0)
        self.assertEqual(audit["corner_max_channel_delta"], 0)

    def test_handoff_sources_keep_their_sha256(self):
        root = MODULE_PATH.parents[1]
        expected = {
            "aurora-panorama.png": "77a64a9f474e4aafb5ef21cf73573d99fc6cc9b16724c4e3ac058dae6210150e",
            "crimson-sunset-panorama.png": "bb45b3c3ab2a2e54e3760bbad68ce011e40944bf6bf4c9d5cd2035ade6a35772",
        }
        for name, digest in expected.items():
            with self.subTest(name=name):
                self.assertEqual(
                    sky.sha256_file(root / "art" / "sky" / "source-art" / name),
                    digest,
                )

    def test_checked_in_faces_are_512_rgba_with_recorded_hashes(self):
        root = MODULE_PATH.parents[1]
        expected = {
            "aurora/posx.png": "a6482aaafab30d6a3c040a86124522f3ebb7863e67603d062d0b2e9a6a2d30d1",
            "aurora/negx.png": "d7e8e182e720c6f23d6bcd7c5aeee7943d6b817571bcabb0867d41db419ad8fe",
            "aurora/posy.png": "d4c1960f8bb3af20c80eed81964174f26b9f6b6762c486c8120c65096fcd0308",
            "aurora/negy.png": "5aba8afe1069e714cde7f578f74bad3c946b852ed284a1a90faa59bcc86c05f8",
            "aurora/posz.png": "fc0b751952360ecdc6358712fb65862097c9d443e27baa067126258e98478d08",
            "aurora/negz.png": "98221ef844a12c0cda426aec2b65c5d53fc5e715695c317ea33ce3cd4de024fc",
            "crimson-sunset/posx.png": "57e6ea88b08dc8aa230384fa3ad03b7e147334ead7868fe9db8bd87158305ae8",
            "crimson-sunset/negx.png": "587d1bbc06159d4e05e35faed50566944c79cd660a6d0fc7c60424f9cf3915f9",
            "crimson-sunset/posy.png": "993aade0f694bc95342e4e70c0f9ebb278fd9c1089a74f901e35fea11c82baeb",
            "crimson-sunset/negy.png": "7b7ac932a4aa6bed6f131ac660edf4236c2141215aad8d1d36680a57a7e6a4f2",
            "crimson-sunset/posz.png": "808d81825cca9d182ba44443054d8f9a32da1cbafd4bd94736706ac1132f6c28",
            "crimson-sunset/negz.png": "cfa3627821209e06e3b799b85a60e74920e585ff60e69c774704e08b27fbdf99",
        }
        for relative, digest in expected.items():
            path = root / "assets" / "sky" / relative
            with self.subTest(relative=relative), Image.open(path) as image:
                self.assertEqual(image.size, (512, 512))
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(sky.sha256_file(path), digest)


if __name__ == "__main__":
    unittest.main()
