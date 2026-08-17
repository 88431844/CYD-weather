#!/usr/bin/env python3
"""Static regression checks for the settings layout, Shenzhen defaults, and sounds."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")


class SettingsLayoutAndSoundTests(unittest.TestCase):
    def test_shenzhen_is_the_default_location(self):
        self.assertIn('#define LATITUDE_DEFAULT "22.5431"', WEATHER)
        self.assertIn('#define LONGITUDE_DEFAULT "114.0579"', WEATHER)
        self.assertIn('#define LOCATION_DEFAULT "Shenzhen"', WEATHER)

    def test_settings_content_is_scrollable_and_rows_are_stable(self):
        settings = WEATHER[WEATHER.index("void create_settings_window() {") :]
        self.assertIn("lv_obj_set_size(settings_win, SCREEN_WIDTH, SCREEN_HEIGHT)", settings)
        self.assertRegex(settings, r"lv_obj_set_scroll_dir\(cont, LV_DIR_VER\)")
        self.assertIn("lv_obj_set_height(row,", settings)
        self.assertIn("lv_obj_set_width(row,", settings)
        self.assertNotIn("lv_obj_align_to(lbl_24hr, unit_switch, LV_ALIGN_OUT_RIGHT_MID", settings)
        self.assertNotIn("lv_obj_align_to(btn_reset, btn_change_loc, LV_ALIGN_OUT_RIGHT_MID", settings)

    def test_sound_preferences_and_effect_profiles_are_wired(self):
        for key in ("soundEnabled", "soundEffect"):
            self.assertIn(key, WEATHER)
        self.assertRegex(WEATHER, r"static\s+bool\s+sound_enabled\s*=\s*true")
        self.assertRegex(WEATHER, r"static\s+uint8_t\s+sound_effect\s*=\s*0")
        self.assertRegex(WEATHER, r"void\s+play_click_sound\s*\(\s*\)\s*\{[\s\S]*?if\s*\(!sound_enabled\)")
        for frequency in ("2200", "1500", "3000", "900"):
            self.assertIn(f"tone(SPEAKER_PIN, {frequency}", WEATHER)
        self.assertIn("sound_enabled_switch", WEATHER)
        self.assertIn("sound_effect_dropdown", WEATHER)
        self.assertIn("sound_enabled", TRANSLATIONS)
        self.assertIn("sound_effect", TRANSLATIONS)

    def test_settings_close_control_is_in_the_header(self):
        settings = WEATHER[WEATHER.index("void create_settings_window() {") :]
        self.assertIn("btn_close_obj = lv_btn_create(header)", settings)
        self.assertIn('lv_label_set_text(close_label, "X")', settings)
        self.assertIn("lv_obj_set_size(btn_close_obj, 42, LV_PCT(100))", settings)
        self.assertIn("LV_EVENT_PRESSED", settings)
        self.assertNotIn("lv_obj_t *close_row = create_row", settings)


if __name__ == "__main__":
    unittest.main()
