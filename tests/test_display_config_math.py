#!/usr/bin/env python3
"""Host tests for display rotation geometry, touch mapping, and theme palettes."""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "aura"


class DisplayConfigMathTests(unittest.TestCase):
    def run_checked(self, command, phase):
        try:
            return subprocess.run(
                command,
                check=True,
                capture_output=True,
                text=True,
            )
        except subprocess.CalledProcessError as error:
            self.fail(
                f"{phase}失败（退出码 {error.returncode}）\n"
                f"stdout:\n{error.stdout}\n"
                f"stderr:\n{error.stderr}"
            )

    def run_cpp(self, body):
        source = f'''#include "display_config.h"
#include <cstdint>

int main() {{
{body}
}}
'''
        with tempfile.TemporaryDirectory() as temp_dir:
            source_path = Path(temp_dir) / "test.cpp"
            binary_path = Path(temp_dir) / "test"
            source_path.write_text(source, encoding="ascii")
            self.run_checked(
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
                "C++ 编译",
            )
            return self.run_checked([str(binary_path)], "C++ 程序运行")

    def test_invalid_values_fall_back_to_default_rotation_and_theme(self):
        result = self.run_cpp(r'''
  if (validated_rotation(99) != SCREEN_ROTATION_0) return 1;
  if (validated_theme(99) != THEME_DEEP_SEA) return 2;
  if (SCREEN_ROTATION_COUNT != 4 || THEME_COUNT != 5) return 3;
  return 0;
''')
        self.assertEqual(result.returncode, 0)

    def test_rotation_geometry_uses_portrait_or_landscape_logical_dimensions(self):
        result = self.run_cpp(r'''
  if (PORTRAIT_WIDTH != 240 || PORTRAIT_HEIGHT != 320) return 1;
  const DisplayGeometry portrait = geometry_for_rotation(SCREEN_ROTATION_0);
  if (portrait.width != 240 || portrait.height != 320 || portrait.landscape) return 2;
  const DisplayGeometry upside_down = geometry_for_rotation(SCREEN_ROTATION_180);
  if (upside_down.width != 240 || upside_down.height != 320 || upside_down.landscape) return 3;
  const DisplayGeometry right = geometry_for_rotation(SCREEN_ROTATION_90);
  if (right.width != 320 || right.height != 240 || !right.landscape) return 4;
  const DisplayGeometry left = geometry_for_rotation(SCREEN_ROTATION_270);
  if (left.width != 320 || left.height != 240 || !left.landscape) return 5;
  return 0;
''')
        self.assertEqual(result.returncode, 0)

    def test_rotate_portrait_touch_maps_corners_and_center_in_all_directions(self):
        result = self.run_cpp(r'''
  const int points[][2] = {{0, 0}, {239, 319}, {120, 160}};
  const int expected[][3][2] = {
      {{0, 0}, {239, 319}, {120, 160}},
      {{319, 0}, {0, 239}, {159, 120}},
      {{239, 319}, {0, 0}, {119, 159}},
      {{0, 239}, {319, 0}, {160, 119}},
  };
  const ScreenRotation rotations[] = {
      SCREEN_ROTATION_0, SCREEN_ROTATION_90,
      SCREEN_ROTATION_180, SCREEN_ROTATION_270};
  const int point_count = sizeof(points) / sizeof(points[0]);
  for (int rotation = 0; rotation < SCREEN_ROTATION_COUNT; rotation++) {
    for (int point = 0; point < point_count; point++) {
      int x = -1;
      int y = -1;
      if (!rotate_portrait_touch(rotations[rotation], points[point][0],
                                 points[point][1], &x, &y)) return 1;
      if (x != expected[rotation][point][0] ||
          y != expected[rotation][point][1]) return 2;
    }
  }
  return 0;
''')
        self.assertEqual(result.returncode, 0)

    def test_rotate_portrait_touch_rejects_out_of_range_input_and_null_outputs(self):
        result = self.run_cpp(r'''
  int x = 1234;
  int y = 5678;
  if (rotate_portrait_touch(static_cast<ScreenRotation>(99), 0, 0, &x, &y)) {
    if (x != 0 || y != 0) return 1;
  } else {
    return 2;
  }
  x = 1234;
  y = 5678;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, -1, 0, &x, &y)) return 3;
  if (x != 1234 || y != 5678) return 4;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, 240, 0, &x, &y)) return 5;
  if (x != 1234 || y != 5678) return 6;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, 0, -1, &x, &y)) return 7;
  if (x != 1234 || y != 5678) return 8;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, 0, 320, &x, &y)) return 9;
  if (x != 1234 || y != 5678) return 10;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, 0, 0, nullptr, &y)) return 11;
  if (y != 5678) return 12;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, 0, 0, &x, nullptr)) return 13;
  if (x != 1234) return 14;
  return 0;
''')
        self.assertEqual(result.returncode, 0)

    def test_all_theme_palettes_match_approved_color_tokens(self):
        result = self.run_cpp(r'''
  const ThemePalette expected[] = {
      {0x101820u, 0x1B2932u, 0xF8FBFCu, 0x8FA5AFu, 0x30434Du, 0xFFD25Fu, 0x63C6FFu, 0x73E1D5u},
      {0xF6FAFBu, 0xE5EEF1u, 0x18333Du, 0x647981u, 0xCCDADDu, 0xDF633Du, 0x197FADu, 0x087B73u},
      {0x132019u, 0x21362Cu, 0xF2F8F4u, 0x9AB5A6u, 0x395246u, 0xFFC857u, 0x7BCBE6u, 0x65D49Eu},
      {0x2B1B29u, 0x462839u, 0xFFF6F7u, 0xC8A8B4u, 0x614052u, 0xFFBA62u, 0x6FD1D8u, 0xFF7C79u},
      {0x050606u, 0x202323u, 0xFFFFFFu, 0xC8CCCCu, 0x4B5151u, 0xFFE100u, 0x00D9FFu, 0xFFFFFFu},
  };
  for (int theme = 0; theme < 5; theme++) {
    const ThemePalette &actual = theme_palette(static_cast<ThemeId>(theme));
    const uint32_t actual_values[] = {
        actual.background, actual.panel, actual.text, actual.muted,
        actual.grid, actual.high_temperature, actual.low_temperature,
        actual.accent};
    const uint32_t expected_values[] = {
        expected[theme].background, expected[theme].panel, expected[theme].text,
        expected[theme].muted, expected[theme].grid,
        expected[theme].high_temperature, expected[theme].low_temperature,
        expected[theme].accent};
    for (int color = 0; color < 8; color++) {
      if (actual_values[color] != expected_values[color]) return 1;
    }
  }
  if (theme_palette(static_cast<ThemeId>(99)).background != 0x101820u) return 2;
  return 0;
''')
        self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
