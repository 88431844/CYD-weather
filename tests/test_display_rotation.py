#!/usr/bin/env python3
"""Static contracts for runtime display rotation and touch integration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


def function_body(signature):
    start = WEATHER.find(signature)
    while start >= 0:
        opening_brace = WEATHER.find("{", start)
        semicolon = WEATHER.find(";", start)
        if opening_brace >= 0 and (semicolon < 0 or opening_brace < semicolon):
            break
        start = WEATHER.find(signature, start + 1)
    if start < 0 or opening_brace < 0:
        raise AssertionError(f"未找到函数定义：{signature}")
    depth = 0
    for index in range(opening_brace, len(WEATHER)):
        if WEATHER[index] == "{":
            depth += 1
        elif WEATHER[index] == "}":
            depth -= 1
            if depth == 0:
                return WEATHER[start:index + 1]
    raise AssertionError(f"未找到函数结束位置：{signature}")


class DisplayRotationContractTests(unittest.TestCase):
    def test_display_config_and_rotation_state_are_declared(self):
        self.assertIn('#include "display_config.h"', WEATHER)
        for declaration in (
            "static lv_display_t *display = nullptr;",
            "static lv_indev_t *touch_indev = nullptr;",
            "static ScreenRotation current_rotation = SCREEN_ROTATION_0;",
            "static ThemeId current_theme = THEME_DEEP_SEA;",
        ):
            self.assertIn(declaration, WEATHER)

        rotation = function_body("static lv_display_rotation_t lv_rotation_for")
        for value in ("LV_DISPLAY_ROTATION_0", "LV_DISPLAY_ROTATION_90",
                      "LV_DISPLAY_ROTATION_180", "LV_DISPLAY_ROTATION_270"):
            self.assertIn(value, rotation)
        self.assertIn("default:\n      return LV_DISPLAY_ROTATION_0;", rotation)

    def test_display_dimensions_follow_current_rotation_geometry(self):
        width = function_body("static int display_width")
        height = function_body("static int display_height")
        self.assertIn("geometry_for_rotation(current_rotation).width", width)
        self.assertIn("geometry_for_rotation(current_rotation).height", height)

    def test_setup_loads_validated_display_preferences_before_creation(self):
        setup = function_body("void setup()")
        prefs_begin = setup.index('prefs.begin("weather", false);')
        display_create = setup.index("lv_tft_espi_create")
        self.assertLess(prefs_begin, display_create)
        self.assertEqual(setup.count('prefs.begin("weather", false);'), 1)
        preference_block = setup[prefs_begin:display_create]
        self.assertIn(
            "current_rotation = validated_rotation(prefs.getUInt(\"screenRotation\", SCREEN_ROTATION_0));",
            preference_block,
        )
        self.assertIn(
            "current_theme = validated_theme(prefs.getUInt(\"theme\", THEME_DEEP_SEA));",
            preference_block,
        )

    def test_setup_creates_portrait_display_then_binds_rotated_touch_device(self):
        setup = function_body("void setup()")
        create = "display = lv_tft_espi_create(PORTRAIT_WIDTH, PORTRAIT_HEIGHT, draw_buf, sizeof(draw_buf));"
        self.assertIn(create, setup)
        self.assertIn("lv_display_set_rotation(display, lv_rotation_for(current_rotation));", setup)
        self.assertIn("touch_indev = lv_indev_create();", setup)
        self.assertIn("lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);", setup)
        bind = "lv_indev_set_display(touch_indev, display);"
        self.assertIn(bind, setup)
        self.assertIn("// LVGL 9 applies display rotation to bound pointer input.", setup)
        self.assertIn("lv_indev_set_read_cb(touch_indev, touchscreen_read);", setup)
        self.assertLess(setup.index(create), setup.index("lv_display_set_rotation"))
        self.assertLess(setup.index(bind), setup.index("lv_indev_set_read_cb"))

    def test_touch_is_calibrated_in_portrait_and_leaves_rotation_to_lvgl(self):
        touch = function_body("void touchscreen_read")
        self.assertIn("int portrait_x = map(p.x, 200, 3700, 1, PORTRAIT_WIDTH);", touch)
        self.assertIn("int portrait_y = map(p.y, 240, 3800, 1, PORTRAIT_HEIGHT);", touch)
        calibration = "apply_touch_calibration(touch_calibration, p.x, p.y,\n                                  PORTRAIT_WIDTH, PORTRAIT_HEIGHT,"
        self.assertIn(calibration, touch)
        self.assertIn("portrait_x = calibrated_x;", touch)
        self.assertIn("portrait_y = calibrated_y;", touch)
        self.assertIn("portrait_x = constrain(portrait_x, 0, PORTRAIT_WIDTH - 1);", touch)
        self.assertIn("portrait_y = constrain(portrait_y, 0, PORTRAIT_HEIGHT - 1);", touch)
        self.assertIn("x = portrait_x;", touch)
        self.assertIn("y = portrait_y;", touch)
        self.assertNotIn("rotate_portrait_touch", touch)

    def test_calibration_temporarily_uses_portrait_dimensions_and_targets(self):
        calibration_start = function_body("static void start_touch_calibration()")
        self.assertIn(
            "calibration_previous_rotation = current_rotation;",
            calibration_start,
        )
        self.assertIn("current_rotation = SCREEN_ROTATION_0;", calibration_start)
        self.assertIn(
            "lv_display_set_rotation(display, LV_DISPLAY_ROTATION_0);",
            calibration_start,
        )

        overlay = function_body("static void create_touch_calibration_overlay")
        self.assertIn(
            "lv_obj_set_size(calibration_overlay, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);",
            overlay,
        )

        target_update = function_body("static void update_calibration_target")
        self.assertIn(
            "const TouchScreenPoint portrait_target = TOUCH_CALIBRATION_TARGETS[calibration_target_index];",
            target_update,
        )
        self.assertNotIn("rotate_portrait_touch", target_update)
        self.assertIn("static_cast<int>(portrait_target.x) - 12,", target_update)
        self.assertIn("static_cast<int>(portrait_target.y) - 12);", target_update)

        finish = function_body("static void finish_touch_calibration(bool success)")
        self.assertIn("targets[i] = TOUCH_CALIBRATION_TARGETS[i];", finish)
        self.assertIn("fit_touch_calibration(calibration_points, targets, 5, &fitted)", finish)

    def test_rebuild_ui_discards_old_objects_and_does_not_fetch_weather(self):
        rebuild = function_body("static void rebuild_ui")
        guard = "if (calibration_active || qweather_portal_active) return;"
        detach = "detach_keyboard_from_textarea();"
        clear = "clear_screen_object_references();"
        clean = "lv_obj_clean(lv_scr_act());"
        self.assertIn(guard, rebuild)
        self.assertIn(detach, rebuild)
        self.assertIn(clear, rebuild)
        self.assertIn(clean, rebuild)
        self.assertIn("create_ui();", rebuild)
        self.assertIn("if (reopen_settings) create_settings_window();", rebuild)
        self.assertNotIn("fetch_and_update_weather", rebuild)
        self.assertLess(rebuild.index(guard), rebuild.index(detach))
        self.assertLess(rebuild.index(detach), rebuild.index(clear))
        self.assertLess(rebuild.index(clear), rebuild.index(clean))
        self.assertLess(rebuild.index(clean), rebuild.index("create_ui();"))

    def test_screen_reference_clearer_covers_every_owned_object_and_series(self):
        clear = function_body("static void clear_screen_object_references()")
        for pointer in (
            "lbl_today_temp", "lbl_today_feels_like", "img_today_icon",
            "lbl_forecast", "box_daily", "box_hourly", "lbl_home_location",
            "lbl_settings_location", "lbl_clock", "lbl_network_status",
            "lbl_update_status", "landscape_current_condition",
            "landscape_daily_button", "landscape_hourly_button", "daily_chart",
            "hourly_chart", "daily_high_series", "daily_low_series",
            "hourly_temperature_series", "loc_ta", "results_dd",
            "btn_close_loc", "btn_close_obj", "kb", "settings_win",
            "location_win", "unit_switch", "clock_24hr_switch",
            "night_mode_switch", "language_dropdown", "touch_calibration_btn",
            "sound_enabled_switch", "sound_effect_dropdown",
            "qweather_config_btn", "rotation_buttonmatrix",
        ):
            self.assertIn(f"{pointer} = nullptr;", clear)
        for array in (
            "lbl_daily_day", "lbl_daily_high", "lbl_daily_low", "img_daily",
            "lbl_hourly", "lbl_precipitation_probability", "lbl_hourly_temp",
            "img_hourly", "landscape_daily_dates", "landscape_daily_icons",
            "landscape_daily_conditions", "daily_high_labels",
            "daily_low_labels", "landscape_hourly_times",
            "landscape_hourly_icons", "landscape_hourly_conditions",
            "hourly_temperature_labels",
        ):
            self.assertIn(f"{array}[i] = nullptr;", clear)
        self.assertIn("theme_buttons[i] = nullptr;", clear)

    def test_every_full_screen_clean_detaches_then_clears_references(self):
        self.assertIn("static void detach_keyboard_from_textarea()", WEATHER)
        self.assertIn("static void clear_screen_object_references()", WEATHER)
        cases = (
            ("static void rebuild_ui(bool reopen_settings)", "lv_obj_clean(lv_scr_act());"),
            ("void setup()", "lv_obj_clean(lv_scr_act());"),
            ("void wifi_splash_screen()", "lv_obj_clean(scr);"),
            ("static void start_touch_calibration()", "lv_obj_clean(lv_scr_act());"),
        )
        for signature, clean_call in cases:
            body = function_body(signature)
            detach = body.index("detach_keyboard_from_textarea();")
            clear = body.index("clear_screen_object_references();")
            clean = body.index(clean_call)
            self.assertLess(detach, clear, signature)
            self.assertLess(clear, clean, signature)

    def test_ap_mode_finishes_active_calibration_before_splash(self):
        callback = function_body("void apModeCallback(WiFiManager *mgr)")
        self.assertIn("if (calibration_active)", callback)
        self.assertIn("finish_touch_calibration(false);", callback)
        self.assertLess(
            callback.index("finish_touch_calibration(false);"),
            callback.index("wifi_splash_active = true;"),
        )
        self.assertLess(
            callback.index("finish_touch_calibration(false);"),
            callback.index("wifi_splash_screen();"),
        )

    def test_backlight_is_enabled_after_rotated_display_and_touch_setup(self):
        setup = function_body("void setup()")
        backlight = setup.index("analogWrite(LCD_BACKLIGHT_PIN, brightness);")
        for initialization in (
            "display = lv_tft_espi_create",
            "lv_display_set_rotation(display, lv_rotation_for(current_rotation));",
            "touch_indev = lv_indev_create();",
            "lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);",
            "lv_indev_set_display(touch_indev, display);",
            "lv_indev_set_read_cb(touch_indev, touchscreen_read);",
        ):
            self.assertLess(setup.index(initialization), backlight)


if __name__ == "__main__":
    unittest.main()
