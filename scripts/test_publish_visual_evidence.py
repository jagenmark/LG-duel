from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import publish_visual_evidence as publish


PNG = b"\x89PNG\r\n\x1a\nsmall-test-image"


class PublishVisualEvidenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(dir=publish.ROOT)
        self.root = Path(self.temporary.name)
        self.image = self.root / "20260726T120000Z-ui-menu-01.png"
        self.image.write_bytes(PNG)
        self.metadata = self.root / "capture.json"
        self.config = self.root / "config.json"
        self.config.write_text(
            json.dumps({"gallery_origin": None, "max_bytes": 1024, "sites_project_id": None}),
            encoding="utf-8",
        )
        self.record = {
            "schema_version": 1,
            "task_id": "LGD-42",
            "capture_id": "20260726T120000Z-ui-menu-01",
            "captured_by": "capture-agent",
            "captured_at": "2026-07-26T12:00:00Z",
            "title": "Main menu on phone layout",
            "description": "Shows the menu state intended for the user.",
            "image": self.image.name,
            "contains_sensitive_data": False,
        }
        self._write()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write(self) -> None:
        self.metadata.write_text(json.dumps(self.record), encoding="utf-8")

    def test_ordinary_image_does_not_need_review(self) -> None:
        checked, image, _ = publish.validate_metadata(self.metadata, self.config)
        self.assertEqual(image, self.image)
        self.assertEqual(checked["review_status"], "not_reviewed")
        self.assertEqual(checked["sha256"], hashlib.sha256(PNG).hexdigest())

    def test_independent_pass_is_kept_for_evidence(self) -> None:
        self.record["review"] = {
            "reviewer": "review-agent",
            "reviewed_at": "2026-07-26T12:05:00Z",
            "verdict": "pass",
            "notes": "The capture proves the task.",
        }
        self._write()
        checked, _, _ = publish.validate_metadata(self.metadata, self.config)
        self.assertEqual(checked["review_status"], "pass")

    def test_false_independent_pass_is_blocked(self) -> None:
        self.record["review"] = {
            "reviewer": "CAPTURE-AGENT",
            "reviewed_at": "2026-07-26T12:05:00Z",
            "verdict": "pass",
            "notes": "Self review.",
        }
        self._write()
        with self.assertRaisesRegex(publish.ValidationError, "must be independent"):
            publish.validate_metadata(self.metadata, self.config)

    def test_sensitive_image_is_blocked(self) -> None:
        self.record["contains_sensitive_data"] = True
        self._write()
        with self.assertRaisesRegex(publish.ValidationError, "must be false"):
            publish.validate_metadata(self.metadata, self.config)

    def test_hash_drift_is_blocked(self) -> None:
        self.record["sha256"] = "0" * 64
        self._write()
        with self.assertRaisesRegex(publish.ValidationError, "does not match"):
            publish.validate_metadata(self.metadata, self.config)

    def test_upload_posts_metadata_and_image_in_one_request(self) -> None:
        checked, image, _ = publish.validate_metadata(self.metadata, self.config)
        config = {
            "gallery_origin": "https://gallery.example",
            "upload_path": "/api/evidence",
        }

        class FakeResponse:
            def __enter__(self):
                return self

            def __exit__(self, *_args):
                return False

            def read(self):
                return json.dumps({
                    "status": "uploaded",
                    "capture": {
                        "capture_id": checked["capture_id"],
                        "preview_url": "/api/evidence/capture/review",
                        "full_size_url": "/api/evidence/capture/review",
                        "original_url": None,
                        "review_status": "not_reviewed",
                    },
                }).encode()

        with mock.patch.object(publish, "_open", return_value=FakeResponse()) as opened:
            result = publish.upload_capture(
                checked, image, config, "secret-token", "sites-token"
            )

        self.assertEqual(result["status"], "uploaded")
        request = opened.call_args.args[0]
        self.assertEqual(request.full_url, "https://gallery.example/api/evidence")
        self.assertEqual(request.get_header("Authorization"), "Bearer secret-token")
        self.assertEqual(
            request.get_header("Oai-sites-authorization"), "Bearer sites-token"
        )
        self.assertIn(checked["capture_id"].encode(), request.data)
        self.assertIn(PNG, request.data)

    def test_passing_review_retains_original(self) -> None:
        self.record["review"] = {
            "reviewer": "review-agent",
            "reviewed_at": "2026-07-26T12:05:00Z",
            "verdict": "pass",
            "notes": "The capture proves the task.",
        }
        self._write()
        checked, _, _ = publish.validate_metadata(self.metadata, self.config)
        self.assertTrue(checked["retain_original"])

    def test_dry_run_never_stages(self) -> None:
        with mock.patch.object(publish, "upload_capture") as upload:
            code = publish.main(
                [str(self.metadata), "--config", str(self.config), "--dry-run"]
            )
        self.assertEqual(code, 0)
        upload.assert_not_called()


if __name__ == "__main__":
    unittest.main()
