import importlib.util
import json
import pathlib
import struct
import sys
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("import_q3_map.py")
SPEC = importlib.util.spec_from_file_location("import_q3_map", MODULE_PATH)
assert SPEC and SPEC.loader
q3 = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = q3
SPEC.loader.exec_module(q3)


def cube(material: str = "gothic_block/wall") -> str:
    return f"""{{
( 0 0 0 ) ( 0 0 8 ) ( 0 8 8 ) {material} 0 0 0 1 1
( 8 0 0 ) ( 8 8 0 ) ( 8 8 8 ) {material} 0 0 0 1 1
( 0 0 0 ) ( 8 0 0 ) ( 8 0 8 ) {material} 0 0 0 1 1
( 0 8 0 ) ( 0 8 8 ) ( 8 8 8 ) {material} 0 0 0 1 1
( 0 0 0 ) ( 0 8 0 ) ( 8 8 0 ) {material} 0 0 0 1 1
( 0 0 8 ) ( 8 0 8 ) ( 8 8 8 ) {material} 0 0 0 1 1
}}"""


def entity(classname: str, properties: str = "", brushes: str = "") -> str:
    return f"""{{
\"classname\" \"{classname}\"
{properties}
{brushes}
}}"""


def make_bsp(path: pathlib.Path, bounds=(-16.0, -8.0, -4.0, 32.0, 24.0, 20.0)) -> None:
    header_size = 8 + 17 * 8
    data = bytearray(header_size + 40)
    struct.pack_into("<4si", data, 0, b"IBSP", 46)
    for index in range(17):
        struct.pack_into("<ii", data, 8 + index * 8, header_size, 0)
    struct.pack_into("<ii", data, 8 + 7 * 8, header_size, 40)
    struct.pack_into("<6f4i", data, header_size, *bounds, 0, 0, 0, 0)
    path.write_bytes(data)


def make_aas(path: pathlib.Path, checksum=-1234567) -> None:
    path.write_bytes(struct.pack("<4sii", b"EAAS", 5, checksum) + b"route-data-is-not-read")


class ParserTests(unittest.TestCase):
    def test_parses_classic_brush_and_reports_both_patch_versions(self):
        patch2 = """{
patchDef2
{
curves/arch
( 3 3 0 0 0 )
(
( ( 0 0 0 0 0 ) ( 1 0 0 0 0 ) ( 2 0 0 0 0 ) )
( ( 0 1 0 0 0 ) ( 1 1 1 0 0 ) ( 2 1 0 0 0 ) )
( ( 0 2 0 0 0 ) ( 1 2 0 0 0 ) ( 2 2 0 0 0 ) )
)
}
}"""
        patch3 = patch2.replace("patchDef2", "patchDef3").replace("curves/arch", "curves/bevel")
        document = q3.parse_map(entity("worldspawn", brushes=cube() + patch2 + patch3))
        self.assertEqual(1, len(document))
        self.assertEqual(1, len(document[0].brushes))
        self.assertEqual(["patchDef2", "patchDef3"], [patch.kind for patch in document[0].patches])
        self.assertEqual(["curves/arch", "curves/bevel"], [patch.material for patch in document[0].patches])

    def test_brush_primitives_are_explicitly_unsupported(self):
        raw = entity("worldspawn", brushes="{ brushDef3 { ( 1 0 0 -8 ) ( ( 1 0 0 ) ( 0 1 0 ) ) stone } }")
        document = q3.parse_map(raw)
        self.assertEqual("brushDef3", document[0].unsupported[0].kind)


