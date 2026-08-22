#!/usr/bin/env python3
"""Static contracts for runtime display rotation and touch integration."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


def function_body(signature, next_signature):
    start = WEATHER.index(signature)
    end = WEATHER.index(next_signature, start)
    return WEATHER[start:end]


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

        rotation = function_body("static lv_display_rotation_t lv_rotation_for", "static int display_width")
        for value in ("LV_DISPLAY_ROTATION_0", "LV_DISPLAY_ROTATION_90",
                      "LV_DISPLAY_ROTATION_180", "LV_DISPLAY_ROTATION_270"):
            self.assertIn(value, rotation)
        self.assertRegex(rotation, r"default:\s*return LV_DISPLAY_ROTATION_0;")

    def test_display_dimensions_follow_current_rotation_geometry(self):
        width = function_body("static int display_width", "static int display_height")
        height = function_body("static int display_height", "void touchscreen_read")
        self.assertIn("geometry_for_rotation(current_rotation).width", width)
        self.assertIn("geometry_for_rotation(current_rotation).height", height)

    def test_setup_loads_validated_display_preferences_before_creation(self):
        setup = function_body("void setup()", "static void startup_weather_timer_cb")
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
        setup = function_body("void setup()", "static void startup_weather_timer_cb")
        create = "display = lv_tft_espi_create(PORTRAIT_WIDTH, PORTRAIT_HEIGHT, draw_buf, sizeof(draw_buf));"
        self.assertIn(create, setup)
        self.assertIn("lv_display_set_rotation(display, lv_rotation_for(current_rotation));", setup)
        self.assertIn("touch_indev = lv_indev_create();", setup)
        self.assertIn("lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);", setup)
        self.assertIn("lv_indev_set_display(touch_indev, display);", setup)
        self.assertIn("lv_indev_set_read_cb(touch_indev, touchscreen_read);", setup)
        self.assertLess(setup.index(create), setup.index("lv_display_set_rotation"))

    def test_touch_is_calibrated_in_portrait_before_rotation(self):
        touch = function_body("void touchscreen_read", "void setup()")
        self.assertIn("int portrait_x = map(p.x, 200, 3700, 1, PORTRAIT_WIDTH);", touch)
        self.assertIn("int portrait_y = map(p.y, 240, 3800, 1, PORTRAIT_HEIGHT);", touch)
        calibration = "apply_touch_calibration(touch_calibration, p.x, p.y,\n                                  PORTRAIT_WIDTH, PORTRAIT_HEIGHT,"
        self.assertIn(calibration, touch)
        self.assertIn("portrait_x = calibrated_x;", touch)
        self.assertIn("portrait_y = calibrated_y;", touch)
        self.assertIn("portrait_x = constrain(portrait_x, 0, PORTRAIT_WIDTH - 1);", touch)
        self.assertIn("portrait_y = constrain(portrait_y, 0, PORTRAIT_HEIGHT - 1);", touch)
        rotate = "rotate_portrait_touch(current_rotation, portrait_x, portrait_y, &x, &y);"
        self.assertIn(rotate, touch)
        self.assertLess(touch.index("apply_touch_calibration"), touch.index(rotate))

    def test_rebuild_ui_discards_old_objects_and_does_not_fetch_weather(self):
        rebuild = function_body("static void rebuild_ui", "void setup()")
        self.assertIn("lv_keyboard_set_textarea(kb, nullptr);", rebuild)
        for assignment in ("kb = nullptr;", "settings_win = nullptr;", "location_win = nullptr;"):
            self.assertIn(assignment, rebuild)
        self.assertIn("lv_obj_clean(lv_scr_act());", rebuild)
        self.assertIn("create_ui();", rebuild)
        self.assertIn("if (reopen_settings) create_settings_window();", rebuild)
        self.assertNotIn("fetch_and_update_weather", rebuild)


if __name__ == "__main__":
    unittest.main()
