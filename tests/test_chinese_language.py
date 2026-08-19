#!/usr/bin/env python3
"""Static regression checks for Simplified Chinese support."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


class ChineseLanguageSupportTests(unittest.TestCase):
    def test_simplified_chinese_is_the_default_language(self):
        self.assertRegex(WEATHER, r"static Language current_language\s*=\s*LANG_ZH")
        self.assertRegex(WEATHER, r"prefs\.getUInt\(\"language\",\s*LANG_ZH\)")

    def test_chinese_is_a_selectable_localized_language(self):
        self.assertRegex(TRANSLATIONS, r"LANG_ZH\s*=\s*7")
        self.assertIn("static const LocalizedStrings strings_zh", TRANSLATIONS)
        self.assertIn('"体感温度"', TRANSLATIONS)
        self.assertIn('"七日天气预报"', TRANSLATIONS)
        self.assertIn("简体中文", WEATHER)
        self.assertRegex(TRANSLATIONS, r"case LANG_ZH:\s*return &strings_zh;")

    def test_chinese_uses_embedded_cjk_fonts(self):
        self.assertIn("lv_font_noto_sans_sc", WEATHER)
        self.assertRegex(WEATHER, r"current_language\s*==\s*LANG_ZH")
        chinese_chars = sorted({
            char
            for text in (TRANSLATIONS, WEATHER)
            for char in text
            if "\u4e00" <= char <= "\u9fff"
        })
        for size in (12, 14, 16, 20):
            font_path = ROOT / "aura" / f"lv_font_noto_sans_sc_{size}.c"
            self.assertTrue(font_path.exists(), f"missing Chinese font for size {size}")
            font = font_path.read_text(encoding="utf-8")
            for char in chinese_chars:
                self.assertIn(f"U+{ord(char):04X}", font, f"missing glyph {char}")


if __name__ == "__main__":
    unittest.main()
