#!/usr/bin/env python3
"""Static contract checks for QWeather requests and AP key configuration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")


class QWeatherIntegrationTests(unittest.TestCase):
    def test_qweather_api_key_is_loaded_and_exposed_in_wifi_manager(self):
        for symbol in (
            "qweather_key",
            "qweatherKey",
            "WiFiManagerParameter",
            "setSaveParamsCallback",
            "qweather_key_param",
            "wm.addParameter(&qweather_key_param)",
            "prefs.putString(\"qweatherKey\"",
        ):
            self.assertIn(symbol, WEATHER)

    def test_weather_fetch_uses_qweather_endpoints_and_schema(self):
        fetch = WEATHER[WEATHER.rindex("void fetch_and_update_weather()") : WEATHER.index("const lv_img_dsc_t* choose_image")]
        for endpoint in (
            "/v7/weather/now",
            "/v7/weather/7d",
            "/v7/weather/24h",
        ):
            self.assertIn(endpoint, fetch)
        for field in ("now\"][\"temp\"", "feelsLike", "daily", "tempMax", "tempMin", "hourly", "fxTime", "pop"):
            self.assertIn(field, fetch)
        self.assertIn("qweather_icon_to_wmo", fetch)
        self.assertNotIn("api.open-meteo.com/v1/forecast", fetch)

    def test_missing_key_is_handled_without_sending_weather_request(self):
        self.assertIn("QWeather API key missing", WEATHER)
        self.assertIn("strlen(qweather_key) == 0", WEATHER)

    def test_missing_key_uses_open_meteo_fallback(self):
        fetch = WEATHER[WEATHER.rindex("void fetch_and_update_weather()") : WEATHER.index("const lv_img_dsc_t* choose_image")]
        self.assertIn("fetch_open_meteo_weather()", fetch)
        self.assertIn("using Open-Meteo fallback", WEATHER)
        self.assertIn("api.open-meteo.com/v1/forecast", WEATHER)

    def test_settings_can_open_qweather_configuration_portal(self):
        settings = WEATHER[WEATHER.index("void create_settings_window() {") :]
        self.assertIn("qweather_config_btn", settings)
        self.assertIn("open_qweather_config_portal", settings)
        self.assertIn("startConfigPortal(DEFAULT_CAPTIVE_SSID)", WEATHER)
        self.assertIn("setConfigPortalTimeout", WEATHER)


if __name__ == "__main__":
    unittest.main()
