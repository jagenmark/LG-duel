import importlib.util
import io
import json
import os
import pathlib
import struct
import sys
import tarfile
import tempfile
import unittest
import unittest.mock
from types import SimpleNamespace
import zipfile


MODULE_PATH = pathlib.Path(__file__).with_name("asset_pipeline.py")
SPEC = importlib.util.spec_from_file_location("asset_pipeline", MODULE_PATH)
assert SPEC and SPEC.loader
pipeline = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = pipeline
SPEC.loader.exec_module(pipeline)

BLENDER_SPEC = importlib.util.spec_from_file_location("asset_pipeline_blender", MODULE_PATH.parents[1] / "tools" / "asset_pipeline_blender.py")
assert BLENDER_SPEC and BLENDER_SPEC.loader
blender_pipeline = importlib.util.module_from_spec(BLENDER_SPEC)
BLENDER_SPEC.loader.exec_module(blender_pipeline)


def policy() -> dict:
    return {
        "version": 1,
        "staging_root": "asset_pipeline/staging",
        "review_import_root": "asset_pipeline/review_imports",
        "engine_validator": "engine-check.exe" if os.name == "nt" else "engine-check",
        "blender_version_prefix": "5.1",
        "blender_script": "script.py",
        "validation_manifests": {},
        "approved_sources": [{"name": "safe", "domains": ["assets.example"]}],
        "licenses": {
            "permitted": {"CC0-1.0": {"attribution": False, "commercial_use": True, "modification": True}},
            "review_required": ["LicenseRef-Custom"],
            "prohibited": ["CC-BY-NC-4.0"],
        },
        "budgets": {"prop": {"max_vertices": 100, "max_triangles": 100, "max_materials": 4,
                               "max_texture_dimension": 1024, "max_bones": 0,
                               "max_influences_per_vertex": 0, "max_animations": 0}},
        "formats": {"source": ["obj", "zip"], "runtime": ["glb"], "textures": ["png", "jpg"]},
        "archive_limits": {"max_files": 8, "max_file_bytes": 1024, "max_total_bytes": 4096,
                           "max_path_length": 100, "max_download_bytes": 4096},
    }


def candidate(**changes) -> dict:
    value = {
        "source": "safe",
        "id": "crate-01",
        "name": "Crate",
        "page_url": "https://assets.example/crate",
        "download_url": "https://cdn.assets.example/crate.obj",
        "author": "Asset Author",
        "license": {"id": "CC0-1.0", "url": "https://assets.example/license", "text": "CC0 1.0 Universal terms"},
    }
    value.update(changes)
    return value


def make_glb(document: dict) -> bytes:
    payload = json.dumps(document, separators=(",", ":"), sort_keys=True).encode()
    payload += b" " * ((4 - len(payload) % 4) % 4)
    length = 12 + 8 + len(payload)
    return struct.pack("<4sII", b"glTF", 2, length) + struct.pack("<II", len(payload), 0x4E4F534A) + payload


def write_evidence(root: pathlib.Path, **changes) -> dict:
    value = {
        "license_id": "CC0-1.0",
        "license_url": "https://assets.example/license",
        "license_text": "Exact terms",
        "captured_utc": "2026-01-02T03:04:05Z",
    }
    value.update(changes)
    path = root / "license-evidence.json"
    path.write_text(json.dumps(value), encoding="utf-8")
    return pipeline.load_license_evidence(path)


def make_processed_package(root: pathlib.Path, *, attribution: bool = False) -> pathlib.Path:
    package = root / "asset_pipeline" / "staging" / "safe-x"
    original = package / "original" / "crate.obj"
    snapshot = package / "license" / "LICENSE.txt"
    runtime = package / "work" / "processed" / "crate.glb"
    original.parent.mkdir(parents=True)
    snapshot.parent.mkdir(parents=True)
    runtime.parent.mkdir(parents=True)
    original.write_bytes(b"v 0 0 0\n")
    snapshot.write_text("Exact terms\n", encoding="utf-8")
    runtime.write_bytes(make_glb({"asset": {"version": "2.0"}, "meshes": []}))
    pipeline.write_json(package / "provenance.json", {
        "asset_name": "Crate", "author": "Asset Author", "source": "safe", "source_asset_id": "crate-01",
        "asset_page_url": "https://assets.example/crate", "license_id": "CC-BY-4.0" if attribution else "CC0-1.0",
        "license_url": "https://assets.example/license", "original_filename": "crate.obj",
        "original_sha256": pipeline.sha256_file(original), "license_snapshot": "license/LICENSE.txt",
        "license_snapshot_sha256": pipeline.sha256_file(snapshot), "attribution_required": attribution,
        "required_attribution_text": "Crate by Asset Author, CC BY 4.0" if attribution else "",
    })
    pipeline.write_json(package / "state.json", {"stage": "processed", "pipeline_version": pipeline.PIPELINE_VERSION})
    return package


