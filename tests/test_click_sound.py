#!/usr/bin/env python3
"""Static regression checks for the touch click sound."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


class ClickSoundTests(unittest.TestCase):
    def test_speaker_is_initialized_on_the_cyd_pin(self):
        self.assertRegex(WEATHER, r"#define\s+SPEAKER_PIN\s+26\b")
        self.assertRegex(WEATHER, r"pinMode\(\s*SPEAKER_PIN\s*,\s*OUTPUT\s*\)")
        self.assertRegex(WEATHER, r"void\s+play_click_sound\s*\(\s*\)\s*\{[\s\S]*?tone\(\s*SPEAKER_PIN\s*,")

    def test_primary_click_handlers_play_sound(self):
        for callback in (
            "screen_event_cb",
            "daily_cb",
            "hourly_cb",
            "change_location_event_cb",
            "location_save_event_cb",
            "location_cancel_event_cb",
            "reset_wifi_event_handler",
            "reset_confirm_yes_cb",
            "reset_confirm_no_cb",
        ):
            match = re.search(
                rf"(?:static\s+)?void\s+{callback}\s*\(lv_event_t \*e\)\s*\{{([\s\S]*?)\n\}}",
                WEATHER,
            )
            self.assertIsNotNone(match, callback)
            body = match.group(1)
            self.assertIn("play_click_sound();", body, callback)


if __name__ == "__main__":
    unittest.main()
