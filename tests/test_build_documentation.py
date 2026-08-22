#!/usr/bin/env python3
"""构建说明和中文 README 的静态契约。"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
PLAN = (ROOT / "docs" / "superpowers" / "plans" /
        "2026-08-22-landscape-weather-ui.md").read_text(encoding="utf-8")
README = (ROOT / "README.md").read_text(encoding="utf-8")


class BuildDocumentationTests(unittest.TestCase):
    def test_arduino_cli_uses_a_temporary_matching_sketch_name(self):
        compile_section = PLAN[PLAN.index("- [ ] **步骤 3：检查变更和编译固件**") :]
        self.assertRegex(compile_section, r'cp -R aura "\$\{?sketch_dir\}?/aura"')
        self.assertRegex(
            compile_section,
            r'mv "\$\{?sketch_dir\}?/aura/weather\.ino" '
            r'"\$\{?sketch_dir\}?/aura/aura\.ino"',
        )
        self.assertNotRegex(
            compile_section,
            r'cp "\$\{?sketch_dir\}?/aura/weather\.ino"',
        )
        self.assertRegex(compile_section, r'"\$\{?sketch_dir\}?/aura"\s*```')
        self.assertNotRegex(compile_section, r"(?m)^\s{2}aura\s*$")
        self.assertNotIn("LV_LVGL_H_INCLUDE_SIMPLE", compile_section)

    def test_readme_is_fully_chinese_while_preserving_project_facts(self):
        for english_heading in (
            "### License", "### How to compile:", "### Libraries required to compile:",
            "### Languages", "### Thanks & Credits",
        ):
            self.assertNotIn(english_heading, README)
        for chinese_heading in (
            "### 许可", "### 编译方法", "### 编译所需库", "### 支持语言", "### 致谢",
        ):
            self.assertIn(chinese_heading, README)
        for fact in (
            "https://makerworld.com/en/models/1382304-aura-smart-weather-forecast-display",
            "GPL 3.0", "SIL Open Font License 1.1", "ArduinoJson 7.4.1",
            "HttpClient 2.2.0", "TFT_eSPI 2.5.43_", "WifiManager 2.0.17",
            "XPT2046_Touchscreen 1.4", "lvgl 9.2.2",
        ):
            self.assertIn(fact, README)

    def test_readme_documents_weather_provider_behavior(self):
        for fact in (
            "Open-Meteo",
            "无需 API Key",
            "默认天气源",
            "和风天气",
            "可选天气源",
        ):
            self.assertIn(fact, README)


if __name__ == "__main__":
    unittest.main()
