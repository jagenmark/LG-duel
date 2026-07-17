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
                entity("trigger_teleport", '"target" "tele_dest"', cube("common/trigger")),
                entity("misc_teleporter_dest", '"targetname" "tele_dest"\n"origin" "9 9 9"'),
            ]
        )
        output, report = q3.convert(raw, "fixture.map", self.bsp)

        self.assertIn('"classname" "func_group"', output)
        self.assertEqual(2, output.count('"classname" "lg_spawn"'))
        self.assertIn('"classname" "trigger_jumppad"', output)
        self.assertIn('"classname" "target_position"', output)
        self.assertIn('"classname" "item_health_small"', output)
        self.assertIn('"classname" "item_health_large"', output)
        self.assertNotIn("gothic_block/gkc15_big", output)
        self.assertNotIn("metal/rust", output)
        self.assertIn("Tiny3/Stone/Stone_14-128x128", output)
        self.assertIn("Tiny3/Metal/Metal_04-128x128", output)
        self.assertEqual(1, report["gameplay"]["converted"]["jump_pads"])
        self.assertEqual(1, report["gameplay"]["source_teleports"])
        self.assertEqual(1, report["conversion"]["omitted_entities"]["trigger_teleport:teleport_unsupported"])
        self.assertEqual(
            q3._sha256(output.encode("utf-8")),
            report["outputs"]["candidate_map"]["sha256"],
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
        self.assertEqual(12, output.count("common/playerclip"))
        self.assertNotIn("common/nodrop", output)
        self.assertIn("weapon-only collision mask", report["conversion"]["collision_material_policy"])

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
            spawns = [entity("info_player_deathmatch", f'"origin" "{index} 0 0"') for index in range(7)]
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
            self.assertEqual(7, output_path.read_text().count('"classname" "lg_spawn"'))
            report = json.loads(json_path.read_text())
            self.assertEqual(q3._sha256(output_path.read_bytes()), report["outputs"]["candidate_map"]["sha256"])
            self.assertEqual("output.map", report["outputs"]["candidate_map"]["name"])
            self.assertEqual("over_limit", report["status"])
            self.assertEqual(7, report["conversion"]["projected_counts"]["spawns"])
            self.assertEqual(6, report["conversion"]["runtime_effective_spawns"])
            self.assertEqual(1, report["conversion"]["runtime_inactive_spawns"])
            self.assertEqual(1, report["conversion"]["over_limits"]["spawns"]["excess"])
            self.assertIn("converter did not truncate", markdown_path.read_text())


if __name__ == "__main__":
    unittest.main()
