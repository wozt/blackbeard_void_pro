"""Pure versioning tests; must not call GitHub, audio or USB devices."""
import importlib.util
import unittest
from pathlib import Path

PATH = Path(__file__).resolve().parents[1] / "tools" / "next_release_version.py"
SPEC = importlib.util.spec_from_file_location("next_release_version", PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class IncrementalReleaseTests(unittest.TestCase):
    def test_no_matching_tags_begins_at_zero(self):
        self.assertEqual(MODULE.next_tag([]), "v1.0.0.0")

    def test_increments_last_component_only(self):
        self.assertEqual(MODULE.next_tag(["v1.0.0.0"]), "v1.0.0.1")
        self.assertEqual(MODULE.next_tag(["v1.0.0.7", "v1.0.0.2"]), "v1.0.0.8")

    def test_ignores_other_series_and_malformed_tags(self):
        tags = ["v1.0.1.999", "v2.0.0.50", "v1.0.0.7-rc", "v1.0.0.07", "bad", "v1.0.0.3"]
        self.assertEqual(MODULE.next_tag(tags), "v1.0.0.4")

    def test_large_rebuild_counter(self):
        self.assertEqual(MODULE.next_tag(["v1.0.0.999"]), "v1.0.0.1000")


if __name__ == "__main__":
    unittest.main()
