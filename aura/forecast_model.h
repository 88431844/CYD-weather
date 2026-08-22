#ifndef AURA_FORECAST_MODEL_H
#define AURA_FORECAST_MODEL_H

#include <math.h>
#include <stdint.h>
#include <string.h>

static constexpr int FORECAST_POINT_COUNT = 7;

struct CurrentConditions {
  float temperature;
  float feels_like;
  int weather_code;
  bool is_day;
  bool valid;
};

struct DailyForecastPoint {
  float minimum;
  float maximum;
  int weather_code;
  uint8_t month;
  uint8_t day;
  bool valid;
};

struct HourlyForecastPoint {
  float temperature;
  float precipitation_probability;
  int weather_code;
  uint8_t hour;
  bool is_day;
  bool has_precipitation;
  bool valid;
};

struct WeatherSnapshot {
  CurrentConditions current;
  DailyForecastPoint daily[FORECAST_POINT_COUNT];
  HourlyForecastPoint hourly[FORECAST_POINT_COUNT];
};

static inline void clear_weather_snapshot(WeatherSnapshot *snapshot) {
  if (snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
  }
}

static inline bool padded_chart_range(
    float minimum,
    float maximum,
    int *out_minimum,
    int *out_maximum) {
  if (!out_minimum || !out_maximum) {
    return false;
  }

  if (minimum > maximum) {
    const float swapped = minimum;
    minimum = maximum;
    maximum = swapped;
  }

  int padded_minimum = static_cast<int>(floorf(minimum - 2.0f));
  int padded_maximum = static_cast<int>(ceilf(maximum + 2.0f));
  if (padded_maximum - padded_minimum < 4) {
    padded_maximum = padded_minimum + 4;
  }
  *out_minimum = padded_minimum;
  *out_maximum = padded_maximum;
  return true;
}

static inline bool daily_chart_range(
    const WeatherSnapshot &snapshot,
    int *out_minimum,
    int *out_maximum) {
  if (!out_minimum || !out_maximum) {
    return false;
  }

  bool has_valid_point = false;
  float minimum = 0.0f;
  float maximum = 0.0f;
  for (int index = 0; index < FORECAST_POINT_COUNT; index++) {
    const DailyForecastPoint &point = snapshot.daily[index];
    if (!point.valid) {
      continue;
    }
    if (!has_valid_point) {
      minimum = point.minimum;
      maximum = point.maximum;
      has_valid_point = true;
      continue;
    }
    if (point.minimum < minimum) {
      minimum = point.minimum;
    }
    if (point.maximum > maximum) {
      maximum = point.maximum;
    }
  }
  return has_valid_point && padded_chart_range(
      minimum, maximum, out_minimum, out_maximum);
}

static inline bool hourly_chart_range(
    const WeatherSnapshot &snapshot,
    int *out_minimum,
    int *out_maximum) {
  if (!out_minimum || !out_maximum) {
    return false;
  }

  bool has_valid_point = false;
  float minimum = 0.0f;
  float maximum = 0.0f;
  for (int index = 0; index < FORECAST_POINT_COUNT; index++) {
    const HourlyForecastPoint &point = snapshot.hourly[index];
    if (!point.valid) {
      continue;
    }
    if (!has_valid_point) {
      minimum = point.temperature;
      maximum = point.temperature;
      has_valid_point = true;
      continue;
    }
    if (point.temperature < minimum) {
      minimum = point.temperature;
    }
    if (point.temperature > maximum) {
      maximum = point.temperature;
    }
  }
  return has_valid_point && padded_chart_range(
      minimum, maximum, out_minimum, out_maximum);
}

#endif  // AURA_FORECAST_MODEL_H
