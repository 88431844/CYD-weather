#!/usr/bin/env python3
"""Host tests that compile and exercise the firmware's calibration helper."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "aura"


class TouchCalibrationMathTests(unittest.TestCase):
    def run_cpp(self, body):
        source = f'''#include "touch_calibration.h"
#include <cmath>
#include <iostream>

int main() {{
{body}
}}
'''
        with tempfile.TemporaryDirectory() as temp_dir:
            source_path = Path(temp_dir) / "test.cpp"
            binary_path = Path(temp_dir) / "test"
            source_path.write_text(source, encoding="ascii")
            subprocess.run(
                ["c++", "-std=c++17", "-Wall", "-Werror", "-I", str(HEADER_DIR),
                 str(source_path), "-o", str(binary_path)],
                check=True,
                capture_output=True,
                text=True,
            )
            return subprocess.run([str(binary_path)], check=True, capture_output=True, text=True)

    def test_fit_recovers_affine_transform_and_converts_points(self):
        result = self.run_cpp(r'''
  const TouchRawPoint raw[] = {{100, 100}, {3900, 100}, {2000, 2000}, {100, 3900}, {3900, 3900}};
  const TouchScreenPoint screen[] = {{64, 83}, {1964, -107}, {1204, 1508}, {444, 3123}, {2344, 2933}};
  TouchCalibration calibration{};
  if (!fit_touch_calibration(raw, screen, 5, &calibration)) return 1;
  if (std::fabs(calibration.a - 0.5f) > 1e-5f) return 2;
  if (std::fabs(calibration.b - 0.1f) > 1e-5f) return 3;
  if (std::fabs(calibration.c - 4.0f) > 1e-4f) return 4;
  if (std::fabs(calibration.d + 0.05f) > 1e-5f) return 5;
  if (std::fabs(calibration.e - 0.8f) > 1e-5f) return 6;
  if (std::fabs(calibration.f - 8.0f) > 1e-4f) return 7;
  int x = 0, y = 0;
  if (!apply_touch_calibration(calibration, 300, 150, 240, 320, &x, &y)) return 8;
  if (x != 169 || y != 113) return 9;
  if (!apply_touch_calibration(calibration, -50, 10, 240, 320, &x, &y)) return 10;
  if (x != 0 || y != 19) return 11;
  return 0;
''')
        self.assertEqual(result.returncode, 0)

    def test_fit_rejects_invalid_or_singular_samples(self):
        result = self.run_cpp(r'''
  const TouchScreenPoint screen[] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};
  TouchCalibration calibration{};
  const TouchRawPoint collinear[] = {{100, 100}, {200, 200}, {300, 300}, {400, 400}, {500, 500}};
  if (fit_touch_calibration(collinear, screen, 5, &calibration)) return 1;
  const TouchRawPoint invalid[] = {{100, 100}, {200, 200}, {300, 300}, {400, 400}, {5000, 500}};
  if (fit_touch_calibration(invalid, screen, 5, &calibration)) return 2;
  return 0;
''')
        self.assertEqual(result.returncode, 0)

    def test_invalid_calibration_reports_failure_for_runtime_fallback(self):
        result = self.run_cpp(r'''
  TouchCalibration calibration{};
  int x = 0, y = 0;
  if (apply_touch_calibration(calibration, 2000, 2000, 240, 320, &x, &y)) return 1;
  calibration.valid = true;
  if (apply_touch_calibration(calibration, 2000, 2000, 240, 320, &x, &y)) return 2;
  return 0;
''')
        self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
