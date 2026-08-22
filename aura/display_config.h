#ifndef AURA_DISPLAY_CONFIG_H
#define AURA_DISPLAY_CONFIG_H

#include <stdint.h>

constexpr int PORTRAIT_WIDTH = 240;
constexpr int PORTRAIT_HEIGHT = 320;

enum ScreenRotation : uint8_t {
  SCREEN_ROTATION_0 = 0,
  SCREEN_ROTATION_90 = 1,
  SCREEN_ROTATION_180 = 2,
  SCREEN_ROTATION_270 = 3,
  SCREEN_ROTATION_COUNT = 4,
};

enum ThemeId : uint8_t {
  THEME_DEEP_SEA = 0,
  THEME_CLEAR_SKY = 1,
  THEME_RAINFOREST = 2,
  THEME_SUNSET = 3,
  THEME_HIGH_CONTRAST = 4,
  THEME_COUNT = 5,
};

struct DisplayGeometry {
  int width;
  int height;
  bool landscape;
};

struct ThemePalette {
  uint32_t background;
  uint32_t panel;
  uint32_t text;
  uint32_t muted;
  uint32_t grid;
  uint32_t high_temperature;
  uint32_t low_temperature;
  uint32_t accent;
};

static inline ScreenRotation validated_rotation(uint32_t value) {
  switch (value) {
    case SCREEN_ROTATION_0:
      return SCREEN_ROTATION_0;
    case SCREEN_ROTATION_90:
      return SCREEN_ROTATION_90;
    case SCREEN_ROTATION_180:
      return SCREEN_ROTATION_180;
    case SCREEN_ROTATION_270:
      return SCREEN_ROTATION_270;
    default:
      return SCREEN_ROTATION_0;
  }
}

static inline ThemeId validated_theme(uint32_t value) {
  switch (value) {
    case THEME_DEEP_SEA:
      return THEME_DEEP_SEA;
    case THEME_CLEAR_SKY:
      return THEME_CLEAR_SKY;
    case THEME_RAINFOREST:
      return THEME_RAINFOREST;
    case THEME_SUNSET:
      return THEME_SUNSET;
    case THEME_HIGH_CONTRAST:
      return THEME_HIGH_CONTRAST;
    default:
      return THEME_DEEP_SEA;
  }
}

static inline DisplayGeometry geometry_for_rotation(ScreenRotation rotation) {
  const ScreenRotation validated = validated_rotation(static_cast<uint32_t>(rotation));
  if (validated == SCREEN_ROTATION_90 || validated == SCREEN_ROTATION_270) {
    return {PORTRAIT_HEIGHT, PORTRAIT_WIDTH, true};
  }
  return {PORTRAIT_WIDTH, PORTRAIT_HEIGHT, false};
}

static inline bool rotate_portrait_touch(
    ScreenRotation rotation,
    int px,
    int py,
    int *rotated_x,
    int *rotated_y) {
  if (!rotated_x || !rotated_y || px < 0 || px >= PORTRAIT_WIDTH ||
      py < 0 || py >= PORTRAIT_HEIGHT) {
    return false;
  }

  switch (validated_rotation(static_cast<uint32_t>(rotation))) {
    case SCREEN_ROTATION_0:
      *rotated_x = px;
      *rotated_y = py;
      break;
    case SCREEN_ROTATION_90:
      *rotated_x = PORTRAIT_HEIGHT - 1 - py;
      *rotated_y = px;
      break;
    case SCREEN_ROTATION_180:
      *rotated_x = PORTRAIT_WIDTH - 1 - px;
      *rotated_y = PORTRAIT_HEIGHT - 1 - py;
      break;
    case SCREEN_ROTATION_270:
      *rotated_x = py;
      *rotated_y = PORTRAIT_WIDTH - 1 - px;
      break;
    default:
      return false;
  }
  return true;
}

static inline ThemePalette theme_palette(ThemeId theme) {
  switch (validated_theme(static_cast<uint32_t>(theme))) {
    case THEME_CLEAR_SKY:
      return {0xF6FAFBu, 0xE5EEF1u, 0x18333Du, 0x647981u, 0xCCDADDu,
              0xDF633Du, 0x197FADu, 0x087B73u};
    case THEME_RAINFOREST:
      return {0x132019u, 0x21362Cu, 0xF2F8F4u, 0x9AB5A6u, 0x395246u,
              0xFFC857u, 0x7BCBE6u, 0x65D49Eu};
    case THEME_SUNSET:
      return {0x2B1B29u, 0x462839u, 0xFFF6F7u, 0xC8A8B4u, 0x614052u,
              0xFFBA62u, 0x6FD1D8u, 0xFF7C79u};
    case THEME_HIGH_CONTRAST:
      return {0x050606u, 0x202323u, 0xFFFFFFu, 0xC8CCCCu, 0x4B5151u,
              0xFFE100u, 0x00D9FFu, 0xFFFFFFu};
    case THEME_DEEP_SEA:
    default:
      return {0x101820u, 0x1B2932u, 0xF8FBFCu, 0x8FA5AFu, 0x30434Du,
              0xFFD25Fu, 0x63C6FFu, 0x73E1D5u};
  }
}

#endif  // AURA_DISPLAY_CONFIG_H