class ConversionTests(unittest.TestCase):
    def setUp(self):
        self.bsp = {
            "name": "fixture.bsp",
            "size": 1,
            "sha256": "0" * 64,
            "magic": "IBSP",
            "version": 46,
            "world_model_bounds": {"min": [-16.0, -8.0, -4.0], "max": [32.0, 24.0, 20.0]},
            "lumps": [],
        }

    def adaptation_v2(self, raw: str, **extra):
        result = {
            "schema_version": 2,
            "source_bsp_sha256": self.bsp["sha256"],
            "raw_decompile_sha256": q3._sha256(raw.encode("utf-8")),
            "materials": {"wall": "Overkill/Wall", "accent": "Overkill/Accent"},
            "material_roles": {"wall": ["*"]},
        }
        result.update(extra)
        return result

    def adaptation_v3(self, raw: str, **extra):
        result = self.adaptation_v2(raw, **extra)
        result["schema_version"] = 3
        return result

    @staticmethod
    def cube_faces():
        return [
            [[0, 0, 0], [0, 0, 8], [0, 8, 8]],
            [[8, 0, 0], [8, 8, 0], [8, 8, 8]],
            [[0, 0, 0], [8, 0, 0], [8, 0, 8]],
            [[0, 8, 0], [0, 8, 8], [8, 8, 8]],
            [[0, 0, 0], [0, 8, 0], [8, 8, 0]],
            [[0, 0, 8], [8, 0, 8], [8, 8, 8]],
        ]

    def test_maps_entities_materials_and_clean_jump_pad(self):
        raw = "\n".join(
            [
                entity("worldspawn", brushes=cube("gothic_block/gkc15_big")),
                entity("func_static", brushes=cube("metal/rust")),
                entity("info_player_deathmatch", '"origin" "1 2 3"\n"angle" "90"'),
                entity("info_player_start", '"origin" "4 5 6"'),
                entity("light", '"origin" "1 2 8"\n"_color" "1 0.5 0"\n"light" "300"'),
                entity("item_health", '"origin" "2 2 2"'),
                entity("item_health_small", '"origin" "3 3 3"'),
                entity("item_health_large", '"origin" "4 4 4"'),
                entity("target_position", '"targetname" "jump_target"\n"origin" "8 8 32"'),
                entity("trigger_push", '"target" "jump_target"', cube("common/trigger")),
                entity("trigger_hurt", '"dmg" "9999"\n"spawnflags" "12"', cube("common/trigger")),
                entity("trigger_teleport", '"target" "tele_dest"', cube("common/trigger")),
                entity("misc_teleporter_dest", '"targetname" "tele_dest"\n"origin" "9 9 9"'),
            ]
        )
        output, report = q3.convert(raw, "fixture.map", self.bsp)

        self.assertIn('"classname" "func_group"', output)
        self.assertEqual(2, output.count('"classname" "lg_spawn"'))
        self.assertIn('"classname" "trigger_jumppad"', output)
        self.assertIn('"classname" "trigger_kill"', output)
        self.assertIn('"lg_source_classname" "trigger_hurt"', output)
        self.assertIn('"dmg" "9999"', output)
        self.assertIn('"spawnflags" "12"', output)
        self.assertIn('"classname" "trigger_teleport"', output)
        self.assertIn('"classname" "target_position"', output)
        self.assertIn('"classname" "item_health_small"', output)
        self.assertIn('"classname" "item_health_large"', output)
        self.assertNotIn("gothic_block/gkc15_big", output)
        self.assertNotIn("metal/rust", output)
        self.assertIn("Tiny3/Stone/Stone_14-128x128", output)
        self.assertIn("Tiny3/Metal/Metal_04-128x128", output)
        self.assertEqual(1, report["gameplay"]["converted"]["jump_pads"])
        self.assertEqual(1, report["gameplay"]["source_trigger_hurt"])
        self.assertEqual(1, report["gameplay"]["converted"]["kill_volumes"])
        self.assertEqual(1, report["gameplay"]["source_teleports"])
        self.assertEqual(1, report["gameplay"]["converted"]["teleports"])
        self.assertEqual(
            q3._sha256(output.encode("utf-8")),
            report["outputs"]["candidate_map"]["sha256"],
        )

    def test_nonlethal_or_stateful_hurt_triggers_stay_unsupported(self):
        sloped_brush = """{
( -1 -1 0 ) ( -1 1 0 ) ( -1 1 1.5 ) common/trigger 0 0 0 1 1
( 1 -1 0 ) ( 1 -1 0.5 ) ( 1 1 0.5 ) common/trigger 0 0 0 1 1
( -1 -1 0 ) ( 1 -1 0 ) ( 1 -1 0.5 ) common/trigger 0 0 0 1 1
( -1 1 0 ) ( -1 1 1.5 ) ( 1 1 0.5 ) common/trigger 0 0 0 1 1
( -1 -1 0 ) ( -1 1 0 ) ( 1 1 0 ) common/trigger 0 0 0 1 1
( -1 -1 1.5 ) ( 1 -1 0.5 ) ( 1 1 0.5 ) common/trigger 0 0 0 1 1
}"""
        raw = "\n".join(
            [
                entity("worldspawn", brushes=cube()),
                entity("trigger_hurt", '"dmg" "5"', cube("common/trigger")),
                entity("trigger_hurt", '"dmg" "9999"\n"spawnflags" "1"', cube("common/trigger")),
                entity("trigger_hurt", '"dmg" "9999"\n"spawnflags" "12"'),
                entity(
                    "trigger_hurt",
                    '"dmg" "9999"\n"spawnflags" "12"',
                    sloped_brush,
                ),
            ]
        )
        output, report = q3.convert(raw, "hurt-semantics.map", self.bsp)
        self.assertNotIn('"classname" "trigger_kill"', output)
        self.assertEqual(
            4,
            report["conversion"]["omitted_entities"][
                "trigger_hurt:nonlethal_or_unsupported"
            ],
        )

    def test_ambiguous_jump_target_is_reported_and_not_mapped(self):
        raw = "\n".join(
            [
                entity("worldspawn", brushes=cube()),
                entity("target_position", '"targetname" "duplicate"\n"origin" "8 8 32"'),
                entity("target_position", '"targetname" "duplicate"\n"origin" "16 8 32"'),
                entity("trigger_push", '"target" "duplicate"', cube("common/trigger")),
            ]
        )
        output, report = q3.convert(raw, "ambiguous.map", self.bsp)
        self.assertNotIn('"classname" "trigger_jumppad"', output)
        self.assertEqual(1, report["conversion"]["omitted_entities"]["trigger_push:ambiguous_target"])
        self.assertEqual(2, report["conversion"]["ambiguous_targetnames"]["duplicate"])

    def test_output_and_reports_are_deterministic(self):
        raw = entity("worldspawn", brushes=cube("base/wall"))
        first_output, first_report = q3.convert(raw, "same.map", self.bsp)
        second_output, second_report = q3.convert(raw, "same.map", self.bsp)
        self.assertEqual(first_output, second_output)
        self.assertEqual(
            json.dumps(first_report, indent=2, sort_keys=True),
            json.dumps(second_report, indent=2, sort_keys=True),
        )
        self.assertEqual(q3.markdown_report(first_report), q3.markdown_report(second_report))

    def test_utility_and_collision_materials_are_handled_explicitly(self):
        raw = entity(
            "worldspawn",
            brushes=cube("common/nodrop") + cube("common/clip") + cube("common/weapclip"),
        )
        output, report = q3.convert(raw, "materials.map", self.bsp)
        self.assertEqual(1, report["conversion"]["omitted_brushes"]["confident_non_solid_utility"])
        self.assertEqual(2, report["conversion"]["projected_counts"]["walls"])
        self.assertEqual(6, output.count("common/playerclip"))
        self.assertEqual(6, output.count("common/weapclip"))
        self.assertNotIn("common/nodrop", output)
        self.assertIn("remain common/weapclip", report["conversion"]["collision_material_policy"])

    def test_only_wholly_sky_brushes_keep_common_sky(self):
        adaptation = {
            "materials": {"wall": "Fixture/Wall"},
            "material_roles": {"wall": ["*"]},
        }
        all_sky = q3.parse_map(
            entity("worldspawn", brushes=cube("textures/skies/blue"))
        )[0].brushes[0]
        mixed_text = cube("skies/blue").replace(
            "skies/blue", "common/caulk", 1
        )
        mixed = q3.parse_map(
            entity("worldspawn", brushes=mixed_text)
        )[0].brushes[0]

        sky_lines = q3._emit_brush(all_sky, adaptation=adaptation)
        mixed_lines = q3._emit_brush(mixed, adaptation=adaptation)

        self.assertEqual(sum(" common/sky " in line for line in sky_lines), 6)
        self.assertFalse(any(" common/sky " in line for line in mixed_lines))
        self.assertEqual(
            sum(" Fixture/Wall " in line for line in mixed_lines),
            6,
        )

    def test_adaptation_can_set_one_allow_listed_sky(self):
        raw = entity("worldspawn", brushes=cube())
        adaptation = self.adaptation_v2(raw)
        adaptation["sky"] = "crimson-sunset"
        output, report = q3.convert(
            raw,
            "sky.map",
            self.bsp,
            adaptation=adaptation,
        )
        self.assertEqual(1, output.count('"lg_sky" "crimson-sunset"'))
        self.assertEqual(
            "crimson-sunset",
            report["conversion"]["adaptation"]["sky"],
        )

        adaptation["sky"] = "../outside"
        with self.assertRaisesRegex(
            q3.ConversionError,
            "adaptation sky must be",
        ):
            q3.convert(raw, "sky.map", self.bsp, adaptation=adaptation)

    def test_v2_adaptation_rejects_source_hash_mismatches(self):
        raw = entity("worldspawn", brushes=cube())
        for field in ("source_bsp_sha256", "raw_decompile_sha256"):
            adaptation = self.adaptation_v2(raw)
            adaptation[field] = "f" * 64
            with self.subTest(field=field), self.assertRaisesRegex(q3.ConversionError, "SHA-256 mismatch"):
                q3.convert(raw, "bound.map", self.bsp, adaptation=adaptation)

    def test_brush_policy_rejects_duplicate_and_out_of_range_locators(self):
        raw = entity("worldspawn", brushes=cube())
        base = {"source_entity_index": 0, "source_brush_index": 0, "action": "drop", "reason": "reviewed"}
        cases = [
            ([base, dict(base)], "duplicate locator"),
            ([{**base, "source_brush_index": 1}], "outside the source inventory"),
        ]
        for rules, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(q3.ConversionError, message):
                q3.convert(raw, "policy.map", self.bsp, adaptation=self.adaptation_v2(
                    raw, brush_policy={"default_action": "allow", "rules": rules}
                ))

    def test_brush_policy_rejects_invalid_fields_actions_classifications_and_roles(self):
        raw = entity("worldspawn", brushes=cube())
        locator = {"source_entity_index": 0, "source_brush_index": 0, "reason": "reviewed"}
        cases = [
            ({**locator, "action": "paint"}, "invalid action"),
            ({**locator, "action": "override", "classification": "ghost"}, "invalid classification"),
            ({**locator, "action": "override", "classification": "visible_solid", "material_role": "missing"}, "invalid material_role"),
            ({**locator, "action": "allow", "classification": "visible_solid"}, "invalid fields"),
            ({**locator, "action": "drop", "reason": ""}, "nonempty reason"),
        ]
        for rule, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(q3.ConversionError, message):
                q3.convert(raw, "policy.map", self.bsp, adaptation=self.adaptation_v2(
                    raw, brush_policy={"default_action": "allow", "rules": [rule]}
                ))

    def test_brush_policy_allows_drops_and_overrides_with_stable_provenance(self):
        raw = entity("worldspawn", brushes=cube("base/first") + cube("base/second") + cube("base/third"))
        adaptation = self.adaptation_v2(raw, brush_policy={
            "default_action": "allow",
            "rules": [
                {"source_entity_index": 0, "source_brush_index": 0, "action": "drop", "reason": "remove first"},
                {"source_entity_index": 0, "source_brush_index": 1, "action": "override", "classification": "weapclip", "reason": "weapon blocker"},
                {"source_entity_index": 0, "source_brush_index": 2, "action": "override", "classification": "visible_solid", "material_role": "accent", "reason": "route accent"},
            ],
        })
        output, report = q3.convert(raw, "policy.map", self.bsp, adaptation=adaptation)
        self.assertNotIn('"lg_source_brush_index" "0"', output)
        self.assertIn('"lg_source_brush_index" "1"', output)
        self.assertIn('"lg_source_brush_index" "2"', output)
        self.assertEqual(6, output.count("common/weapclip"))
        self.assertEqual(6, output.count("Overkill/Accent"))
        self.assertEqual([False, True, True], [row["emitted"] for row in report["conversion"]["brushes"]])
        self.assertEqual("weapclip", report["conversion"]["brushes"][1]["effective_classification"])

    def test_policy_cannot_resurrect_fog_volume(self):
        raw = entity("worldspawn", brushes=cube("sfx/hellfog"))
        adaptation = self.adaptation_v2(raw, brush_policy={
            "default_action": "allow",
            "rules": [{
                "source_entity_index": 0, "source_brush_index": 0, "action": "override",
                "classification": "visible_solid", "material_role": "accent", "reason": "attempted recovery",
            }],
        })
        output, report = q3.convert(raw, "fog.map", self.bsp, adaptation=adaptation)
        self.assertNotIn('"classname" "func_group"', output)
        self.assertFalse(report["conversion"]["brushes"][0]["emitted"])
        self.assertEqual("fog_volume", report["conversion"]["brushes"][0]["omission_reason"])

    def test_source_brushes_are_one_brush_groups_with_root_hashes(self):
        raw = entity("worldspawn", brushes=cube()) + "\n" + entity("func_static", brushes=cube())
        output, report = q3.convert(raw, "provenance.map", self.bsp)
        self.assertEqual(2, output.count('"classname" "func_group"'))
        self.assertIn('"lg_source_entity_index" "0"', output)
        self.assertIn('"lg_source_entity_index" "1"', output)
        self.assertEqual(2, output.count('"lg_source_brush_index" "0"'))
        self.assertIn(f'"lg_source_bsp_sha256" "{self.bsp["sha256"]}"', output)
        self.assertIn(f'"lg_raw_decompile_sha256" "{q3._sha256(raw.encode())}"', output)
        self.assertEqual(2, len(report["conversion"]["brushes"]))

    def test_hellfog_volume_emits_neither_render_nor_collision_geometry(self):
        raw = entity("worldspawn", brushes=cube("textures/sfx/hellfog"))
        adaptation = {
            "materials": {
                "wall": "Overkill/Wall",
                "accent": "Overkill/Accent",
            },
            # Omission must win even if a future adaptation accidentally maps
            # the fog shader back onto a visible material role.
            "material_roles": {
                "accent": ["sfx/hellfog"],
                "wall": ["*"],
            },
        }

        output, report = q3.convert(
            raw,
            "hellfog.map",
            self.bsp,
            adaptation=adaptation,
        )

        self.assertEqual(1, report["conversion"]["omitted_brushes"]["fog_volume"])
        self.assertEqual(0, report["conversion"]["projected_counts"]["walls"])
        self.assertEqual(0, report["conversion"]["projected_counts"]["convex_brushes"])
        self.assertNotIn("sfx/hellfog", output)
        self.assertNotIn("Overkill/Accent", output)
        self.assertNotIn("common/playerclip", output)
        self.assertIn(
            "neither render nor collision geometry",
            report["conversion"]["volume_material_policy"],
        )

    def test_invalid_and_degenerate_geometry_is_reported_not_emitted(self):
        invalid = cube().replace(
            "( 0 0 0 ) ( 0 0 8 ) ( 0 8 8 )",
            "( 0 0 0 ) ( 0 0 1 ) ( 0 0 2 )",
            1,
        )
        raw = entity("worldspawn", brushes=invalid)
        output, report = q3.convert(raw, "invalid.map", self.bsp)
        self.assertEqual(1, report["geometry"]["invalid_brush_count"])
        self.assertEqual(1, report["geometry"]["degenerate_face_count"])
        self.assertEqual(0, report["conversion"]["projected_counts"]["walls"])
        self.assertNotIn("Tiny3/", output)

    def test_patch_and_shader_inventory_are_complete(self):
        patch = """{ patchDef2 { curves/arch ( 3 3 0 0 0 ) ( ( ( 0 0 0 0 0 ) ) ) } }"""
        raw = entity("worldspawn", brushes=cube("textures/custom/shader") + patch)
        _, report = q3.convert(raw, "patch.map", self.bsp)
        self.assertEqual(1, report["inventory"]["patch_count"])
        self.assertEqual(1, report["inventory"]["patch_materials"]["curves/arch"])
        self.assertEqual(6, report["inventory"]["face_materials"]["textures/custom/shader"])
        self.assertEqual(1, report["conversion"]["omitted_patches"])

    def test_checked_in_adaptation_selects_spawns_materials_and_patches(self):
        patch = """{ patchDef2 { curves/arch ( 3 3 0 0 0 ) (
        ( ( 0 0 8 0 0 ) ( 0 4 10 0 0 ) ( 0 8 8 0 0 ) )
        ( ( 4 0 10 0 0 ) ( 4 4 12 0 0 ) ( 4 8 10 0 0 ) )
        ( ( 8 0 8 0 0 ) ( 8 4 10 0 0 ) ( 8 8 8 0 0 ) )
        ) } }"""
        spawns = [entity("info_player_deathmatch", f'"origin" "{index} 0 8"') for index in range(7)]
        raw = entity("worldspawn", brushes=cube("gothic_block/wall") + patch) + "\n" + "\n".join(spawns)
        adaptation = {
            "schema_version": 1,
            "materials": {"wall": "Overkill/Wall", "accent": "Overkill/Accent"},
            "material_roles": {"accent": ["curves/*"], "wall": ["*"]},
            "selected_spawn_origins": [[index, 0, 8] for index in range(6)],
            "reconstruct_patch_indices": [0],
            "patch_subdivisions": 2,
            "patch_thickness": 1.0,
            "marker_boxes": [{
                "min": [16, 0, 0],
                "max": [24, 8, 1],
                "material": "accent",
            }],
            "sun": {
                "direction": [0.35, -0.5, -1.0],
                "color": [255, 226, 184],
                "intensity": 0.85,
            },
            "lights": [{
                "origin": [4, 4, 16],
                "color": [255, 226, 184],
                "intensity": 1.0,
                "radius": 256,
            }],
        }
        output, report = q3.convert(raw, "adapted.map", self.bsp, adaptation=adaptation)
        self.assertEqual(6, output.count('"classname" "lg_spawn"'))
        self.assertIn("Overkill/Wall", output)
        self.assertIn("Overkill/Accent", output)
        self.assertIn('"classname" "light_sun"', output)
        self.assertIn('"intensity" "1.0"', output)
        self.assertEqual(1, report["gameplay"]["converted"]["sun_lights"])
        self.assertEqual(1, report["gameplay"]["converted"]["lights"])
        self.assertEqual(1, report["conversion"]["reconstructed_patches"])
        self.assertEqual(1, report["conversion"]["adaptation_marker_boxes"])
        self.assertEqual(0, report["conversion"]["omitted_patches"])
        self.assertEqual("convertible", report["status"])

    def test_v3_patch_rules_emit_render_only_pieces_with_stable_provenance(self):
        patch = """{ patchDef2 { curves/arch ( 3 3 0 0 0 ) (
        ( ( 0 0 8 0 0 ) ( 0 4 10 0 0 ) ( 0 8 8 0 0 ) )
        ( ( 4 0 10 0 0 ) ( 4 4 12 0 0 ) ( 4 8 10 0 0 ) )
        ( ( 8 0 8 0 0 ) ( 8 4 10 0 0 ) ( 8 8 8 0 0 ) )
        ) } }"""
        raw = entity("worldspawn", brushes=cube() + patch)
        adaptation = self.adaptation_v3(raw, patch_rules=[{
            "source_patch_index": 0,
            "action": "reconstruct",
            "role": "render_only",
            "reason": "restore reviewed doorway arch without collision",
        }], patch_subdivisions=1, patch_thickness=1.0)

        first_output, first_report = q3.convert(raw, "patch-v3.map", self.bsp, adaptation=adaptation)
        second_output, second_report = q3.convert(raw, "patch-v3.map", self.bsp, adaptation=adaptation)

        self.assertEqual(first_output, second_output)
        self.assertEqual(first_report, second_report)
        self.assertEqual(2, first_output.count('"lg_geometry_role" "render_only"'))
        self.assertEqual(2, first_output.count('"lg_source_patch_index" "0"'))
        self.assertIn('"lg_source_patch_piece_index" "0"', first_output)
        self.assertIn('"lg_source_patch_piece_index" "1"', first_output)
        roles = first_report["conversion"]["reconstructed_patches_by_role"]
        self.assertEqual({"patch_count": 1, "brush_count": 2, "indices": [0]}, roles["render_only"])
        self.assertEqual({"patch_count": 0, "brush_count": 0, "indices": []}, roles["solid"])

    def test_v3_patch_rules_are_strict_and_replace_flat_indices(self):
        patch = """{ patchDef2 { curves/arch ( 3 3 0 0 0 ) (
        ( ( 0 0 8 0 0 ) ( 0 4 8 0 0 ) ( 0 8 8 0 0 ) )
        ( ( 4 0 8 0 0 ) ( 4 4 9 0 0 ) ( 4 8 8 0 0 ) )
        ( ( 8 0 8 0 0 ) ( 8 4 8 0 0 ) ( 8 8 8 0 0 ) )
        ) } }"""
        raw = entity("worldspawn", brushes=cube() + patch)
        base = {
            "source_patch_index": 0, "action": "reconstruct", "role": "solid", "reason": "reviewed",
        }
        cases = [
            ({"reconstruct_patch_indices": [0]}, "must use patch_rules"),
            ({"patch_rules": [{**base, "extra": True}]}, "invalid fields"),
            ({"patch_rules": [{**base, "role": "collision"}]}, "invalid role"),
            ({"patch_rules": [{**base, "reason": ""}]}, "nonempty reason"),
            ({"patch_rules": [base, dict(base)]}, "duplicate source_patch_index"),
            ({"patch_rules": [{**base, "source_patch_index": 1}]}, "outside the source inventory"),
        ]
        for extra, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(q3.ConversionError, message):
                q3.convert(raw, "patch-rules.map", self.bsp, adaptation=self.adaptation_v3(raw, **extra))

    def test_v3_derived_collision_hull_replaces_dropped_clip(self):
        raw = entity("worldspawn", brushes=cube("common/playerclip"))
        adaptation = self.adaptation_v3(raw,
            brush_policy={"default_action": "allow", "rules": [{
                "source_entity_index": 0, "source_brush_index": 0,
                "action": "drop", "reason": "replace snagging doorway clip",
            }]},
            derived_collision_hulls=[{
                "id": "doorway-0-0-smooth", "reason": "flush reviewed replacement",
                "classification": "playerclip",
                "replaces": {"source_entity_index": 0, "source_brush_index": 0},
                "faces": self.cube_faces(),
            }])

        output, report = q3.convert(raw, "derived.map", self.bsp, adaptation=adaptation)

        self.assertEqual(1, output.count('"lg_adaptation_derived_id" "doorway-0-0-smooth"'))
        self.assertEqual(6, output.count("common/playerclip"))
        self.assertEqual(1, report["conversion"]["derived_collision_hulls"]["count"])
        item = report["conversion"]["derived_collision_hulls"]["items"][0]
        self.assertEqual({"source_entity_index": 0, "source_brush_index": 0}, item["replaces"])
        self.assertEqual({"min": [0.0, 0.0, 0.0], "max": [8.0, 8.0, 8.0]}, item["bounds"])

    def test_v3_derived_collision_hulls_reject_invalid_relationships_and_geometry(self):
        raw = entity("worldspawn", brushes=cube("common/playerclip") + cube("common/playerclip"))
        drop_rules = [{
            "source_entity_index": 0, "source_brush_index": index,
            "action": "drop", "reason": "reviewed replacement",
        } for index in range(2)]
        base = {
            "id": "smooth-a", "reason": "reviewed smooth hull", "classification": "playerclip",
            "replaces": {"source_entity_index": 0, "source_brush_index": 0},
            "faces": self.cube_faces(),
        }
        open_faces = self.cube_faces()[:-1]
        malformed_faces = self.cube_faces()
        malformed_faces[0] = malformed_faces[0][:-1]
        nonfinite_faces = self.cube_faces()
        nonfinite_faces[0][0][0] = float("inf")
        cases = [
            ([{**base, "faces": open_faces}], drop_rules, "closed convex brush"),
            ([{**base, "faces": malformed_faces}], drop_rules, "exactly three points"),
            ([{**base, "faces": nonfinite_faces}], drop_rules, "finite three-number array"),
            ([base, {**base, "replaces": {"source_entity_index": 0, "source_brush_index": 1}}], drop_rules, "duplicate id"),
            ([base, {**base, "id": "smooth-b"}], drop_rules, "duplicate replacement locator"),
            ([{**base, "classification": "weapclip"}], drop_rules, "class mismatch"),
            ([base], drop_rules[1:], "explicitly dropped"),
        ]
        for hulls, rules, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(q3.ConversionError, message):
                q3.convert(raw, "invalid-derived.map", self.bsp, adaptation=self.adaptation_v3(
                    raw,
                    brush_policy={"default_action": "allow", "rules": rules},
                    derived_collision_hulls=hulls,
                ))

    def test_v3_collision_visual_clones_clip_as_render_only_visible_geometry(self):
        raw = entity("worldspawn", brushes=cube("common/playerclip"))
        visual = {
            "id": "teleporter-frame-left",
            "reason": "explain the retained teleporter clip",
            "source_entity_index": 0,
            "source_brush_index": 0,
            "material_role": "accent",
        }
        output, report = q3.convert(
            raw,
            "collision-visual.map",
            self.bsp,
            adaptation=self.adaptation_v3(raw, collision_visuals=[visual]),
        )

        self.assertEqual(1, output.count('"lg_adaptation_visual_id" "teleporter-frame-left"'))
        self.assertEqual(1, output.count('"lg_geometry_role" "render_only"'))
        self.assertEqual(6, output.count("Overkill/Accent"))
        self.assertEqual(1, report["conversion"]["collision_visuals"]["count"])
        self.assertEqual("playerclip", report["conversion"]["collision_visuals"]["items"][0]["source_classification"])

        invalid_cases = [
            ({**visual, "source_brush_index": 1}, "outside the source inventory"),
            ({**visual, "material_role": "missing"}, "invalid material_role"),
            ({**visual, "id": "bad id"}, "invalid stable id"),
        ]
        for invalid, message in invalid_cases:
            with self.subTest(message=message), self.assertRaisesRegex(q3.ConversionError, message):
                q3.convert(
                    raw,
                    "invalid-collision-visual.map",
                    self.bsp,
                    adaptation=self.adaptation_v3(raw, collision_visuals=[invalid]),
                )

    def test_adaptation_accepts_up_to_thirty_two_selected_spawns(self):
        spawns = [entity("info_player_deathmatch", f'"origin" "{index} 0 8"') for index in range(32)]
        raw = entity("worldspawn", brushes=cube()) + "\n" + "\n".join(spawns)
        adaptation = {
            "materials": {"wall": "Overkill/Wall"},
            "selected_spawn_origins": [[index, 0, 8] for index in range(32)],
        }

        output, report = q3.convert(raw, "thirty-two-spawns.map", self.bsp, adaptation=adaptation)

        self.assertEqual(32, output.count('"classname" "lg_spawn"'))
        self.assertEqual(32, report["conversion"]["runtime_effective_spawns"])
        self.assertEqual(0, report["conversion"]["runtime_inactive_spawns"])

    def test_adaptation_rejects_more_than_thirty_two_selected_spawns(self):
        spawns = [entity("info_player_deathmatch", f'"origin" "{index} 0 8"') for index in range(33)]
        raw = entity("worldspawn", brushes=cube()) + "\n" + "\n".join(spawns)
        adaptation = {
            "materials": {"wall": "Overkill/Wall"},
            "selected_spawn_origins": [[index, 0, 8] for index in range(33)],
        }

        with self.assertRaisesRegex(q3.ConversionError, "between 2 and 32 origins"):
            q3.convert(raw, "thirty-three-spawns.map", self.bsp, adaptation=adaptation)

    def test_adaptation_rejects_duplicate_selected_spawns(self):
        spawns = [
            entity("info_player_deathmatch", '"origin" "0 0 8"'),
            entity("info_player_deathmatch", '"origin" "16 0 8"'),
        ]
        raw = entity("worldspawn", brushes=cube()) + "\n" + "\n".join(spawns)
        adaptation = {
            "materials": {"wall": "Overkill/Wall"},
            "selected_spawn_origins": [[0, 0, 8], [0, 0, 8]],
        }
        with self.assertRaisesRegex(q3.ConversionError, "must not contain duplicates"):
            q3.convert(raw, "duplicate-spawns.map", self.bsp, adaptation=adaptation)


