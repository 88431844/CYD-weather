#!/usr/bin/env python3
"""Static regression checks for Simplified Chinese support."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
EXTRACTOR = ROOT / "aura" / "extract_unicode_chars.py"
FONT_REGENERATOR = ROOT / "aura" / "regenerate_chinese_fonts.sh"


class ChineseLanguageSupportTests(unittest.TestCase):
    def test_simplified_chinese_is_the_default_language(self):
        self.assertRegex(WEATHER, r"static Language current_language\s*=\s*LANG_ZH")
        self.assertRegex(
            WEATHER,
            r"current_language\s*=\s*validated_language\(\s*"
            r"prefs\.getUInt\(\"language\",\s*LANG_ZH\)\s*\)",
        )

    def test_only_english_and_chinese_are_selectable_localized_languages(self):
        self.assertRegex(
            TRANSLATIONS,
            r"enum Language\s*\{\s*LANG_EN\s*=\s*0,\s*LANG_ZH\s*=\s*1\s*\}",
        )
        self.assertIn("static const LocalizedStrings strings_zh", TRANSLATIONS)
        self.assertIn('"体感温度"', TRANSLATIONS)
        self.assertIn('"Sunrise"', TRANSLATIONS)
        self.assertIn('"Sunset"', TRANSLATIONS)
        self.assertIn('"日出"', TRANSLATIONS)
        self.assertIn('"日落"', TRANSLATIONS)
        self.assertIn('"七日天气预报"', TRANSLATIONS)
        self.assertIn(
            'lv_dropdown_set_options(language_dropdown, "English\\n简体中文")',
            WEATHER,
        )
        for unsupported in ("LANG_ES", "LANG_DE", "LANG_FR", "LANG_TR", "LANG_SV", "LANG_IT"):
            self.assertNotIn(unsupported, TRANSLATIONS)
        get_strings = TRANSLATIONS[TRANSLATIONS.index("static const LocalizedStrings* get_strings") :]
        for unsupported in ("strings_es", "strings_de", "strings_fr", "strings_tr", "strings_sv", "strings_it"):
            self.assertNotIn(unsupported, get_strings)
        self.assertRegex(get_strings, r"case LANG_ZH:\s*return &strings_zh;")

    def test_saved_legacy_language_values_fall_back_to_chinese(self):
        self.assertRegex(
            TRANSLATIONS,
            r"validated_language\(uint32_t value\)[\s\S]*?"
            r"value == LANG_EN\s*\?\s*LANG_EN\s*:\s*LANG_ZH",
        )

    def test_settings_symbol_uses_a_font_with_lvgl_symbol_fallback(self):
        segmented = WEATHER[
            WEATHER.index("static void create_forecast_segmented_control") :
            WEATHER.index("static void style_landscape_chart")
        ]
        symbol_style = segmented[
            segmented.index("lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS)") :
            segmented.index("lv_obj_center(settings_label)")
        ]
        self.assertIn("&lv_font_montserrat_14", symbol_style)
        self.assertNotIn("get_font_14()", symbol_style)

    def test_language_dropdown_uses_bilingual_font_in_english_mode(self):
        settings = WEATHER[
            WEATHER.index("// Language selection") :
            WEATHER.index("// Sound enable")
        ]
        self.assertEqual(settings.count("&lv_font_noto_sans_sc_12"), 3)
        self.assertNotIn("get_font_12()", settings)

    def test_chinese_uses_embedded_cjk_fonts(self):
        self.assertIn("lv_font_noto_sans_sc", WEATHER)
        self.assertRegex(WEATHER, r"current_language\s*==\s*LANG_ZH")
        required_chinese_text = "显示设置主题屏幕方向自动校正触摸深海晴空雨林晚霞高对比晴多云阴雾毛毛雨小雨大雨雨夹雪雷雨"
        self.assertIn('"显示设置"', TRANSLATIONS)
        self.assertIn('"自动校正触摸"', TRANSLATIONS)
        chinese_codepoints = sorted({
            ord(char)
            for text in (TRANSLATIONS, WEATHER, required_chinese_text)
            for char in text
            if "\u4e00" <= char <= "\u9fff"
        })
        for size in (12, 14, 16, 20):
            font_path = ROOT / "aura" / f"lv_font_noto_sans_sc_{size}.c"
            self.assertTrue(font_path.exists(), f"missing Chinese font for size {size}")
            font = font_path.read_text(encoding="utf-8")
            bitmap_match = re.search(
                r"glyph_bitmap\[\]\s*=\s*\{(.*?)\n\};", font, re.DOTALL
            )
            self.assertIsNotNone(bitmap_match, f"missing bitmap array for size {size}")
            bitmap_bytes = re.findall(r"0x[0-9a-fA-F]+", bitmap_match.group(1))
            self.assertTrue(bitmap_bytes, f"empty bitmap array for size {size}")

            descriptor_match = re.search(
                r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", font, re.DOTALL
            )
            self.assertIsNotNone(descriptor_match, f"missing glyph descriptors for size {size}")
            descriptors = [
                tuple(map(int, match))
                for match in re.findall(
                    r"\.bitmap_index = (\d+),.*?\.box_w = (\d+), \.box_h = (\d+)",
                    descriptor_match.group(1),
                )
            ]
            self.assertGreater(len(descriptors), 1, f"missing glyph descriptors for size {size}")

            unicode_match = re.search(
                r"unicode_list_1\[\]\s*=\s*\{(.*?)\n\};", font, re.DOTALL
            )
            self.assertIsNotNone(unicode_match, f"missing Unicode list for size {size}")
            unicode_offsets = [
                int(value, 16)
                for value in re.findall(r"0x[0-9a-fA-F]+", unicode_match.group(1))
            ]
            cmap_match = re.search(
                r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s+"
                r"\.unicode_list = unicode_list_1,.*?\.list_length = (\d+),.*?"
                r"LV_FONT_FMT_TXT_CMAP_SPARSE_TINY",
                font,
                re.DOTALL,
            )
            self.assertIsNotNone(cmap_match, f"missing sparse cmap for size {size}")
            range_start, range_length, glyph_id_start, list_length = map(
                int, cmap_match.groups()
            )
            self.assertEqual(len(unicode_offsets), list_length, f"bad Unicode list for size {size}")
            self.assertLessEqual(max(unicode_offsets), range_length - 1)
            self.assertLessEqual(glyph_id_start + list_length, len(descriptors))

            for codepoint in chinese_codepoints:
                offset = codepoint - range_start
                self.assertIn(offset, unicode_offsets, f"missing CJK mapping U+{codepoint:04X}")
                glyph_index = glyph_id_start + unicode_offsets.index(offset)
                bitmap_index, box_width, box_height = descriptors[glyph_index]
                self.assertGreater(box_width, 0, f"empty width for U+{codepoint:04X}")
                self.assertGreater(box_height, 0, f"empty height for U+{codepoint:04X}")
                self.assertLess(bitmap_index, len(bitmap_bytes), f"bad bitmap offset U+{codepoint:04X}")
                next_index = (
                    descriptors[glyph_index + 1][0]
                    if glyph_index + 1 < len(descriptors)
                    else len(bitmap_bytes)
                )
                required_bytes = (box_width * box_height + 1) // 2
                self.assertLessEqual(
                    bitmap_index + required_bytes,
                    next_index,
                    f"truncated bitmap U+{codepoint:04X}",
                )

    def test_font_regenerator_is_pinned_verified_and_path_independent(self):
        script = FONT_REGENERATOR.read_text(encoding="utf-8")
        self.assertRegex(
            script,
            r"https://raw\.githubusercontent\.com/notofonts/noto-cjk/[0-9a-f]{40}/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular\.otf",
        )
        self.assertNotIn("/main/", script)
        self.assertRegex(script, r'font_sha256="[0-9a-f]{64}"')
        self.assertIn("shasum -a 256 -c -", script)
        self.assertIn("Opts: reproducible via aura/regenerate_chinese_fonts.sh", script)
        self.assertIn("s/\\n+\\z/\\n/", script)
        for size in (12, 14, 16, 20):
            font = (ROOT / "aura" / f"lv_font_noto_sans_sc_{size}.c").read_text(
                encoding="utf-8"
            )
            self.assertNotIn("/tmp/aura-font.", font)
            self.assertNotIn(".worktrees", font)
            self.assertNotIn(str(ROOT), font)

    def test_generated_fonts_and_regenerator_preserve_lvgl_include_probe(self):
        probe = (
            '#ifdef __has_include\n'
            '    #if __has_include("lvgl.h")\n'
            '        #ifndef LV_LVGL_H_INCLUDE_SIMPLE\n'
            '            #define LV_LVGL_H_INCLUDE_SIMPLE\n'
            '        #endif\n'
            '    #endif\n'
            '#endif\n'
        )
        script = FONT_REGENERATOR.read_text(encoding="utf-8")
        self.assertIn("normalize_font_output()", script)
        self.assertIn('__has_include("lvgl.h")', script)
        self.assertIn("LV_LVGL_H_INCLUDE_SIMPLE", script)
        for size in (12, 14, 16, 20):
            font = (ROOT / "aura" / f"lv_font_noto_sans_sc_{size}.c").read_text(
                encoding="utf-8"
            )
            self.assertIn(probe, font, f"font {size} lost the LVGL include probe")

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
