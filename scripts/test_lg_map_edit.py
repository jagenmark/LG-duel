#!/usr/bin/env python3
"""Focused tests for the safe MCP map editor."""

from __future__ import annotations

import os
import hashlib
import copy
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import lg_map_edit
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

    def create_hand_map(self, name: str = "hand_map") -> bytes:
        data = (
            "// hand-authored header and spacing must stay exact\r\n"
            "{\r\n"
            '"classname" "worldspawn"\r\n'
            '"lg_bounds_min" "-512 -512 -128"\r\n'
            '"lg_bounds_max" "512 512 256"\r\n'
            '"lg_ambient_intensity" "0.2"\r\n'
            "{\r\n"
            "( -64 -64 -16 ) ( -64 -64 0 ) ( -64 64 -16 ) arena 0 0 0 1 1\r\n"
            "( 64 -64 -16 ) ( 64 64 -16 ) ( 64 -64 0 ) arena 0 0 0 1 1\r\n"
            "( -64 -64 -16 ) ( 64 -64 -16 ) ( -64 -64 0 ) arena 0 0 0 1 1\r\n"
            "( -64 64 -16 ) ( -64 64 0 ) ( 64 64 -16 ) arena 0 0 0 1 1\r\n"
            "( -64 -64 -16 ) ( -64 64 -16 ) ( 64 -64 -16 ) arena 0 0 0 1 1\r\n"
            "( -64 -64 0 ) ( 64 -64 0 ) ( -64 64 0 ) arena 0 0 0 1 1\r\n"
            "}\r\n"
            "}\r\n"
            "// UNRELATED-SENTINEL: keep this entity byte-for-byte\r\n"
            "{\r\n"
            '"classname" "light"\r\n'
            '"origin" "200 0 64"\r\n'
            '"light" "450"\r\n'
            '"radius" "280"\r\n'
            '"_color" "255 128 64"\r\n'
            "}\r\n"
        ).encode("utf-8")
        (self.repo / "maps" / f"{name}.map").write_bytes(data)
        return data

    def create_hand_map_with_sun(self, name: str, *, tagged: bool = True) -> None:
        data = self.create_hand_map(name)
        agent_id = '"lg_agent_id" "authored-sun"\r\n' if tagged else ""
        sun = (
            "{\r\n"
            '"classname" "light_sun"\r\n'
            f"{agent_id}"
            '"direction" "0.15 -0.35 -1"\r\n'
            '"color" "32 96 224"\r\n'
            '"intensity" "0.55"\r\n'
            "}\r\n"
        ).encode("utf-8")
        (self.repo / "maps" / f"{name}.map").write_bytes(data + sun)

    def append_hand_teleport_brushes(
        self, name: str, brush_count: int, *, target: str = "legacy-exit"
    ) -> None:
        brush = (
            "{\r\n"
            "( -16 -16 0 ) ( -16 -16 32 ) ( -16 16 0 ) common/trigger 0 0 0 1 1\r\n"
            "( 16 -16 0 ) ( 16 16 0 ) ( 16 -16 32 ) common/trigger 0 0 0 1 1\r\n"
            "( -16 -16 0 ) ( 16 -16 0 ) ( -16 -16 32 ) common/trigger 0 0 0 1 1\r\n"
            "( -16 16 0 ) ( -16 16 32 ) ( 16 16 0 ) common/trigger 0 0 0 1 1\r\n"
            "( -16 -16 0 ) ( -16 16 0 ) ( 16 -16 0 ) common/trigger 0 0 0 1 1\r\n"
            "( -16 -16 32 ) ( 16 -16 32 ) ( -16 16 32 ) common/trigger 0 0 0 1 1\r\n"
            "}\r\n"
        )
        entities = (
            "{\r\n"
            '"classname" "trigger_teleport"\r\n'
            f'"target" "{target}"\r\n'
            + brush * brush_count
            + "}\r\n"
            "{\r\n"
            '"classname" "target_position"\r\n'
            f'"targetname" "{target}"\r\n'
            '"origin" "128 0 16"\r\n'
            '"angle" "90"\r\n'
            "}\r\n"
        ).encode("utf-8")
        path = self.repo / "maps" / f"{name}.map"
        path.write_bytes(path.read_bytes() + entities)

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
        path = (self.repo / "maps" / "agent_test.map").resolve()
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
                "lg_map_api_version": "2",
            },
        )
        self.assertEqual(spawn_changed["diff"]["objects_changed"], ["spawn-a"])
        self.assertEqual(bounds_changed["diff"]["objects_changed"], ["worldspawn"])

    def test_point_light_crud_renders_runtime_keys_and_rolls_back(self) -> None:
        created = self.create_map()
        added = self.editor.add_point_light(
            "agent_test", "torch-a", [32, 4, 48], [255, 160, 64],
            2.5, 320, created["revision_after"],
            casts_shadows=True, source_radius=12, priority=25,
            flicker_enabled=True, flicker_seed=123, flicker_frequency=7.5,
            flicker_min=0.7, flicker_max=1.2,
        )
        light = self.editor.list_point_lights("agent_test")["point_lights"][0]
        self.assertEqual(light["id"], "torch-a")
        self.assertEqual(light["flicker"]["seed"], 123)
        source = (self.repo / "maps" / "agent_test.map").read_text("utf-8")
        for text in (
            '"classname" "light_point"', '"lg_agent_id" "torch-a"',
            '"casts_shadows" "1"', '"source_radius" "12"',
            '"priority" "25"', '"flicker" "1"',
            '"flicker_seed" "123"', '"flicker_frequency" "7.5"',
            '"flicker_min" "0.7"', '"flicker_max" "1.2"',
        ):
            self.assertIn(text, source)

        updated = self.editor.update_point_light(
            "agent_test", "torch-a", added["revision_after"],
            origin=[40, 8, 48], priority=-5, flicker_enabled=False,
        )
        changed = self.editor.inspect("agent_test")["point_lights"][0]
        self.assertEqual(changed["origin"], [40.0, 8.0, 48.0])
        self.assertEqual(changed["priority"], -5)
        self.assertEqual(changed["flicker"]["frequency"], 0.0)
        removed = self.editor.remove_point_light(
            "agent_test", "torch-a", updated["revision_after"]
        )
        self.assertEqual(self.editor.list_point_lights("agent_test")["count"], 0)
        self.editor.rollback(removed["rollback_token"], removed["revision_after"])
        self.assertEqual(self.editor.list_point_lights("agent_test")["count"], 1)

    def test_world_lighting_renders_ambient_and_one_optional_sun(self) -> None:
        created = self.create_map()
        changed = self.editor.set_world_lighting(
            "agent_test", created["revision_after"],
            ambient_color=[80, 120, 200], ambient_intensity=0.45,
            sun_enabled=True, sun_id="day-sun",
            sun_direction=[0.25, -0.5, -1], sun_color=[255, 230, 190],
            sun_intensity=0.8,
        )
        lighting = self.editor.get_world_lighting("agent_test")["world_lighting"]
        self.assertEqual(lighting["ambient_color"], [80.0, 120.0, 200.0])
        self.assertEqual(lighting["sun"]["id"], "day-sun")
        source = (self.repo / "maps" / "agent_test.map").read_text("utf-8")
        self.assertIn(
            '"lg_ambient_color" "0.31372549 0.470588235 0.784313725"',
            source,
        )
        self.assertIn('"lg_ambient_intensity" "0.45"', source)
        self.assertEqual(source.count('"classname" "light_sun"'), 1)

        disabled = self.editor.set_world_lighting(
            "agent_test", changed["revision_after"], sun_enabled=False
        )
        self.assertIsNone(
            self.editor.get_world_lighting("agent_test")["world_lighting"]["sun"]
        )
        self.assertNotIn(
            '"classname" "light_sun"',
            (self.repo / "maps" / "agent_test.map").read_text("utf-8"),
        )
        self.assertTrue(disabled["applied"])

    def test_managed_partial_world_lighting_preserves_omitted_fields(self) -> None:
        cases = (
            ("ambient_color", {"ambient_color": [1, 2, 3]}),
            ("ambient_intensity", {"ambient_intensity": 0.9}),
            ("sun_id", {"sun_id": "other-sun"}),
            ("sun_direction", {"sun_direction": [-0.2, 0.4, -1]}),
            ("sun_color", {"sun_color": [4, 5, 6]}),
            ("sun_intensity", {"sun_intensity": 1.1}),
            ("sun_enabled", {"sun_enabled": True}),
        )
        for index, (field, update) in enumerate(cases):
            with self.subTest(field=field):
                name = f"managed_partial_{index}"
                created = self.editor.create(name)
                seeded = self.editor.set_world_lighting(
                    name, created["revision_after"],
                    ambient_color=[20, 40, 80], ambient_intensity=0.42,
                    sun_enabled=True, sun_id="authored-sun",
                    sun_direction=[0.15, -0.35, -1],
                    sun_color=[32, 96, 224], sun_intensity=0.55,
                )
                before = self.editor.get_world_lighting(name)["world_lighting"]
                changed = self.editor.set_world_lighting(
                    name, seeded["revision_after"], **update
                )
                after = self.editor.get_world_lighting(name)["world_lighting"]
                expected = copy.deepcopy(before)
                if field.startswith("ambient_"):
                    expected[field] = [float(item) for item in update[field]] if (
                        field == "ambient_color"
                    ) else float(update[field])
                elif field == "sun_enabled":
                    pass
                else:
                    sun_field = field.removeprefix("sun_")
                    value = update[field]
                    expected["sun"][sun_field] = (
                        [float(item) for item in value]
                        if isinstance(value, list) else value
                    )
                self.assertEqual(after, expected)
                self.assertEqual(
                    changed["applied"], field != "sun_enabled"
                )

        name = "managed_partial_disable"
        created = self.editor.create(name)
        seeded = self.editor.set_world_lighting(
            name, created["revision_after"],
            ambient_color=[20, 40, 80], ambient_intensity=0.42,
            sun_enabled=True, sun_color=[32, 96, 224],
        )
        disabled = self.editor.set_world_lighting(
            name, seeded["revision_after"], sun_enabled=False
        )
        after = self.editor.get_world_lighting(name)["world_lighting"]
        self.assertEqual(after["ambient_color"], [20.0, 40.0, 80.0])
        self.assertEqual(after["ambient_intensity"], 0.42)
        self.assertIsNone(after["sun"])
        self.assertTrue(disabled["applied"])

    def test_direct_partial_world_lighting_preserves_omitted_fields(self) -> None:
        cases = (
            ("ambient_color", {"ambient_color": [1, 2, 3]}),
            ("ambient_intensity", {"ambient_intensity": 0.9}),
            ("sun_id", {"sun_id": "other-sun"}),
            ("sun_direction", {"sun_direction": [-0.2, 0.4, -1]}),
            ("sun_color", {"sun_color": [4, 5, 6]}),
            ("sun_intensity", {"sun_intensity": 1.1}),
            ("sun_enabled", {"sun_enabled": True}),
        )
        for index, (field, update) in enumerate(cases):
            with self.subTest(field=field):
                name = f"direct_partial_{index}"
                self.create_hand_map_with_sun(name)
                before_result = self.editor.get_world_lighting(name)
                before = before_result["world_lighting"]
                changed = self.editor.set_world_lighting(
                    name, before_result["revision"], **update
                )
                after = self.editor.get_world_lighting(name)["world_lighting"]
                expected = copy.deepcopy(before)
                if field.startswith("ambient_"):
                    expected[field] = [float(item) for item in update[field]] if (
                        field == "ambient_color"
                    ) else float(update[field])
                elif field == "sun_enabled":
                    pass
                else:
                    sun_field = field.removeprefix("sun_")
                    value = update[field]
                    expected["sun"][sun_field] = (
                        [float(item) for item in value]
                        if isinstance(value, list) else value
                    )
                self.assertEqual(after, expected)
                self.assertTrue(changed["applied"])

        name = "direct_partial_disable"
        self.create_hand_map_with_sun(name)
        before = self.editor.get_world_lighting(name)
        disabled = self.editor.set_world_lighting(
            name, before["revision"], sun_enabled=False
        )
        after = self.editor.get_world_lighting(name)["world_lighting"]
        self.assertEqual(after["ambient_color"], [255.0, 255.0, 255.0])
        self.assertEqual(after["ambient_intensity"], 0.2)
        self.assertIsNone(after["sun"])
        self.assertTrue(disabled["applied"])

    def test_direct_untagged_sun_partial_updates_keep_values_and_crlf(self) -> None:
        self.create_hand_map_with_sun("untagged_sun", tagged=False)
        before = self.editor.get_world_lighting("untagged_sun")
        self.assertEqual(before["world_lighting"]["sun"]["color"], [32.0, 96.0, 224.0])
        intensity_changed = self.editor.set_world_lighting(
            "untagged_sun", before["revision"], sun_intensity=0.75
        )
        after = self.editor.get_world_lighting("untagged_sun")["world_lighting"]
        self.assertEqual(after["sun"]["id"], "sun")
        self.assertEqual(after["sun"]["direction"], [0.15, -0.35, -1.0])
        self.assertEqual(after["sun"]["color"], [32.0, 96.0, 224.0])
        self.assertEqual(after["sun"]["intensity"], 0.75)
        source = (self.repo / "maps" / "untagged_sun.map").read_bytes()
        self.assertEqual(source.count(b"\n"), source.count(b"\r\n"))

        id_changed = self.editor.set_world_lighting(
            "untagged_sun", intensity_changed["revision_after"],
            sun_id="named-sun",
        )
        after_id = self.editor.get_world_lighting("untagged_sun")["world_lighting"]
        self.assertEqual(after_id["sun"]["id"], "named-sun")
        self.assertEqual(after_id["sun"]["direction"], [0.15, -0.35, -1.0])
        self.assertEqual(after_id["sun"]["color"], [32.0, 96.0, 224.0])
        self.assertEqual(after_id["sun"]["intensity"], 0.75)
        source = (self.repo / "maps" / "untagged_sun.map").read_bytes()
        self.assertEqual(source.count(b"\n"), source.count(b"\r\n"))
        self.assertTrue(id_changed["applied"])

    def test_point_light_validation_is_closed_and_bounded(self) -> None:
        created = self.create_map()
        base = {
            "map_name": "agent_test", "object_id": "bad-light",
            "origin": [0, 0, 0], "color": [255, 255, 255],
            "intensity": 1, "radius": 100,
            "expected_revision": created["revision_after"],
        }
        invalid = (
            ({"origin": [900, 0, 0]}, "outside worldspawn bounds"),
            ({"radius": 0}, "point light radius"),
            ({"radius": 4097}, "point light radius"),
            ({"intensity": -1}, "point light intensity"),
            ({"intensity": 17}, "point light intensity"),
            ({"source_radius": 101}, "source radius"),
            ({"priority": 1001}, "priority"),
            (
                {
                    "flicker_enabled": True, "flicker_frequency": 0.05,
                },
                "enabled flicker frequency",
            ),
            (
                {
                    "flicker_enabled": True, "flicker_frequency": 2,
                    "flicker_min": 2, "flicker_max": 1,
                },
                "flicker min",
            ),
        )
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()
        for changes, message in invalid:
            with self.subTest(changes=changes):
                with self.assertRaisesRegex(MapEditError, message):
                    self.editor.add_point_light(**{**base, **changes})
                self.assertEqual(path.read_bytes(), before)

        duplicate = [
            {
                "op": "add_point_light", "id": "same-light",
                "origin": [0, 0, 0], "color": [255, 255, 255],
                "intensity": 1, "radius": 100,
            },
        ] * 2
        with self.assertRaisesRegex(MapEditError, "operation 1.*already exists"):
            self.editor.apply_batch(
                "agent_test", duplicate, created["revision_after"]
            )
        cap_batch = [
            {
                "op": "add_point_light", "id": f"light-{index}",
                "origin": [0, 0, 0], "color": [255, 255, 255],
                "intensity": 1, "radius": 100,
            }
            for index in range(97)
        ]
        with self.assertRaisesRegex(MapEditError, "operation 96.*at most 96"):
            self.editor.apply_batch(
                "agent_test", cap_batch, created["revision_after"]
            )
        self.assertEqual(path.read_bytes(), before)

    def test_api_colors_round_trip_0_1_255_without_map_scale_ambiguity(self) -> None:
        created = self.create_map()
        changed = self.editor.apply_batch(
            "agent_test",
            [
                {
                    "op": "set_world_lighting",
                    "ambient_color": [0, 1, 255],
                    "sun_enabled": True,
                    "sun_color": [255, 1, 0],
                },
                {
                    "op": "add_point_light", "id": "color-light",
                    "origin": [0, 0, 32], "color": [1, 0, 255],
                    "intensity": 1, "radius": 100,
                },
            ],
            created["revision_after"],
        )
        inspected = self.editor.inspect("agent_test")
        self.assertEqual(
            inspected["world_lighting"]["ambient_color"], [0.0, 1.0, 255.0]
        )
        self.assertEqual(
            inspected["world_lighting"]["sun"]["color"], [255.0, 1.0, 0.0]
        )
        self.assertEqual(
            inspected["point_lights"][0]["color"], [1.0, 0.0, 255.0]
        )
        source = (self.repo / "maps" / "agent_test.map").read_text("utf-8")
        self.assertIn(
            '"lg_ambient_color" "0 0.00392156863 1"', source
        )
        self.assertIn('"color" "1 0.00392156863 0"', source)
        self.assertIn('"_color" "0.00392156863 0 1"', source)
        self.assertEqual(
            self.editor.inspect("agent_test")["revision"],
            changed["revision_after"],
        )

    def test_teleport_crud_renders_linked_trigger_and_exit(self) -> None:
        created = self.create_map()
        added = self.editor.add_teleport(
            "agent_test", "upper-exit",
            [-32, -24, 0], [32, 24, 48], [160, 80, 32], 135,
            created["revision_after"],
        )
        teleport = self.editor.list_teleports("agent_test")["teleports"][0]
        self.assertEqual(
            teleport,
            {
                "id": "upper-exit",
                "min": [-32.0, -24.0, 0.0],
                "max": [32.0, 24.0, 48.0],
                "destination": [160.0, 80.0, 32.0],
                "exit_yaw": 135.0,
            },
        )
        source = (self.repo / "maps" / "agent_test.map").read_text("utf-8")
        target_name = "lg_agent_teleport_target_upper-exit"
        self.assertIn('"classname" "trigger_teleport"', source)
        self.assertIn('"classname" "target_position"', source)
        self.assertIn(
            '"lg_agent_id" "lg-internal-teleport-trigger-upper-exit"', source
        )
        self.assertIn(
            '"lg_agent_id" "lg-internal-teleport-target-upper-exit"', source
        )
        self.assertIn(f'"target" "{target_name}"', source)
        self.assertIn(f'"targetname" "{target_name}"', source)
        self.assertIn('"origin" "160 80 32"', source)
        self.assertIn('"angle" "135"', source)
        self.assertEqual(
            sum(line.startswith("( ") for line in source.splitlines()), 6
        )
        self.assertTrue(
            all(
                line.endswith("common/trigger 0 0 0 1 1")
                for line in source.splitlines() if line.startswith("( ")
            )
        )

        updated = self.editor.update_teleport(
            "agent_test", "upper-exit", added["revision_after"],
            destination=[128, -64, 24], exit_yaw=-45,
        )
        changed = self.editor.inspect("agent_test")["teleports"][0]
        self.assertEqual(changed["destination"], [128.0, -64.0, 24.0])
        self.assertEqual(changed["exit_yaw"], -45.0)
        removed = self.editor.remove_teleport(
            "agent_test", "upper-exit", updated["revision_after"]
        )
        self.assertEqual(self.editor.list_teleports("agent_test")["count"], 0)
        self.editor.rollback(removed["rollback_token"], removed["revision_after"])
        self.assertEqual(self.editor.list_teleports("agent_test")["count"], 1)

    def test_teleport_bounds_cap_ids_and_revisions_fail_closed(self) -> None:
        created = self.create_map()
        path = self.repo / "maps" / "agent_test.map"
        before = path.read_bytes()
        base = (
            "agent_test", "bad-teleport",
            [-16, -16, 0], [16, 16, 32], [100, 0, 16], 0,
            created["revision_after"],
        )
        invalid = (
            (([16, -16, 0], [16, 16, 32], [100, 0, 16]), "max must exceed min"),
            (([-600, -16, 0], [16, 16, 32], [100, 0, 16]), "outside worldspawn"),
            (([-16, -16, 0], [16, 16, 32], [600, 0, 16]), "destination is outside"),
        )
        for (minimum, maximum, destination), message in invalid:
            with self.subTest(message=message):
                with self.assertRaisesRegex(MapEditError, message):
                    self.editor.add_teleport(
                        base[0], base[1], minimum, maximum, destination,
                        base[5], base[6],
                    )
                self.assertEqual(path.read_bytes(), before)
        with self.assertRaisesRegex(MapEditError, "stale map revision"):
            self.editor.add_teleport(
                *base[:6], "0" * 64
            )
        self.assertEqual(path.read_bytes(), before)

        operations = [
            {
                "op": "add_teleport", "id": f"teleport-{index}",
                "min": [-16, -16, 0], "max": [16, 16, 32],
                "destination": [100, 0, 16], "exit_yaw": index,
            }
            for index in range(17)
        ]
        with self.assertRaisesRegex(MapEditError, "operation 16.*at most 16"):
            self.editor.apply_batch(
                "agent_test", operations, created["revision_after"]
            )
        self.assertEqual(path.read_bytes(), before)

        light = self.editor.add_point_light(
            "agent_test", "shared-id", [0, 0, 16], [255, 255, 255],
            1, 100, created["revision_after"],
        )
        with self.assertRaisesRegex(MapEditError, "already exists"):
            self.editor.add_teleport(
                "agent_test", "shared-id", [-16, -16, 0], [16, 16, 32],
                [100, 0, 16], 0, light["revision_after"],
            )

    def test_teleport_batch_is_atomic_and_uses_one_rollback(self) -> None:
        created = self.create_map()
        before = (self.repo / "maps" / "agent_test.map").read_bytes()
        result = self.editor.apply_batch(
            "agent_test",
            [
                {
                    "op": "add_teleport", "id": "gate-a",
                    "min": [-24, -24, 0], "max": [24, 24, 40],
                    "destination": [200, 0, 24], "exit_yaw": 180,
                },
                {
                    "op": "update_teleport", "id": "gate-a",
                    "destination": [220, 0, 24],
                },
            ],
            created["revision_after"],
        )
        self.assertEqual(result["diff"]["objects_added"], ["gate-a"])
        self.assertEqual(result["diff"]["objects_changed"], ["gate-a"])
        self.assertIsNotNone(result["rollback_token"])
        self.assertEqual(
            self.editor.inspect("agent_test")["teleports"][0]["destination"],
            [220.0, 0.0, 24.0],
        )
        self.editor.rollback(result["rollback_token"], result["revision_after"])
        self.assertEqual(
            (self.repo / "maps" / "agent_test.map").read_bytes(), before
        )

    def test_atomic_batch_is_all_or_nothing_and_has_one_rollback(self) -> None:
        created = self.create_map()
        before = (self.repo / "maps" / "agent_test.map").read_bytes()
        operations = [
            {
                "op": "set_world_lighting",
                "ambient_color": [180, 190, 220],
                "ambient_intensity": 0.35,
            },
            {
                "op": "add_point_light", "id": "torch-a",
                "origin": [20, 0, 32], "color": [255, 140, 40],
                "intensity": 2, "radius": 240,
                "flicker_enabled": True, "flicker_seed": 4,
                "flicker_frequency": 6, "flicker_min": 0.8,
                "flicker_max": 1.15,
            },
            {
                "op": "update_point_light", "id": "torch-a",
                "casts_shadows": True, "priority": 10,
            },
            {
                "op": "add_cuboid", "id": "torch-bracket",
                "min": [16, -2, 24], "max": [24, 2, 32],
                "material": "arena",
            },
        ]
        applied = self.editor.apply_batch(
            "agent_test", operations, created["revision_after"]
        )
        self.assertTrue(applied["applied"])
        self.assertIsNotNone(applied["rollback_token"])
        self.assertEqual(
            applied["diff"]["objects_added"], ["torch-a", "torch-bracket"]
        )
        self.assertEqual(applied["diff"]["objects_changed"], ["worldspawn", "torch-a"])
        self.editor.rollback(applied["rollback_token"], applied["revision_after"])
        self.assertEqual(
            (self.repo / "maps" / "agent_test.map").read_bytes(), before
        )

        revision = self.editor.inspect("agent_test")["revision"]
        bad_batch = operations + [{
            "op": "add_point_light", "id": "outside",
            "origin": [1000, 0, 0], "color": [255, 255, 255],
            "intensity": 1, "radius": 100,
        }]
        with self.assertRaisesRegex(MapEditError, "operation 4"):
            self.editor.apply_batch("agent_test", bad_batch, revision)
        self.assertEqual(
            (self.repo / "maps" / "agent_test.map").read_bytes(), before
        )
        with self.assertRaisesRegex(MapEditError, "stale map revision"):
            self.editor.apply_batch("agent_test", operations, "0" * 64)
        self.assertEqual(
            (self.repo / "maps" / "agent_test.map").read_bytes(), before
        )

    def test_canonical_v1_migrates_on_write_and_hand_edits_do_not(self) -> None:
        state_v1 = {
            "format": 1,
            "template": "initial",
            "entities": [
                {
                    "id": "worldspawn", "classname": "worldspawn",
                    "properties": {
                        "lg_bounds_max": "512 512 256",
                        "lg_bounds_min": "-512 -512 -128",
                        "lg_map_api_version": "1",
                    },
                },
                {
                    "id": "spawn-a", "classname": "lg_spawn",
                    "properties": {"angle": "0", "origin": "-128 0 0"},
                },
            ],
            "cuboids": [],
        }
        path = self.repo / "maps" / "legacy.map"
        old_bytes = lg_map_edit._render_version(state_v1, version=1)
        path.write_bytes(old_bytes)
        inspected = self.editor.inspect("legacy")
        self.assertEqual(inspected["format"], 2)
        self.assertEqual(inspected["teleports"], [])
        migrated = self.editor.set_world_lighting(
            "legacy", inspected["revision"], ambient_intensity=0.4
        )
        self.assertTrue(path.read_bytes().startswith(b"// lg-map-api-state-v2 "))
        self.editor.rollback(migrated["rollback_token"], migrated["revision_after"])
        self.assertEqual(path.read_bytes(), old_bytes)

        hand_path = self.repo / "maps" / "hand.map"
        hand_path.write_bytes(old_bytes + b"// hand edit\n")
        with self.assertRaisesRegex(MapEditError, "changed outside the API"):
            self.editor.inspect("hand")

    def test_list_maps_keeps_tampered_managed_files_closed(self) -> None:
        current = self.create_map("current")
        current_path = self.repo / "maps" / "current.map"
        current_path.write_bytes(current_path.read_bytes() + b"// hand edit\n")

        state_v1 = {
            "format": 1,
            "template": "initial",
            "entities": [{
                "id": "worldspawn",
                "classname": "worldspawn",
                "properties": {
                    "lg_bounds_max": "512 512 256",
                    "lg_bounds_min": "-512 -512 -128",
                    "lg_map_api_version": "1",
                },
            }],
            "cuboids": [],
        }
        legacy_path = self.repo / "maps" / "legacy_tampered.map"
        legacy_path.write_bytes(
            lg_map_edit._render_version(state_v1, version=1) + b"// hand edit\n"
        )

        listed = {item["map"]: item for item in self.editor.list_maps()["maps"]}
        expected = (
            "managed map text changed outside the API; use TrenchBroom or "
            "recreate the managed map"
        )
        for name in ("current", "legacy_tampered"):
            with self.subTest(name=name):
                self.assertTrue(listed[name]["managed"])
                self.assertFalse(listed[name]["editable"])
                self.assertEqual(listed[name]["edit_status"], expected)
                self.assertNotIn("objects", listed[name])

    def test_hand_map_world_and_point_light_edits_preserve_unrelated_bytes(self) -> None:
        before = self.create_hand_map()
        inspected = self.editor.inspect("hand_map")
        self.assertFalse(inspected["managed"])
        self.assertEqual(inspected["editing_mode"], "direct_non_lossy")
        self.assertEqual(inspected["unowned"]["point_lights"], 1)
        self.assertEqual(inspected["point_lights"], [])
        sentinel = before.split(b"// UNRELATED-SENTINEL:", 1)[1]

        changed = self.editor.apply_batch(
            "hand_map",
            [
                {
                    "op": "set_world_lighting",
                    "ambient_color": [0, 1, 255],
                    "ambient_intensity": 0.35,
                    "sun_enabled": True,
                    "sun_id": "agent-sun",
                    "sun_direction": [0.2, -0.4, -1],
                    "sun_color": [255, 220, 180],
                    "sun_intensity": 0.6,
                },
                {
                    "op": "add_point_light", "id": "agent-torch",
                    "origin": [-100, 0, 64], "color": [255, 120, 32],
                    "intensity": 2, "radius": 240,
                    "casts_shadows": True,
                },
            ],
            inspected["revision"],
        )
        after = (self.repo / "maps" / "hand_map.map").read_bytes()
        self.assertFalse(after.startswith(b"// lg-map-api-state-"))
        self.assertIn(sentinel, after)
        self.assertIn(b'"lg_ambient_color" "0 0.00392156863 1"', after)
        self.assertIn(b'"lg_agent_id" "agent-torch"', after)
        self.assertIn(b'"lg_agent_id" "agent-sun"', after)
        self.assertEqual(
            self.editor.get_world_lighting("hand_map")["world_lighting"]["sun"][
                "id"
            ],
            "agent-sun",
        )
        self.assertEqual(
            self.editor.list_point_lights("hand_map")["point_lights"][0]["id"],
            "agent-torch",
        )

        flicker_on = self.editor.update_point_light(
            "hand_map", "agent-torch", changed["revision_after"],
            flicker_enabled=True,
        )
        flicker = self.editor.list_point_lights("hand_map")["point_lights"][0][
            "flicker"
        ]
        self.assertTrue(flicker["enabled"])
        self.assertEqual(flicker["frequency"], 8.0)
        flicker_off = self.editor.update_point_light(
            "hand_map", "agent-torch", flicker_on["revision_after"],
            flicker_enabled=False,
        )
        flicker = self.editor.list_point_lights("hand_map")["point_lights"][0][
            "flicker"
        ]
        self.assertFalse(flicker["enabled"])
        self.assertEqual(flicker["frequency"], 0.0)

        updated = self.editor.update_point_light(
            "hand_map", "agent-torch", flicker_off["revision_after"],
            origin=[-80, 0, 72], intensity=3,
        )
        self.assertIn(
            b'"origin" "-80 0 72"',
            (self.repo / "maps" / "hand_map.map").read_bytes(),
        )
        with self.assertRaisesRegex(MapEditError, "stale map revision"):
            self.editor.remove_point_light(
                "hand_map", "agent-torch", changed["revision_after"]
            )
        removed = self.editor.remove_point_light(
            "hand_map", "agent-torch", updated["revision_after"]
        )
        self.assertEqual(self.editor.list_point_lights("hand_map")["count"], 0)
        self.editor.rollback(removed["rollback_token"], removed["revision_after"])
        self.assertEqual(self.editor.list_point_lights("hand_map")["count"], 1)
        self.editor.rollback(updated["rollback_token"], updated["revision_after"])
        self.editor.rollback(
            flicker_off["rollback_token"], flicker_off["revision_after"]
        )
        self.editor.rollback(
            flicker_on["rollback_token"], flicker_on["revision_after"]
        )
        restored = self.editor.rollback(
            changed["rollback_token"], changed["revision_after"]
        )
        self.assertEqual(
            restored["revision_after"], hashlib.sha256(before).hexdigest()
        )
        self.assertEqual(
            (self.repo / "maps" / "hand_map.map").read_bytes(), before
        )

    def test_hand_map_scanner_handles_same_line_properties_and_quoted_syntax(self) -> None:
        source = (
            '// Format: Standard\n'
            '{ "classname" "worldspawn" "lg_bounds_min" "-512 -512 -128" '
            '"lg_bounds_max" "512 512 256" "note" "{ // quoted }" }\n'
            '{ "classname" "lg_spawn" "origin" "0 0 0" "angle" "0" }\n'
            '{ "classname" "light" "lg_agent_id" "same-line-light" '
            '"origin" "20 0 32" "_light" "600" "radius" "320" }\n'
        ).encode("utf-8")
        path = self.repo / "maps" / "same_line.map"
        path.write_bytes(source)

        inspected = self.editor.inspect("same_line")
        self.assertEqual(inspected["point_lights"][0]["intensity"], 2.0)
        changed = self.editor.set_world_lighting(
            "same_line", inspected["revision"], ambient_intensity=0.4
        )
        after = path.read_text("utf-8")
        self.assertIn('"note" "{ // quoted }"', after)
        self.assertIn('"lg_ambient_intensity" "0.4"', after)
        self.assertLess(
            after.index('"lg_ambient_intensity" "0.4"'),
            after.index("}\n"),
        )
        updated = self.editor.update_point_light(
            "same_line", "same-line-light", changed["revision_after"], radius=400
        )
        self.assertTrue(updated["applied"])
        self.assertEqual(
            self.editor.list_point_lights("same_line")["point_lights"][0]["radius"],
            400.0,
        )

    def test_same_line_world_patch_passes_built_runtime_validator(self) -> None:
        project_editor = MapEditor(Path(__file__).resolve().parents[1])
        validator = project_editor.validator_path()
        if validator is None:
            self.skipTest("lg_duel_map_validate is not built")
        path = self.repo / "maps" / "same_line_validated.map"
        path.write_text(
            '// Format: Standard\n'
            '{ "classname" "worldspawn" "lg_bounds_min" "-512 -512 -128" '
            '"lg_bounds_max" "512 512 256" }\n'
            '{ "classname" "lg_spawn" "origin" "0 0 0" "angle" "0" }\n'
            '{ "classname" "lg_spawn" "origin" "128 0 0" "angle" "180" }\n'
            '{\n"classname" "func_group"\n{\n'
            '( -64 -64 -16 ) ( -64 -64 0 ) ( -64 64 -16 ) arena 0 0 0 1 1\n'
            '( 64 -64 -16 ) ( 64 64 -16 ) ( 64 -64 0 ) arena 0 0 0 1 1\n'
            '( -64 -64 -16 ) ( 64 -64 -16 ) ( -64 -64 0 ) arena 0 0 0 1 1\n'
            '( -64 64 -16 ) ( -64 64 0 ) ( 64 64 -16 ) arena 0 0 0 1 1\n'
            '( -64 -64 -16 ) ( -64 64 -16 ) ( 64 -64 -16 ) arena 0 0 0 1 1\n'
            '( -64 -64 0 ) ( 64 -64 0 ) ( -64 64 0 ) arena 0 0 0 1 1\n'
            '}\n}\n',
            encoding="utf-8",
        )
        before = self.editor.inspect("same_line_validated")
        with mock.patch.object(
            self.editor, "validator_path", return_value=validator
        ):
            changed = self.editor.set_world_lighting(
                "same_line_validated",
                before["revision"],
                ambient_intensity=0.4,
            )
        self.assertTrue(changed["applied"])

    def test_direct_edit_stages_backslash_common_material_like_runtime(self) -> None:
        validator = MapEditor(Path(__file__).resolve().parents[1]).validator_path()
        if validator is None:
            self.skipTest("lg_duel_map_validate is not built")
        self.create_hand_map("backslash_material")
        path = self.repo / "maps" / "backslash_material.map"
        path.write_bytes(
            path.read_bytes().replace(
                b" arena 0 0 0 1 1",
                b" textures\\common\\playerclip 0 0 0 1 1",
            )
            + b'{\n"classname" "lg_spawn"\n"origin" "0 0 0"\n}\n'
            + b'{\n"classname" "lg_spawn"\n"origin" "128 0 0"\n}\n'
        )
        before = self.editor.inspect("backslash_material")
        with mock.patch.object(
            self.editor, "validator_path", return_value=validator
        ):
            changed = self.editor.set_world_lighting(
                "backslash_material",
                before["revision"],
                ambient_intensity=0.4,
            )
        self.assertTrue(changed["applied"])

    def test_hand_map_scanner_handles_long_comment_runs(self) -> None:
        comments = "".join(f"// comment {index}\n" for index in range(1200))
        path = self.repo / "maps" / "many_comments.map"
        path.write_text(
            '// Format: Standard\n{\n"classname" "worldspawn"\n'
            + comments
            + '"lg_bounds_min" "-512 -512 -128"\n'
            '"lg_bounds_max" "512 512 256"\n}\n',
            encoding="utf-8",
        )
        before = self.editor.inspect("many_comments")
        changed = self.editor.set_world_lighting(
            "many_comments", before["revision"], ambient_intensity=0.45
        )
        self.assertTrue(changed["applied"])
        after = path.read_text("utf-8")
        self.assertEqual(after.count("// comment "), 1200)
        self.assertIn('"lg_ambient_intensity" "0.45"', after)

    def test_hand_sun_uses_runtime_direction_and_color_fallbacks(self) -> None:
        cases = [
            (
                "default_sun",
                '"_color" "0.1 0.2 0.3"\n',
                lg_map_edit.DEFAULT_SUN_DIRECTION,
                [25.5, 51.0, 76.5],
            ),
            (
                "angled_sun",
                '"angle" "90"\n"pitch" "-30"\n"_color" "32 96 224"\n',
                [0.0, 0.8660254037844387, -0.5],
                [32.0, 96.0, 224.0],
            ),
        ]
        for name, fields, expected_direction, expected_color in cases:
            with self.subTest(name=name):
                self.create_hand_map(name)
                path = self.repo / "maps" / f"{name}.map"
                path.write_text(
                    path.read_text("utf-8")
                    + '{\n"classname" "light_sun"\n'
                    + fields
                    + '"intensity" "0.55"\n}\n',
                    encoding="utf-8",
                )
                before = self.editor.get_world_lighting(name)
                sun = before["world_lighting"]["sun"]
                for actual, expected in zip(sun["direction"], expected_direction):
                    self.assertAlmostEqual(actual, expected, places=6)
                self.assertEqual(sun["color"], expected_color)
                changed = self.editor.set_world_lighting(
                    name, before["revision"], sun_intensity=0.8
                )
                after = self.editor.get_world_lighting(name)["world_lighting"]["sun"]
                for actual, expected in zip(after["direction"], expected_direction):
                    self.assertAlmostEqual(actual, expected, places=6)
                self.assertEqual(after["color"], expected_color)
                self.assertEqual(after["intensity"], 0.8)
                self.assertTrue(changed["applied"])

    def test_hand_sun_rejects_zero_direction_without_validator(self) -> None:
        self.create_hand_map_with_sun("zero_sun")
        path = self.repo / "maps" / "zero_sun.map"
        before = path.read_bytes()
        revision = self.editor.inspect("zero_sun")["revision"]
        self.assertIsNone(self.editor.validator_path())
        with self.assertRaisesRegex(MapEditError, "sun direction must be non-zero"):
            self.editor.set_world_lighting(
                "zero_sun", revision, sun_direction=[0, 0, 0]
            )
        self.assertEqual(path.read_bytes(), before)

    def test_tagged_legacy_point_light_matches_runtime_defaults_and_precedence(self) -> None:
        cases = [
            ("origin_only", "", [255.0, 255.0, 255.0], 1.0, 320.0),
            ("scalar", '"_light" "600"\n', [255.0, 255.0, 255.0], 2.0, 320.0),
            (
                "tuple",
                '"_color" "1 1 1"\n"_light" "0.1 0.2 0.3 900"\n',
                [25.5, 51.0, 76.5],
                3.0,
                320.0,
            ),
            (
                "precedence",
                '"_light" "600"\n"light" "900"\n"intensity" "4"\n'
                '"radius" "500"\n',
                [255.0, 255.0, 255.0],
                4.0,
                500.0,
            ),
        ]
        for name, fields, color, intensity, radius in cases:
            with self.subTest(name=name):
                self.create_hand_map(name)
                path = self.repo / "maps" / f"{name}.map"
                path.write_text(
                    path.read_text("utf-8")
                    + '{\n"classname" "light"\n'
                    f'"lg_agent_id" "{name}-light"\n'
                    '"origin" "20 0 32"\n'
                    + fields
                    + "}\n",
                    encoding="utf-8",
                )
                before = self.editor.list_point_lights(name)
                light = before["point_lights"][0]
                self.assertEqual(light["color"], color)
                self.assertEqual(light["intensity"], intensity)
                self.assertEqual(light["radius"], radius)
                changed = self.editor.update_point_light(
                    name, f"{name}-light", before["revision"],
                    casts_shadows=True,
                )
                after = self.editor.list_point_lights(name)["point_lights"][0]
                self.assertEqual(after["color"], color)
                self.assertEqual(after["intensity"], intensity)
                self.assertEqual(after["radius"], radius)
                self.assertTrue(after["casts_shadows"])
                self.assertTrue(changed["applied"])

    def test_hand_map_teleports_are_owned_linked_and_atomic(self) -> None:
        before = self.create_hand_map()
        revision = self.editor.inspect("hand_map")["revision"]
        added = self.editor.add_teleport(
            "hand_map", "gate-a",
            [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
            revision,
        )
        source = (self.repo / "maps" / "hand_map.map").read_text("utf-8")
        self.assertIn(
            '"lg_agent_id" "lg-internal-teleport-trigger-gate-a"', source
        )
        self.assertIn(
            '"target" "lg_agent_teleport_target_gate-a"', source
        )
        self.assertEqual(self.editor.list_teleports("hand_map")["count"], 1)
        updated = self.editor.update_teleport(
            "hand_map", "gate-a", added["revision_after"],
            destination=[220, 0, 32], exit_yaw=180,
        )
        self.assertEqual(
            self.editor.list_teleports("hand_map")["teleports"][0]["destination"],
            [220.0, 0.0, 32.0],
        )
        removed = self.editor.remove_teleport(
            "hand_map", "gate-a", updated["revision_after"]
        )
        self.assertEqual(self.editor.list_teleports("hand_map")["count"], 0)
        self.assertIn(
            before.split(b"// UNRELATED-SENTINEL:", 1)[1],
            (self.repo / "maps" / "hand_map.map").read_bytes(),
        )
        self.editor.rollback(removed["rollback_token"], removed["revision_after"])
        self.assertEqual(self.editor.list_teleports("hand_map")["count"], 1)

    def test_hand_teleport_rejects_targetname_collision_without_validator(self) -> None:
        self.create_hand_map("target_collision")
        path = self.repo / "maps" / "target_collision.map"
        path.write_text(
            path.read_text("utf-8")
            + '{\n"classname" "target_position"\n'
            '"targetname" "lg_agent_teleport_target_gate-a"\n'
            '"origin" "128 0 16"\n}\n',
            encoding="utf-8",
        )
        before = path.read_bytes()
        inspected = self.editor.inspect("target_collision")
        self.assertIsNone(self.editor.validator_path())
        with self.assertRaisesRegex(MapEditError, "targetname .* already exists"):
            self.editor.add_teleport(
                "target_collision", "gate-a",
                [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
                inspected["revision"],
            )
        self.assertEqual(path.read_bytes(), before)

    def test_hand_teleport_update_rejects_changed_owned_link(self) -> None:
        self.create_hand_map("changed_link")
        initial = self.editor.inspect("changed_link")
        added = self.editor.add_teleport(
            "changed_link", "gate-a",
            [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
            initial["revision"],
        )
        path = self.repo / "maps" / "changed_link.map"
        before = path.read_text("utf-8").replace(
            '"target" "lg_agent_teleport_target_gate-a"',
            '"target" "other-target"',
            1,
        )
        path.write_text(before, encoding="utf-8")
        revision = hashlib.sha256(path.read_bytes()).hexdigest()
        with self.assertRaisesRegex(MapEditError, "owned data is inconsistent"):
            self.editor.update_teleport(
                "changed_link", "gate-a", revision, exit_yaw=180
            )

    def test_hand_owned_teleport_drift_fails_closed(self) -> None:
        def remove_brush(source: str) -> str:
            raw = lg_map_edit._parse_raw_map(source.encode("utf-8"))
            trigger = next(
                entity for entity in raw["entities"]
                if entity["classname"] == "trigger_teleport"
            )
            start, end = trigger["brushes"][0]
            return source[:start] + source[end:]

        def add_brush(source: str) -> str:
            raw = lg_map_edit._parse_raw_map(source.encode("utf-8"))
            trigger = next(
                entity for entity in raw["entities"]
                if entity["classname"] == "trigger_teleport"
            )
            start, end = trigger["brushes"][0]
            brush = source[start:end]
            return source[:trigger["end"] - 1] + brush + source[trigger["end"] - 1:]

        mutations = [
            (
                "target_origin",
                lambda source: source.replace(
                    '"origin" "180 0 32"', '"origin" "181 0 32"', 1
                ),
                "target origin",
            ),
            (
                "target_angle",
                lambda source: source.replace(
                    '"angle" "90"', '"angle" "91"', 1
                ),
                "target angle",
            ),
            (
                "brush_bounds",
                lambda source: source.replace(
                    "( -32 -32 0 )", "( -40 -32 0 )", 1
                ),
                "brush bounds",
            ),
            ("extra_brush", add_brush, "exactly one"),
            ("missing_brush", remove_brush, "exactly one"),
            (
                "metadata",
                lambda source: source.replace(
                    '"lg_api_destination" "180 0 32"',
                    '"lg_api_destination" "182 0 32"',
                    1,
                ),
                "target origin",
            ),
            (
                "link",
                lambda source: source.replace(
                    '"target" "lg_agent_teleport_target_gate-a"',
                    '"target" "other-target"',
                    1,
                ),
                "trigger link",
            ),
            (
                "public_id",
                lambda source: source.replace(
                    '"lg_api_id" "gate-a"', '"lg_api_id" "other-gate"', 1
                ),
                "public ID",
            ),
            (
                "internal_target_id",
                lambda source: source.replace(
                    '"lg_agent_id" "lg-internal-teleport-target-gate-a"',
                    '"lg_agent_id" "lg-internal-teleport-target-other"',
                    1,
                ),
                "not owned",
            ),
            (
                "duplicate_target",
                lambda source: source
                + '{\n"classname" "target_position"\n'
                '"targetname" "lg_agent_teleport_target_gate-a"\n'
                '"origin" "180 0 32"\n"angle" "90"\n}\n',
                "expected one owned target",
            ),
        ]
        for name, mutate, detail in mutations:
            with self.subTest(name=name):
                map_name = f"drift_{name}"
                self.create_hand_map(map_name)
                initial = self.editor.inspect(map_name)
                self.editor.add_teleport(
                    map_name, "gate-a",
                    [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
                    initial["revision"],
                )
                path = self.repo / "maps" / f"{map_name}.map"
                changed_source = mutate(path.read_text("utf-8"))
                path.write_text(changed_source, encoding="utf-8")
                before = path.read_bytes()
                revision = hashlib.sha256(before).hexdigest()
                with self.assertRaisesRegex(MapEditError, detail):
                    self.editor.list_teleports(map_name)
                listed = next(
                    item for item in self.editor.list_maps()["maps"]
                    if item["map"] == map_name
                )
                self.assertFalse(listed["managed"])
                self.assertFalse(listed["editable"])
                self.assertIn(detail.replace("\\", ""), listed["edit_status"])
                with self.assertRaises(MapEditError):
                    self.editor.update_teleport(
                        map_name, "gate-a", revision, exit_yaw=180
                    )
                self.assertEqual(path.read_bytes(), before)

    def test_hand_owned_teleport_with_invalid_public_id_fails_closed(self) -> None:
        self.create_hand_map("invalid_public_id")
        initial = self.editor.inspect("invalid_public_id")
        self.editor.add_teleport(
            "invalid_public_id", "gate-a",
            [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
            initial["revision"],
        )
        path = self.repo / "maps" / "invalid_public_id.map"
        path.write_text(
            path.read_text("utf-8").replace("gate-a", "-bad"),
            encoding="utf-8",
        )
        before = path.read_bytes()
        revision = hashlib.sha256(before).hexdigest()
        expected = "teleport id must start with a lower-case letter"

        with self.assertRaisesRegex(MapEditError, expected):
            self.editor.inspect("invalid_public_id")
        with self.assertRaisesRegex(MapEditError, expected):
            self.editor.list_teleports("invalid_public_id")
        listed = next(
            item for item in self.editor.list_maps()["maps"]
            if item["map"] == "invalid_public_id"
        )
        self.assertFalse(listed["managed"])
        self.assertFalse(listed["editable"])
        self.assertIn(expected, listed["edit_status"])
        with self.assertRaisesRegex(MapEditError, expected):
            self.editor.set_world_lighting(
                "invalid_public_id", revision, ambient_intensity=0.5
            )
        with self.assertRaisesRegex(MapEditError, expected):
            self.editor.update_teleport(
                "invalid_public_id", "-bad", revision, exit_yaw=180
            )
        self.assertEqual(path.read_bytes(), before)

    def test_hand_teleport_cap_counts_each_top_level_trigger_brush(self) -> None:
        self.create_hand_map("sixteen_brushes")
        self.append_hand_teleport_brushes("sixteen_brushes", 16)
        self.assertIsNone(self.editor.validator_path())
        inspected = self.editor.inspect("sixteen_brushes")
        self.assertEqual(inspected["unowned"]["teleports"], 16)
        self.assertEqual(inspected["teleports"], [])
        before = (self.repo / "maps" / "sixteen_brushes.map").read_bytes()
        with self.assertRaisesRegex(MapEditError, "at most 16 teleports"):
            self.editor.add_teleport(
                "sixteen_brushes", "overflow",
                [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
                inspected["revision"],
            )
        self.assertEqual(
            (self.repo / "maps" / "sixteen_brushes.map").read_bytes(), before
        )

    def test_hand_teleport_cap_edge_mixes_owned_and_unowned_volumes(self) -> None:
        self.create_hand_map("mixed_teleports")
        self.append_hand_teleport_brushes("mixed_teleports", 15)
        initial = self.editor.inspect("mixed_teleports")
        added = self.editor.add_teleport(
            "mixed_teleports", "owned-gate",
            [-32, -32, 0], [32, 32, 48], [180, 0, 32], 90,
            initial["revision"],
        )
        inspected = self.editor.inspect("mixed_teleports")
        self.assertEqual(inspected["unowned"]["teleports"], 15)
        self.assertEqual(self.editor.list_teleports("mixed_teleports")["count"], 1)
        before = (self.repo / "maps" / "mixed_teleports.map").read_bytes()
        with self.assertRaisesRegex(MapEditError, "at most 16 teleports"):
            self.editor.add_teleport(
                "mixed_teleports", "second-owned",
                [-48, -48, 0], [-36, -36, 32], [200, 0, 32], 0,
                added["revision_after"],
            )
        self.assertEqual(
            (self.repo / "maps" / "mixed_teleports.map").read_bytes(), before
        )

    def test_hand_teleport_zero_brush_entity_is_rejected(self) -> None:
        self.create_hand_map("zero_brush_teleport")
        self.append_hand_teleport_brushes("zero_brush_teleport", 0)
        with self.assertRaisesRegex(
            MapEditError, "trigger_teleport requires at least one"
        ):
            self.editor.inspect("zero_brush_teleport")

    def test_hand_map_rejects_lossy_geometry_and_unowned_light_changes(self) -> None:
        self.create_hand_map()
        revision = self.editor.inspect("hand_map")["revision"]
        with self.assertRaisesRegex(MapEditError, "not owned"):
            self.editor.update_point_light(
                "hand_map", "authored-light", revision, intensity=2
            )
        with self.assertRaisesRegex(MapEditError, "do not support operation"):
            self.editor.apply_batch(
                "hand_map",
                [{
                    "op": "add_cuboid", "id": "box",
                    "min": [-1, -1, -1], "max": [1, 1, 1],
                    "material": "arena",
                }],
                revision,
            )


if __name__ == "__main__":
    unittest.main()