def accept_validation(package: pathlib.Path, root: pathlib.Path, current_policy: dict | None = None) -> dict:
    validator = root / ("engine-check.exe" if os.name == "nt" else "engine-check")
    validator.write_text("", encoding="utf-8")
    result = SimpleNamespace(returncode=0, stdout="ok", stderr="")
    with unittest.mock.patch.object(pipeline, "REPO_ROOT", root), unittest.mock.patch.object(pipeline, "_run_tool", return_value=result):
        return pipeline.validate_package(package, current_policy or policy(), "prop", [str(validator)])


class PolicyAndLicenseTests(unittest.TestCase):
    def test_source_allowlist_and_domain_are_enforced(self):
        with self.assertRaisesRegex(pipeline.PipelineError, "not approved"):
            pipeline.require_candidate(candidate(source="other"), policy())
        with self.assertRaisesRegex(pipeline.PipelineError, "outside approved"):
            pipeline.require_candidate(candidate(download_url="https://evil.example/file.obj"), policy())

    def test_license_fails_closed_for_missing_and_noncommercial_terms(self):
        missing = pipeline.inspect_license(candidate(license={}), policy())
        self.assertFalse(missing["approved"])
        restricted = candidate(license={"id": "CC0-1.0", "url": "https://assets.example/license",
                                        "text": "Noncommercial use only"})
        result = pipeline.inspect_license(restricted, policy())
        self.assertFalse(result["approved"])
        self.assertTrue(any("restricted" in reason for reason in result["reasons"]))
        with tempfile.TemporaryDirectory() as raw:
            evidence = write_evidence(pathlib.Path(raw), license_text="Noncommercial use only")
            self.assertFalse(pipeline.inspect_license(candidate(), policy(), evidence)["approved"])

    def test_candidate_license_metadata_never_replaces_an_evidence_file(self):
        self.assertFalse(pipeline.inspect_license(candidate(), policy())["approved"])
        with tempfile.TemporaryDirectory() as raw:
            evidence = write_evidence(pathlib.Path(raw))
            self.assertTrue(pipeline.inspect_license(candidate(), policy(), evidence)["approved"])

    def test_license_fails_closed_for_conflict_custom_and_unknown(self):
        conflict = pipeline.inspect_license(candidate(license_ids=["CC0-1.0", "MIT-0"]), policy())
        self.assertFalse(conflict["approved"])
        custom = candidate(license={"id": "LicenseRef-Custom", "url": "https://assets.example/l", "text": "Custom terms"})
        self.assertFalse(pipeline.inspect_license(custom, policy())["approved"])
        self.assertFalse(pipeline.inspect_license(candidate(license={"id": "NOASSERTION", "url": "https://assets.example/l", "text": "Unknown"}), policy())["approved"])

    def test_license_snapshot_reference_loads_text_and_cannot_escape(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            (root / "LICENSE.txt").write_text("Exact license body", encoding="utf-8")
            evidence = root / "evidence.json"
            evidence.write_text(json.dumps({"license_id": "CC0-1.0", "license_url": "https://assets.example/l",
                                            "license_text": "LICENSE.txt"}), encoding="utf-8")
            loaded = pipeline.load_license_evidence(evidence)
            self.assertEqual("Exact license body", loaded["license_text"])
            self.assertEqual("snapshot", loaded["_evidence_kind"])
            evidence.write_text(json.dumps({"evidence_path": "../outside.txt"}), encoding="utf-8")
            with self.assertRaisesRegex(pipeline.PipelineError, "escapes"):
                pipeline.load_license_evidence(evidence)

    def test_license_evidence_id_and_url_must_match_candidate(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            wrong_id = write_evidence(root, license_id="MIT-0")
            self.assertFalse(pipeline.inspect_license(candidate(), policy(), wrong_id)["approved"])
            wrong_url = write_evidence(root, license_url="https://assets.example/other-license")
            result = pipeline.inspect_license(candidate(), policy(), wrong_url)
            self.assertFalse(result["approved"])
            self.assertTrue(any("URL conflicts" in reason for reason in result["reasons"]))

    def test_saved_standard_legal_text_does_not_trip_word_scan(self):
        root = pipeline.REPO_ROOT / "examples" / "assets" / "cc0_crate"
        value = pipeline.read_json(root / "candidate.json")
        value["_candidate_base"] = str(root)
        evidence = pipeline.load_license_evidence(root / "evidence.json")
        self.assertTrue(pipeline.inspect_license(value, pipeline.load_policy(), evidence)["approved"])

    def test_catalog_provider_returns_stable_source_neutral_results(self):
        provider = pipeline.CatalogProvider("safe", {"assets": [
            {"id": "z", "name": "Red crate"}, {"id": "a", "name": "Red crate"}, {"id": "x", "name": "Blue box"}]})
        self.assertEqual(["a", "z"], [item["id"] for item in provider.search("red crate")])
        self.assertTrue(all(item["source"] == "safe" for item in provider.search("red")))


class DownloadBoundaryTests(unittest.TestCase):
    def test_each_redirect_hop_must_stay_on_an_approved_domain(self):
        handler = pipeline._ApprovedRedirectHandler({"domains": ["assets.example"]})
        request = pipeline.urllib.request.Request("https://assets.example/file")
        with self.assertRaisesRegex(pipeline.PipelineError, "redirect.*outside"):
            handler.redirect_request(request, None, 302, "Found", {}, "https://evil.example/file")

    def test_final_response_url_must_stay_on_an_approved_domain(self):
        class Response(io.BytesIO):
            headers = {}

            def geturl(self):
                return "https://evil.example/file"

            def __enter__(self):
                return self

            def __exit__(self, *_args):
                self.close()

        opener = SimpleNamespace(open=lambda *_args, **_kwargs: Response(b"asset"))
        with tempfile.TemporaryDirectory() as raw, unittest.mock.patch.object(
                pipeline.urllib.request, "build_opener", return_value=opener):
            with self.assertRaisesRegex(pipeline.PipelineError, "response.*outside"):
                pipeline._default_fetch("https://assets.example/file", pathlib.Path(raw) / "file", 100,
                                        {"domains": ["assets.example"]})


class ArchiveTests(unittest.TestCase):
    def test_zip_traversal_is_reported_and_nothing_is_extracted(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); archive = root / "bad.zip"; output = root / "out"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("../escape.obj", "v 0 0 0")
            report = pipeline.extract_archive(archive, output, policy())
            self.assertFalse(report["accepted"])
            self.assertIn("path traversal", report["suspicious_entries"][0]["reason"])
            self.assertFalse(output.exists())

    def test_zip_executable_name_and_disguised_header_are_rejected(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            for filename, content in (("run.exe", b"data"), ("model.obj", b"MZ\x00\x00")):
                archive = root / (filename + ".zip")
                with zipfile.ZipFile(archive, "w") as bundle:
                    bundle.writestr(filename, content)
                report = pipeline.extract_archive(archive, root / (filename + "-out"), policy())
                self.assertFalse(report["accepted"])

    def test_tar_symlink_is_rejected(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); archive = root / "bad.tar"
            with tarfile.open(archive, "w") as bundle:
                link = tarfile.TarInfo("model.obj"); link.type = tarfile.SYMTYPE; link.linkname = "outside"
                bundle.addfile(link)
            report = pipeline.extract_archive(archive, root / "out", policy())
            self.assertFalse(report["accepted"])
            self.assertEqual("unsupported symlink", report["suspicious_entries"][0]["reason"])

    def test_safe_zip_extracts_with_stable_report(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); archive = root / "ok.zip"; output = root / "out"
            with zipfile.ZipFile(archive, "w") as bundle:
                bundle.writestr("mesh/model.obj", "v 0 0 0\n")
            report = pipeline.extract_archive(archive, output, policy())
            self.assertEqual({"accepted": True, "archive_type": "zip", "entries": 1, "expanded_bytes": 8,
                              "suspicious_entries": []}, report)
            self.assertTrue((output / "mesh" / "model.obj").is_file())


class InspectionTests(unittest.TestCase):
    def test_obj_metrics_bounds_and_errors(self):
        with tempfile.TemporaryDirectory() as raw:
            path = pathlib.Path(raw) / "quad.obj"
            path.write_text("o Body\nv -1 0 2\nv 1 0 2\nv 1 3 2\nv -1 3 2\nusemtl Steel\nf 1 2 3 4\n", encoding="utf-8")
            report = pipeline.inspect_obj(path)
            self.assertEqual((1, 4, 2, 1), (report["meshes"], report["vertices"], report["triangles"], report["materials"]))
            self.assertEqual({"min": [-1.0, 0.0, 2.0], "max": [1.0, 3.0, 2.0], "size": [2.0, 3.0, 0.0]}, report["bounds"])
            self.assertEqual([], report["errors"])

    def test_glb_metrics_skin_animation_bounds_and_bad_length(self):
        document = {
            "asset": {"version": "2.0"},
            "accessors": [
                {"count": 6, "type": "VEC3", "min": [-1, -2, -3], "max": [4, 5, 6]},
                {"count": 6, "type": "SCALAR"}, {"count": 6, "type": "VEC4"}, {"count": 6, "type": "VEC4"}],
            "meshes": [{"primitives": [{"indices": 1, "attributes": {"POSITION": 0, "JOINTS_0": 2, "WEIGHTS_0": 3}}]}],
            "materials": [{}, {}], "skins": [{"joints": [0, 1, 2]}], "animations": [{}],
        }
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); path = root / "model.glb"; path.write_bytes(make_glb(document))
            report = pipeline.inspect_gltf(path)
            self.assertEqual((1, 6, 2, 2, 3, 4, 1), (report["meshes"], report["vertices"], report["triangles"], report["materials"], report["bones"], report["max_influences_per_vertex"], report["animations"]))
            self.assertEqual([-1.0, -2.0, -3.0], report["bounds"]["min"])
            path.write_bytes(path.read_bytes()[:-1])
            self.assertTrue(pipeline.inspect_gltf(path)["errors"])

    def test_png_dimensions_and_tree_output_are_deterministic(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            png = b"\x89PNG\r\n\x1a\n" + b"\0" * 8 + struct.pack(">II", 32, 64) + b"\0" * 4
            (root / "z.png").write_bytes(png); (root / "a.obj").write_text("v 0 0 0\n", encoding="utf-8")
            first = pipeline.canonical_json(pipeline.inspect_tree(root))
            second = pipeline.canonical_json(pipeline.inspect_tree(root))
            self.assertEqual(first, second)
            parsed = json.loads(first)
            self.assertEqual(["a.obj", "z.png"], [item["path"] for item in parsed["files"]])
            self.assertEqual((32, 64), pipeline.image_dimensions(png))


class GatingAndProvenanceTests(unittest.TestCase):
    def test_animation_still_name_cannot_escape_output(self):
        self.assertEqual("idle-two-handed", blender_pipeline._safe_still_name("idle-two-handed"))
        with self.assertRaisesRegex(blender_pipeline.JobError, "safe file stem"):
            blender_pipeline._safe_still_name("../../outside")
    def test_download_writes_hash_provenance_and_copied_license(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            def fetch(_url, target, _limit):
                target.write_bytes(b"v 0 0 0\n")
            evidence = write_evidence(root)
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                package = pipeline.stage_download(candidate(), policy(), evidence, fetch)
            provenance = json.loads((package / "provenance.json").read_text(encoding="utf-8"))
            self.assertEqual(hashlib_sha256(b"v 0 0 0\n"), provenance["original_sha256"])
            self.assertEqual("2026-01-02T03:04:05Z", provenance["retrieved_utc"])
            self.assertEqual("Exact terms\n", (package / "license" / "LICENSE.txt").read_text(encoding="utf-8"))
            self.assertEqual(evidence["_evidence_file_sha256"], provenance["license_evidence"]["file_sha256"])
            self.assertEqual("network", provenance["acquisition_method"])
            self.assertTrue(provenance["retrieved_from_url"])
            self.assertTrue((package / "license" / "evidence.json").is_file())
            self.assertEqual("downloaded", json.loads((package / "state.json").read_text())["stage"])

    def test_local_override_is_named_as_local_in_provenance(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); local = root / "manual.obj"; local.write_bytes(b"v 0 0 0\n")
            evidence = write_evidence(root)
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                package = pipeline.stage_download(candidate(), policy(), evidence, local_source=local)
            provenance = pipeline.read_json(package / "provenance.json")
            self.assertEqual("local_override", provenance["acquisition_method"])
            self.assertFalse(provenance["retrieved_from_url"])
            self.assertEqual("manual.obj", provenance["local_source"]["file_name"])

    def test_failed_license_never_fetches_or_leaves_package(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); called = False
            def fetch(_url, _target, _limit):
                nonlocal called; called = True
            bad = candidate(license={})
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "license rejected"):
                    pipeline.stage_download(bad, policy(), None, fetch)
            self.assertFalse(called)
            self.assertFalse((root / "asset_pipeline" / "staging").exists())

    def test_process_and_import_stage_gates(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); package = root / "asset_pipeline" / "staging" / "safe-x"
            package.mkdir(parents=True); pipeline.write_json(package / "state.json", {"stage": "downloaded"})
            tool = root / "tool"; script = root / "script.py"; tool.write_text(""); script.write_text("")
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "inspected"):
                    pipeline.process_package(package, policy(), tool, script)
                with self.assertRaisesRegex(pipeline.PipelineError, "validated"):
                    pipeline.import_for_review(package, policy())

    def test_subprocess_environment_and_command_are_fixed(self):
        seen = {}
        def fake_run(command, **kwargs):
            seen.update(command=command, kwargs=kwargs)
            return object()
        with unittest.mock.patch.object(pipeline.subprocess, "run", fake_run), unittest.mock.patch.dict(pipeline.os.environ, {"SECRET_VALUE": "no", "PATH": "bin"}, clear=True):
            pipeline._run_tool(["tool", "arg"], pathlib.Path.cwd(), 7)
        self.assertEqual(["tool", "arg"], seen["command"])
        self.assertEqual("0", seen["kwargs"]["env"]["SOURCE_DATE_EPOCH"])
        self.assertNotIn("SECRET_VALUE", seen["kwargs"]["env"])
        self.assertFalse(seen["kwargs"]["shell"])


class ValidationAndManifestTests(unittest.TestCase):
    def test_validate_requires_processed_state_runtime_output_and_glb_validator(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw)
            validator = root / ("engine-check.exe" if os.name == "nt" else "engine-check"); validator.write_text("", encoding="utf-8")
            package = make_processed_package(root)
            pipeline.write_json(package / "state.json", {"stage": "inspected"})
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "processed"):
                    pipeline.validate_package(package, policy(), "prop", [str(validator)])

            pipeline.write_json(package / "state.json", {"stage": "processed"})
            (package / "work" / "processed" / "crate.glb").unlink()
            (package / "work" / "processed" / "notes.txt").write_text("not runtime", encoding="utf-8")
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "validation failed"):
                    pipeline.validate_package(package, policy(), "prop", [str(validator)])
            report = pipeline.read_json(package / "reports" / "validation.json")
            self.assertTrue(any("no nonempty" in error for error in report["errors"]))

            (package / "work" / "processed" / "notes.txt").unlink()
            (package / "work" / "processed" / "crate.glb").write_bytes(
                make_glb({"asset": {"version": "2.0"}, "meshes": []}))
            validator.unlink()
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "validation failed"):
                    pipeline.validate_package(package, policy(), "prop")
            report = pipeline.read_json(package / "reports" / "validation.json")
            self.assertTrue(any("engine validator" in error for error in report["errors"]))

    def test_caller_cannot_replace_fixed_engine_validator(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); package = make_processed_package(root)
            (root / ("engine-check.exe" if os.name == "nt" else "engine-check")).write_text("", encoding="utf-8")
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "fixed policy path"):
                    pipeline.validate_package(package, policy(), "prop", [sys.executable])

    def test_successful_validation_writes_stable_full_file_manifest(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); package = make_processed_package(root)
            accept_validation(package, root)
            manifest = pipeline.read_json(package / pipeline.REVIEW_MANIFEST)
            self.assertEqual(sorted(item["path"] for item in manifest["files"]),
                             [item["path"] for item in manifest["files"]])
            self.assertTrue(all(set(item) == {"path", "sha256", "size", "type"} for item in manifest["files"]))
            self.assertIn("reports/validation.json", [item["path"] for item in manifest["files"]])
            self.assertIn("state.json", [item["path"] for item in manifest["files"]])

    def test_import_rejects_changed_and_new_files(self):
        for change in ("changed", "new"):
            with self.subTest(change=change), tempfile.TemporaryDirectory() as raw:
                root = pathlib.Path(raw); package = make_processed_package(root)
                accept_validation(package, root)
                if change == "changed":
                    (package / "work" / "processed" / "crate.glb").write_bytes(b"changed")
                else:
                    (package / "work" / "processed" / "new.txt").write_text("new", encoding="utf-8")
                with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                    with self.assertRaisesRegex(pipeline.PipelineError, "manifest"):
                        pipeline.import_for_review(package, policy())

    def test_import_rejects_symlinks_added_after_validation(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); package = make_processed_package(root)
            accept_validation(package, root)
            link = package / "work" / "processed" / "link.glb"
            try:
                os.symlink(package / "work" / "processed" / "crate.glb", link)
            except OSError as exc:
                self.skipTest(f"symbolic links are not available: {exc}")
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "symbolic link"):
                    pipeline.import_for_review(package, policy())

    def test_manifest_scan_fails_closed_when_a_file_is_a_symlink(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); package = make_processed_package(root)
            real_is_link = pipeline.stat.S_ISLNK

            def report_first_regular_file_as_link(mode):
                return pipeline.stat.S_ISREG(mode) or real_is_link(mode)

            with unittest.mock.patch.object(pipeline.stat, "S_ISLNK", side_effect=report_first_regular_file_as_link):
                with self.assertRaisesRegex(pipeline.PipelineError, "symbolic link"):
                    pipeline._review_manifest(package)


