#!/usr/bin/env python3
"""Focused tests for the safe MCP map editor."""

from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from lg_map_edit import MapEditError, MapEditor


class MapEditorTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.repo = Path(self.temporary.name)
        (self.repo / "maps").mkdir()
        (self.repo / "textures").mkdir()
        (self.repo / "textures" / "arena.png").write_bytes(b"test texture")
        (self.repo / "textures" / "accent.bmp").write_bytes(b"test texture")
        self.editor = MapEditor(self.repo)

    def create_map(self, name: str = "agent_test") -> dict:
        return self.editor.create(name)

    def add_box(self, name: str = "agent_test", **overrides: object) -> dict:
        revision = self.editor.inspect(name)["revision"]
        arguments = {
            "map_name": name,
            "object_id": "floor",
            "minimum": [-64, -64, -16],
            "maximum": [64, 64, 0],
            "material": "arena",
            "expected_revision": revision,
        }
        arguments.update(overrides)
        return self.editor.add_cuboid(**arguments)

    def test_create_and_add_cuboid_writes_canonical_standard_six_face_map(self) -> None:
        created = self.create_map()
        added = self.add_box()

        self.assertTrue(created["applied"])
        self.assertTrue(added["applied"])
        self.assertEqual(added["diff"]["objects_added"], ["floor"])
        inspected = self.editor.inspect("agent_test")
        self.assertEqual(
            inspected["cuboids"],
            [{
                "id": "floor",
                "min": [-64.0, -64.0, -16.0],
                "max": [64.0, 64.0, 0.0],
                "material": "arena",
            }],
        )

        source = (self.repo / "maps" / "agent_test.map").read_text(encoding="utf-8")
        face_lines = [line for line in source.splitlines() if line.startswith("( ")]
        self.assertEqual(len(face_lines), 6)
        self.assertTrue(all(line.endswith("arena 0 0 0 1 1") for line in face_lines))
        self.assertIn("// Format: Standard", source)
        self.assertNotIn("[", source)
        self.assertNotIn("]", source)

        # Reading and rendering managed state must stay byte-for-byte stable.
        before = source.encode("utf-8")
        dry_run = self.editor.translate_cuboid(
            "agent_test",
            "floor",
            [0, 0, 0],
            inspected["revision"],
            dry_run=True,
        )
        self.assertEqual(dry_run["revision_before"], dry_run["revision_after"])
        self.assertEqual((self.repo / "maps" / "agent_test.map").read_bytes(), before)

    def test_invalid_geometry_is_rejected_without_changing_source(self) -> None:
        self.create_map()
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()

        with self.assertRaisesRegex(MapEditError, "max must exceed min"):
            self.add_box(maximum=[64, 64, -16])

        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(self.editor.inspect("agent_test")["cuboids"], [])

        with self.assertRaisesRegex(MapEditError, "outside worldspawn bounds"):
            self.add_box(minimum=[500, 500, 0], maximum=[600, 600, 16])

        self.assertEqual(path.read_bytes(), before)

    def test_stale_revision_is_rejected_without_changing_source(self) -> None:
        self.create_map()
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()

        with self.assertRaisesRegex(MapEditError, "stale map revision"):
            self.editor.add_cuboid(
                "agent_test",
                "floor",
                [-64, -64, -16],
                [64, 64, 0],
                "arena",
                "0" * 64,
            )

        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(self.editor.inspect("agent_test")["cuboids"], [])

    def test_rollback_restores_exact_prior_bytes(self) -> None:
        self.create_map()
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()
        added = self.add_box()
        self.assertNotEqual(path.read_bytes(), before)

        rolled_back = self.editor.rollback(
            added["rollback_token"], added["revision_after"]
        )

        self.assertTrue(rolled_back["applied"])
        self.assertEqual(rolled_back["revision_after"], added["revision_before"])
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(self.editor.inspect("agent_test")["cuboids"], [])

    def test_failed_atomic_source_replace_leaves_old_source(self) -> None:
        self.create_map()
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()
        transactions = self.repo / "maps" / ".lg-map-api" / "transactions"
        transaction_count = len(list(transactions.glob("*.json")))
        real_replace = os.replace

        def fail_source_replace(source: os.PathLike[str], destination: os.PathLike[str]) -> None:
            if Path(destination) == path:
                raise OSError("injected source replace failure")
            real_replace(source, destination)

        with mock.patch("lg_map_edit.os.replace", side_effect=fail_source_replace):
            with self.assertRaisesRegex(OSError, "injected source replace failure"):
                self.add_box()

        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(self.editor.inspect("agent_test")["cuboids"], [])
        self.assertEqual(list(path.parent.glob(f".{path.name}.*.tmp")), [])
        self.assertEqual(len(list(transactions.glob("*.json"))), transaction_count)

    def test_unsafe_map_names_and_materials_are_rejected(self) -> None:
        with self.assertRaisesRegex(MapEditError, "safe map stem"):
            self.editor.create("../outside")
        with self.assertRaisesRegex(MapEditError, "safe map stem"):
            self.editor.create("nested/map")

        self.create_map()
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()
        with self.assertRaisesRegex(MapEditError, "safe texture id"):
            self.add_box(material="../secret")
        self.assertEqual(path.read_bytes(), before)

        with self.assertRaisesRegex(MapEditError, "does not exist"):
            self.add_box(material="missing/texture")
        self.assertEqual(path.read_bytes(), before)
        self.assertFalse((self.repo / "outside.map").exists())

    def test_validate_sync_reload_returns_hashes_and_loaded_revision(self) -> None:
        self.create_map()
        added = self.add_box()
        ensure_runtime = mock.Mock()
        status = mock.Mock(side_effect=[
            {"map": "agent_test", "map_revision": 8},
            {"map": "agent_test", "map_revision": 9},
        ])
        load = mock.Mock()
        reload_current = mock.Mock(
            return_value={"map_revision": 9, "previous_map_revision": 8}
        )
        validation = {
            "ok": True,
            "available": True,
            "map": "agent_test",
            "source_revision": added["revision_after"],
            "structural": {"ok": True, "cuboids": 1},
            "exit_code": 0,
            "stdout": "valid",
            "stderr": "",
        }

        with mock.patch.object(self.editor, "_validate_snapshot", return_value=validation):
            result = self.editor.validate_sync_reload(
                "agent_test",
                added["revision_after"],
                ensure_runtime,
                status,
                load,
                reload_current,
            )

        self.assertEqual(result["validation"], validation)
        self.assertEqual(result["source_revision"], added["revision_after"])
        self.assertEqual(result["runtime_revision"], added["revision_after"])
        self.assertEqual(
            (self.repo / "build" / "default" / "maps" / "agent_test.map").read_bytes(),
            (self.repo / "maps" / "agent_test.map").read_bytes(),
        )
        self.assertEqual(
            result["loaded"],
            {
                "operation": "reload",
                "map_revision": 9,
                "previous_map_revision": 8,
            },
        )
        ensure_runtime.assert_called_once_with()
        self.assertEqual(status.call_count, 2)
        reload_current.assert_called_once_with()
        load.assert_not_called()

    def test_copy_translate_resize_set_material_and_delete_cuboids(self) -> None:
        self.create_map()
        added = self.add_box()
        copied = self.editor.copy_cuboid(
            "agent_test",
            "floor",
            "platform",
            [0, 0, 32],
            added["revision_after"],
        )
        translated = self.editor.translate_cuboid(
            "agent_test",
            "platform",
            [16, -8, 4],
            copied["revision_after"],
        )
        resized = self.editor.resize_cuboid(
            "agent_test",
            "platform",
            [-32, -24, 16],
            [48, 40, 28],
            translated["revision_after"],
        )
        material_changed = self.editor.set_material(
            "agent_test",
            "platform",
            "common/clip",
            resized["revision_after"],
        )
        bmp_changed = self.editor.set_material(
            "agent_test",
            "platform",
            "accent.bmp",
            material_changed["revision_after"],
        )
        deleted = self.editor.delete_cuboid(
            "agent_test",
            "floor",
            bmp_changed["revision_after"],
        )

        self.assertEqual(copied["diff"]["objects_added"], ["platform"])
        self.assertEqual(translated["diff"]["objects_changed"], ["platform"])
        self.assertEqual(resized["diff"]["objects_changed"], ["platform"])
        self.assertEqual(material_changed["diff"]["objects_changed"], ["platform"])
        self.assertEqual(deleted["diff"]["objects_deleted"], ["floor"])
        self.assertEqual(
            self.editor.inspect("agent_test")["cuboids"],
            [{
                "id": "platform",
                "min": [-32.0, -24.0, 16.0],
                "max": [48.0, 40.0, 28.0],
                "material": "accent.bmp",
            }],
        )

    def test_typed_spawn_and_worldspawn_property_edits(self) -> None:
        created = self.create_map()
        spawn_changed = self.editor.set_entity_properties(
            "agent_test",
            "spawn-a",
            created["revision_after"],
            origin=[-96, 12, 8],
            yaw=30.5,
        )
        bounds_changed = self.editor.set_entity_properties(
            "agent_test",
            "worldspawn",
            spawn_changed["revision_after"],
            bounds_min=[-1024, -768, -256],
            bounds_max=[1024, 768, 512],
        )

        entities = {
            entity["id"]: entity
            for entity in self.editor.inspect("agent_test")["entities"]
        }
        self.assertEqual(
            entities["spawn-a"]["properties"],
            {"origin": "-96 12 8", "angle": "30.5"},
        )
        self.assertEqual(
            entities["worldspawn"]["properties"],
            {
                "lg_bounds_min": "-1024 -768 -256",
                "lg_bounds_max": "1024 768 512",
                "lg_map_api_version": "1",
            },
        )
        self.assertEqual(spawn_changed["diff"]["objects_changed"], ["spawn-a"])
        self.assertEqual(bounds_changed["diff"]["objects_changed"], ["worldspawn"])


if __name__ == "__main__":
    unittest.main()
