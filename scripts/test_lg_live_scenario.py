from __future__ import annotations

import argparse
import json
import socket
import tempfile
import unittest
import xml.etree.ElementTree as element_tree
from datetime import datetime, timezone
from pathlib import Path
from unittest import mock

import lg_control
import lg_launch
import lg_live_scenario


class LiveScenarioTests(unittest.TestCase):
    def setUp(self) -> None:
        handle = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
        handle.close()
        self.capture_path = Path(handle.name)
        self.capture_path.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            b"\x00\x00\x00\rIHDR"
            b"\x00\x00\x00\x01"
            b"\x00\x00\x00\x01"
        )

    def tearDown(self) -> None:
        self.capture_path.unlink(missing_ok=True)

    def test_free_port_probes_the_requested_socket_type(self) -> None:
        real_socket = socket.socket
        with mock.patch.object(
            lg_live_scenario.socket,
            "socket",
            wraps=real_socket,
        ) as socket_factory:
            port = lg_live_scenario._free_port(socket.SOCK_DGRAM)

        socket_factory.assert_called_once_with(socket.AF_INET, socket.SOCK_DGRAM)
        with real_socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(("127.0.0.1", port))

    def test_capture_source_names_are_utc_compliant_and_run_unique(self) -> None:
        captured_at = datetime(2026, 7, 28, 13, 14, 15, tzinfo=timezone.utc)
        full = lg_live_scenario._capture_source_name(
            "rocket_launcher_visual_validation",
            "rlvv-before",
            "a1b2c3d4" + "0" * 24,
            1,
            captured_at=captured_at,
        )
        reduced = lg_live_scenario._capture_source_name(
            "rocket_launcher_visual_validation",
            "rlvv-before",
            "d4c3b2a1" + "0" * 24,
            1,
            captured_at=captured_at,
        )

        self.assertEqual(
            full,
            "20260728T131415Z-rocket-launc-a1b2c3d4-rlvv-before-01",
        )
        self.assertNotEqual(full, reduced)
        self.assertLessEqual(len(full), 64)
        self.assertTrue(
            all(character.isalnum() or character in "_-" for character in full)
        )

    def test_event_capture_waits_for_the_named_occurrence(self) -> None:
        expected = {
            "type": "explosion_created",
            "actor": 0,
            "weapon": "rocket_launcher",
            "occurrence": 3,
        }
        events = [
            {
                "type": "explosion_created",
                "actor": 0,
                "weapon": "rocket_launcher",
            },
            {
                "type": "explosion_created",
                "actor": 0,
                "weapon": "rocket_launcher",
            },
        ]

        self.assertFalse(
            lg_live_scenario._event_occurrence_reached(events, expected)
        )
        events.append(dict(events[-1]))
        self.assertTrue(
            lg_live_scenario._event_occurrence_reached(events, expected)
        )

    def test_capture_phase_accepts_exact_render_and_authoritative_state(self) -> None:
        shot = {
            "name": "muzzle",
            "render_phase": "muzzle",
            "actor": 0,
            "result": {
                "frame_state": {
                    "local_player_index": 0,
                    "local_rocket_launcher_fired": False,
                    "local_rocket_launcher_projectiles": 1,
                    "local_rocket_launcher_explosions": 0,
                    "renderer_rocket_instances": 1,
                    "renderer_tracer_instances": 1,
                    "renderer_explosion_instances": 0,
                }
            },
            "trigger": {
                "events": [
                    {
                        "type": "weapon_fired",
                        "actor": 0,
                        "weapon": "rocket_launcher",
                    },
                    {
                        "type": "projectile_spawned",
                        "actor": 0,
                        "weapon": "rocket_launcher",
                    },
                ]
            },
        }

        lg_live_scenario._validate_capture_phase(shot)

    def test_impact_phase_accepts_retained_render_after_snapshot_flag(self) -> None:
        shot = {
            "name": "impact",
            "render_phase": "impact",
            "actor": 0,
            "result": {
                "frame_state": {
                    "local_player_index": 0,
                    "local_rocket_launcher_fired": False,
                    "local_rocket_launcher_projectiles": 0,
                    "local_rocket_launcher_explosions": 0,
                    "renderer_rocket_instances": 0,
                    "renderer_tracer_instances": 0,
                    "renderer_explosion_instances": 3,
                }
            },
            "trigger": {
                "events": [
                    {
                        "type": "weapon_fired",
                        "actor": 0,
                        "weapon": "rocket_launcher",
                        "sequence": 7,
                    },
                    {
                        "type": "projectile_spawned",
                        "actor": 0,
                        "weapon": "rocket_launcher",
                        "sequence": 8,
                    },
                    {
                        "type": "explosion_created",
                        "actor": 0,
                        "weapon": "rocket_launcher",
                        "sequence": 10,
                    },
                ]
            },
        }

        lg_live_scenario._validate_capture_phase(shot)

    def test_surface_impact_phase_requires_the_matching_active_contact(self) -> None:
        shot = {
            "name": "freeze-contact",
            "render_phase": "surface_impact",
            "surface_impact_weapon": "freeze_gun",
            "actor": 0,
            "result": {
                "frame_state": {
                    "local_player_index": 0,
                    "local_surface_impact_active": True,
                    "local_surface_impact_weapon": "freeze_gun",
                    "local_surface_contact_effect_count": 2,
                }
            },
            "trigger": {
                "capture_mode": "prearmed_exact_render_phase",
                "surface_impact_weapon": "freeze_gun",
            },
        }

        lg_live_scenario._validate_capture_phase(shot)

        shot["result"]["frame_state"]["local_surface_impact_weapon"] = "railgun"
        with self.assertRaisesRegex(
            lg_live_scenario.LiveScenarioStageError,
            "matching active local surface impact",
        ):
            lg_live_scenario._validate_capture_phase(shot)

    def test_capture_phase_rejects_idle_and_wrong_authority(self) -> None:
        idle_shot = {
            "name": "projectile",
            "render_phase": "projectile",
            "actor": 0,
            "result": {
                "frame_state": {
                    "local_player_index": 0,
                    "local_rocket_launcher_fired": False,
                    "local_rocket_launcher_projectiles": 0,
                    "local_rocket_launcher_explosions": 0,
                    "renderer_rocket_instances": 0,
                    "renderer_tracer_instances": 0,
                    "renderer_explosion_instances": 0,
                }
            },
            "trigger": {
                "events": [
                    {
                        "type": "projectile_spawned",
                        "actor": 0,
                        "weapon": "rocket_launcher",
                    }
                ]
            },
        }
        wrong_authority = {
            "name": "impact",
            "render_phase": "impact",
            "actor": 0,
            "result": {
                "frame_state": {
                    "local_player_index": 0,
                    "local_rocket_launcher_fired": False,
                    "local_rocket_launcher_projectiles": 0,
                    "local_rocket_launcher_explosions": 1,
                    "renderer_rocket_instances": 0,
                    "renderer_tracer_instances": 0,
                    "renderer_explosion_instances": 1,
                }
            },
            "trigger": {
                "events": [
                    {
                        "type": "explosion_created",
                        "actor": 1,
                        "weapon": "rocket_launcher",
                    }
                ]
            },
        }

        with self.assertRaisesRegex(
            lg_live_scenario.LiveScenarioStageError,
            "idle or does not match",
        ):
            lg_live_scenario._validate_capture_phase(idle_shot)
        with self.assertRaisesRegex(
            lg_live_scenario.LiveScenarioStageError,
            "idle or does not match",
        ):
            lg_live_scenario._validate_capture_phase(wrong_authority)

    def test_before_phase_rejects_a_rendered_shot(self) -> None:
        shot = {
            "name": "before",
            "render_phase": "before_fire",
            "actor": 0,
            "result": {
                "frame_state": {
                    "local_player_index": 0,
                    "local_rocket_launcher_fired": True,
                    "local_rocket_launcher_projectiles": 1,
                    "local_rocket_launcher_explosions": 0,
                    "renderer_rocket_instances": 1,
                    "renderer_tracer_instances": 1,
                    "renderer_explosion_instances": 0,
                }
            },
            "trigger": {"events": []},
        }

        with self.assertRaisesRegex(
            lg_live_scenario.LiveScenarioStageError,
            "idle or does not match",
        ):
            lg_live_scenario._validate_capture_phase(shot)

    def scenario(self, *, capture: bool = False, network: bool = False) -> dict:
        value = {
            "schema_version": 1, "name": "live-test",
            "execution": {"mode": "client_server", "max_ticks": 3, "repeat": 1},
            "world": {"map": "default", "game_mode": "duel", "seed": 9},
            "timeline": [{
                "at_tick": 0, "player": 0, "duration_ticks": 2,
                "one_tick_edges": ["jump", "attack"],
                "input": {"forward": 1, "jump": True, "attack": True},
            }],
            "assertions": [
                {"type": "command_acknowledged", "classification": "CLIENT_BOUNDED", "at_completion": True, "timeline_index": 0, "max_ticks": 3},
                {"type": "input_edge_count", "classification": "CLIENT_BOUNDED", "at_completion": True, "edge": "attack", "count": 1},
                {"type": "client_pending_commands_max", "classification": "CLIENT_BOUNDED", "at_completion": True, "max": 8},
            ],
        }
        if network:
            value["network"] = {"latency_ms": 40, "jitter_ms": 0, "packet_loss_percent": 0, "reorder_percent": 0, "seed": 1}
        if capture:
            value["captures"] = [{"name": "shot", "after_event": {"type": "weapon_fired", "actor": 0}, "wait_rendered_frames": 2}]
            value["assertions"].append({"type": "screenshot_checkpoint", "classification": "VISUAL_REVIEW", "at_completion": True, "capture": "shot", "width": 1, "height": 1})
        return value

    def launcher(self, scenario: dict, *, cleanup_failures: list[str] | None = None):
        def launch(path, run_dir, token, server_port, control_port, renderer, allow_fallback, timeout, build_dir):
            run = Path(run_dir)
            run.mkdir(parents=True, exist_ok=True)
            (run / "ready.json").write_text(json.dumps({"token": token, "scenario": scenario["name"], "map": "default", "map_revision": 1}), encoding="utf-8")
            (run / "checkpoint-0.json").write_text(json.dumps({"events": [], "state": {"map_revision": 1}, "relative_tick": 0, "absolute_server_tick": 100}), encoding="utf-8")
            (run / "checkpoint-3.json").write_text(
                json.dumps(
                    {
                        "events": [
                            {
                                "type": "weapon_fired",
                                "actor": 0,
                                "weapon": "rocket_launcher",
                            },
                            {
                                "type": "projectile_spawned",
                                "actor": 0,
                                "weapon": "rocket_launcher",
                            },
                        ],
                        "state": {"map_revision": 1},
                        "relative_tick": 3,
                        "absolute_server_tick": 103,
                    }
                ),
                encoding="utf-8",
            )
            (run / "result.json").write_text(
                json.dumps(
                    {
                        "passed": True,
                        "relative_tick": 3,
                        "assertions": [{"status": "passed"}],
                        "events": [],
                        "hashes": {"final": "abc"},
                        "latest_consumed_command": {
                            "consumed_action_edge_counts": {
                                "jump": 1,
                                "attack": 1,
                            }
                        },
                    }
                ),
                encoding="utf-8",
            )
            return {"control_port": control_port, "run_token": token, "status": {"renderer": "SDL_GPU/vulkan"}, "logs": {}}
        return launch

    def sender(self, calls: list[tuple[str, dict]]) -> mock.Mock:
        sequence = 10
        cvars = {"r_combat_effects": "2", "r_bloom": "1"}
        armed_capture: str | None = None
        armed_phase: str | None = None
        def request(operation, **values):
            nonlocal sequence, armed_capture, armed_phase
            calls.append((operation, values))
            if operation == "get_cvar":
                return {
                    "name": values["name"],
                    "value": cvars[values["name"]],
                }
            if operation == "set_cvar":
                cvars[values["name"]] = values["value"]
                return {
                    "name": values["name"],
                    "value": values["value"],
                }
            if operation == "send_input":
                response = {"command_sequence": sequence}
                sequence += 1
                return response
            if operation == "get_client_state":
                return {
                    "connected": True,
                    "pending_command_count": 0,
                    "maximum_pending_command_count": 0,
                    "maximum_correction_distance": 0,
                    "client_tick": 20,
                    "network_simulation": {"decisions": []},
                }
            if operation == "capture_screenshot":
                return {
                    "path": str(self.capture_path),
                    "width": 1,
                    "height": 1,
                    "frame_state": {"latest_snapshot_tick": 103},
                }
            if operation == "arm_phase_capture":
                armed_capture = values["name"]
                armed_phase = values["phase"]
                return {
                    "name": armed_capture,
                    "phase": armed_phase,
                    "armed": True,
                }
            if operation == "collect_phase_capture":
                self.assertEqual(values["name"], armed_capture)
                impact = armed_phase == "local_rocket_launcher_impact"
                surface_impact = armed_phase == "local_surface_impact"
                return {
                    "path": str(self.capture_path),
                    "width": 1,
                    "height": 1,
                    "frame_state": {
                        "latest_snapshot_tick": 103,
                        "local_player_index": 0,
                        "local_rocket_launcher_fired": not impact,
                        "local_rocket_launcher_projectiles": 0 if impact else 1,
                        "local_rocket_launcher_explosions": 0,
                        "renderer_rocket_instances": 0 if impact else 1,
                        "renderer_tracer_instances": 0 if impact else 1,
                        "renderer_explosion_instances": 2 if impact else 0,
                        "local_surface_impact_active": surface_impact,
                        "local_surface_impact_weapon": (
                            "freeze_gun" if surface_impact else None
                        ),
                        "local_surface_contact_effect_count": (
                            2 if surface_impact else 0
                        ),
                    },
                }
            return {"ok": True}
        return mock.Mock(side_effect=request)

    def run_scenario(
        self,
        document: dict,
        *,
        cleanup_failures: list[str] | None = None,
        client_cvars: dict[str, str] | None = None,
    ):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "live-test.json"
            source.write_text("{}", encoding="utf-8")
            calls: list[tuple[str, dict]] = []
            with mock.patch.object(lg_live_scenario, "validate_live_scenario", return_value=document), \
                 mock.patch.object(lg_launch, "launch_scenario_session", side_effect=self.launcher(document)), \
                 mock.patch.object(lg_launch, "cleanup_scenario_session", return_value={"stopped": ["client", "server"], "failures": cleanup_failures or []}), \
                 mock.patch.object(lg_control, "send_request", self.sender(calls)):
                if cleanup_failures:
                    with self.assertRaises(lg_live_scenario.LiveScenarioError):
                        result = lg_live_scenario.run_live_scenario(
                            source,
                            root / "out",
                            timeout=1,
                            client_cvars=client_cvars,
                        )
                else:
                    result = lg_live_scenario.run_live_scenario(
                        source,
                        root / "out",
                        timeout=1,
                        client_cvars=client_cvars,
                    )
            return result if not cleanup_failures else {"artifact_path": str(next((root / "out").iterdir()))}, calls

    def test_client_cvar_parser_is_strict_and_rejects_duplicates(self) -> None:
        self.assertEqual(
            lg_live_scenario._parse_client_cvar_override(
                "r_combat_effects=1"
            ),
            ("r_combat_effects", "1"),
        )
        self.assertEqual(
            lg_live_scenario._parse_client_cvar_override("r_bloom=0"),
            ("r_bloom", "0"),
        )
        self.assertEqual(
            lg_live_scenario._parse_client_cvar_override("r_player_model=2"),
            ("r_player_model", "2"),
        )
        self.assertEqual(
            lg_live_scenario._parse_client_cvar_override("r_material_quality=0"),
            ("r_material_quality", "0"),
        )
        self.assertEqual(
            lg_live_scenario._parse_client_cvar_override(
                "r_player_outline_mode=2"
            ),
            ("r_player_outline_mode", "2"),
        )
        with self.assertRaises(argparse.ArgumentTypeError):
            lg_live_scenario._parse_client_cvar_override("g_rl_damage=1")
        with self.assertRaises(argparse.ArgumentTypeError):
            lg_live_scenario._parse_client_cvar_override("r_bloom=2")
        with self.assertRaises(argparse.ArgumentTypeError):
            lg_live_scenario._parse_client_cvar_override("r_player_model=3")
        with self.assertRaisesRegex(
            lg_live_scenario.LiveScenarioStageError,
            "duplicate",
        ):
            lg_live_scenario._client_cvar_overrides(
                [("r_bloom", "1"), ("r_bloom", "0")]
            )

    def test_client_cvars_apply_and_read_back_in_order(self) -> None:
        calls: list[tuple[str, dict]] = []
        values = {"r_combat_effects": "2", "r_bloom": "1"}

        def request(operation: str, *_args, **parameters):
            calls.append((operation, parameters))
            name = parameters["name"]
            if operation == "get_cvar":
                return {"name": name, "value": values[name]}
            if operation == "set_cvar":
                values[name] = parameters["value"]
                return {"name": name, "value": values[name]}
            self.fail(f"unexpected operation: {operation}")

        with mock.patch.object(
            lg_live_scenario,
            "_request",
            side_effect=request,
        ):
            result = lg_live_scenario._apply_client_cvar_overrides(
                {"control_port": 1},
                1,
                {"r_combat_effects": "1", "r_bloom": "0"},
            )

        self.assertEqual(
            [name for name, _ in calls],
            [
                "get_cvar",
                "set_cvar",
                "get_cvar",
                "get_cvar",
                "set_cvar",
                "get_cvar",
            ],
        )
        self.assertEqual(
            result["applied"],
            {"r_combat_effects": "1", "r_bloom": "0"},
        )
        self.assertTrue(result["applied_before_scenario_start"])

    def test_run_applies_client_cvars_before_initial_client_state(self) -> None:
        _, calls = self.run_scenario(
            self.scenario(),
            client_cvars={"r_combat_effects": "1", "r_bloom": "0"},
        )
        names = [name for name, _ in calls]
        self.assertEqual(
            names[:6],
            [
                "get_cvar",
                "set_cvar",
                "get_cvar",
                "get_cvar",
                "set_cvar",
                "get_cvar",
            ],
        )
        self.assertLess(names.index("set_cvar"), names.index("get_client_state"))

    def test_tick_ack_schedule_releases_edges_once(self) -> None:
        result, calls = self.run_scenario(self.scenario(network=True))
        self.assertEqual(result["status"], "passed")
        self.assertEqual([name for name, _ in calls].count("set_network_simulation"), 1)
        sent = [values for name, values in calls if name == "send_input"]
        self.assertEqual(len(sent), 1)
        self.assertEqual(sent[0]["ticks"], 2)
        self.assertEqual(sent[0]["one_tick_edges"], ["jump", "attack"])
        self.assertEqual([name for name, _ in calls].count("wait_command_ack"), 1)
        self.assertIn("wait_client_tick", [name for name, _ in calls])
        self.assertIn("wait_snapshot_tick", [name for name, _ in calls])

    def test_input_parts_normalize_canonical_weapon_name(self) -> None:
        parts = lg_live_scenario._input_parts(
            {
                "duration_ticks": 1,
                "one_tick_edges": ["attack"],
                "input": {"attack": True, "weapon": "machine_gun"},
            }
        )

        self.assertEqual(parts[0]["weapon"], "machinegun")

    def test_capture_after_event_waits_for_frames(self) -> None:
        _, calls = self.run_scenario(self.scenario(capture=True))
        names = [name for name, _ in calls]
        self.assertIn("capture_screenshot", names)
        self.assertLess(names.index("wait_frames"), names.index("capture_screenshot"))
        capture_index = names.index("capture_screenshot")
        state_index = min(
            index
            for index, name in enumerate(names[capture_index + 1:], capture_index + 1)
            if name == "get_client_state"
        )
        self.assertLess(capture_index, state_index)
        capture_request = calls[capture_index][1]
        self.assertNotEqual(capture_request["name"], "shot")
        self.assertRegex(
            capture_request["name"],
            r"^\d{8}T\d{6}Z-live-test-[0-9a-f]{8}-shot-01$",
        )

    def test_event_capture_starts_before_redundant_ack_wait(self) -> None:
        _, calls = self.run_scenario(self.scenario(capture=True))
        names = [name for name, _ in calls]
        ack_index = names.index("wait_command_ack")
        capture_index = names.index("capture_screenshot")

        self.assertLess(capture_index, ack_index)

    def test_muzzle_capture_arms_before_input_and_collects_after(self) -> None:
        scenario = self.scenario()
        scenario["timeline"][0]["input"]["weapon"] = "rocket_launcher"
        scenario["captures"] = [
            {
                "name": "muzzle",
                "after_event": {
                    "type": "weapon_fired",
                    "actor": 0,
                    "weapon": "rocket_launcher",
                    "occurrence": 1,
                },
                "wait_rendered_frames": 0,
                "render_phase": "muzzle",
            }
        ]

        result, calls = self.run_scenario(scenario)
        names = [name for name, _ in calls]
        arm_index = names.index("arm_phase_capture")
        input_index = names.index("send_input")
        collect_index = names.index("collect_phase_capture")

        self.assertEqual(result["status"], "passed")
        self.assertLess(arm_index, input_index)
        self.assertLess(input_index, collect_index)
        self.assertNotIn(
            "capture_screenshot",
            names[arm_index:collect_index + 1],
        )

    def test_surface_capture_arms_before_freeze_input_and_collects_after(self) -> None:
        scenario = self.scenario()
        scenario["timeline"][0]["input"]["weapon"] = "freeze_gun"
        scenario["captures"] = [
            {
                "name": "freeze-contact",
                "surface_impact_weapon": "freeze_gun",
                "wait_rendered_frames": 0,
                "render_phase": "surface_impact",
            }
        ]

        result, calls = self.run_scenario(scenario)
        names = [name for name, _ in calls]
        arm_index = names.index("arm_phase_capture")
        input_index = names.index("send_input")
        collect_index = names.index("collect_phase_capture")
        armed_phase = calls[arm_index][1]["phase"]

        self.assertEqual(result["status"], "passed")
        self.assertEqual(armed_phase, "local_surface_impact")
        self.assertLess(arm_index, input_index)
        self.assertLess(input_index, collect_index)

    def test_surface_capture_matches_only_its_local_weapon_input(self) -> None:
        capture = {
            "name": "rail-contact",
            "surface_impact_weapon": "railgun",
            "render_phase": "surface_impact",
        }

        self.assertIsNone(
            lg_live_scenario._phase_capture_for_surface_impact(
                [capture],
                set(),
                {"attack": True, "weapon": "shotgun"},
                0,
            )
        )
        selected = lg_live_scenario._phase_capture_for_surface_impact(
            [capture],
            set(),
            {"attack": True, "weapon": "railgun"},
            0,
        )
        self.assertEqual(selected, (capture, "local_surface_impact"))

    def test_third_impact_maps_to_third_rocket_input(self) -> None:
        capture = {
            "name": "impact",
            "after_event": {
                "type": "explosion_created",
                "actor": 0,
                "weapon": "rocket_launcher",
                "occurrence": 3,
            },
            "wait_rendered_frames": 0,
            "render_phase": "impact",
        }

        self.assertIsNone(
            lg_live_scenario._phase_capture_for_rocket_attack(
                [capture],
                set(),
                2,
            )
        )
        selected = lg_live_scenario._phase_capture_for_rocket_attack(
            [capture],
            set(),
            3,
        )
        self.assertIsNotNone(selected)
        armed_capture, armed_phase = selected
        self.assertEqual(armed_capture["name"], "impact")
        self.assertEqual(
            armed_phase,
            "local_rocket_launcher_impact",
        )

    def test_second_projectile_maps_to_second_rocket_input(self) -> None:
        capture = {
            "name": "flight",
            "after_event": {
                "type": "projectile_spawned",
                "actor": 0,
                "weapon": "rocket_launcher",
                "occurrence": 2,
            },
            "wait_rendered_frames": 0,
            "render_phase": "projectile",
        }

        self.assertIsNone(
            lg_live_scenario._phase_capture_for_rocket_attack(
                [capture],
                set(),
                1,
            )
        )
        selected = lg_live_scenario._phase_capture_for_rocket_attack(
            [capture],
            set(),
            2,
        )
        self.assertIsNotNone(selected)
        armed_capture, armed_phase = selected
        self.assertEqual(armed_capture["name"], "flight")
        self.assertEqual(
            armed_phase,
            "local_rocket_launcher_projectile",
        )

    def test_tick_capture_runs_before_later_timeline_input(self) -> None:
        scenario = self.scenario()
        scenario["timeline"].append(
            {
                "at_tick": 2,
                "player": 0,
                "duration_ticks": 1,
                "one_tick_edges": [],
                "input": {"forward": 0},
            }
        )
        scenario["captures"] = [
            {
                "name": "between-inputs",
                "at_server_tick": 2,
                "wait_rendered_frames": 0,
            }
        ]
        _, calls = self.run_scenario(scenario)
        sent_indexes = [
            index
            for index, (name, _) in enumerate(calls)
            if name == "send_input"
        ]
        capture_index = next(
            index
            for index, (name, _) in enumerate(calls)
            if name == "capture_screenshot"
        )
        self.assertEqual(len(sent_indexes), 2)
        self.assertLess(sent_indexes[0], capture_index)
        self.assertLess(capture_index, sent_indexes[1])
        sent = [calls[index][1] for index in sent_indexes]
        self.assertEqual([part["ticks"] for part in sent], [2, 1])

    def test_capture_reserves_runtime_without_changing_evidence_scenario(self) -> None:
        scenario = self.scenario(capture=True)
        observed_runtime_ticks: list[int] = []
        base_launcher = self.launcher(scenario)

        def inspect_launcher(path, *args, **kwargs):
            runtime = json.loads(Path(path).read_text(encoding="utf-8"))
            observed_runtime_ticks.append(runtime["execution"]["max_ticks"])
            return base_launcher(path, *args, **kwargs)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "live-test.json"
            source.write_text("{}", encoding="utf-8")
            calls: list[tuple[str, dict]] = []
            with mock.patch.object(
                lg_live_scenario,
                "validate_live_scenario",
                return_value=scenario,
            ), mock.patch.object(
                lg_launch,
                "launch_scenario_session",
                side_effect=inspect_launcher,
            ), mock.patch.object(
                lg_launch,
                "cleanup_scenario_session",
                return_value={"stopped": ["client", "server"], "failures": []},
            ), mock.patch.object(
                lg_control,
                "send_request",
                self.sender(calls),
            ):
                result = lg_live_scenario.run_live_scenario(
                    source,
                    root / "out",
                    timeout=1,
                )

            evidence_scenario = json.loads(
                (
                    Path(result["artifact_path"])
                    / "scenarios"
                    / scenario["name"]
                    / "scenario.json"
                ).read_text(encoding="utf-8")
            )
        self.assertEqual(
            observed_runtime_ticks,
            [
                scenario["execution"]["max_ticks"]
                + lg_live_scenario.CAPTURE_RUNTIME_PADDING_TICKS
            ],
        )
        self.assertEqual(
            evidence_scenario["execution"]["max_ticks"],
            scenario["execution"]["max_ticks"],
        )

    def test_dispatch_lateness_is_checked_after_client_state_read(self) -> None:
        scenario = self.scenario()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "test.json"
            source.write_text("{}", encoding="utf-8")
            calls: list[tuple[str, dict]] = []
            with mock.patch.object(
                lg_live_scenario,
                "validate_live_scenario",
                return_value=scenario,
            ), mock.patch.object(
                lg_launch,
                "launch_scenario_session",
                side_effect=self.launcher(scenario),
            ), mock.patch.object(
                lg_launch,
                "cleanup_scenario_session",
                return_value={"stopped": ["client", "server"], "failures": []},
            ), mock.patch.object(
                lg_control,
                "send_request",
                self.sender(calls),
            ), mock.patch.object(
                lg_live_scenario,
                "_latest_checkpoint",
                return_value={"relative_tick": 140},
            ):
                with self.assertRaisesRegex(
                    lg_live_scenario.LiveScenarioError,
                    "started 140 ticks late",
                ):
                    lg_live_scenario.run_live_scenario(
                        source,
                        root / "out",
                        timeout=1,
                    )
            names = [name for name, _ in calls]
            self.assertIn("get_client_state", names)
            self.assertNotIn("send_input", names)

    def test_dispatch_lateness_only_expands_after_measured_capture_pause(self) -> None:
        self.assertEqual(
            lg_live_scenario._schedule_late_limit(0),
            lg_live_scenario.BASE_SCHEDULE_LATE_TICKS,
        )
        self.assertEqual(
            lg_live_scenario._schedule_late_limit(10),
            lg_live_scenario.BASE_SCHEDULE_LATE_TICKS + 10,
        )
        self.assertEqual(
            lg_live_scenario._schedule_late_limit(1000),
            lg_live_scenario.BASE_SCHEDULE_LATE_TICKS
            + lg_live_scenario.MAX_CAPTURE_RECOVERY_TICKS,
        )

    def test_launch_failure_writes_partial_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root, source = Path(temporary), Path(temporary) / "test.json"
            source.write_text("{}", encoding="utf-8")
            with mock.patch.object(lg_live_scenario, "validate_live_scenario", return_value=self.scenario()), \
                 mock.patch.object(lg_launch, "launch_scenario_session", side_effect=lg_launch.LaunchError("renderer failed")):
                with self.assertRaisesRegex(lg_live_scenario.LiveScenarioError, "renderer failed"):
                    lg_live_scenario.run_live_scenario(source, root / "out", timeout=1)
            run = next((root / "out").iterdir())
            self.assertTrue((run / "manifest.json").is_file())
            scenario_dir = next((run / "scenarios").iterdir())
            self.assertTrue((scenario_dir / "assertions.json").is_file())

    def test_cpp_validation_rejection_never_launches(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / "test.json"
            source.write_text("{}", encoding="utf-8")
            with mock.patch.object(lg_live_scenario, "validate_live_scenario", side_effect=lg_live_scenario.LiveScenarioStageError("validate", "bad schema")), \
                 mock.patch.object(lg_launch, "launch_scenario_session") as launcher:
                with self.assertRaises(lg_live_scenario.LiveScenarioError):
                    lg_live_scenario.run_live_scenario(source, Path(temporary) / "out", timeout=1)
            launcher.assert_not_called()

    def test_control_failure_cleans_up_and_keeps_evidence(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root, source = Path(temporary), Path(temporary) / "test.json"
            source.write_text("{}", encoding="utf-8")
            with mock.patch.object(lg_live_scenario, "validate_live_scenario", return_value=self.scenario(network=True)), \
                 mock.patch.object(lg_launch, "launch_scenario_session", side_effect=self.launcher(self.scenario())), \
                 mock.patch.object(lg_launch, "cleanup_scenario_session", return_value={"stopped": ["client"], "failures": []}) as cleanup, \
                 mock.patch.object(lg_control, "send_request", side_effect=lg_control.ControlError("attach failed")):
                with self.assertRaisesRegex(lg_live_scenario.LiveScenarioError, "attach failed"):
                    lg_live_scenario.run_live_scenario(source, root / "out", timeout=1)
            cleanup.assert_called_once()
            run = next((root / "out").iterdir())
            self.assertTrue((run / "summary.json").is_file())

    def test_cleanup_failure_marks_run_failed_after_success(self) -> None:
        result, _ = self.run_scenario(self.scenario(), cleanup_failures=["server: did not exit"])
        self.assertIn("live-", result["artifact_path"])

    def test_failed_server_result_marks_run_failed(self) -> None:
        scenario = self.scenario()
        launch = self.launcher(scenario)

        def failed_launcher(*args, **kwargs):
            state = launch(*args, **kwargs)
            run_dir = Path(args[1])
            (run_dir / "result.json").write_text(
                json.dumps(
                    {
                        "passed": False,
                        "completion_reason": "server setup failed",
                        "assertions": [],
                    }
                ),
                encoding="utf-8",
            )
            return state

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "test.json"
            source.write_text("{}", encoding="utf-8")
            with mock.patch.object(
                lg_live_scenario, "validate_live_scenario", return_value=scenario
            ), mock.patch.object(
                lg_launch,
                "launch_scenario_session",
                side_effect=failed_launcher,
            ), mock.patch.object(
                lg_launch,
                "cleanup_scenario_session",
                return_value={
                    "stopped": ["client", "server"],
                    "failures": [],
                },
            ), mock.patch.object(
                lg_control,
                "send_request",
                self.sender([]),
            ):
                with self.assertRaises(lg_live_scenario.LiveScenarioError):
                    lg_live_scenario.run_live_scenario(
                        source,
                        root / "out",
                        timeout=1,
                    )
            artifact = next((root / "out").iterdir())
            summary = json.loads(
                (artifact / "summary.json").read_text(encoding="utf-8")
            )
            self.assertEqual(summary["authoritative_failed"], 1)

    def test_junit_is_valid_xml(self) -> None:
        document = lg_live_scenario._junit("quoted\"name", [{"type": "test", "status": "failed", "actual": "x"}], None)
        self.assertEqual(element_tree.fromstring(document).tag, "testsuite")

    def test_screenshot_assertion_requires_a_real_file(self) -> None:
        scenario = {
            "assertions": [
                {
                    "type": "screenshot_checkpoint",
                    "classification": "VISUAL_REVIEW",
                    "capture": "missing",
                    "width": 1280,
                    "height": 720,
                }
            ]
        }
        assertions = lg_live_scenario._live_assertions(
            scenario,
            {},
            {},
            [],
            [
                {
                    "name": "missing",
                    "result": {
                        "path": "does-not-exist.png",
                        "width": 1280,
                        "height": 720,
                    },
                }
            ],
            {},
            [],
        )
        self.assertEqual(assertions[0]["status"], "failed")


if __name__ == "__main__":
    unittest.main()
