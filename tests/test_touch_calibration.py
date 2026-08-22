#!/usr/bin/env python3
"""Static contract checks for five-point resistive touchscreen calibration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")


class TouchCalibrationContractTests(unittest.TestCase):
    def test_five_targets_are_declared_in_approved_order(self):
        start = WEATHER.index("TOUCH_CALIBRATION_TARGETS")
        block = WEATHER[start:WEATHER.index("};", start) + 2]
        points = [(18, 18), (222, 18), (120, 160), (18, 302), (222, 302)]
        positions = [block.index(f"{{{x}, {y}}}") for x, y in points]
        self.assertEqual(positions, sorted(positions))
        self.assertIn("TOUCH_CALIBRATION_WAIT_RELEASE", WEATHER)

    def test_calibration_is_applied_and_uncalibrated_mapping_remains(self):
        self.assertIn('#include "touch_calibration.h"', WEATHER)
        self.assertIn("apply_touch_calibration", WEATHER)
        self.assertIn("map(p.x, 200, 3700, 1, PORTRAIT_WIDTH)", WEATHER)
        self.assertIn("map(p.y, 240, 3800, 1, PORTRAIT_HEIGHT)", WEATHER)

    def test_persistence_contains_all_six_coefficients(self):
        for key in (
            "touchCalibrated",
            "touchCalA",
            "touchCalB",
            "touchCalC",
            "touchCalD",
            "touchCalE",
            "touchCalF",
        ):
            self.assertIn(key, WEATHER)

    def test_calibration_points_have_a_versioned_persistence_format(self):
        self.assertIn("TOUCH_CALIBRATION_VERSION", WEATHER)
        self.assertIn("touchCalVersion", WEATHER)
        self.assertIn("prefs.getUInt(\"touchCalVersion\", 0)", WEATHER)

    def test_settings_and_modal_flow_are_present(self):
        for symbol in (
            "touch_calibration",
            "start_touch_calibration",
            "finish_touch_calibration",
            "calibration_timer_cb",
            "TOUCH_CALIBRATION_TIMEOUT_MS",
            "play_click_sound();",
        ):
            self.assertIn(symbol, WEATHER)
        for symbol in (
            "calibration_instructions",
            "calibration_progress",
            "calibration_cancel",
            "calibration_success",
            "calibration_failed",
        ):
            self.assertIn(symbol, TRANSLATIONS)
        self.assertIn('"屏幕校验"', TRANSLATIONS)
        self.assertIn('"请长按每个校验点，直到确认通过。"', TRANSLATIONS)
        self.assertNotIn('"点调", "请点五点。"', TRANSLATIONS)

    def test_calibration_targets_do_not_overlap_instructions_or_cancel(self):
        self.assertIn("lv_obj_set_width(instructions, 170)", WEATHER)
        self.assertIn("lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -44)", WEATHER)

    def test_calibration_temporarily_uses_portrait_and_restores_previous_rotation(self):
        self.assertIn("calibration_previous_rotation", WEATHER)
        start = WEATHER[
            WEATHER.index("static void start_touch_calibration() {") :
            WEATHER.index("void daily_cb", WEATHER.index("static void start_touch_calibration() {"))
        ]
        self.assertIn("calibration_previous_rotation = current_rotation;", start)
        self.assertIn("current_rotation = SCREEN_ROTATION_0;", start)
        self.assertIn("lv_display_set_rotation(display, LV_DISPLAY_ROTATION_0);", start)
        self.assertNotIn('prefs.putUInt("screenRotation"', start)
        self.assertIn("create_touch_calibration_overlay();", start)

        restore = WEATHER[
            WEATHER.index("static void restore_rotation_after_calibration(bool success) {") :
            WEATHER.index("static void calibration_timer_cb", WEATHER.index("static void restore_rotation_after_calibration(bool success) {"))
        ]
        self.assertIn("current_rotation = calibration_previous_rotation;", restore)
        self.assertIn("lv_display_set_rotation(display, lv_rotation_for(current_rotation));", restore)
        self.assertLess(restore.index("rebuild_ui(false);"), restore.index("show_calibration_result(success);"))

    def test_calibration_start_clears_old_ui_pointers_before_cleaning_screen(self):
        start = WEATHER[
            WEATHER.index("static void start_touch_calibration() {") :
            WEATHER.index("void daily_cb", WEATHER.index("static void start_touch_calibration() {"))
        ]
        self.assertIn("detach_keyboard_from_textarea();", start)
        self.assertIn("clear_screen_object_references();", start)
        self.assertLess(
            start.index("detach_keyboard_from_textarea();"),
            start.index("clear_screen_object_references();"),
        )
        self.assertLess(
            start.index("clear_screen_object_references();"),
            start.index("lv_obj_clean(lv_scr_act());"),
        )

        overlay = WEATHER[
            WEATHER.index("static void create_touch_calibration_overlay() {") :
            WEATHER.index("static void start_touch_calibration()", WEATHER.index("static void create_touch_calibration_overlay() {"))
        ]
        self.assertIn("lv_obj_set_size(calibration_overlay, PORTRAIT_WIDTH, PORTRAIT_HEIGHT);", overlay)
        self.assertNotIn("rotate_portrait_touch", overlay)

    def test_every_calibration_exit_is_coalesced_through_finish(self):
        self.assertIn("static bool calibration_finish_pending = false;", WEATHER)
        self.assertIn("static void queue_touch_calibration_finish(bool success)", WEATHER)
        self.assertIn("lv_async_call(finish_touch_calibration_async", WEATHER)
        cancel = WEATHER[
            WEATHER.index("static void calibration_cancel_event_cb") :
            WEATHER.index("static void finish_touch_calibration", WEATHER.index("static void calibration_cancel_event_cb"))
        ]
        self.assertIn("queue_touch_calibration_finish(false);", cancel)
        self.assertNotIn("lv_obj_del", cancel)
        timer = WEATHER[
            WEATHER.index("static void calibration_timer_cb") :
            WEATHER.index("static void create_touch_calibration_overlay", WEATHER.index("static void calibration_timer_cb"))
        ]
        self.assertIn("queue_touch_calibration_finish(false);", timer)
        self.assertIn("queue_touch_calibration_finish(true);", timer)
        finish = WEATHER[
            WEATHER.index("static void finish_touch_calibration(bool success) {") :
            WEATHER.index("static void calibration_timer_cb", WEATHER.index("static void finish_touch_calibration(bool success) {"))
        ]
        self.assertEqual(finish.count("restore_rotation_after_calibration(success);"), 1)

    def test_calibration_start_is_deferred_outside_settings_event(self):
        handler = WEATHER[
            WEATHER.index("if (tgt == touch_calibration_btn") :
            WEATHER.index("if (tgt == qweather_config_btn")
        ]
        self.assertIn("queue_touch_calibration_start();", handler)
        self.assertNotIn("start_touch_calibration();", handler)
        self.assertNotIn("lv_obj_del", handler)


if __name__ == "__main__":
    unittest.main()
