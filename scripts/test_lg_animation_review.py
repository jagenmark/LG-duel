import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import lg_animation_review as review
import lg_control


class AnimationReviewTests(unittest.TestCase):
    def test_pose_sequences_capture_before_release_and_hold_crouch(self):
        for pose in review.POSES:
            segments, tick, _ = review.pose_input(pose)
            self.assertLess(tick, sum(part["ticks"] for part in segments))
            if pose in {"crouch", "crouch_walk", "slide"}:
                self.assertTrue(any(part.get("crouch") for part in segments))

    def test_camera_views_frame_body_from_spawn(self):
        spawn = [-14.0, 0.0, 1.0]
        for view in review.VIEWS:
            camera = review.camera_for(spawn, view)
            self.assertEqual(camera["fov"], 45.0)
            self.assertAlmostEqual(camera["position"][2], 1.8)

    def test_state_assertions_only_check_requested_fields(self):
        frame = {"body_visible": True, "development_camera": True,
                 "rendered_player": {"crouched": True}}
        self.assertEqual(review._state_assertions(frame, {"crouched": True}), [])
        self.assertEqual(len(review._state_assertions({"predicted_local_player": {}}, {"on_ground": True})), 3)

    def test_native_phase_name_is_not_the_old_phase(self):
        self.assertEqual(review.build_parser().parse_args(
            ["--port", "1", "--output", tempfile.mkdtemp(), "--poses", "stand", "--views", "front"]
        ).poses, ["stand"])
        seen = {}
        class FakeSession:
            broken = True
            session_id = "s"
            def request(self, operation, **kwargs):
                raise lg_control.ControlError("stop after attachment")
        def factory(*args, **kwargs):
            seen.update(kwargs)
            return FakeSession()
        args = review.build_parser().parse_args(["--port", "1", "--output", tempfile.mkdtemp(), "--poses", "stand", "--views", "front"])
        with patch.object(review, "PlaytestSession", side_effect=factory):
            with self.assertRaises(lg_control.ControlError):
                review.run_review(args)
        self.assertEqual(seen["expected_phase"], "WAITING FOR PLAYERS")

    def test_collision_debug_parser_is_bounded(self):
        args = review.build_parser().parse_args(["--port", "1", "--output", tempfile.mkdtemp(), "--collision-debug", "5"])
        self.assertEqual(args.collision_debug, 5)
        with self.assertRaises(SystemExit):
            review.build_parser().parse_args(["--port", "1", "--output", tempfile.mkdtemp(), "--collision-debug", "6"])

    def test_restoration_runs_after_ordinary_failure(self):
        class FakeSession:
            broken = False
            session_id = "s"
            def __init__(self): self.calls = []
            def request(self, operation, **kwargs):
                self.calls.append((operation, kwargs))
                if operation == "get_camera": return {"position": [1, 2, 3], "yaw": 4, "pitch": 5, "fov": 60}
                if operation == "status": return {"connected": True, "spectator": False, "map": "glide_duel", "match_state": "WAITING FOR PLAYERS", "development_camera": False}
                if operation == "get_cvar": return {"value": "0"}
                if operation == "reload_map": raise lg_control.ControlError("fixture failed")
                return {}
        fake = FakeSession()
        args = review.build_parser().parse_args(["--port", "1", "--output", tempfile.mkdtemp(), "--poses", "stand", "--views", "front"])
        with patch.object(review, "PlaytestSession", return_value=fake):
            result = review.run_review(args)
        names = [operation for operation, _ in fake.calls]
        self.assertEqual(result["outcome"], "incomplete")
        self.assertIn("set_cvar", names)
        self.assertIn("set_player_view", names)

    def test_uncertain_failure_skips_restoration(self):
        class FakeSession:
            broken = False
            session_id = "s"
            def __init__(self): self.calls = []
            def request(self, operation, **kwargs):
                self.calls.append((operation, kwargs))
                if operation == "get_camera": return {"position": [1, 2, 3], "yaw": 4, "pitch": 5}
                if operation == "status": return {"connected": True, "spectator": False, "map": "glide_duel", "match_state": "WAITING FOR PLAYERS", "development_camera": False}
                if operation == "get_cvar": return {"value": "0"}
                if operation == "set_cvar" and kwargs["name"] == "r_dev_camera_draw_connected_body":
                    self.broken = True
                    raise lg_control.ControlError("uncertain")
                return {}
        fake = FakeSession()
        args = review.build_parser().parse_args(["--port", "1", "--output", tempfile.mkdtemp(), "--poses", "stand", "--views", "front"])
        with patch.object(review, "PlaytestSession", return_value=fake):
            result = review.run_review(args)
        self.assertFalse(result["restored"])
        self.assertEqual([op for op, _ in fake.calls].count("set_camera"), 0)


if __name__ == "__main__":
    unittest.main()
