import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest


MODULE_PATH = Path(__file__).parents[1] / "modular_men_batch.py"
SPEC = importlib.util.spec_from_file_location("modular_men_batch", MODULE_PATH)
assert SPEC and SPEC.loader
batch = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(batch)

ADAPTER_PATH = Path(__file__).parents[1] / "run_modular_job.py"
ADAPTER_SPEC = importlib.util.spec_from_file_location("run_modular_job", ADAPTER_PATH)
assert ADAPTER_SPEC and ADAPTER_SPEC.loader
adapter = importlib.util.module_from_spec(ADAPTER_SPEC)
ADAPTER_SPEC.loader.exec_module(adapter)


class RecipeTests(unittest.TestCase):
    def test_farmer_derives_shared_worker_setup(self):
        recipe = batch.RECIPES_ROOT / "quaternius_farmer.json"
        job, metadata = batch.build_job(recipe)
        self.assertEqual("Farmer", metadata["character"])
        self.assertTrue(job["input_path"].endswith("Individual Characters\\FBX\\Farmer.fbx"))
        self.assertTrue(job["rig_reference_path"].endswith("Individual Characters\\FBX\\Worker.fbx"))
        self.assertEqual("retarget", job["options"]["animation_transfer_mode"])
        self.assertEqual("Wrist.R", job["options"]["animation_bone_map"]["Hand.R"])
        self.assertEqual("Wrist.R", job["options"]["attachment_points"]["weapon_socket"])
        self.assertEqual("Wrist.L", job["options"]["bone_map"]["Hand.L"])
        self.assertEqual("Idle_Gun_TwoHanded", job["options"]["two_handed_idle"]["name"])
        self.assertEqual("JUMP", job["options"]["gameplay_jump"]["name"])
        self.assertIn({"name": "idle", "action": "IDLE", "frame": 25}, job["options"]["animation_preview_stills"])
        self.assertFalse(job["options"]["consolidate_materials"])
        self.assertEqual(3, len(job["input_bindings"]))

    def test_character_recipe_rejects_unshared_overrides(self):
        with tempfile.TemporaryDirectory() as raw:
            recipe = Path(raw) / "bad.json"
            recipe.write_text(json.dumps({
                "version": 1, "profile": "quaternius_modular_men", "character": "Farmer",
                "output_name": "farmer", "options": {"attachment_points": {}},
            }), encoding="utf-8")
            with self.assertRaisesRegex(batch.BatchError, "unknown fields"):
                batch.build_job(recipe)

    def test_worker_recipe_authors_jump_and_fall_instead_of_roll_aliases(self):
        recipe = json.loads((batch.RECIPES_ROOT / "quaternius_worker.json").read_text(encoding="utf-8"))
        aliases = recipe["options"]["animation_aliases"]
        jump = recipe["options"]["gameplay_jump"]
        self.assertNotIn("JUMP", aliases)
        self.assertNotIn("FALL", aliases)
        self.assertEqual("JUMP", jump["name"])
        self.assertEqual("FALL", jump["fall_name"])

    def test_character_path_cannot_escape_template(self):
        with tempfile.TemporaryDirectory() as raw:
            recipe = Path(raw) / "bad.json"
            recipe.write_text(json.dumps({
                "version": 1, "profile": "quaternius_modular_men", "character": "../Farmer",
                "output_name": "farmer",
            }), encoding="utf-8")
            with self.assertRaisesRegex(batch.BatchError, "character must"):
                batch.build_job(recipe)


class PoseGateTests(unittest.TestCase):
    def setUp(self):
        self.original_run = adapter.ORIGINAL_RUN
        adapter.ORIGINAL_RUN = lambda _job, _path: {"processing": {}}
        adapter.POSE_SIGNATURES.clear()

    def tearDown(self):
        adapter.ORIGINAL_RUN = self.original_run
        adapter.POSE_SIGNATURES.clear()

    def test_pose_gate_rejects_identical_representative_actions(self):
        adapter.POSE_SIGNATURES.update({"IDLE": (1,), "RUN": (1,), "Idle_Gun_TwoHanded": (2,)})
        with self.assertRaisesRegex(adapter.core.JobError, "poses are identical"):
            adapter._run_with_pose_gate({}, Path("unused"))

    def test_pose_gate_records_three_distinct_pairs(self):
        adapter.POSE_SIGNATURES.update({"IDLE": (1,), "RUN": (2,), "Idle_Gun_TwoHanded": (3,)})
        result = adapter._run_with_pose_gate({}, Path("unused"))
        self.assertEqual({"passed": True, "actions": ["IDLE", "RUN", "Idle_Gun_TwoHanded"],
                          "distinct_pairs": 3}, result["processing"]["pose_validation"])

    def test_gameplay_jump_rejects_one_name_for_jump_and_fall(self):
        fake_bpy = SimpleNamespace(data=SimpleNamespace(actions=SimpleNamespace(get=lambda _name: None)))
        with self.assertRaisesRegex(adapter.core.JobError, "names must differ"):
            adapter.core._create_gameplay_jump(fake_bpy, {
                "source": "Idle_Gun_TwoHanded", "name": "AIR", "fall_name": "AIR",
            })


if __name__ == "__main__":
    unittest.main()
