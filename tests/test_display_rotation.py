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

    def test_calibration_overlay_uses_logical_dimensions_and_rotates_targets_only(self):
        calibration_start = function_body("static void start_touch_calibration")
        self.assertIn(
            "lv_obj_set_size(calibration_overlay, display_width(), display_height());",
            calibration_start,
        )

        target_update = function_body("static void update_calibration_target")
        self.assertIn(
            "const TouchScreenPoint portrait_target = TOUCH_CALIBRATION_TARGETS[calibration_target_index];",
            target_update,
        )
        self.assertIn(
            "rotate_portrait_touch(current_rotation, portrait_target.x, portrait_target.y, &target_x, &target_y);",
            target_update,
        )
        self.assertIn("target_x - 12,", target_update)
        self.assertIn("target_y - 12);", target_update)

        finish = function_body("static void finish_touch_calibration")
        self.assertIn("targets[i] = TOUCH_CALIBRATION_TARGETS[i];", finish)
        self.assertIn("fit_touch_calibration(calibration_points, targets, 5, &fitted)", finish)

    def test_rebuild_ui_discards_old_objects_and_does_not_fetch_weather(self):
        rebuild = function_body("static void rebuild_ui")
        guard = "if (calibration_active || qweather_portal_active) return;"
        detach = "lv_keyboard_set_textarea(kb, nullptr);"
        clean = "lv_obj_clean(lv_scr_act());"
        self.assertIn(guard, rebuild)
        self.assertIn("if (kb && lv_obj_is_valid(kb))", rebuild)
        self.assertIn(detach, rebuild)
        for assignment in ("kb = nullptr;", "settings_win = nullptr;", "location_win = nullptr;"):
            self.assertIn(assignment, rebuild)
        self.assertIn(clean, rebuild)
        self.assertIn("create_ui();", rebuild)
        self.assertIn("if (reopen_settings) create_settings_window();", rebuild)
        self.assertNotIn("fetch_and_update_weather", rebuild)
        self.assertLess(rebuild.index(guard), rebuild.index(detach))
        self.assertLess(rebuild.index(detach), rebuild.index(clean))
        self.assertLess(rebuild.index("kb = nullptr;"), rebuild.index(clean))
        self.assertLess(rebuild.index("settings_win = nullptr;"), rebuild.index(clean))
        self.assertLess(rebuild.index("location_win = nullptr;"), rebuild.index(clean))
        self.assertLess(rebuild.index(clean), rebuild.index("create_ui();"))

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
