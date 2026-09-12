import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import lg_control
import lg_launch
import lg_playtest as pt

STATUS = {'control_session_id': 'session-A', 'connected': True,
          'spectator': False, 'map': 'glide_duel', 'match_state': 'warmup'}
STATE = {'predicted_local_player': {'position': [0, 0, 1], 'velocity': [0, 0, 0], 'health': 100},
         'pending_command_count': 0, 'last_correction_distance': 0}

class PlaytestTests(unittest.TestCase):
    def session(self):
        with patch.object(pt, 'attach_client', return_value=STATUS):
            return pt.PlaytestSession(28161)

    def test_sequence_one_native_request_with_pinned_identity(self):
        session = self.session()
        calls = []
        def request(operation, **kwargs):
            calls.append((operation, kwargs))
            return STATUS if operation == 'status' else STATE
        with patch.object(lg_control, 'send_request', side_effect=request):
            row = session.step({'input': [{'ticks': 20, 'forward': 1}, {'ticks': 30, 'crouch': True}], 'capture': False})
        self.assertEqual([op for op, _ in calls], ['status', 'get_client_state', 'send_input_sequence', 'get_client_state', 'status'])
        self.assertTrue(all(args['expected_control_session'] == 'session-A' for _, args in calls))
        self.assertEqual(row['outcome'], 'recorded')

    def test_timeout_never_retries_or_releases_into_new_session(self):
        session = self.session()
        with patch.object(lg_control, 'send_request', side_effect=lg_control.ControlError('timeout')) as send:
            for _ in range(2):
                with self.assertRaises(lg_control.ControlError): session.request('send_input', ticks=1)
            self.assertEqual(send.call_count, 1)

    def test_validate_entire_plan_before_input(self):
        session = self.session()
        with patch.object(session, 'step') as step, tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(lg_control.ControlError):
                pt.run_plan({'version': 1, 'steps': [{'input': {'ticks': 1}}, {'input': {'ticks': 0}}]}, session, Path(temp)/'run')
            step.assert_not_called()

    def test_wrong_map_never_sends_input(self):
        session = self.session()
        with patch.object(lg_control, 'send_request', return_value={**STATUS, 'map': 'other'}) as send:
            with self.assertRaises(lg_control.ControlError): session.step({'input': {'ticks': 1}})
            self.assertEqual(send.call_count, 1)

    def test_missing_measurements_fail_assertions(self):
        _, failures = pt.assess({}, {}, {'max_planar_speed': 1, 'min_displacement': 1})
        self.assertEqual(len(failures), 2)

    def test_sequence_limits_and_edge_validation(self):
        for action in ([], [{'ticks': 250}]*6, [{'ticks': True}], [{'ticks': 1, 'one_tick_edges': ['oops']}], [{'ticks': 1, 'yaw': 0}]):
            with self.assertRaises(lg_control.ControlError): pt.validate_step({'input': action})

    def test_report_preserves_evidence_and_stops_on_failed_check(self):
        session = self.session()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root/'capture.png'; source.write_bytes(b'evidence')
            row = {'label': '<unsafe>', 'outcome': 'failed', 'capture': {'path': str(source)}, 'failures': ['speed']}
            with patch.object(session, 'step', return_value=row) as step:
                result = pt.run_plan({'version': 1, 'steps': [{}, {}]}, session, root/'run')
            self.assertEqual(step.call_count, 1)
            self.assertEqual(result['outcome'], 'failed')
            self.assertEqual((root/'run/000.png').read_bytes(), b'evidence')
            self.assertIn('&lt;unsafe&gt;', (root/'run/index.html').read_text())
            self.assertEqual(json.loads((root/'run/result.json').read_text())['outcome'], 'failed')

    def test_uncertain_run_leaves_incomplete_report(self):
        session = self.session()
        with tempfile.TemporaryDirectory() as temp, patch.object(session, 'step', side_effect=lg_control.ControlError('timeout')):
            result = pt.run_plan({'version': 1, 'steps': [{}]}, session, Path(temp)/'run')
            self.assertEqual(result['outcome'], 'incomplete')
            self.assertTrue(Path(result['report_path']).is_file())

    def test_attachment_failure_cannot_launch(self):
        with patch.object(lg_launch, 'send_request', side_effect=lg_control.ControlError('offline')), patch.object(lg_launch, 'ensure_client') as launch:
            with self.assertRaises(lg_control.ControlError): lg_launch.attach_client(control_port=28161)
            launch.assert_not_called()

    def test_linux_rebuilt_executable_keeps_identity_check(self):
        entry = {'pid': 123, 'path': '/tmp/game', 'creation_time': 42}
        with patch.object(lg_launch.platform, 'system', return_value='Linux'), patch.object(lg_launch.os, 'readlink', return_value='/tmp/game (deleted)'), patch.object(lg_launch, '_process_creation_time', return_value=42):
            self.assertTrue(lg_launch._entry_matches(entry))
            self.assertFalse(lg_launch._entry_matches({**entry, 'creation_time': 41}))

if __name__ == '__main__': unittest.main()
