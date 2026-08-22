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
        self.assertRegex(WEATHER, r"#define\s+SPEAKER_LEDC_CHANNEL\s+\d+\b")
        self.assertRegex(WEATHER, r"pinMode\(\s*SPEAKER_PIN\s*,\s*OUTPUT\s*\)")
        self.assertRegex(WEATHER, r"ledcSetup\(\s*SPEAKER_LEDC_CHANNEL\s*,")
        self.assertRegex(WEATHER, r"ledcAttachPin\(\s*SPEAKER_PIN\s*,\s*SPEAKER_LEDC_CHANNEL\s*\)")
        self.assertIn("lv_timer_set_repeat_count(speaker_timer, -1)", WEATHER)
        self.assertNotRegex(WEATHER, r"\btone\(\s*SPEAKER_PIN\s*,")

    def test_click_tone_uses_low_duty_pwm(self):
        self.assertRegex(WEATHER, r"#define\s+SPEAKER_DUTY\s+24\b")
        match = re.search(
            r"static void configure_click_tone\(uint32_t frequency\)\s*\{([\s\S]*?)\n\}",
            WEATHER,
        )
        self.assertIsNotNone(match)
        body = match.group(1)
        self.assertIn("ledcWrite(SPEAKER_PIN, SPEAKER_DUTY)", body)
        self.assertIn("ledcWrite(SPEAKER_LEDC_CHANNEL, SPEAKER_DUTY)", body)
        self.assertNotIn("ledcWriteTone", body)

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

    def test_settings_close_defers_blocking_weather_refresh_until_sound_stops(self):
        handler = WEATHER[
            WEATHER.index("static void settings_event_handler(lv_event_t *e) {") :
            WEATHER.index("static void configure_click_tone", WEATHER.index("static void settings_event_handler(lv_event_t *e) {"))
        ]
        close_match = re.search(
            r"if \(tgt == btn_close_obj[\s\S]*?\n  \}",
            handler,
        )
        self.assertIsNotNone(close_match)
        close_branch = close_match.group(0)

        self.assertIn("schedule_weather_refresh_after_click();", close_branch)
        self.assertNotIn("fetch_and_update_weather();", close_branch)
        self.assertRegex(
            WEATHER,
            r"#define\s+CLICK_SOUND_REFRESH_DELAY_MS\s+60\b",
        )
        self.assertRegex(
            WEATHER,
            r"lv_timer_create\(\s*refresh_weather_after_click_sound\s*,\s*CLICK_SOUND_REFRESH_DELAY_MS",
        )


if __name__ == "__main__":
    unittest.main()
