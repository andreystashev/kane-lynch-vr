import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import compare_kl1_performance as compare


class SelectionTest(unittest.TestCase):
    def write(self, folder, name, pid, created):
        (folder / name).write_text(json.dumps({'event': 'session', 'gamePid': pid, 'processCreationFileTime': created}) + '\n', encoding='utf-8')

    def test_rotations_must_share_pid_and_creation(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            self.write(p, 'performance-a.jsonl', 7, 100)
            self.write(p, 'performance-a.jsonl.1', 7, 100)
            self.write(p, 'performance-b.jsonl', 7, 200)
            self.write(p, 'performance-c.jsonl', 8, 100)
            with self.assertRaisesRegex(ValueError, 'Ambiguous'):
                compare.select_session(p, 7)
            identity, files = compare.select_session(p, 7, 100)
            self.assertEqual(identity, (7, 100))
            self.assertEqual({file.name for file in files}, {'performance-a.jsonl', 'performance-a.jsonl.1'})
            with self.assertRaisesRegex(ValueError, 'No session'):
                compare.select_session(p, 7, 300)

    def test_rejects_inconsistent_headers(self):
        data = {'sessions': [{'gamePid': 7, 'processCreationFileTime': 100, 'version': 'a'},
                             {'gamePid': 7, 'processCreationFileTime': 100, 'version': 'b'}]}
        with self.assertRaisesRegex(ValueError, 'Inconsistent'):
            compare.summarize(data, (7, 100))

    def test_list_coalesces_rotations_and_preserves_pid_reuse(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            self.write(p, 'performance-a.jsonl', 7, 100)
            self.write(p, 'performance-a.jsonl.1', 7, 100)
            self.write(p, 'performance-b.jsonl', 7, 200)
            result = subprocess.run([sys.executable, compare.__file__, '--logs', str(p), '--list'],
                                    check=True, capture_output=True, text=True)
            rows = json.loads(result.stdout)
            self.assertEqual([(r['creationFileTime'], r['segments']) for r in rows], [(100, 2), (200, 1)])

    def test_unknown_gpu_data_does_not_look_like_zero(self):
        data = {'sessions': [{'gamePid': 7, 'processCreationFileTime': 100}],
                'cpuCaptureStages': {'groups': [{'pid': 7, 'creationFileTime': 100, 'gpuTransportActive': True, 'stages': {}}]}}
        summary = compare.summarize(data, (7, 100))
        self.assertIsNone(summary['cpuCapture'][0]['mapMsPerCall'])
        self.assertIsNone(summary['cpuCapture'][0]['publishMsPerCall'])


if __name__ == '__main__':
    unittest.main()
