#!/usr/bin/env python3
"""Static regression checks for the ESP32-2432S028R display wiring."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


class DisplayConfigurationTests(unittest.TestCase):
    def test_cyd_tft_configuration_is_declared_before_tft_include(self):
        setup = WEATHER.split("#include <TFT_eSPI.h>", 1)[0]
        expected = {
            "USER_SETUP_LOADED",
            "ILI9341_2_DRIVER",
            "TFT_MISO 12",
            "TFT_MOSI 13",
            "TFT_SCLK 14",
            "TFT_CS 15",
            "TFT_DC 2",
            "TFT_RST -1",
            "TFT_BL 21",
        }
        for define in expected:
            self.assertIn(f"#define {define}", setup, define)


if __name__ == "__main__":
    unittest.main()
