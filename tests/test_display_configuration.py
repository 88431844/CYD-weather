#!/usr/bin/env python3
"""Static regression checks for the ESP32-2432S028R display wiring."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


class DisplayConfigurationTests(unittest.TestCase):
    def test_tft_setup_is_not_overridden_in_the_sketch(self):
        self.assertNotIn("#define USER_SETUP_LOADED", WEATHER)
        self.assertNotIn("#define ILI9341_2_DRIVER", WEATHER)

    def test_cyd_tft_configuration_is_declared_before_tft_include(self):
        setup = (ROOT / "TFT_eSPI" / "User_Setup.h").read_text(encoding="utf-8")
        expected = {
            "ILI9341_2_DRIVER": None,
            "TFT_MISO": "12",
            "TFT_MOSI": "13",
            "TFT_SCLK": "14",
            "TFT_CS": "15",
            "TFT_DC": "2",
            "TFT_RST": "-1",
            "TFT_BL": "21",
        }
        for name, value in expected.items():
            suffix = "" if value is None else rf"\s+{re.escape(value)}"
            self.assertRegex(setup, rf"#define\s+{name}{suffix}\b", name)


if __name__ == "__main__":
    unittest.main()
