#!/usr/bin/env python3
"""Static regression checks for the settings layout, Shenzhen defaults, and sounds."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")


class SettingsLayoutAndSoundTests(unittest.TestCase):
    def test_rebuild_renders_cached_snapshot_once_without_fetching(self):
        rebuild = WEATHER[
            WEATHER.index("static void rebuild_ui(bool reopen_settings) {") :
            WEATHER.index("void setup()")
        ]
        self.assertEqual(rebuild.count("create_ui();"), 1)
        self.assertEqual(rebuild.count("render_weather_snapshot();"), 1)
        self.assertLess(rebuild.index("create_ui();"), rebuild.index("render_weather_snapshot();"))
        self.assertLess(
            rebuild.index("render_weather_snapshot();"),
            rebuild.index("if (reopen_settings) create_settings_window();"),
        )
        self.assertNotIn("fetch_and_update_weather", rebuild)

    def test_temperature_and_clock_switches_rerender_cache_without_fetching(self):
        handler = WEATHER[
            WEATHER.index("static void settings_event_handler(lv_event_t *e) {") :
            WEATHER.index("static void refresh_weather_after_click_sound")
        ]
        branches = (
            (
                handler[handler.index("if (tgt == unit_switch") : handler.index("if (tgt == clock_24hr_switch")],
                "use_fahrenheit = lv_obj_has_state(unit_switch, LV_STATE_CHECKED);",
            ),
            (
                handler[handler.index("if (tgt == clock_24hr_switch") : handler.index("if (tgt == night_mode_switch")],
                "use_24_hour = lv_obj_has_state(clock_24hr_switch, LV_STATE_CHECKED);",
            ),
        )
        for branch, state_update in branches:
            self.assertIn(state_update, branch)
            self.assertEqual(branch.count("render_weather_snapshot();"), 1)
            self.assertLess(
                branch.index(state_update),
                branch.index("render_weather_snapshot();"),
            )
            self.assertRegex(branch, r"render_weather_snapshot\(\);\s*return;\s*\}")
            self.assertNotIn("fetch_and_update_weather", branch)

    def test_forecast_tabs_save_active_view_and_do_not_fetch(self):
        daily = WEATHER[WEATHER.index("void daily_cb(lv_event_t *e) {") : WEATHER.index("void hourly_cb")]
        hourly = WEATHER[WEATHER.index("void hourly_cb(lv_event_t *e) {") : WEATHER.index("static void reset_wifi_event_handler")]
        self.assertIn("active_forecast_view = FORECAST_HOURLY;", daily)
        self.assertIn("active_forecast_view = FORECAST_DAILY;", hourly)
        for callback in (daily, hourly):
            self.assertIn("play_click_sound();", callback)
            self.assertNotIn("fetch_and_update_weather", callback)

    def test_language_rebuild_uses_snapshot_instead_of_fetching_weather(self):
        handler = WEATHER[
            WEATHER.index("static void settings_event_handler(lv_event_t *e) {") :
            WEATHER.index("static void refresh_weather_after_click_sound")
        ]
        language_branch = handler[
            handler.index("if (tgt == language_dropdown") :
            handler.index("if (tgt == btn_close_obj")
        ]
        self.assertIn("rebuild_ui(true);", language_branch)
        self.assertNotIn("fetch_and_update_weather", language_branch)

    def test_shenzhen_is_the_default_location(self):
        self.assertIn('#define LATITUDE_DEFAULT "22.5431"', WEATHER)
        self.assertIn('#define LONGITUDE_DEFAULT "114.0579"', WEATHER)
        self.assertIn('#define LOCATION_DEFAULT "Shenzhen"', WEATHER)

    def test_settings_content_is_scrollable_and_rows_are_stable(self):
        settings = WEATHER[WEATHER.index("void create_settings_window() {") :]
        self.assertIn("lv_obj_set_size(settings_win, SCREEN_WIDTH, SCREEN_HEIGHT)", settings)
        self.assertRegex(settings, r"lv_obj_set_scroll_dir\(cont, LV_DIR_VER\)")
        self.assertIn("lv_obj_clear_flag(settings_win, LV_OBJ_FLAG_SCROLLABLE)", settings)
        self.assertIn("lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE)", settings)
        self.assertIn("lv_obj_set_width(row, LV_PCT(100))", settings)
        self.assertIn("lv_obj_set_height(row,", settings)
        self.assertNotIn("lv_obj_set_width(row, 214)", settings)
        self.assertNotIn("lv_obj_align_to(lbl_24hr, unit_switch, LV_ALIGN_OUT_RIGHT_MID", settings)
        self.assertNotIn("lv_obj_align_to(btn_reset, btn_change_loc, LV_ALIGN_OUT_RIGHT_MID", settings)

    def test_sound_preferences_and_effect_profiles_are_wired(self):
        for key in ("soundEnabled", "soundEffect"):
            self.assertIn(key, WEATHER)
        self.assertRegex(WEATHER, r"static\s+bool\s+sound_enabled\s*=\s*true")
        self.assertRegex(WEATHER, r"static\s+uint8_t\s+sound_effect\s*=\s*0")
        self.assertRegex(WEATHER, r"void\s+play_click_sound\s*\(\s*\)\s*\{[\s\S]*?if\s*\(!sound_enabled\)")
        self.assertIn("start_click_tone(2200, 18)", WEATHER)
        self.assertIn("start_click_tone(2200, 25)", WEATHER)
        self.assertIn("start_click_tone(1500, 35)", WEATHER)
        self.assertIn("configure_click_tone(3000)", WEATHER)
        self.assertIn("start_click_tone(900, 45)", WEATHER)
        self.assertIn("ledcDetachPin(SPEAKER_PIN)", WEATHER)
        self.assertIn("ledcDetach(SPEAKER_PIN)", WEATHER)
        self.assertNotIn("tone(SPEAKER_PIN", WEATHER)
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

    def test_home_screen_shows_network_source_and_update_status(self):
        for symbol in (
            "lbl_network_status",
            "lbl_update_status",
            "update_home_status",
            "weather_updated_at",
            "WiFi.localIP().toString()",
        ):
            self.assertIn(symbol, WEATHER)
        for symbol in (
            "device_ip",
            "weather_source",
            "weather_updated",
            "qweather_name",
            "open_meteo_name",
        ):
            self.assertIn(symbol, TRANSLATIONS)
        self.assertIn(
            "lv_obj_align(lbl_network_status, LV_ALIGN_TOP_LEFT, 4, 2)",
            WEATHER,
        )
        self.assertIn(
            "lv_obj_align(lbl_update_status, LV_ALIGN_TOP_RIGHT, -10, 118)",
            WEATHER,
        )

    def test_home_screen_is_fixed_width_and_status_labels_are_bounded(self):
        home = WEATHER[
            WEATHER.index("static void create_portrait_ui(lv_obj_t *scr) {") :
            WEATHER.index("void populate_results_dropdown()")
        ]
        self.assertIn("lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE)", home)
        self.assertIn("lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF)", home)
        self.assertIn("lv_obj_set_width(lbl_network_status, 150)", home)
        self.assertIn("lv_obj_set_width(lbl_forecast, 120)", home)
        self.assertIn("lv_obj_set_width(lbl_update_status, 100)", home)
        self.assertIn("lv_label_set_long_mode(lbl_update_status, LV_LABEL_LONG_DOT)", home)
        self.assertIn(
            "lv_obj_align(lbl_forecast, LV_ALIGN_TOP_LEFT, 10, 118)",
            home,
        )

    def test_home_update_line_uses_compact_time_to_avoid_horizontal_overflow(self):
        update = WEATHER[
            WEATHER.index("void update_home_status(") :
            WEATHER.index("static void update_clock", WEATHER.index("void update_home_status("))
        ]
        self.assertIn("updated.substring(11, 16)", update)
        self.assertIn(
            'lv_label_set_text_fmt(lbl_update_status, "%s %s"',
            update,
        )


if __name__ == "__main__":
    unittest.main()
