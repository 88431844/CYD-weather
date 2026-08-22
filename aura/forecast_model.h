#ifndef AURA_FORECAST_MODEL_H
#define AURA_FORECAST_MODEL_H

#include <math.h>
#include <limits.h>
#include <stdint.h>

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
    *snapshot = WeatherSnapshot{};
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

  if (!isfinite(static_cast<double>(minimum)) ||
      !isfinite(static_cast<double>(maximum))) {
    return false;
  }

  if (minimum > maximum) {
    const float swapped = minimum;
    minimum = maximum;
    maximum = swapped;
  }

  const double padded_minimum_value =
      floor(static_cast<double>(minimum) - 2.0);
  const double padded_maximum_value =
      ceil(static_cast<double>(maximum) + 2.0);
  if (!isfinite(padded_minimum_value) || !isfinite(padded_maximum_value) ||
      padded_minimum_value < static_cast<double>(INT_MIN) ||
      padded_minimum_value > static_cast<double>(INT_MAX) ||
      padded_maximum_value < static_cast<double>(INT_MIN) ||
      padded_maximum_value > static_cast<double>(INT_MAX)) {
    return false;
  }

  int64_t padded_minimum = static_cast<int64_t>(padded_minimum_value);
  int64_t padded_maximum = static_cast<int64_t>(padded_maximum_value);
  const int64_t span = padded_maximum - padded_minimum;
  if (span < 4) {
    const int64_t required = 4 - span;
    if (padded_maximum + required <= static_cast<int64_t>(INT_MAX)) {
      padded_maximum += required;
    } else if (padded_minimum - required >= static_cast<int64_t>(INT_MIN)) {
      padded_minimum -= required;
    } else {
      return false;
    }
  }
  *out_minimum = static_cast<int>(padded_minimum);
  *out_maximum = static_cast<int>(padded_maximum);
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
    if (!isfinite(static_cast<double>(point.minimum)) ||
        !isfinite(static_cast<double>(point.maximum))) {
      return false;
    }
    float point_minimum = point.minimum;
    float point_maximum = point.maximum;
    if (point_minimum > point_maximum) {
      const float swapped = point_minimum;
      point_minimum = point_maximum;
      point_maximum = swapped;
    }
    if (!has_valid_point) {
      minimum = point_minimum;
      maximum = point_maximum;
      has_valid_point = true;
      continue;
    }
    if (point_minimum < minimum) {
      minimum = point_minimum;
    }
    if (point_maximum > maximum) {
      maximum = point_maximum;
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
    if (!isfinite(static_cast<double>(point.temperature))) {
      return false;
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
