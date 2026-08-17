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
        self.assertIn("map(p.x, 200, 3700, 1, SCREEN_WIDTH)", WEATHER)
        self.assertIn("map(p.y, 240, 3800, 1, SCREEN_HEIGHT)", WEATHER)

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

    def test_calibration_targets_do_not_overlap_instructions_or_cancel(self):
        self.assertIn("lv_obj_set_width(instructions, 170)", WEATHER)
        self.assertIn("lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -44)", WEATHER)


if __name__ == "__main__":
    unittest.main()