class AttributionTests(unittest.TestCase):
    def test_attribution_license_requires_both_outputs_and_keeps_exact_credit(self):
        with tempfile.TemporaryDirectory() as raw:
            root = pathlib.Path(raw); package = make_processed_package(root, attribution=True)
            current_policy = policy()
            current_policy["licenses"]["permitted"]["CC-BY-4.0"] = {
                "attribution": True, "commercial_use": True, "modification": True}
            accept_validation(package, root, current_policy)
            with unittest.mock.patch.object(pipeline, "REPO_ROOT", root):
                with self.assertRaisesRegex(pipeline.PipelineError, "output files are required"):
                    pipeline.import_for_review(package, current_policy)
                destination = pipeline.import_for_review(
                    package, current_policy, root / "ATTRIBUTION.md", root / "THIRD_PARTY.md")
            exact_credit = "Crate by Asset Author, CC BY 4.0"
            entry = pipeline.read_json(destination / "review-entry.json")
            self.assertEqual(exact_credit, entry["required_attribution_text"])
            self.assertIn(exact_credit, (root / "ATTRIBUTION.md").read_text(encoding="utf-8"))
            self.assertIn(exact_credit, (root / "THIRD_PARTY.md").read_text(encoding="utf-8"))
            manifest = pipeline._load_review_manifest(destination)
            self.assertIn("review-entry.json", [item["path"] for item in manifest["files"]])


