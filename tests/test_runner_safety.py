"""Regression tests for the safe test runner; no audio APIs are invoked."""
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SafeTestPolicy(unittest.TestCase):
    def test_make_test_has_no_live_dependencies(self):
        make = (ROOT / 'Makefile').read_text(encoding='utf8')
        self.assertRegex(make, r'(?m)^test: \$\(BIN\)$')
        self.assertIn('bash ./tests/run_tests.sh safe', make)
        self.assertNotRegex(make, r'(?m)^test:.*chain_probe')

    def test_default_runner_has_no_audio_control_calls(self):
        script = (ROOT / 'tests/run_tests.sh').read_text(encoding='utf8')
        self.assertIn('BVP_TESTS_SAFE_V1', script)
        for dangerous in (r'\bpkill\b', r'\bkillall\b',
                          r'\bwpctl\b', r'\bpactl\b',
                          r'\bpw-cli\b', r'\bpaplay\b',
                          r'\bparec\b', r'\bkill\s+-', r'/dev/hidraw'):
            self.assertIsNone(re.search(dangerous, script), dangerous)

    def test_live_runner_requires_consent_before_operations(self):
        result = subprocess.run(['bash', str(ROOT / 'tests/run_tests_live.sh'), 'audio'],
                                cwd=ROOT, capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 2)
        self.assertIn('REFUSED', result.stderr)

    def test_live_runner_never_kills_unrelated_processes(self):
        script = (ROOT / 'tests/run_tests_live.sh').read_text(encoding='utf8')
        self.assertNotRegex(script, r'\bpkill\b|\bkillall\b|kill -9|kill_chain|kill_app')
        self.assertIn('pactl set-default-sink "$original_default"', script)


if __name__ == '__main__':
    unittest.main()
