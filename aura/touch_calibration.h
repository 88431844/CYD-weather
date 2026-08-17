#ifndef TOUCH_CALIBRATION_H
#define TOUCH_CALIBRATION_H

#include <math.h>
#include <stddef.h>

struct TouchRawPoint {
  float x;
  float y;
};

struct TouchScreenPoint {
  float x;
  float y;
};

struct TouchCalibration {
  float a;
  float b;
  float c;
  float d;
  float e;
  float f;
  bool valid;
};

static inline bool touch_calibration_finite(float value) {
  return isfinite(value);
}

static inline bool solve_touch_calibration_system(
    double matrix[3][4], double result[3]) {
  double scale = 0.0;
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 4; column++) {
      double magnitude = fabs(matrix[row][column]);
      if (magnitude > scale) scale = magnitude;
    }
  }
  if (scale <= 0.0) return false;

  for (int column = 0; column < 3; column++) {
    int pivot = column;
    for (int row = column + 1; row < 3; row++) {
      if (fabs(matrix[row][column]) > fabs(matrix[pivot][column])) {
        pivot = row;
      }
    }

    if (fabs(matrix[pivot][column]) <= scale * 1e-12) return false;
    if (pivot != column) {
      for (int item = column; item < 4; item++) {
        double temporary = matrix[column][item];
        matrix[column][item] = matrix[pivot][item];
        matrix[pivot][item] = temporary;
      }
    }

    for (int row = column + 1; row < 3; row++) {
      double factor = matrix[row][column] / matrix[column][column];
      for (int item = column; item < 4; item++) {
        matrix[row][item] -= factor * matrix[column][item];
      }
    }
  }

  for (int row = 2; row >= 0; row--) {
    double value = matrix[row][3];
    for (int column = row + 1; column < 3; column++) {
      value -= matrix[row][column] * result[column];
    }
    result[row] = value / matrix[row][row];
    if (!isfinite(result[row])) return false;
  }
  return true;
}

static inline bool fit_touch_calibration(
    const TouchRawPoint *raw,
    const TouchScreenPoint *screen,
    size_t count,
    TouchCalibration *out) {
  if (!raw || !screen || !out || count < 3) return false;

  double normal_x[3][4] = {};
  double normal_y[3][4] = {};
  for (size_t i = 0; i < count; i++) {
    if (!touch_calibration_finite(raw[i].x) ||
        !touch_calibration_finite(raw[i].y) ||
        raw[i].x < 0.0f || raw[i].x > 4095.0f ||
        raw[i].y < 0.0f || raw[i].y > 4095.0f ||
        !touch_calibration_finite(screen[i].x) ||
        !touch_calibration_finite(screen[i].y)) {
      return false;
    }

    const double values[3] = {raw[i].x, raw[i].y, 1.0};
    for (int row = 0; row < 3; row++) {
      for (int column = 0; column < 3; column++) {
        normal_x[row][column] += values[row] * values[column];
        normal_y[row][column] += values[row] * values[column];
      }
      normal_x[row][3] += values[row] * screen[i].x;
      normal_y[row][3] += values[row] * screen[i].y;
    }
  }

  double x_coefficients[3] = {};
  double y_coefficients[3] = {};
  if (!solve_touch_calibration_system(normal_x, x_coefficients) ||
      !solve_touch_calibration_system(normal_y, y_coefficients)) {
    return false;
  }

  TouchCalibration candidate = {
      static_cast<float>(x_coefficients[0]),
      static_cast<float>(x_coefficients[1]),
      static_cast<float>(x_coefficients[2]),
      static_cast<float>(y_coefficients[0]),
      static_cast<float>(y_coefficients[1]),
      static_cast<float>(y_coefficients[2]),
      true};
  if (!touch_calibration_finite(candidate.a) ||
      !touch_calibration_finite(candidate.b) ||
      !touch_calibration_finite(candidate.c) ||
      !touch_calibration_finite(candidate.d) ||
      !touch_calibration_finite(candidate.e) ||
      !touch_calibration_finite(candidate.f) ||
      fabs(static_cast<double>(candidate.a) * candidate.e -
           static_cast<double>(candidate.b) * candidate.d) <= 1e-8) {
    return false;
  }

  *out = candidate;
  return true;
}

static inline bool apply_touch_calibration(
    const TouchCalibration &calibration,
    int raw_x,
    int raw_y,
    int width,
    int height,
    int *screen_x,
    int *screen_y) {
  if (!calibration.valid || !screen_x || !screen_y || width <= 0 || height <= 0 ||
      !touch_calibration_finite(calibration.a) ||
      !touch_calibration_finite(calibration.b) ||
      !touch_calibration_finite(calibration.c) ||
      !touch_calibration_finite(calibration.d) ||
      !touch_calibration_finite(calibration.e) ||
      !touch_calibration_finite(calibration.f) ||
      fabs(static_cast<double>(calibration.a) * calibration.e -
           static_cast<double>(calibration.b) * calibration.d) <= 1e-8) {
    return false;
  }

  float converted_x = calibration.a * raw_x + calibration.b * raw_y + calibration.c;
  float converted_y = calibration.d * raw_x + calibration.e * raw_y + calibration.f;
  if (!touch_calibration_finite(converted_x) || !touch_calibration_finite(converted_y)) {
    return false;
  }

  int x = static_cast<int>(lroundf(converted_x));
  int y = static_cast<int>(lroundf(converted_y));
  *screen_x = x < 0 ? 0 : (x >= width ? width - 1 : x);
  *screen_y = y < 0 ? 0 : (y >= height ? height - 1 : y);
  return true;
}

#endif  // TOUCH_CALIBRATION_H
