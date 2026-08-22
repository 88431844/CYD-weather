#!/usr/bin/env python3
"""Static contract checks for QWeather requests and AP key configuration."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")


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

    def test_qweather_location_uses_longitude_then_latitude(self):
        fetch = WEATHER[
            WEATHER.rindex("void fetch_and_update_weather()") :
            WEATHER.index("const lv_img_dsc_t* choose_image")
        ]
        self.assertIn(
            'String location_query = String(longitude) + "," + latitude;',
            fetch,
        )
        self.assertNotIn(
            'String location_query = String(latitude) + "," + longitude;',
            fetch,
        )

    def test_missing_key_is_handled_without_sending_weather_request(self):
        self.assertIn("QWeather API key missing", WEATHER)
        self.assertIn("strlen(qweather_key) == 0", WEATHER)

    def test_qweather_json_failures_report_bounded_format_diagnostics(self):
        request = WEATHER[
            WEATHER.index("static bool request_qweather(const String &path, DynamicJsonDocument &doc) {") :
            WEATHER.index("static int qweather_icon_to_wmo", WEATHER.index("static bool request_qweather(const String &path, DynamicJsonDocument &doc) {"))
        ]
        self.assertIn("DeserializationError json_error", request)
        self.assertIn("QWeather JSON parse failed: %s", request)
        self.assertIn("first bytes: %02X %02X %02X %02X", request)
        self.assertNotIn("Serial.println(payload)", request)

    def test_qweather_gzip_payload_is_decompressed_before_json_parsing(self):
        self.assertIn("#include <miniz.h>", WEATHER)
        helper_start = WEATHER.index("static bool decode_qweather_payload(String &payload) {")
        helper_end = WEATHER.index("static bool request_qweather", helper_start)
        helper = WEATHER[helper_start:helper_end]
        for symbol in (
            "0x1f",
            "0x8b",
            "QWEATHER_MAX_DECOMPRESSED_SIZE",
            "malloc(sizeof(tinfl_decompressor))",
            "tinfl_decompress(",
            "TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF",
            "TINFL_STATUS_DONE",
        ):
            self.assertIn(symbol, helper)
        self.assertNotIn("tinfl_decompress_mem_to_mem", helper)

        request_start = WEATHER.index(
            "static bool request_qweather(const String &path, DynamicJsonDocument &doc) {"
        )
        request_end = WEATHER.index("static int qweather_icon_to_wmo", request_start)
        request = WEATHER[request_start:request_end]
        self.assertIn("decode_qweather_payload(payload)", request)
        self.assertLess(
            request.index("decode_qweather_payload(payload)"),
            request.index("deserializeJson(doc, payload)"),
        )

    def test_missing_key_uses_open_meteo_fallback(self):
        fetch = WEATHER[WEATHER.rindex("void fetch_and_update_weather()") : WEATHER.index("const lv_img_dsc_t* choose_image")]
        self.assertIn("fetch_open_meteo_weather()", fetch)
        self.assertIn("using Open-Meteo fallback", WEATHER)
        self.assertIn("api.open-meteo.com/v1/forecast", WEATHER)

    def test_weather_response_updates_source_and_timestamp_status(self):
        self.assertIn("update_home_status(WEATHER_SOURCE_OPEN_METEO", WEATHER)
        self.assertIn("update_home_status(WEATHER_SOURCE_QWEATHER", WEATHER)
        self.assertIn('doc["current"]["time"]', WEATHER)
        self.assertIn('doc["updateTime"]', WEATHER)
        self.assertIn("weather_updated_at", WEATHER)

    def test_failed_qweather_requests_use_open_meteo_fallback(self):
        fetch = WEATHER[
            WEATHER.rindex("void fetch_and_update_weather()") :
            WEATHER.index("const lv_img_dsc_t* choose_image")
        ]
        self.assertGreaterEqual(fetch.count("fetch_open_meteo_weather();"), 4)
        for endpoint in (
            "/v7/weather/now",
            "/v7/weather/7d",
            "/v7/weather/24h",
        ):
            request = fetch.index(endpoint)
            fallback = fetch.index("fetch_open_meteo_weather();", request)
            self.assertLess(fallback - request, 300, endpoint)

    def test_forecast_labels_are_initialized_before_weather_arrives(self):
        create_ui = WEATHER[
            WEATHER.index("void create_ui() {") :
            WEATHER.index("void populate_results_dropdown()")
        ]
        for label in (
            "lbl_daily_day[i]",
            "lbl_daily_high[i]",
            "lbl_daily_low[i]",
            "lbl_hourly[i]",
            "lbl_precipitation_probability[i]",
            "lbl_hourly_temp[i]",
        ):
            self.assertIn(f'lv_label_set_text({label}, "--");', create_ui)

    def test_settings_can_open_qweather_configuration_portal(self):
        settings = WEATHER[WEATHER.index("void create_settings_window() {") :]
        self.assertIn("qweather_config_btn", settings)
        self.assertIn("open_qweather_config_portal", settings)
        self.assertIn("startConfigPortal(DEFAULT_CAPTIVE_SSID)", WEATHER)
        self.assertIn("setConfigPortalTimeout", WEATHER)

    def test_on_demand_portal_is_non_blocking_and_cancellable(self):
        for symbol in (
            "qweather_portal_manager",
            "qweather_portal_active",
            "setConfigPortalBlocking(false)",
            "qweather_portal_manager.process()",
            "qweather_portal_manager.stopConfigPortal()",
            "qweather_cancel_event_cb",
            "qweather_portal_manager.getConfigPortalActive()",
        ):
            self.assertIn(symbol, WEATHER)

    def test_prompt_is_rendered_outside_the_lvgl_event_before_ap_start(self):
        open_start = WEATHER.index("static void open_qweather_config_portal() {")
        open_end = WEATHER.index("static void qweather_cancel_event_cb", open_start)
        open_portal = WEATHER[open_start:open_end]
        process_start = WEATHER.index("static void process_qweather_config_portal() {")
        process_end = WEATHER.index("static bool request_qweather", process_start)
        process_portal = WEATHER[process_start:process_end]

        self.assertIn("qweather_portal_start_requested = true", open_portal)
        self.assertNotIn("flush_wifi_splashscreen", open_portal)
        self.assertNotIn("startConfigPortal", open_portal)
        self.assertIn("lv_refr_now(nullptr)", process_portal)
        self.assertIn("qweather_portal_manager.startConfigPortal", process_portal)
        self.assertLess(
            process_portal.index("lv_refr_now(nullptr)"),
            process_portal.index("qweather_portal_manager.startConfigPortal"),
        )

    def test_ap_page_has_a_dedicated_qweather_key_entry(self):
        for symbol in (
            "setMenu(qweather_menu",
            '"param"',
            "setCustomMenuHTML(qweather_menu_html)",
            'href=\'/param\'',
            "QWeather API Key",
        ):
            self.assertIn(symbol, WEATHER)

    def test_qweather_configuration_prompt_and_button_are_localized(self):
        self.assertIn('"和风天气API Key配置"', TRANSLATIONS)
        self.assertIn('"正在配置天气 API Key。', TRANSLATIONS)
        self.assertIn('"取消"', TRANSLATIONS)

    def test_startup_does_not_block_touch_on_the_first_weather_request(self):
        setup = WEATHER[WEATHER.index("void setup()") : WEATHER.index(
            "\n}\n\nstatic void startup_weather_timer_cb")]
        self.assertNotIn("fetch_and_update_weather();", setup)
        self.assertIn("startup_weather_timer", WEATHER)

        timer_start = WEATHER.index("static void startup_weather_timer_cb(lv_timer_t *timer) {")
        timer_end = WEATHER.index("void flush_wifi_splashscreen", timer_start)
        timer_callback = WEATHER[timer_start:timer_end]
        self.assertIn("wifi_connection_started", timer_callback)

        wifi_start = WEATHER.index("static void process_initial_wifi() {")
        wifi_end = WEATHER.index("static void open_qweather_config_portal", wifi_start)
        initial_wifi = WEATHER[wifi_start:wifi_end]
        self.assertIn(
            "if (wifi_manager.getConfigPortalActive()) return;",
            initial_wifi,
        )

    def test_weather_http_requests_have_connection_and_read_timeouts(self):
        self.assertIn("WEATHER_HTTP_TIMEOUT_MS", WEATHER)
        self.assertGreaterEqual(WEATHER.count("setConnectTimeout(WEATHER_HTTP_TIMEOUT_MS)"), 2)
        self.assertGreaterEqual(WEATHER.count("setTimeout(WEATHER_HTTP_TIMEOUT_MS)"), 2)

    def test_initial_wifi_connection_does_not_block_the_ui_loop(self):
        setup = WEATHER[WEATHER.index("void setup()") : WEATHER.index(
            "\n}\n\nstatic void startup_weather_timer_cb")]
        loop = WEATHER[WEATHER.index("void loop()") : WEATHER.index("void wifi_splash_screen")]
        self.assertIn("WiFi.begin();", setup)
        self.assertNotIn("wm.autoConnect(DEFAULT_CAPTIVE_SSID)", setup)
        for symbol in ("process_initial_wifi", "wifi_manager.process()", "WIFI_CONNECT_TIMEOUT_MS"):
            self.assertIn(symbol, WEATHER)


if __name__ == "__main__":
    unittest.main()
