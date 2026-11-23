"""Static checks for WOZT palette and translation assets (no GTK required)."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = set('background panel entry border text muted accent button hover active negative'.split())
THEMES = ['graphite', 'light', 'ocean', 'emerald', 'rose', 'lime', 'steam-legacy']
LANGUAGES = ['fr', 'de', 'it', 'es', 'pt', 'ru', 'ja', 'ko', 'zh-cn']

class UIAssetsTest(unittest.TestCase):
    def test_theme_palettes(self):
        assets = sorted((ROOT / 'data/themes').glob('*.json'))
        self.assertEqual(len(assets), 7)
        ids = []
        for path in assets:
            data = json.loads(path.read_text(encoding='utf8'))
            ids.append(data['id'])
            self.assertTrue(REQUIRED <= data['colors'].keys())
            self.assertTrue(all(re.fullmatch(r'#[a-fA-F0-9]{6}', data['colors'][k]) for k in REQUIRED))
        self.assertEqual(ids, THEMES)

    def test_languages(self):
        assets = sorted((ROOT / 'data/languages').glob('*.json'))
        self.assertEqual(len(assets), 9)
        self.assertEqual([json.loads(x.read_text(encoding='utf8'))['id'] for x in assets], LANGUAGES)
        for path in assets:
            data = json.loads(path.read_text(encoding='utf8'))
            self.assertTrue(data['name'])
            self.assertTrue(data['translations'])

if __name__ == '__main__':
    unittest.main()