class WorkerReviewPacketTests(unittest.TestCase):
    def test_tracked_worker_review_packet_is_sealed(self):
        package = pipeline.REPO_ROOT / "imports" / "assets" / "review" / "quaternius_worker"
        manifest = pipeline._load_review_manifest(package)
        provenance = pipeline.read_json(package / "provenance.json")
        output = package / provenance["review_output"]["path"]
        self.assertEqual(provenance["review_output"]["sha256"], pipeline.sha256_file(output))
        self.assertEqual(provenance["review_output"]["size"], output.stat().st_size)
        self.assertIn("quaternius_worker.glb", [item["path"] for item in manifest["files"]])

    def test_worker_glb_keeps_source_clips_and_approved_two_handed_idle(self):
        package = pipeline.REPO_ROOT / "imports" / "assets" / "review" / "quaternius_worker"
        document, _, errors = pipeline._glb_json(package / "quaternius_worker.glb")
        self.assertEqual([], errors)
        names = [item.get("name") for item in document.get("animations", [])]
        self.assertEqual(33, len(names))
        self.assertEqual(1, names.count("Idle_Gun_TwoHanded"))
        self.assertIn("Idle_Gun_Pointing", names)
        self.assertIn("IDLE", names)


def hashlib_sha256(data: bytes) -> str:
    import hashlib
    return hashlib.sha256(data).hexdigest()


if __name__ == "__main__":
    unittest.main()
