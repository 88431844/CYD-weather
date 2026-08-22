#!/usr/bin/env python3
"""主题与天气状态翻译的回归测试。"""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "aura"
TRANSLATIONS = (HEADER_DIR / "translations.h").read_text(encoding="utf-8")


class ThemeSettingsTranslationTests(unittest.TestCase):
    def run_cpp(self, body):
        source = f'''#include "translations.h"
#include <cstring>
#include <type_traits>

int main() {{
  const LocalizedStrings *default_strings = get_strings(LANG_EN);
  (void)default_strings;
{body}
}}
'''
        with tempfile.TemporaryDirectory() as temp_dir:
            source_path = Path(temp_dir) / "test.cpp"
            binary_path = Path(temp_dir) / "test"
            source_path.write_text(source, encoding="utf-8")
            compiled = subprocess.run(
                [
                    "c++", "-std=c++17", "-Wall", "-Werror", "-I", str(HEADER_DIR),
                    str(source_path), "-o", str(binary_path),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(binary_path)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_header_uses_theme_count_for_new_arrays(self):
        self.assertIn('#include "display_config.h"', TRANSLATIONS)
        for field in (
            "display_settings",
            "theme",
            "screen_orientation",
            "touch_rotation",
            "theme_names[THEME_COUNT]",
            "weather_conditions[10]",
        ):
            self.assertIn(field, TRANSLATIONS)

    def test_all_languages_provide_ordered_theme_and_weather_strings(self):
        self.run_cpp(r'''
  static_assert(std::extent<decltype(strings_en.theme_names)>::value == THEME_COUNT);
  static_assert(std::extent<decltype(strings_en.weather_conditions)>::value == 10);
  const LocalizedStrings *languages[] = {
      &strings_en, &strings_es, &strings_de, &strings_fr,
      &strings_tr, &strings_sv, &strings_it, &strings_zh};
  for (const LocalizedStrings *language : languages) {
    if (!language->display_settings || !language->theme ||
        !language->screen_orientation || !language->touch_rotation) return 1;
    for (int index = 0; index < THEME_COUNT; index++) {
      if (!language->theme_names[index] || !language->theme_names[index][0]) return 2;
    }
    for (int index = 0; index < 10; index++) {
      if (!language->weather_conditions[index] ||
          !language->weather_conditions[index][0]) return 3;
    }
  }
  const char *english_themes[] = {
      "Deep Sea", "Clear Sky", "Rainforest", "Sunset", "High Contrast"};
  const char *english_weather[] = {
      "Clear", "Partly cloudy", "Cloudy", "Fog", "Drizzle",
      "Light rain", "Heavy rain", "Sleet", "Snow", "Thunderstorm"};
  const char *chinese_themes[] = {"深海", "晴空", "雨林", "晚霞", "高对比"};
  const char *chinese_weather[] = {
      "晴", "多云", "阴", "雾", "毛毛雨", "小雨", "大雨", "雨夹雪", "雪", "雷雨"};
  const char *english_settings[] = {
      "Display", "Theme", "Orientation", "Correct touch"};
  const char *chinese_settings[] = {
      "显示设置", "主题", "屏幕方向", "自动校正触摸"};
  const char *english_actual[] = {
      strings_en.display_settings, strings_en.theme,
      strings_en.screen_orientation, strings_en.touch_rotation};
  const char *chinese_actual[] = {
      strings_zh.display_settings, strings_zh.theme,
      strings_zh.screen_orientation, strings_zh.touch_rotation};
  for (int index = 0; index < 4; index++) {
    if (std::strcmp(english_actual[index], english_settings[index])) return 4;
    if (std::strcmp(chinese_actual[index], chinese_settings[index])) return 5;
  }
  for (int index = 0; index < THEME_COUNT; index++) {
    if (std::strcmp(strings_en.theme_names[index], english_themes[index]) ||
        std::strcmp(strings_zh.theme_names[index], chinese_themes[index])) return 6;
  }
  for (int index = 0; index < 10; index++) {
    if (std::strcmp(strings_en.weather_conditions[index], english_weather[index]) ||
        std::strcmp(strings_zh.weather_conditions[index], chinese_weather[index])) return 7;
  }
  return 0;
''')


if __name__ == "__main__":
    unittest.main()
