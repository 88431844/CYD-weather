#!/usr/bin/env python3
"""Static contract checks for QWeather requests and AP key configuration."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
TRANSLATIONS = (ROOT / "aura" / "translations.h").read_text(encoding="utf-8")


def function_body(signature: str, next_signature: str) -> str:
    start = WEATHER.index(signature)
    return WEATHER[start : WEATHER.index(next_signature, start)]


class QWeatherIntegrationTests(unittest.TestCase):
    def test_open_meteo_is_the_validated_default_provider(self):
        self.assertRegex(
            WEATHER,
            r"enum\s+WeatherProvider\s*:\s*uint8_t\s*\{\s*"
            r"WEATHER_PROVIDER_OPEN_METEO\s*=\s*0\s*,\s*"
            r"WEATHER_PROVIDER_QWEATHER\s*=\s*1\s*\}",
        )
        self.assertRegex(
            WEATHER,
            r"static\s+WeatherProvider\s+weather_provider\s*=\s*"
            r"WEATHER_PROVIDER_OPEN_METEO",
        )
        self.assertIn(
            'prefs.getUInt("weatherProvider", WEATHER_PROVIDER_OPEN_METEO)',
            WEATHER,
        )
        self.assertIn("static WeatherProvider validated_weather_provider", WEATHER)
        validator_start = WEATHER.index(
            "static WeatherProvider validated_weather_provider"
        )
        validator_end = WEATHER.index("static WeatherProvider weather_provider")
        validator = WEATHER[validator_start:validator_end]
        self.assertIn("WEATHER_PROVIDER_QWEATHER", validator)
        self.assertIn("WEATHER_PROVIDER_OPEN_METEO", validator)

    def test_fetch_dispatches_open_meteo_before_qweather(self):
        fetch = WEATHER[
            WEATHER.rindex("void fetch_and_update_weather() {") :
            WEATHER.index("const lv_img_dsc_t* choose_image")
        ]
        dispatch = "weather_provider == WEATHER_PROVIDER_OPEN_METEO"
        self.assertIn(dispatch, fetch)
        self.assertLess(fetch.index(dispatch), fetch.index("strlen(qweather_key)"))
        open_meteo_call = fetch.index("fetch_open_meteo_weather();", fetch.index(dispatch))
        self.assertLess(open_meteo_call, fetch.index("strlen(qweather_key)"))

    def test_provider_validator_has_an_arduino_safe_explicit_prototype(self):
        prototype = (
            "static WeatherProvider "
            "validated_weather_provider(uint32_t value);"
        )
        self.assertIn(prototype, WEATHER)
        enum_start = WEATHER.index("enum WeatherProvider : uint8_t")
        enum_end = WEATHER.index("};", enum_start)
        declaration = WEATHER.index(prototype)
        definition = WEATHER.index(
            "static WeatherProvider validated_weather_provider(uint32_t value) {"
        )
        self.assertLess(enum_end, declaration)
        self.assertLess(declaration, definition)

    def test_weather_snapshot_is_the_single_publish_and_render_contract(self):
        self.assertIn('#include "forecast_model.h"', WEATHER)
        self.assertRegex(
            WEATHER,
            r"enum\s+ForecastView\s*:\s*uint8_t\s*\{\s*FORECAST_DAILY\s*,\s*FORECAST_HOURLY\s*\}",
        )
        self.assertRegex(WEATHER, r"static\s+WeatherSnapshot\s+weather_snapshot\s*\{\s*\}")
        self.assertRegex(
            WEATHER,
            r"static\s+ForecastView\s+active_forecast_view\s*=\s*FORECAST_DAILY",
        )

        declaration = WEATHER.index("static void render_weather_snapshot();")
        publish_start = WEATHER.index("static void publish_weather_snapshot(")
        self.assertLess(declaration, publish_start)
        publish = function_body(
            "static void publish_weather_snapshot(",
            "static void render_portrait_snapshot()",
        )
        self.assertIn("weather_snapshot = candidate;", publish)
        self.assertIn("render_weather_snapshot();", publish)
        self.assertLess(
            publish.index("weather_snapshot = candidate;"),
            publish.index("render_weather_snapshot();"),
        )

    def test_calibration_caches_new_snapshot_without_rendering_deleted_widgets(self):
        publish = function_body(
            "static void publish_weather_snapshot(",
            "static void render_portrait_snapshot()",
        )
        self.assertLess(
            publish.index("weather_snapshot = candidate;"),
            publish.index("render_weather_snapshot();"),
        )
        render = function_body(
            "static void render_weather_snapshot() {",
            "String urlencode",
        )
        self.assertRegex(
            render,
            r"\{\s*if \(calibration_active \|\| wifi_splash_active\) return;",
        )

    def test_wifi_splash_restore_rebuilds_and_renders_cached_snapshot(self):
        restore = function_body(
            "static void restore_home_ui_after_wifi() {",
            "static void process_initial_wifi()",
        )
        self.assertIn("wifi_splash_active = false;", restore)
        self.assertIn("rebuild_ui(false);", restore)
        self.assertNotIn("lv_obj_clean", restore)
        self.assertNotIn("create_ui();", restore)
        self.assertNotIn("fetch_and_update_weather", restore)
        self.assertLess(
            restore.index("wifi_splash_active = false;"),
            restore.index("rebuild_ui(false);"),
        )

    def test_initial_wifi_finishes_when_portal_ends_connected_or_not(self):
        self.assertIn("static void finish_initial_wifi_connection() {", WEATHER)
        finish = function_body(
            "static void finish_initial_wifi_connection() {",
            "static void process_initial_wifi()",
        )
        self.assertIn("wifi_connection_started = false;", finish)
        self.assertIn("wifi_config_portal_started = false;", finish)
        self.assertIn("restore_home_ui_after_wifi();", finish)

        process = function_body(
            "static void process_initial_wifi() {",
            "static void open_qweather_config_portal()",
        )
        self.assertGreaterEqual(
            process.count("finish_initial_wifi_connection();"), 2
        )
        self.assertRegex(
            process,
            r"if \(WiFi\.status\(\) == WL_CONNECTED\)\s*\{\s*"
            r"finish_initial_wifi_connection\(\);\s*return;",
        )
        self.assertRegex(
            process,
            r"if \(wifi_config_portal_started\)\s*\{\s*"
            r"finish_initial_wifi_connection\(\);\s*return;",
        )

    def test_open_meteo_builds_one_complete_candidate_before_publish(self):
        parser = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        self.assertIn("WeatherSnapshot candidate{};", parser)
        for assignment in (
            "candidate.current.temperature =",
            "candidate.current.feels_like =",
            "candidate.current.weather_code =",
            "candidate.current.is_day =",
            "candidate.current.valid = true",
            "candidate.daily[i].minimum =",
            "candidate.daily[i].maximum =",
            "candidate.daily[i].weather_code =",
            "candidate.daily[i].month =",
            "candidate.daily[i].day =",
            "candidate.daily[i].valid = true",
            "candidate.hourly[i].temperature =",
            "candidate.hourly[i].precipitation_probability =",
            "candidate.hourly[i].weather_code =",
            "candidate.hourly[i].hour =",
            "candidate.hourly[i].is_day =",
            "candidate.hourly[i].has_precipitation =",
            "candidate.hourly[i].valid = true",
        ):
            self.assertIn(assignment, parser)
        self.assertEqual(parser.count("publish_weather_snapshot(candidate);"), 1)
        self.assertLess(
            parser.index("publish_weather_snapshot(candidate);"),
            parser.index("update_home_status(WEATHER_SOURCE_OPEN_METEO"),
        )
        self.assertIn("!precipitation_probabilities[i].isNull()", parser)

    def test_open_meteo_missing_precipitation_array_does_not_block_publish(self):
        parser = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        completeness = parser[
            parser.index("bool current_complete =") :
            parser.index("if (!current_complete)")
        ]
        self.assertNotIn(
            "precipitation_probabilities.size() >= FORECAST_POINT_COUNT",
            completeness,
        )

        precipitation_guard = "i < precipitation_probabilities.size()"
        self.assertRegex(
            parser,
            r"i < precipitation_probabilities\.size\(\)\s*&&\s*"
            r"!precipitation_probabilities\[i\]\.isNull\(\)",
        )
        guard_start = parser.index(precipitation_guard)
        probability_assignment = parser.index(
            "candidate.hourly[i].precipitation_probability =",
            guard_start,
        )
        guard_end = parser.index("candidate.hourly[i].valid = true", guard_start)
        self.assertLess(probability_assignment, guard_end)

    def test_open_meteo_daily_points_are_validated_independently(self):
        parser = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        self.assertNotIn("times.size() >= FORECAST_POINT_COUNT", parser)
        self.assertNotIn("points_complete", parser)
        self.assertIn("i < times.size()", parser)
        self.assertIn("i < tmin.size()", parser)
        self.assertIn("i < tmax.size()", parser)
        self.assertIn("i < weather_codes.size()", parser)
        daily_guard = parser[parser.index("i < times.size()") : parser.index(
            "candidate.hourly[i].temperature ="
        )]
        self.assertIn("candidate.daily[i].valid = true;", daily_guard)
        self.assertNotIn("break;", daily_guard)

    def test_open_meteo_hourly_points_are_validated_independently(self):
        parser = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        self.assertNotIn("hours.size() >= FORECAST_POINT_COUNT", parser)
        for size_guard in (
            "i < hours.size()",
            "i < hourly_temps.size()",
            "i < hourly_weather_codes.size()",
            "i < hourly_is_day.size()",
        ):
            self.assertIn(size_guard, parser)
        hourly_guard = parser[parser.index("i < hours.size()") :]
        self.assertIn("candidate.hourly[i].valid = true;", hourly_guard)
        self.assertNotIn("break;", hourly_guard)

    def test_open_meteo_rejects_wholly_unusable_forecast_sections(self):
        parser = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        for counter in ("valid_daily_points", "valid_hourly_points"):
            self.assertIn(f"int {counter} = 0;", parser)
            self.assertIn(f"{counter}++;", parser)
        rejection = (
            "if (valid_daily_points == 0 || valid_hourly_points == 0)"
        )
        self.assertIn(rejection, parser)
        self.assertLess(parser.index(rejection), parser.index("publish_weather_snapshot"))

    def test_open_meteo_populates_optional_solar_times(self):
        parser = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        self.assertIn(
            "&daily=temperature_2m_min,temperature_2m_max,weather_code,sunrise,sunset",
            parser,
        )
        self.assertIn(
            'JsonArray sunrises = doc["daily"]["sunrise"].as<JsonArray>();',
            parser,
        )
        self.assertIn(
            'JsonArray sunsets = doc["daily"]["sunset"].as<JsonArray>();',
            parser,
        )
        self.assertIn("candidate.solar.has_sunrise = parse_hh_mm(", parser)
        self.assertIn("candidate.solar.has_sunset = parse_hh_mm(", parser)
        current_guard = parser[
            parser.index("bool current_complete") : parser.index("if (!current_complete)")
        ]
        self.assertNotIn("sunrise", current_guard)
        self.assertNotIn("sunset", current_guard)

    def test_qweather_populates_optional_solar_times_from_today(self):
        parser = function_body(
            "void fetch_and_update_weather() {",
            "const lv_img_dsc_t* choose_image",
        )
        self.assertIn('daily[0]["sunrise"]', parser)
        self.assertIn('daily[0]["sunset"]', parser)
        self.assertIn("candidate.solar.has_sunrise = parse_hh_mm(", parser)
        self.assertIn("candidate.solar.has_sunset = parse_hh_mm(", parser)
        required_start = parser.index('const char *date = daily[i]["fxDate"]')
        daily_required = parser[
            required_start : parser.index("continue;", required_start)
        ]
        self.assertNotIn("sunrise", daily_required)
        self.assertNotIn("sunset", daily_required)

    def test_qweather_falls_back_when_forecast_sections_have_no_valid_points(self):
        fetch = WEATHER[
            WEATHER.rindex("void fetch_and_update_weather() {") :
            WEATHER.index("const lv_img_dsc_t* choose_image")
        ]
        for counter in ("valid_daily_points", "valid_hourly_points"):
            self.assertIn(f"int {counter} = 0;", fetch)
            self.assertIn(f"{counter}++;", fetch)
            guard = f"if ({counter} == 0)"
            self.assertIn(guard, fetch)
            guard_start = fetch.index(guard)
            fallback = fetch.index("fetch_open_meteo_weather();", guard_start)
            self.assertLess(fallback - guard_start, 240)

    def test_qweather_accumulates_three_endpoints_and_publishes_once(self):
        parser = function_body(
            "void fetch_and_update_weather() {",
            "const lv_img_dsc_t* choose_image",
        )
        self.assertIn("WeatherSnapshot candidate{};", parser)
        self.assertIn('String qweather_updated_at = doc["updateTime"] | "";', parser)
        self.assertLess(
            parser.index('String qweather_updated_at = doc["updateTime"] | "";'),
            parser.index('/v7/weather/7d'),
        )
        for assignment in (
            "candidate.current.temperature =",
            "candidate.current.feels_like =",
            "candidate.current.weather_code = qweather_icon_to_wmo(",
            "candidate.current.is_day = qweather_icon_is_day(",
            "candidate.daily[i].minimum =",
            "candidate.daily[i].maximum =",
            "candidate.daily[i].weather_code = qweather_icon_to_wmo(",
            "candidate.daily[i].month =",
            "candidate.daily[i].day =",
            "candidate.hourly[i].temperature =",
            "candidate.hourly[i].weather_code = qweather_icon_to_wmo(",
            "candidate.hourly[i].is_day = qweather_icon_is_day(",
            "candidate.hourly[i].has_precipitation =",
        ):
            self.assertIn(assignment, parser)
        self.assertEqual(parser.count("publish_weather_snapshot(candidate);"), 1)
        self.assertLess(
            parser.index("publish_weather_snapshot(candidate);"),
            parser.index("update_home_status(WEATHER_SOURCE_QWEATHER"),
        )
        self.assertIn("!hourly[i][\"pop\"].isNull()", parser)

    def test_qweather_daily_middle_missing_and_short_arrays_only_skip_that_point(self):
        parser = function_body(
            "void fetch_and_update_weather() {",
            "const lv_img_dsc_t* choose_image",
        )
        daily_loop = parser[
            parser.index("for (int i = 0; i < FORECAST_POINT_COUNT; i++)") :
            parser.index("if (valid_daily_points == 0)")
        ]
        self.assertIn("if (i >= daily.size())", daily_loop)
        self.assertGreaterEqual(daily_loop.count("continue;"), 2)
        self.assertNotIn("fetch_open_meteo_weather();", daily_loop)
        self.assertNotIn("return;", daily_loop)
        self.assertNotIn("daily.size() < FORECAST_POINT_COUNT", parser)

    def test_qweather_hourly_middle_missing_and_short_arrays_only_skip_that_point(self):
        parser = function_body(
            "void fetch_and_update_weather() {",
            "const lv_img_dsc_t* choose_image",
        )
        hourly_loop = parser[
            parser.index("for (int i = 0; i < FORECAST_POINT_COUNT; i++)", parser.index('/v7/weather/24h')) :
            parser.index("if (valid_hourly_points == 0)")
        ]
        self.assertIn("if (i >= hourly.size())", hourly_loop)
        self.assertGreaterEqual(hourly_loop.count("continue;"), 2)
        self.assertNotIn("fetch_open_meteo_weather();", hourly_loop)
        self.assertNotIn("return;", hourly_loop)
        self.assertNotIn("hourly.size() < FORECAST_POINT_COUNT", parser)

    def test_both_providers_parse_optional_current_humidity(self):
        open_meteo = function_body(
            "static void fetch_open_meteo_weather() {",
            "void fetch_and_update_weather()",
        )
        self.assertIn("relative_humidity_2m", open_meteo)
        self.assertIn("candidate.current.humidity =", open_meteo)
        self.assertIn("candidate.current.has_humidity =", open_meteo)

        qweather = function_body(
            "void fetch_and_update_weather() {",
            "const lv_img_dsc_t* choose_image",
        )
        self.assertIn('doc["now"]["humidity"]', qweather)
        self.assertIn("candidate.current.humidity =", qweather)
        self.assertIn("candidate.current.has_humidity =", qweather)

    def test_weather_parsers_do_not_write_forecast_widgets(self):
        parsers = WEATHER[
            WEATHER.index("static void fetch_open_meteo_weather() {") :
            WEATHER.index("const lv_img_dsc_t* choose_image")
        ]
        for forbidden in (
            "lv_label_set_text(lbl_daily_",
            "lv_label_set_text_fmt(lbl_daily_",
            "lv_label_set_text(lbl_hourly",
            "lv_label_set_text_fmt(lbl_hourly",
            "lv_label_set_text(lbl_precipitation_probability",
            "lv_label_set_text_fmt(lbl_precipitation_probability",
            "lv_img_set_src(img_daily",
            "lv_img_set_src(img_hourly",
        ):
            self.assertNotIn(forbidden, parsers)

    def test_portrait_renderer_covers_valid_invalid_and_missing_precipitation(self):
        self.assertIn("static void render_portrait_snapshot() {", WEATHER)
        renderer = function_body(
            "static void render_portrait_snapshot() {",
            "static void render_weather_snapshot()",
        )
        for symbol in (
            "current.valid",
            "weather_snapshot.daily[i]",
            "weather_snapshot.hourly[i]",
            "choose_image(current.weather_code, current.is_day)",
            "choose_icon(point.weather_code, 1)",
            "choose_icon(hourly_point.weather_code, hourly_point.is_day)",
            "point.has_precipitation",
            'lv_label_set_text(lbl_precipitation_probability[i], "")',
            'lv_label_set_text(lbl_daily_day[i], "--")',
            'lv_label_set_text(lbl_hourly[i], "--")',
        ):
            self.assertIn(symbol, renderer)
        self.assertIn('"%02u/%02u"', renderer)
        self.assertIn('"%.0f%%"', renderer)
        self.assertIn("strings->now", renderer)

    def test_weather_condition_name_maps_all_ten_localized_categories(self):
        self.assertIn("static const char *weather_condition_name(int code) {", WEATHER)
        mapping = function_body(
            "static const char *weather_condition_name(int code) {",
            "static void publish_weather_snapshot(",
        )
        self.assertIn("get_strings(current_language)->weather_conditions", mapping)
        for category in range(10):
            self.assertIn(f"conditions[{category}]", mapping)
        for code_range in (
            "case 0:", "case 1:", "case 2:", "case 3:",
            "case 45:", "case 48:", "code >= 51 && code <= 57",
            "case 61:", "case 63:", "case 80:", "case 81:",
            "case 65:", "case 82:", "case 66:", "case 67:",
            "code >= 71 && code <= 77", "case 85:", "case 86:",
            "case 95:", "case 96:", "case 99:",
        ):
            self.assertIn(code_range, mapping)
        self.assertRegex(mapping, r"default:\s*return conditions\[2\]")

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

    def test_settings_selects_and_persists_weather_provider(self):
        for symbol in (
            "weather_provider_dropdown",
            "strings->weather_provider",
            "strings->open_meteo_name",
            "strings->qweather_name",
            'prefs.putUInt("weatherProvider", weather_provider)',
            "lv_dropdown_get_selected(weather_provider_dropdown)",
        ):
            self.assertIn(symbol, WEATHER)

        handler = function_body(
            "static void settings_event_handler(lv_event_t *e) {",
            "static void configure_click_tone",
        )
        branch_start = handler.index("if (tgt == weather_provider_dropdown")
        branch_end = handler.index("if (tgt == unit_switch", branch_start)
        provider_branch = handler[branch_start:branch_end]
        self.assertNotIn("fetch_and_update_weather();", provider_branch)
        self.assertIn("weather_refresh_requested = true;", provider_branch)
        self.assertIn("WEATHER_PROVIDER_QWEATHER", provider_branch)
        self.assertIn("strlen(qweather_key) == 0", provider_branch)
        self.assertIn("open_qweather_config_portal();", provider_branch)

        loop = function_body("void loop() {", "static lv_color_t theme_color")
        self.assertIn("if (weather_refresh_requested)", loop)
        self.assertIn("weather_refresh_requested = false;", loop)
        self.assertIn("fetch_and_update_weather();", loop)
        self.assertLess(
            loop.index("process_qweather_config_portal();"),
            loop.index("if (weather_refresh_requested)"),
        )

    def test_weather_provider_label_is_localized(self):
        self.assertIn("const char* weather_provider;", TRANSLATIONS)
        self.assertIn('"Weather provider:"', TRANSLATIONS)
        self.assertIn('"天气源:"', TRANSLATIONS)

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

    def test_create_ui_uses_safe_placeholders_until_snapshot_rendering(self):
        create_ui = WEATHER[
            WEATHER.index("static void create_portrait_ui(lv_obj_t *scr) {") :
            WEATHER.index("void create_ui() {")
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
        self.assertNotIn("render_weather_snapshot();", create_ui)
        self.assertNotIn("fetch_and_update_weather();", create_ui)

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