class MetadataAndCliTests(unittest.TestCase):
    def test_bsp_and_aas_headers_are_inventory_only(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            bsp_path = root / "fixture.bsp"
            aas_path = root / "fixture.aas"
            make_bsp(bsp_path)
            make_aas(aas_path)
            bsp = q3.read_bsp_metadata(bsp_path)
            aas = q3.read_aas_metadata(aas_path)
            self.assertEqual("IBSP", bsp["magic"])
            self.assertEqual(46, bsp["version"])
            self.assertEqual([-16.0, -8.0, -4.0], bsp["world_model_bounds"]["min"])
            self.assertEqual(1, bsp["lumps"][7]["count"])
            self.assertEqual("EAAS", aas["magic"])
            self.assertEqual(5, aas["version"])
            self.assertEqual(-1234567, aas["bsp_checksum"])
            self.assertFalse(aas["route_import_attempted"])

    def test_cli_writes_artifacts_but_fails_over_limit_without_truncation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            bsp_path = root / "fixture.bsp"
            raw_path = root / "fixture.raw.map"
            output_path = root / "output.map"
            json_path = root / "report.json"
            markdown_path = root / "report.md"
            make_bsp(bsp_path)
            spawns = [entity("info_player_deathmatch", f'"origin" "{index} 0 0"') for index in range(33)]
            raw_bytes = (entity("worldspawn", brushes=cube()) + "\n" + "\n".join(spawns)).encode()
            raw_path.write_bytes(raw_bytes)
            result = q3.main(
                [
                    "--source-bsp", str(bsp_path),
                    "--raw-map", str(raw_path),
                    "--output-map", str(output_path),
                    "--json-report", str(json_path),
                    "--markdown-report", str(markdown_path),
                ]
            )
            self.assertEqual(2, result)
            self.assertEqual(raw_bytes, raw_path.read_bytes())
            self.assertEqual(33, output_path.read_text().count('"classname" "lg_spawn"'))
            report = json.loads(json_path.read_text())
            self.assertEqual(q3._sha256(output_path.read_bytes()), report["outputs"]["candidate_map"]["sha256"])
            self.assertEqual("output.map", report["outputs"]["candidate_map"]["name"])
            self.assertEqual("over_limit", report["status"])
            self.assertEqual(33, report["conversion"]["projected_counts"]["spawns"])
            self.assertEqual(32, report["conversion"]["runtime_effective_spawns"])
            self.assertEqual(1, report["conversion"]["runtime_inactive_spawns"])
            self.assertEqual(1, report["conversion"]["over_limits"]["spawns"]["excess"])
            self.assertIn("converter did not truncate", markdown_path.read_text())

    def test_checked_in_overkill_adaptation_repairs_the_reviewed_doorway_family(self):
        repository = MODULE_PATH.parent.parent
        adaptation = json.loads(
            (repository / "config" / "q3-import" / "overkill.json").read_text(encoding="utf-8")
        )
        self.assertEqual(3, adaptation["schema_version"])
        self.assertEqual("crimson-sunset", adaptation["sky"])
        render_only = {
            rule["source_patch_index"]
            for rule in adaptation["patch_rules"]
            if rule["role"] == "render_only"
        }
        self.assertEqual({0, 1, 2, 33, 34, 35, 46, 47, 48, 49}, render_only)

        dropped = {
            (rule["source_entity_index"], rule["source_brush_index"])
            for rule in adaptation["brush_policy"]["rules"]
            if rule["action"] == "drop"
        }
        self.assertEqual(
            {
                (0, 1884), (0, 1886), (0, 1908), (0, 1909),
                (0, 1914), (0, 1915), (0, 1918), (0, 1919),
                (0, 1920), (0, 1921),
            },
            dropped,
        )
        replacements = {
            (hull["replaces"]["source_entity_index"], hull["replaces"]["source_brush_index"])
            for hull in adaptation["derived_collision_hulls"]
        }
        self.assertEqual({(0, 1914), (0, 1919), (0, 1920)}, replacements)
        self.assertTrue(replacements.issubset(dropped))
        self.assertTrue(all(len(hull["faces"]) == 6 for hull in adaptation["derived_collision_hulls"]))
        collision_visuals = {
            (visual["source_entity_index"], visual["source_brush_index"])
            for visual in adaptation["collision_visuals"]
        }
        self.assertEqual({(0, 1926), (0, 1927), (0, 1928)}, collision_visuals)

        generated_path = repository / "maps" / "overkill_import.map"
        generated_bytes = generated_path.read_bytes()
        generated = generated_bytes.decode("utf-8")
        import_report = json.loads(
            (repository / "reports" / "q3" / "overkill-import.json").read_text(
                encoding="utf-8"
            )
        )
        candidate = import_report["outputs"]["candidate_map"]
        self.assertEqual(len(generated_bytes), candidate["size"])
        self.assertEqual(q3._sha256(generated_bytes), candidate["sha256"])
        self.assertEqual(105, generated.count('"lg_geometry_role" "render_only"'))
        self.assertEqual(25, generated.count('"lg_adaptation_visual_id"'))
        self.assertEqual(3, generated.count('"lg_adaptation_derived_id"'))


if __name__ == "__main__":
    unittest.main()
