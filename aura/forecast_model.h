#ifndef AURA_FORECAST_MODEL_H
#define AURA_FORECAST_MODEL_H

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static constexpr int FORECAST_POINT_COUNT = 7;
static constexpr double CHART_DISPLAY_TEMPERATURE_MIN = -200.0;
static constexpr double CHART_DISPLAY_TEMPERATURE_MAX = 200.0;
static constexpr int32_t CHART_POINT_NONE_VALUE = INT32_MAX;

struct CurrentConditions {
  float temperature;
  float feels_like;
  float humidity;
  int weather_code;
  bool is_day;
  bool has_humidity;
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

struct SolarTimes {
  char sunrise[6];
  char sunset[6];
  bool has_sunrise;
  bool has_sunset;
};

struct WeatherSnapshot {
  CurrentConditions current;
  DailyForecastPoint daily[FORECAST_POINT_COUNT];
  HourlyForecastPoint hourly[FORECAST_POINT_COUNT];
  SolarTimes solar;
};

static inline void clear_weather_snapshot(WeatherSnapshot *snapshot) {
  if (snapshot) {
    *snapshot = WeatherSnapshot{};
  }
}

static inline bool parse_hh_mm(const char *value, char output[6]) {
  if (!value || !output) return false;
  const char *clock = strchr(value, 'T');
  clock = clock ? clock + 1 : value;
  if (strlen(clock) < 5 || clock[2] != ':' ||
      clock[0] < '0' || clock[0] > '9' ||
      clock[1] < '0' || clock[1] > '9' ||
      clock[3] < '0' || clock[3] > '9' ||
      clock[4] < '0' || clock[4] > '9') {
    return false;
  }
  const int hour = (clock[0] - '0') * 10 + clock[1] - '0';
  const int minute = (clock[3] - '0') * 10 + clock[4] - '0';
  if (hour > 23 || minute > 59) return false;
  memcpy(output, clock, 5);
  output[5] = '\0';
  return true;
}

static inline bool safe_chart_temperature(
    float celsius,
    bool use_fahrenheit,
    float *out_display,
    int32_t *out_chart_value) {
  if (!out_display || !out_chart_value) {
    return false;
  }

  const double source = static_cast<double>(celsius);
  if (!isfinite(source)) {
    return false;
  }

  const double display = use_fahrenheit
      ? source * 9.0 / 5.0 + 32.0
      : source;
  if (!isfinite(display)) {
    return false;
  }
  if (display < CHART_DISPLAY_TEMPERATURE_MIN ||
      display > CHART_DISPLAY_TEMPERATURE_MAX) {
    return false;
  }

  const double rounded = round(display);
  if (!isfinite(rounded) ||
      rounded < CHART_DISPLAY_TEMPERATURE_MIN ||
      rounded > CHART_DISPLAY_TEMPERATURE_MAX) {
    return false;
  }
  if (rounded < static_cast<double>(INT32_MIN) ||
      rounded > static_cast<double>(INT32_MAX)) {
    return false;
  }

  const int32_t chart_value = static_cast<int32_t>(rounded);
  if (chart_value == CHART_POINT_NONE_VALUE) {
    return false;
  }

  *out_display = static_cast<float>(display);
  *out_chart_value = chart_value;
  return true;
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
