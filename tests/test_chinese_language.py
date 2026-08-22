#!/usr/bin/env python3
"""Static regression checks for Simplified Chinese support."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
EXTRACTOR = ROOT / "aura" / "extract_unicode_chars.py"


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
        required_chinese_text = "显示设置主题屏幕方向自动校正触摸深海晴空雨林晚霞高对比晴多云阴雾毛毛雨小雨大雨雨夹雪雷雨"
        self.assertIn('"显示设置"', TRANSLATIONS)
        self.assertIn('"自动校正触摸"', TRANSLATIONS)
        chinese_chars = sorted({
            char
            for text in (TRANSLATIONS, WEATHER, required_chinese_text)
            for char in text
            if "\u4e00" <= char <= "\u9fff"
        })
        for size in (12, 14, 16, 20):
            font_path = ROOT / "aura" / f"lv_font_noto_sans_sc_{size}.c"
            self.assertTrue(font_path.exists(), f"missing Chinese font for size {size}")
            font = font_path.read_text(encoding="utf-8")
            for char in chinese_chars:
                self.assertIn(f"U+{ord(char):04X}", font, f"missing glyph {char}")

    def run_extractor(self, *arguments):
        return subprocess.run(
            [sys.executable, str(EXTRACTOR), *map(str, arguments)],
            capture_output=True,
            text=True,
        )

    def test_unicode_extractor_outputs_sorted_symbols_from_multiple_paths(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            first = Path(temp_dir) / "first.txt"
            second = Path(temp_dir) / "second.txt"
            first.write_text("乙é", encoding="utf-8")
            second.write_text("甲乙丙", encoding="utf-8")
            result = self.run_extractor("--symbols-only", first, second)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "é丙乙甲\n")
        self.assertEqual(result.stderr, "")

    def test_unicode_extractor_reports_missing_file_in_chinese(self):
        result = self.run_extractor("missing-source-file.ino")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("找不到文件", result.stderr)

    def test_unicode_extractor_keeps_single_file_analysis_mode(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "source.txt"
            source.write_text("天气é", encoding="utf-8")
            result = self.run_extractor(source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("字符分析", result.stdout)
        self.assertIn("é天气", result.stdout)


if __name__ == "__main__":
    unittest.main()
