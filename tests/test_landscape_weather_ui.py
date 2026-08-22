#!/usr/bin/env python3
"""横屏天气折线图的静态集成契约。"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
WEATHER = (ROOT / "aura" / "weather.ino").read_text(encoding="utf-8")
FORECAST_MODEL = (ROOT / "aura" / "forecast_model.h").read_text(encoding="utf-8")


def function_body(signature: str, next_signature: str) -> str:
    start = WEATHER.index(signature)
    return WEATHER[start : WEATHER.index(next_signature, start)]


class LandscapeWeatherUiTests(unittest.TestCase):
    def test_landscape_uses_stable_320_by_240_root_layout(self):
        for declaration in (
            "static constexpr int LANDSCAPE_HEADER_HEIGHT = 58;",
            "static constexpr int LANDSCAPE_CHART_X = 6;",
            "static constexpr int LANDSCAPE_CHART_Y = 62;",
            "static constexpr int LANDSCAPE_CHART_WIDTH = 308;",
            "static constexpr int LANDSCAPE_CHART_HEIGHT = 108;",
            "static constexpr int LANDSCAPE_COLUMN_Y = 174;",
            "static constexpr int LANDSCAPE_COLUMN_WIDTH = 44;",
        ):
            self.assertIn(declaration, WEATHER)

        landscape = function_body(
            "static void create_landscape_ui(lv_obj_t *scr) {",
            "void create_ui()",
        )
        self.assertIn("apply_root_theme(scr);", landscape)
        self.assertIn("create_landscape_header(scr);", landscape)
        self.assertIn("create_forecast_segmented_control(scr);", landscape)
        self.assertIn("create_daily_chart(scr);", landscape)
        self.assertIn("create_hourly_chart(scr);", landscape)
        self.assertIn("set_forecast_view(active_forecast_view);", landscape)
        self.assertNotIn("lv_obj_create(scr)", landscape)

    def test_landscape_has_two_line_daily_and_one_line_hourly_charts(self):
        landscape = function_body(
            "static void create_daily_chart(lv_obj_t *scr) {",
            "void create_ui()",
        )
        self.assertEqual(landscape.count("lv_chart_create(scr)"), 2)
        for series in (
            "daily_high_series",
            "daily_low_series",
            "hourly_temperature_series",
        ):
            self.assertIn(series, landscape)
        self.assertEqual(landscape.count("lv_chart_set_point_count"), 2)
        self.assertGreaterEqual(landscape.count("FORECAST_POINT_COUNT"), 5)
        for values in (
            "daily_high_values",
            "daily_low_values",
            "hourly_temperature_values",
        ):
            self.assertRegex(
                WEATHER,
                rf"static\s+int32_t\s+{values}\[FORECAST_POINT_COUNT\]",
            )
            self.assertIn(f"lv_chart_set_ext_y_array", landscape)
            self.assertIn(values, landscape)
        self.assertIn("theme_palette(current_theme).high_temperature", landscape)
        self.assertIn("theme_palette(current_theme).low_temperature", landscape)
        self.assertIn("theme_palette(current_theme).accent", landscape)

    def test_external_chart_arrays_start_without_fake_zero_degree_points(self):
        landscape = function_body(
            "static void create_landscape_ui(lv_obj_t *scr) {",
            "void create_ui()",
        )
        initialization = (
            "daily_high_values[i] = LV_CHART_POINT_NONE;\n"
            "    daily_low_values[i] = LV_CHART_POINT_NONE;\n"
            "    hourly_temperature_values[i] = LV_CHART_POINT_NONE;"
        )
        self.assertIn(initialization, landscape)
        self.assertLess(
            landscape.index(initialization),
            landscape.index("create_daily_chart(scr);"),
        )

    def test_create_and_render_dispatch_by_rotation_geometry(self):
        create = function_body("void create_ui() {", "void populate_results_dropdown")
        self.assertIn("geometry_for_rotation(current_rotation).landscape", create)
        self.assertIn("create_landscape_ui(scr);", create)
        self.assertIn("create_portrait_ui(scr);", create)

        render = function_body(
            "static void render_weather_snapshot() {",
            "String urlencode",
        )
        self.assertIn("geometry_for_rotation(current_rotation).landscape", render)
        self.assertIn("render_landscape_snapshot();", render)
        self.assertIn("render_portrait_snapshot();", render)

    def test_header_has_current_weather_status_segmented_controls_and_settings(self):
        header = function_body(
            "static void create_landscape_header(lv_obj_t *scr) {",
            "static void create_forecast_segmented_control",
        )
        for symbol in (
            "lbl_home_location",
            "lbl_network_status",
            "lbl_update_status",
            "lbl_today_temp",
            "landscape_current_condition",
            "lbl_today_feels_like",
        ):
            self.assertIn(symbol, header)
        segmented = function_body(
            "static void create_forecast_segmented_control(lv_obj_t *scr) {",
            "static void create_daily_chart",
        )
        self.assertIn("strings->daily_tab", segmented)
        self.assertIn("strings->hourly_tab", segmented)
        self.assertNotIn("strings->seven_day_forecast", segmented)
        self.assertNotIn("strings->hourly_forecast", segmented)
        self.assertIn("select_daily_cb", segmented)
        self.assertIn("select_hourly_cb", segmented)
        self.assertIn("LV_SYMBOL_SETTINGS", segmented)
        self.assertIn("screen_event_cb", segmented)

    def test_each_forecast_column_has_temperature_icon_condition_and_fixed_size(self):
        for symbol in (
            "landscape_daily_dates",
            "landscape_daily_icons",
            "landscape_daily_conditions",
            "daily_high_labels",
            "daily_low_labels",
            "landscape_hourly_times",
            "landscape_hourly_icons",
            "landscape_hourly_conditions",
            "hourly_temperature_labels",
        ):
            self.assertRegex(
                WEATHER,
                rf"{symbol}\[FORECAST_POINT_COUNT\]",
            )
        columns = function_body(
            "static void create_landscape_forecast_columns(lv_obj_t *scr) {",
            "static void create_landscape_ui",
        )
        self.assertIn("lv_obj_set_size(daily_high_labels[i], 34, 13)", columns)
        self.assertIn("lv_obj_set_size(daily_low_labels[i], 34, 13)", columns)
        self.assertIn("lv_obj_set_size(hourly_temperature_labels[i], 34, 13)", columns)
        self.assertRegex(
            columns,
            r"lv_label_set_long_mode\(\s*landscape_daily_conditions\[i\],\s*LV_LABEL_LONG_DOT\)",
        )
        self.assertRegex(
            columns,
            r"lv_label_set_long_mode\(\s*landscape_hourly_conditions\[i\],\s*LV_LABEL_LONG_DOT\)",
        )
        self.assertIn(
            "lv_obj_set_size(landscape_hourly_conditions[i], 42, 28)",
            columns,
        )

    def test_renderer_converts_chart_values_ranges_and_labels_to_same_unit(self):
        for contract in (
            "CHART_DISPLAY_TEMPERATURE_MIN",
            "CHART_DISPLAY_TEMPERATURE_MAX",
            "CHART_POINT_NONE_VALUE",
            "static inline bool safe_chart_temperature(",
            "source * 9.0 / 5.0 + 32.0",
            "round(display)",
        ):
            self.assertIn(contract, FORECAST_MODEL)
        self.assertGreaterEqual(FORECAST_MODEL.count("isfinite("), 3)
        self.assertIn("static_cast<double>(INT32_MIN)", FORECAST_MODEL)
        self.assertIn("static_cast<double>(INT32_MAX)", FORECAST_MODEL)
        self.assertIn(
            "static_assert(CHART_POINT_NONE_VALUE == LV_CHART_POINT_NONE",
            WEATHER,
        )

        preparation = function_body(
            "static void prepare_landscape_chart_points() {",
            "static bool daily_display_chart_range",
        )
        self.assertIn("daily_point_renderable[i] =", preparation)
        self.assertGreaterEqual(preparation.count("safe_chart_temperature("), 3)
        self.assertGreaterEqual(preparation.count("use_fahrenheit"), 3)
        self.assertIn("hourly_point_renderable[i] =", preparation)

        daily_range = function_body(
            "static bool daily_display_chart_range",
            "static bool hourly_display_chart_range",
        )
        hourly_range = function_body(
            "static bool hourly_display_chart_range",
            "static void render_landscape_snapshot",
        )
        self.assertIn("daily_point_renderable[i]", daily_range)
        self.assertIn("hourly_point_renderable[i]", hourly_range)
        self.assertIn("padded_chart_range", daily_range)
        self.assertIn("padded_chart_range", hourly_range)

        renderer = function_body(
            "static void render_landscape_snapshot() {",
            "static void set_object_hidden(",
        )
        self.assertIn("prepare_landscape_chart_points();", renderer)
        for renderable in ("daily_point_renderable[i]", "hourly_point_renderable[i]"):
            self.assertIn(renderable, renderer)
        self.assertNotIn("lroundf(", renderer)
        self.assertIn("daily_display_chart_range(&range_min, &range_max)", renderer)
        self.assertIn("hourly_display_chart_range(&range_min, &range_max)", renderer)
        self.assertEqual(renderer.count("lv_chart_set_range"), 2)

    def test_range_failure_invalidates_entire_chart_view_before_positioning(self):
        daily_invalidation = function_body(
            "static void invalidate_daily_chart_points() {",
            "static void invalidate_hourly_chart_points",
        )
        hourly_invalidation = function_body(
            "static void invalidate_hourly_chart_points() {",
            "static void render_landscape_snapshot",
        )
        self.assertIn("daily_point_renderable[i] = false", daily_invalidation)
        self.assertGreaterEqual(daily_invalidation.count("LV_CHART_POINT_NONE"), 2)
        self.assertIn("hourly_point_renderable[i] = false", hourly_invalidation)
        self.assertIn("LV_CHART_POINT_NONE", hourly_invalidation)

        renderer = function_body(
            "static void render_landscape_snapshot() {",
            "static void set_object_hidden(",
        )
        self.assertRegex(
            renderer,
            r"if \(daily_display_chart_range\([\s\S]*?else \{\s*invalidate_daily_chart_points\(\);",
        )
        self.assertRegex(
            renderer,
            r"if \(hourly_display_chart_range\([\s\S]*?else \{\s*invalidate_hourly_chart_points\(\);",
        )
        self.assertLess(
            renderer.index("invalidate_hourly_chart_points();"),
            renderer.index("position_chart_temperature_labels();"),
        )

    def test_renderer_hides_entire_invalid_groups_and_marks_missing_chart_points(self):
        renderer = function_body(
            "static void render_landscape_snapshot() {",
            "static void set_object_hidden(",
        )
        self.assertGreaterEqual(renderer.count("weather_condition_name("), 2)
        preparation = function_body(
            "static void prepare_landscape_chart_points() {",
            "static bool daily_display_chart_range",
        )
        self.assertGreaterEqual(preparation.count("LV_CHART_POINT_NONE"), 5)
        for symbol in (
            "landscape_daily_dates",
            "landscape_daily_icons",
            "landscape_daily_conditions",
            "daily_high_labels",
            "daily_low_labels",
            "landscape_hourly_times",
            "landscape_hourly_icons",
            "landscape_hourly_conditions",
            "hourly_temperature_labels",
        ):
            renderable = (
                "daily_point_renderable[i]"
                if symbol.startswith("landscape_daily") or symbol.startswith("daily_")
                else "hourly_point_renderable[i]"
            )
            self.assertRegex(
                renderer,
                rf"set_object_hidden\(\s*{symbol}\[i\],\s*!{re.escape(renderable)}\)",
            )
        self.assertIn("hourly.has_precipitation", renderer)
        self.assertIn('"%s\\n%.0f%%"', renderer)

    def test_chart_labels_add_chart_origin_and_clamp_to_display(self):
        placement = function_body(
            "static void place_chart_label(",
            "static void position_chart_temperature_labels",
        )
        self.assertIn("lv_chart_get_point_pos_by_id", placement)
        self.assertIn("LANDSCAPE_CHART_X + point.x", placement)
        self.assertIn("LANDSCAPE_CHART_Y + point.y", placement)
        self.assertIn("constrain(", placement)
        self.assertIn("display_width()", placement)
        self.assertIn("display_height()", placement)
        self.assertRegex(
            placement,
            r"constrain\([\s\S]*LANDSCAPE_HEADER_HEIGHT\s*,\s*maximum_y\)",
        )

        positions = function_body(
            "static void position_chart_temperature_labels() {",
            "static void render_weather_snapshot",
        )
        self.assertIn("daily_point_renderable[i]", positions)
        self.assertIn("hourly_point_renderable[i]", positions)
        self.assertNotIn("weather_snapshot.daily[i].valid", positions)
        self.assertNotIn("weather_snapshot.hourly[i].valid", positions)

    def test_forecast_view_hides_unsafe_points_even_when_snapshot_marks_them_valid(self):
        view = function_body(
            "static void set_forecast_view(ForecastView view) {",
            "static void select_daily_cb",
        )
        self.assertIn("daily_point_renderable[i]", view)
        self.assertIn("hourly_point_renderable[i]", view)
        self.assertNotIn("weather_snapshot.daily[i].valid", view)
        self.assertNotIn("weather_snapshot.hourly[i].valid", view)

    def test_landscape_segment_buttons_select_instead_of_toggle(self):
        daily = function_body("static void select_daily_cb", "static void select_hourly_cb")
        hourly = function_body("static void select_hourly_cb", "static void create_landscape_header")
        self.assertIn("active_forecast_view = FORECAST_DAILY;", daily)
        self.assertNotIn("FORECAST_HOURLY", daily)
        self.assertIn("active_forecast_view = FORECAST_HOURLY;", hourly)
        self.assertNotIn("FORECAST_DAILY", hourly)
        for callback in (daily, hourly):
            self.assertIn("play_click_sound();", callback)
            self.assertIn("set_forecast_view(active_forecast_view);", callback)
            self.assertNotIn("fetch_and_update_weather", callback)


if __name__ == "__main__":
    unittest.main()
