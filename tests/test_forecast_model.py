#!/usr/bin/env python3
"""Host tests for the dependency-free seven-point weather snapshot model."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "aura"


class ForecastModelTests(unittest.TestCase):
    def run_cpp(self, body):
        source = f'''#include "forecast_model.h"
#include <cmath>

int main() {{
{body}
}}
'''
        with tempfile.TemporaryDirectory() as temp_dir:
            source_path = Path(temp_dir) / "test.cpp"
            binary_path = Path(temp_dir) / "test"
            source_path.write_text(source, encoding="ascii")
            compile_result = subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Werror",
                    "-I",
                    str(HEADER_DIR),
                    str(source_path),
                    "-o",
                    str(binary_path),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                compile_result.returncode,
                0,
                f"C++ compile failed:\n{compile_result.stderr}",
            )
            run_result = subprocess.run(
                [str(binary_path)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(
                run_result.returncode,
                0,
                f"C++ program failed:\n{run_result.stdout}\n{run_result.stderr}",
            )

    def test_clear_resets_every_field_and_accepts_null(self):
        self.run_cpp(r'''
  WeatherSnapshot snapshot = {
      {25.5f, 24.0f, 100, true, true},
      {{24.0f, 31.0f, 100, 8, 22, true}},
      {{25.5f, 20.0f, 100, 13, true, true, true}},
  };
  clear_weather_snapshot(&snapshot);
  if (snapshot.current.temperature != 0.0f ||
      snapshot.current.feels_like != 0.0f || snapshot.current.weather_code != 0 ||
      snapshot.current.is_day || snapshot.current.valid) return 1;
  for (int index = 0; index < FORECAST_POINT_COUNT; index++) {
    const DailyForecastPoint &daily = snapshot.daily[index];
    if (daily.minimum != 0.0f || daily.maximum != 0.0f ||
        daily.weather_code != 0 || daily.month != 0 || daily.day != 0 || daily.valid) return 2;
    const HourlyForecastPoint &hourly = snapshot.hourly[index];
    if (hourly.temperature != 0.0f || hourly.precipitation_probability != 0.0f ||
        hourly.weather_code != 0 || hourly.hour != 0 || hourly.is_day ||
        hourly.has_precipitation || hourly.valid) return 3;
  }
  clear_weather_snapshot(nullptr);
  return 0;
''')

    def test_structs_support_public_field_order_aggregate_initialization(self):
        self.run_cpp(r'''
  CurrentConditions current = {20.5f, 19.0f, 100, true, true};
  DailyForecastPoint daily = {12.0f, 25.0f, 101, 8, 22, true};
  HourlyForecastPoint hourly = {20.5f, 30.0f, 100, 7, true, true, true};
  WeatherSnapshot snapshot = {current, {daily}, {hourly}};
  if (snapshot.current.temperature != 20.5f || snapshot.current.feels_like != 19.0f ||
      snapshot.current.weather_code != 100 || !snapshot.current.is_day ||
      !snapshot.current.valid) return 1;
  if (snapshot.daily[0].minimum != 12.0f || snapshot.daily[0].maximum != 25.0f ||
      snapshot.daily[0].weather_code != 101 || snapshot.daily[0].month != 8 ||
      snapshot.daily[0].day != 22 || !snapshot.daily[0].valid) return 2;
  if (snapshot.hourly[0].temperature != 20.5f ||
      snapshot.hourly[0].precipitation_probability != 30.0f ||
      snapshot.hourly[0].weather_code != 100 || snapshot.hourly[0].hour != 7 ||
      !snapshot.hourly[0].is_day || !snapshot.hourly[0].has_precipitation ||
      !snapshot.hourly[0].valid) return 3;
  return 0;
''')

    def test_daily_range_adds_two_degrees_to_valid_minimum_and_maximum(self):
        self.run_cpp(r'''
  WeatherSnapshot snapshot = {};
  snapshot.daily[0] = {26.0f, 30.0f, 100, 8, 22, true};
  snapshot.daily[1] = {24.0f, 31.0f, 100, 8, 23, true};
  int minimum = 0;
  int maximum = 0;
  if (!daily_chart_range(snapshot, &minimum, &maximum)) return 1;
  if (minimum != 22 || maximum != 33) return 2;
  return 0;
''')

    def test_hourly_range_uses_valid_temperatures(self):
        self.run_cpp(r'''
  WeatherSnapshot snapshot = {};
  snapshot.hourly[0] = {29.0f, 0.0f, 100, 8, true, false, true};
  snapshot.hourly[1] = {32.0f, 50.0f, 100, 9, true, true, true};
  int minimum = 0;
  int maximum = 0;
  if (!hourly_chart_range(snapshot, &minimum, &maximum)) return 1;
  if (minimum != 27 || maximum != 34) return 2;
  return 0;
''')

    def test_negative_daily_range_uses_floor_and_ceil(self):
        self.run_cpp(r'''
  WeatherSnapshot snapshot = {};
  snapshot.daily[0] = {-6.0f, -1.0f, 100, 1, 1, true};
  int minimum = 0;
  int maximum = 0;
  if (!daily_chart_range(snapshot, &minimum, &maximum)) return 1;
  if (minimum != -8 || maximum != 1) return 2;
  return 0;
''')

    def test_invalid_points_and_null_outputs_are_ignored_or_rejected(self):
        self.run_cpp(r'''
  WeatherSnapshot snapshot = {};
  snapshot.daily[0] = {-100.0f, 100.0f, 100, 1, 1, false};
  snapshot.daily[1] = {10.0f, 12.0f, 100, 1, 2, true};
  snapshot.hourly[0] = {-100.0f, 0.0f, 100, 1, true, false, false};
  snapshot.hourly[1] = {10.0f, 0.0f, 100, 2, true, false, true};
  int minimum = 0;
  int maximum = 0;
  if (!daily_chart_range(snapshot, &minimum, &maximum) || minimum != 8 || maximum != 14) return 1;
  if (!hourly_chart_range(snapshot, &minimum, &maximum) || minimum != 8 || maximum != 12) return 2;
  if (daily_chart_range(snapshot, nullptr, &maximum)) return 3;
  if (daily_chart_range(snapshot, &minimum, nullptr)) return 4;
  if (hourly_chart_range(snapshot, nullptr, &maximum)) return 5;
  if (hourly_chart_range(snapshot, &minimum, nullptr)) return 6;
  WeatherSnapshot empty = {};
  if (daily_chart_range(empty, &minimum, &maximum)) return 7;
  if (hourly_chart_range(empty, &minimum, &maximum)) return 8;
  return 0;
''')

    def test_padded_range_rejects_null_outputs_and_guarantees_four_degree_span(self):
        self.run_cpp(r'''
  int minimum = 0;
  int maximum = 0;
  if (!padded_chart_range(10.0f, 10.0f, &minimum, &maximum)) return 1;
  if (minimum != 8 || maximum != 12 || maximum - minimum < 4) return 2;
  if (!padded_chart_range(10.1f, 10.2f, &minimum, &maximum)) return 3;
  if (maximum - minimum < 4) return 4;
  if (padded_chart_range(10.0f, 10.0f, nullptr, &maximum)) return 5;
  if (padded_chart_range(10.0f, 10.0f, &minimum, nullptr)) return 6;
  return 0;
''')


if __name__ == "__main__":
    unittest.main()
