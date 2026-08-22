# Aura 横屏天气、五主题与四向旋转实施计划

> **供代理执行：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans`，按任务逐项实施。本计划使用复选框（`- [ ]`）跟踪进度。

**目标：** 在 ESP32-2432S028R 上实现四角度直选、方向同步的触摸输入、五套全局主题，以及带逐点温度和天气图标的横屏七天/逐小时折线图，同时保持现有竖屏列表模式可用。

**架构：** 新增两个不依赖 Arduino/LVGL 的纯 C++ 头文件：`display_config.h` 负责旋转、尺寸、触摸坐标和主题令牌，`forecast_model.h` 负责七点天气快照和图表范围。`weather.ino` 继续拥有硬件、Preferences、天气 API 和 LVGL 对象，但天气解析先写入快照，再由竖屏或横屏渲染器消费；主题和旋转只重建 UI，不重新联网。

**技术栈：** Arduino ESP32、LVGL 9.2.2、TFT_eSPI、XPT2046_Touchscreen、Preferences、ArduinoJson、Python `unittest`、C++17 主机测试、Arduino CLI 1.5.1。

---

## 文件职责

- 新建 `aura/display_config.h`：旋转枚举、逻辑尺寸、触摸变换、主题枚举、主题颜色令牌和非法值回退。
- 新建 `aura/forecast_model.h`：当前天气、七天、七小时固定容量快照，以及忽略无效点的图表上下界计算。
- 修改 `aura/weather.ino`：Preferences 集成、LVGL 旋转、触摸映射、快照填充、主题应用、横竖屏渲染、设置交互和校准方向恢复。
- 修改 `aura/translations.h`：显示设置、五个主题和十类天气状态的八语种文本。
- 修改 `aura/extract_unicode_chars.py`：支持扫描多个源文件并输出可直接传给字体转换器的字符集合。
- 新建 `aura/regenerate_chinese_fonts.sh`：固定命令重新生成 12/14/16/20 像素中文字体。
- 修改 `aura/lv_font_noto_sans_sc_{12,14,16,20}.c`：包含新增主题、方向和天气状态中文字形。
- 新建 `tests/test_display_config_math.py`：主机编译并验证四向坐标、尺寸、主题回退和颜色值。
- 新建 `tests/test_display_rotation.py`：静态检查固件加载、应用、保存旋转以及校准恢复契约。
- 新建 `tests/test_forecast_model.py`：主机编译并验证快照初始状态和图表范围。
- 新建 `tests/test_landscape_weather_ui.py`：静态检查横屏分支、LVGL 折线、逐点标签、图标和缺失数据行为。
- 新建 `tests/test_theme_settings.py`：静态检查五主题、翻译、设置控件和不联网重建契约。
- 修改 `tests/test_qweather_integration.py`：从“解析器直接改标签”调整为“解析器填快照并统一渲染”。
- 修改 `tests/test_settings_layout_and_sound.py`：分别断言竖屏和横屏布局，不再假设所有界面固定为 240 x 320。
- 修改 `tests/test_chinese_language.py`：继续要求所有新增中文字符存在于四套内嵌字体。

---

### 任务 1：用主机测试锁定旋转、尺寸和主题令牌

**文件：**
- 新建：`tests/test_display_config_math.py`
- 新建：`aura/display_config.h`

- [ ] **步骤 1：先写会失败的主机测试**

测试程序必须验证：非法值回退、四个逻辑尺寸、中心点和四角坐标，以及五个主题的全部批准色值。

```python
class DisplayConfigMathTests(unittest.TestCase):
    def test_rotation_geometry_touch_mapping_and_themes(self):
        result = self.run_cpp(r'''
  if (validated_rotation(99) != SCREEN_ROTATION_0) return 1;
  if (validated_theme(99) != THEME_DEEP_SEA) return 2;
  DisplayGeometry portrait = geometry_for_rotation(SCREEN_ROTATION_0);
  DisplayGeometry landscape = geometry_for_rotation(SCREEN_ROTATION_90);
  if (portrait.width != 240 || portrait.height != 320 || portrait.landscape) return 3;
  if (landscape.width != 320 || landscape.height != 240 || !landscape.landscape) return 4;
  if (geometry_for_rotation(SCREEN_ROTATION_180).width != 240 ||
      geometry_for_rotation(SCREEN_ROTATION_270).width != 320) return 5;

  int x = -1, y = -1;
  if (!rotate_portrait_touch(SCREEN_ROTATION_0, 0, 0, &x, &y) || x != 0 || y != 0) return 6;
  if (!rotate_portrait_touch(SCREEN_ROTATION_90, 0, 0, &x, &y) || x != 319 || y != 0) return 7;
  if (!rotate_portrait_touch(SCREEN_ROTATION_180, 0, 0, &x, &y) || x != 239 || y != 319) return 8;
  if (!rotate_portrait_touch(SCREEN_ROTATION_270, 0, 0, &x, &y) || x != 0 || y != 239) return 9;
  if (!rotate_portrait_touch(SCREEN_ROTATION_0, 239, 319, &x, &y) || x != 239 || y != 319) return 10;
  if (!rotate_portrait_touch(SCREEN_ROTATION_90, 239, 319, &x, &y) || x != 0 || y != 239) return 11;
  if (!rotate_portrait_touch(SCREEN_ROTATION_180, 239, 319, &x, &y) || x != 0 || y != 0) return 12;
  if (!rotate_portrait_touch(SCREEN_ROTATION_270, 239, 319, &x, &y) || x != 319 || y != 0) return 13;
  if (!rotate_portrait_touch(SCREEN_ROTATION_90, 120, 160, &x, &y) || x != 159 || y != 120) return 14;
  if (rotate_portrait_touch(SCREEN_ROTATION_0, -1, 0, &x, &y)) return 15;

  const uint32_t expected[THEME_COUNT][8] = {
    {0x101820, 0x1B2932, 0xF8FBFC, 0x8FA5AF, 0x30434D, 0xFFD25F, 0x63C6FF, 0x73E1D5},
    {0xF6FAFB, 0xE5EEF1, 0x18333D, 0x647981, 0xCCDADD, 0xDF633D, 0x197FAD, 0x087B73},
    {0x132019, 0x21362C, 0xF2F8F4, 0x9AB5A6, 0x395246, 0xFFC857, 0x7BCBE6, 0x65D49E},
    {0x2B1B29, 0x462839, 0xFFF6F7, 0xC8A8B4, 0x614052, 0xFFBA62, 0x6FD1D8, 0xFF7C79},
    {0x050606, 0x202323, 0xFFFFFF, 0xC8CCCC, 0x4B5151, 0xFFE100, 0x00D9FF, 0xFFFFFF}
  };
  for (uint8_t id = 0; id < THEME_COUNT; ++id) {
    const ThemePalette &p = theme_palette(static_cast<ThemeId>(id));
    const uint32_t actual[8] = {p.background, p.panel, p.text, p.muted, p.grid,
                                p.high_temperature, p.low_temperature, p.accent};
    for (uint8_t token = 0; token < 8; ++token)
      if (actual[token] != expected[id][token]) return 16;
  }
''')
        self.assertEqual(result.returncode, 0)
```

- [ ] **步骤 2：运行测试并确认红灯**

```bash
python3 -m unittest tests.test_display_config_math -v
```

预期：因 `display_config.h` 不存在而失败。

- [ ] **步骤 3：实现完整的纯 C++ 配置 API**

`aura/display_config.h` 使用以下公开接口和坐标公式：

```cpp
#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include <stdint.h>

static constexpr int PORTRAIT_WIDTH = 240;
static constexpr int PORTRAIT_HEIGHT = 320;

enum ScreenRotation : uint8_t {
  SCREEN_ROTATION_0 = 0,
  SCREEN_ROTATION_90 = 1,
  SCREEN_ROTATION_180 = 2,
  SCREEN_ROTATION_270 = 3,
  SCREEN_ROTATION_COUNT = 4
};

enum ThemeId : uint8_t {
  THEME_DEEP_SEA = 0,
  THEME_CLEAR_SKY = 1,
  THEME_RAINFOREST = 2,
  THEME_SUNSET = 3,
  THEME_HIGH_CONTRAST = 4,
  THEME_COUNT = 5
};

struct DisplayGeometry { int width; int height; bool landscape; };
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
  return value < SCREEN_ROTATION_COUNT
      ? static_cast<ScreenRotation>(value) : SCREEN_ROTATION_0;
}

static inline ThemeId validated_theme(uint32_t value) {
  return value < THEME_COUNT ? static_cast<ThemeId>(value) : THEME_DEEP_SEA;
}

static inline DisplayGeometry geometry_for_rotation(ScreenRotation rotation) {
  const bool landscape = rotation == SCREEN_ROTATION_90 || rotation == SCREEN_ROTATION_270;
  return landscape ? DisplayGeometry{320, 240, true}
                   : DisplayGeometry{240, 320, false};
}

static inline bool rotate_portrait_touch(ScreenRotation rotation, int px, int py,
                                         int *out_x, int *out_y) {
  if (!out_x || !out_y || px < 0 || px >= PORTRAIT_WIDTH ||
      py < 0 || py >= PORTRAIT_HEIGHT) return false;
  switch (validated_rotation(rotation)) {
    case SCREEN_ROTATION_90:  *out_x = PORTRAIT_HEIGHT - 1 - py; *out_y = px; break;
    case SCREEN_ROTATION_180: *out_x = PORTRAIT_WIDTH - 1 - px; *out_y = PORTRAIT_HEIGHT - 1 - py; break;
    case SCREEN_ROTATION_270: *out_x = py; *out_y = PORTRAIT_WIDTH - 1 - px; break;
    default:                  *out_x = px; *out_y = py; break;
  }
  return true;
}

static constexpr ThemePalette THEME_PALETTES[THEME_COUNT] = {
  {0x101820, 0x1B2932, 0xF8FBFC, 0x8FA5AF, 0x30434D, 0xFFD25F, 0x63C6FF, 0x73E1D5},
  {0xF6FAFB, 0xE5EEF1, 0x18333D, 0x647981, 0xCCDADD, 0xDF633D, 0x197FAD, 0x087B73},
  {0x132019, 0x21362C, 0xF2F8F4, 0x9AB5A6, 0x395246, 0xFFC857, 0x7BCBE6, 0x65D49E},
  {0x2B1B29, 0x462839, 0xFFF6F7, 0xC8A8B4, 0x614052, 0xFFBA62, 0x6FD1D8, 0xFF7C79},
  {0x050606, 0x202323, 0xFFFFFF, 0xC8CCCC, 0x4B5151, 0xFFE100, 0x00D9FF, 0xFFFFFF}
};

static inline const ThemePalette &theme_palette(ThemeId theme) {
  return THEME_PALETTES[validated_theme(theme)];
}

#endif
```

- [ ] **步骤 4：运行主机测试并提交**

```bash
python3 -m unittest tests.test_display_config_math -v
git add aura/display_config.h tests/test_display_config_math.py
git commit -m "新增屏幕旋转与主题配置模型"
```

预期：测试通过；提交只包含配置模型和对应测试。

---

### 任务 2：把四向旋转接入显示与触摸输入

**文件：**
- 新建：`tests/test_display_rotation.py`
- 修改：`aura/weather.ino`
- 修改：`tests/test_touch_calibration.py`

- [ ] **步骤 1：写旋转集成契约测试**

```python
def test_rotation_loads_before_display_and_uses_lvgl_rotation(self):
    setup = WEATHER[WEATHER.index("void setup()") : WEATHER.index("static void startup_weather_timer_cb")]
    self.assertLess(setup.index('prefs.begin("weather", false)'),
                    setup.index("lv_tft_espi_create"))
    self.assertIn('prefs.getUInt("screenRotation", SCREEN_ROTATION_0)', setup)
    self.assertIn("lv_display_set_rotation(display, lv_rotation_for", WEATHER)

def test_touch_is_calibrated_in_portrait_then_rotated(self):
    touch = WEATHER[WEATHER.index("void touchscreen_read") : WEATHER.index("void setup()")]
    self.assertIn("PORTRAIT_WIDTH, PORTRAIT_HEIGHT", touch)
    self.assertIn("rotate_portrait_touch(current_rotation", touch)
    self.assertLess(touch.index("apply_touch_calibration"),
                    touch.index("rotate_portrait_touch"))

def test_rotation_change_rebuilds_without_weather_fetch(self):
    rebuild = WEATHER[WEATHER.index("static void rebuild_ui") : WEATHER.index("void loop()")]
    self.assertIn("create_ui();", rebuild)
    self.assertNotIn("fetch_and_update_weather();", rebuild)
```

- [ ] **步骤 2：运行测试并确认缺少旋转集成**

```bash
python3 -m unittest tests.test_display_rotation tests.test_touch_calibration -v
```

预期：新测试失败；既有触摸校准测试继续通过或只因预期调用参数改变而失败。

- [ ] **步骤 3：调整初始化顺序并保存显示对象**

在全局状态中加入：

```cpp
#include "display_config.h"

static lv_display_t *display = nullptr;
static lv_indev_t *touch_indev = nullptr;
static ScreenRotation current_rotation = SCREEN_ROTATION_0;
static ThemeId current_theme = THEME_DEEP_SEA;

static lv_display_rotation_t lv_rotation_for(ScreenRotation rotation) {
  switch (rotation) {
    case SCREEN_ROTATION_90: return LV_DISPLAY_ROTATION_90;
    case SCREEN_ROTATION_180: return LV_DISPLAY_ROTATION_180;
    case SCREEN_ROTATION_270: return LV_DISPLAY_ROTATION_270;
    default: return LV_DISPLAY_ROTATION_0;
  }
}

static int display_width() { return geometry_for_rotation(current_rotation).width; }
static int display_height() { return geometry_for_rotation(current_rotation).height; }
```

`setup()` 在创建 LVGL display 前打开 Preferences、验证两个新值，然后创建并旋转显示：

```cpp
prefs.begin("weather", false);
current_rotation = validated_rotation(
    prefs.getUInt("screenRotation", SCREEN_ROTATION_0));
current_theme = validated_theme(
    prefs.getUInt("theme", THEME_DEEP_SEA));

display = lv_tft_espi_create(PORTRAIT_WIDTH, PORTRAIT_HEIGHT,
                             draw_buf, sizeof(draw_buf));
lv_display_set_rotation(display, lv_rotation_for(current_rotation));
touch_indev = lv_indev_create();
lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
lv_indev_set_display(touch_indev, display);
lv_indev_set_read_cb(touch_indev, touchscreen_read);
```

- [ ] **步骤 4：把触摸变换改为“先校准竖屏坐标，再按角度旋转”**

```cpp
int portrait_x = map(p.x, 200, 3700, 1, PORTRAIT_WIDTH);
int portrait_y = map(p.y, 240, 3800, 1, PORTRAIT_HEIGHT);
if (!calibration_active && touch_calibration.valid) {
  apply_touch_calibration(touch_calibration, p.x, p.y,
                          PORTRAIT_WIDTH, PORTRAIT_HEIGHT,
                          &portrait_x, &portrait_y);
}
portrait_x = constrain(portrait_x, 0, PORTRAIT_WIDTH - 1);
portrait_y = constrain(portrait_y, 0, PORTRAIT_HEIGHT - 1);
rotate_portrait_touch(current_rotation, portrait_x, portrait_y, &x, &y);
data->point.x = x;
data->point.y = y;
```

保留原始 `map()` 数值作为无校准回退；更新旧测试，使其断言竖屏常量和后续旋转调用，而不是直接映射到动态宽高。

- [ ] **步骤 5：加入不联网的 UI 重建入口并提交**

```cpp
static void rebuild_ui(bool reopen_settings) {
  if (kb) {
    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  kb = nullptr;
  settings_win = nullptr;
  location_win = nullptr;
  lv_obj_clean(lv_scr_act());
  create_ui();
  if (reopen_settings) create_settings_window();
}
```

```bash
python3 -m unittest tests.test_display_config_math tests.test_display_rotation tests.test_touch_calibration tests.test_touch_calibration_math -v
git add aura/weather.ino tests/test_display_rotation.py tests/test_touch_calibration.py
git commit -m "接入四向显示与触摸旋转"
```

预期：旋转与触摸测试全部通过；重建函数中没有天气请求。

---

### 任务 3：建立可复用的七点天气快照

**文件：**
- 新建：`tests/test_forecast_model.py`
- 新建：`aura/forecast_model.h`

- [ ] **步骤 1：先写快照和图表范围测试**

```python
def test_snapshot_defaults_and_bounds(self):
    result = self.run_cpp(r'''
  WeatherSnapshot snapshot{};
  clear_weather_snapshot(&snapshot);
  for (int i = 0; i < FORECAST_POINT_COUNT; ++i) {
    if (snapshot.daily[i].valid || snapshot.hourly[i].valid) return 1;
  }
  snapshot.daily[0] = {26, 30, 61, 8, 23, true};
  snapshot.daily[1] = {24, 31, 71, 8, 24, true};
  int minimum = 0, maximum = 0;
  if (!daily_chart_range(snapshot, &minimum, &maximum)) return 2;
  if (minimum != 22 || maximum != 33) return 3;
  snapshot.hourly[0] = {29, 40, 61, 10, true, true, true};
  snapshot.hourly[1] = {32, 0, 0, 13, true, false, true};
  if (!hourly_chart_range(snapshot, &minimum, &maximum)) return 4;
  if (minimum != 27 || maximum != 34) return 5;
  clear_weather_snapshot(&snapshot);
  snapshot.daily[0] = {-6, -1, 71, 1, 10, true};
  if (!daily_chart_range(snapshot, &minimum, &maximum)) return 6;
  if (minimum != -8 || maximum != 1) return 7;
''')
    self.assertEqual(result.returncode, 0)
```

- [ ] **步骤 2：确认测试因头文件缺失而失败**

```bash
python3 -m unittest tests.test_forecast_model -v
```

- [ ] **步骤 3：实现固定容量模型与范围计算**

```cpp
#ifndef FORECAST_MODEL_H
#define FORECAST_MODEL_H

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
  if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
}

static inline bool expand_range(float value, bool valid, float *minimum, float *maximum,
                                bool *found) {
  if (!valid || !minimum || !maximum || !found) return false;
  if (!*found) { *minimum = value; *maximum = value; *found = true; }
  else { if (value < *minimum) *minimum = value; if (value > *maximum) *maximum = value; }
  return true;
}

static inline bool padded_chart_range(float minimum, float maximum,
                                      int *out_minimum, int *out_maximum) {
  if (!out_minimum || !out_maximum) return false;
  *out_minimum = static_cast<int>(floorf(minimum)) - 2;
  *out_maximum = static_cast<int>(ceilf(maximum)) + 2;
  if (*out_maximum <= *out_minimum) *out_maximum = *out_minimum + 4;
  return true;
}

static inline bool daily_chart_range(const WeatherSnapshot &snapshot,
                                     int *out_minimum, int *out_maximum) {
  float minimum = 0, maximum = 0; bool found = false;
  for (int i = 0; i < FORECAST_POINT_COUNT; ++i) {
    expand_range(snapshot.daily[i].minimum, snapshot.daily[i].valid, &minimum, &maximum, &found);
    expand_range(snapshot.daily[i].maximum, snapshot.daily[i].valid, &minimum, &maximum, &found);
  }
  return found && padded_chart_range(minimum, maximum, out_minimum, out_maximum);
}

static inline bool hourly_chart_range(const WeatherSnapshot &snapshot,
                                      int *out_minimum, int *out_maximum) {
  float minimum = 0, maximum = 0; bool found = false;
  for (int i = 0; i < FORECAST_POINT_COUNT; ++i)
    expand_range(snapshot.hourly[i].temperature, snapshot.hourly[i].valid,
                 &minimum, &maximum, &found);
  return found && padded_chart_range(minimum, maximum, out_minimum, out_maximum);
}

#endif
```

- [ ] **步骤 4：运行测试并提交**

```bash
python3 -m unittest tests.test_forecast_model -v
git add aura/forecast_model.h tests/test_forecast_model.py
git commit -m "新增七点天气快照模型"
```

---

### 任务 4：补齐中文可见文本、八语种字段和字体再生流程

**文件：**
- 修改：`aura/translations.h`
- 修改：`aura/extract_unicode_chars.py`
- 新建：`aura/regenerate_chinese_fonts.sh`
- 修改：`aura/lv_font_noto_sans_sc_12.c`
- 修改：`aura/lv_font_noto_sans_sc_14.c`
- 修改：`aura/lv_font_noto_sans_sc_16.c`
- 修改：`aura/lv_font_noto_sans_sc_20.c`
- 修改：`tests/test_chinese_language.py`
- 新建：`tests/test_theme_settings.py`

- [ ] **步骤 1：先写翻译和字体契约测试**

```python
def test_display_theme_and_weather_strings_exist(self):
    for field in ("display_settings", "theme", "screen_orientation",
                  "touch_rotation", "theme_names", "weather_conditions"):
        self.assertIn(f"const char* {field}", TRANSLATIONS)
    for text in ("显示设置", "主题", "屏幕方向", "自动校正触摸",
                 "深海", "晴空", "雨林", "晚霞", "高对比",
                 "晴", "多云", "阴", "雾", "毛毛雨", "小雨",
                 "大雨", "雨夹雪", "雪", "雷雨"):
        self.assertIn(f'"{text}"', TRANSLATIONS)
```

沿用 `test_chinese_uses_embedded_cjk_fonts`，使新增汉字在字体未再生前明确失败。

- [ ] **步骤 2：扩展 `LocalizedStrings` 并加入精确中文文案**

`translations.h` 在语言枚举前包含主题数量定义：

```cpp
#include "display_config.h"

const char* display_settings;
const char* theme;
const char* screen_orientation;
const char* touch_rotation;
const char* theme_names[THEME_COUNT];
const char* weather_conditions[10];
```

八语种使用下表内容，数组顺序固定为主题 `Deep Sea / Clear Sky / Rainforest / Sunset / High Contrast`，天气 `Clear / Partly cloudy / Cloudy / Fog / Drizzle / Light rain / Heavy rain / Sleet / Snow / Thunderstorm`：

| 语言 | 显示设置 / 主题 / 屏幕方向 / 自动校正触摸 | 五主题名称 | 十类天气名称 |
| --- | --- | --- | --- |
| 中文 | 显示设置 / 主题 / 屏幕方向 / 自动校正触摸 | 深海、晴空、雨林、晚霞、高对比 | 晴、多云、阴、雾、毛毛雨、小雨、大雨、雨夹雪、雪、雷雨 |
| English | Display / Theme / Orientation / Correct touch | Deep Sea, Clear Sky, Rainforest, Sunset, High Contrast | Clear, Partly cloudy, Cloudy, Fog, Drizzle, Light rain, Heavy rain, Sleet, Snow, Thunderstorm |
| Español | Pantalla / Tema / Orientación / Corregir toque | Mar profundo, Cielo claro, Selva, Atardecer, Alto contraste | Despejado, Parcialmente nublado, Nublado, Niebla, Llovizna, Lluvia ligera, Lluvia fuerte, Aguanieve, Nieve, Tormenta |
| Deutsch | Anzeige / Thema / Ausrichtung / Touch korrigieren | Tiefsee, Klarer Himmel, Regenwald, Abendrot, Hoher Kontrast | Klar, Teilweise bewölkt, Bewölkt, Nebel, Nieselregen, Leichter Regen, Starkregen, Schneeregen, Schnee, Gewitter |
| Français | Affichage / Thème / Orientation / Corriger le tactile | Haute mer, Ciel clair, Forêt, Crépuscule, Contraste élevé | Dégagé, Peu nuageux, Nuageux, Brouillard, Bruine, Pluie faible, Forte pluie, Grésil, Neige, Orage |
| Türkçe | Ekran / Tema / Yön / Dokunmayı düzelt | Derin deniz, Açık gökyüzü, Yağmur ormanı, Gün batımı, Yüksek kontrast | Açık, Parçalı bulutlu, Bulutlu, Sis, Çiseleme, Hafif yağmur, Şiddetli yağmur, Sulu kar, Kar, Fırtına |
| Svenska | Skärm / Tema / Riktning / Korrigera touch | Djuphav, Klar himmel, Regnskog, Solnedgång, Hög kontrast | Klart, Delvis molnigt, Molnigt, Dimma, Duggregn, Lätt regn, Kraftigt regn, Snöblandat, Snö, Åska |
| Italiano | Schermo / Tema / Orientamento / Correggi tocco | Mare profondo, Cielo sereno, Foresta, Tramonto, Alto contrasto | Sereno, Parzialmente nuvoloso, Nuvoloso, Nebbia, Pioviggine, Pioggia leggera, Pioggia forte, Nevischio, Neve, Temporale |

- [ ] **步骤 3：让字符提取脚本支持多个输入和机器输出**

```python
parser = argparse.ArgumentParser()
parser.add_argument("paths", nargs="+")
parser.add_argument("--symbols-only", action="store_true")
args = parser.parse_args()
content = "\n".join(Path(path).read_text(encoding="utf-8") for path in args.paths)
characters = sorted({char for char in content if ord(char) > 127}, key=ord)
if args.symbols_only:
    print("".join(characters))
    return
```

- [ ] **步骤 4：增加可重复执行的字体生成脚本并运行**

`aura/regenerate_chinese_fonts.sh` 使用临时目录，不把 16 MB 字体源文件写入仓库：

```bash
#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
font_dir="$(mktemp -d /tmp/aura-font.XXXXXX)"
trap 'rm -rf "$font_dir"' EXIT
font_path="$font_dir/NotoSansCJKsc-Regular.otf"
curl -L --fail --silent --show-error \
  https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf \
  -o "$font_path"
symbols="$(python3 "$repo_dir/aura/extract_unicode_chars.py" --symbols-only \
  "$repo_dir/aura/weather.ino" "$repo_dir/aura/translations.h")"
for size in 12 14 16 20; do
  npx --yes lv_font_conv@1.5.3 --font "$font_path" --range 0x20-0x7E \
    --symbols "$symbols" --size "$size" --bpp 4 --no-compress --format lvgl \
    --lv-font-name "lv_font_noto_sans_sc_$size" \
    --output "$repo_dir/aura/lv_font_noto_sans_sc_$size.c"
done
```

运行：

```bash
chmod +x aura/regenerate_chinese_fonts.sh
./aura/regenerate_chinese_fonts.sh
python3 -m unittest tests.test_chinese_language tests.test_theme_settings -v
```

预期：四个字体文件都包含新增汉字，翻译与字体测试通过。

字体源固定使用 Noto 官方仓库的简体中文 OTF；临时下载目录由 `trap` 清理且不提交，提交中只包含四个生成后的 `.c` 字体文件和生成脚本。

- [ ] **步骤 5：提交翻译和字体生成能力**

```bash
git add aura/translations.h aura/extract_unicode_chars.py aura/regenerate_chinese_fonts.sh \
  aura/lv_font_noto_sans_sc_12.c aura/lv_font_noto_sans_sc_14.c \
  aura/lv_font_noto_sans_sc_16.c aura/lv_font_noto_sans_sc_20.c \
  tests/test_chinese_language.py tests/test_theme_settings.py
git commit -m "新增五主题与天气状态翻译"
```

---

### 任务 5：让天气解析填充快照并统一渲染

**文件：**
- 修改：`aura/weather.ino`
- 修改：`tests/test_qweather_integration.py`
- 修改：`tests/test_settings_layout_and_sound.py`

- [ ] **步骤 1：先把测试改成快照契约**

```python
def test_weather_providers_publish_complete_snapshot(self):
    self.assertIn('#include "forecast_model.h"', WEATHER)
    self.assertIn("static WeatherSnapshot weather_snapshot", WEATHER)
    self.assertGreaterEqual(WEATHER.count("WeatherSnapshot candidate{};"), 2)
    self.assertGreaterEqual(WEATHER.count("publish_weather_snapshot(candidate);"), 2)

def test_rebuild_and_tab_switch_never_fetch_weather(self):
    for function_name in ("static void rebuild_ui", "void daily_cb", "void hourly_cb"):
        start = WEATHER.index(function_name)
        end = WEATHER.index("\n}", start)
        self.assertNotIn("fetch_and_update_weather", WEATHER[start:end])
```

删除原先要求 `create_ui()` 内所有预报标签由 API 直接初始化的断言，改为断言 `render_weather_snapshot()` 同时支持占位和有效快照。

- [ ] **步骤 2：加入全局快照和唯一发布入口**

```cpp
#include "forecast_model.h"

enum ForecastView : uint8_t { FORECAST_DAILY, FORECAST_HOURLY };
static WeatherSnapshot weather_snapshot{};
static ForecastView active_forecast_view = FORECAST_DAILY;

static void render_weather_snapshot();

static void publish_weather_snapshot(const WeatherSnapshot &candidate) {
  weather_snapshot = candidate;
  render_weather_snapshot();
}

static const char *weather_condition_name(int code) {
  const LocalizedStrings *strings = get_strings(current_language);
  uint8_t index = 2;
  if (code == 0 || code == 1) index = 0;
  else if (code == 2) index = 1;
  else if (code == 3) index = 2;
  else if (code == 45 || code == 48) index = 3;
  else if (code >= 51 && code <= 57) index = 4;
  else if (code == 61 || code == 63 || code == 80 || code == 81) index = 5;
  else if (code == 65 || code == 82) index = 6;
  else if (code == 66 || code == 67) index = 7;
  else if ((code >= 71 && code <= 77) || code == 85 || code == 86) index = 8;
  else if (code == 95 || code == 96 || code == 99) index = 9;
  return strings->weather_conditions[index];
}
```

- [ ] **步骤 3：Open-Meteo 先填 `candidate`，完整后再发布**

当前值、每天和每小时循环分别写入以下字段；单位转换继续在解析阶段完成：

```cpp
candidate.current = {t_now, t_ap, code_now, is_day != 0, true};
candidate.daily[i] = {mn, mx, weather_codes[i].as<int>(),
                      static_cast<uint8_t>(mon), static_cast<uint8_t>(dayd), true};
candidate.hourly[i] = {
  temp,
  precipitation_probability,
  hourly_weather_codes[i].as<int>(),
  static_cast<uint8_t>(hour),
  hourly_is_day[i].as<int>() != 0,
  !precipitation_probabilities[i].isNull(),
  true
};
```

JSON 解析和三个数组循环成功结束后只调用一次：

```cpp
publish_weather_snapshot(candidate);
update_home_status(WEATHER_SOURCE_OPEN_METEO, doc["current"]["time"] | "");
```

- [ ] **步骤 4：QWeather 三个端点填同一个 `candidate`**

```cpp
candidate.current = {t_now, t_ap, code_now, is_day != 0, true};
candidate.daily[i] = {mn, mx, daily_icon,
                      static_cast<uint8_t>(mon), static_cast<uint8_t>(dayd), true};
candidate.hourly[i] = {
  temp,
  precipitation_probability,
  qweather_icon_to_wmo(hourly_icon),
  static_cast<uint8_t>(hour),
  qweather_icon_is_day(hourly_icon),
  !hourly[i]["pop"].isNull(),
  true
};
```

读取当前天气成功后先保存更新时间，避免后续两个请求覆盖 `doc`：

```cpp
String qweather_updated_at = doc["updateTime"] | "";
```

任一 QWeather 端点失败时仍整体回退 Open-Meteo；三段都成功后发布快照，再以 `qweather_updated_at.c_str()` 更新来源时间。解析器不再直接访问 `lbl_daily_*`、`lbl_hourly_*` 或图表对象。

- [ ] **步骤 5：实现竖屏快照渲染并跑回归**

```cpp
static void render_portrait_snapshot() {
  const LocalizedStrings *strings = get_strings(current_language);
  const char unit = use_fahrenheit ? 'F' : 'C';
  if (!weather_snapshot.current.valid) return;
  lv_label_set_text_fmt(lbl_today_temp, "%.0f°%c",
                        weather_snapshot.current.temperature, unit);
  lv_label_set_text_fmt(lbl_today_feels_like, "%s %.0f°%c",
                        strings->feels_like_temp,
                        weather_snapshot.current.feels_like, unit);
  lv_img_set_src(img_today_icon, choose_image(
      weather_snapshot.current.weather_code, weather_snapshot.current.is_day));
  for (int i = 0; i < FORECAST_POINT_COUNT; ++i) {
    const DailyForecastPoint &daily = weather_snapshot.daily[i];
    const HourlyForecastPoint &hourly = weather_snapshot.hourly[i];
    if (daily.valid) {
      lv_label_set_text_fmt(lbl_daily_day[i], "%02u/%02u", daily.month, daily.day);
      lv_label_set_text_fmt(lbl_daily_high[i], "%.0f°%c", daily.maximum, unit);
      lv_label_set_text_fmt(lbl_daily_low[i], "%.0f°%c", daily.minimum, unit);
      lv_img_set_src(img_daily[i], choose_icon(daily.weather_code, 1));
    }
    if (hourly.valid) {
      String hour_name = i == 0 ? String(strings->now) : hour_of_day(hourly.hour);
      lv_label_set_text(lbl_hourly[i], hour_name.c_str());
      lv_label_set_text_fmt(lbl_hourly_temp[i], "%.0f°%c", hourly.temperature, unit);
      if (hourly.has_precipitation) {
        lv_label_set_text_fmt(lbl_precipitation_probability[i], "%.0f%%",
                              hourly.precipitation_probability);
      } else {
        lv_label_set_text(lbl_precipitation_probability[i], "");
      }
      lv_img_set_src(img_hourly[i], choose_icon(hourly.weather_code, hourly.is_day));
    }
  }
}

static void render_weather_snapshot() {
  render_portrait_snapshot();
}
```

在本任务中同时把 `render_weather_snapshot();` 加到 `rebuild_ui()` 的 `create_ui();` 之后。`create_ui()` 只创建对象，`render_weather_snapshot()` 只填充缓存数据，`rebuild_ui()` 按此顺序各调用一次，避免创建阶段和重建阶段双重渲染。此时只存在竖屏渲染器，任务 7 再扩展为横竖屏分派。

```bash
python3 -m unittest tests.test_qweather_integration tests.test_settings_layout_and_sound \
  tests.test_forecast_model -v
git add aura/weather.ino tests/test_qweather_integration.py tests/test_settings_layout_and_sound.py
git commit -m "缓存天气快照并统一渲染"
```

---

### 任务 6：应用五主题并拆分横竖屏创建入口

**文件：**
- 修改：`aura/weather.ino`
- 修改：`tests/test_theme_settings.py`
- 修改：`tests/test_settings_layout_and_sound.py`

- [ ] **步骤 1：写主题应用和布局分派测试**

```python
def test_create_ui_uses_extracted_portrait_builder(self):
    create = WEATHER[WEATHER.index("void create_ui()") : WEATHER.index("void populate_results_dropdown")]
    self.assertIn("create_portrait_ui(scr);", create)

def test_theme_palette_is_applied_to_all_root_surfaces(self):
    for function in ("apply_root_theme", "create_portrait_ui",
                     "create_settings_window", "wifi_splash_screen"):
        self.assertIn(function, WEATHER)
    self.assertIn("theme_palette(current_theme)", WEATHER)
```

- [ ] **步骤 2：增加 LVGL 颜色转换和根主题函数**

```cpp
static lv_color_t theme_color(uint32_t rgb) { return lv_color_hex(rgb); }

static void apply_root_theme(lv_obj_t *root) {
  const ThemePalette &palette = theme_palette(current_theme);
  lv_obj_set_style_bg_color(root, theme_color(palette.background), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(root, theme_color(palette.text), LV_PART_MAIN);
}
```

- [ ] **步骤 3：把现有 `create_ui()` 内容移动到竖屏函数并保留对象名**

将当前 `create_ui()` 中从根背景设置到 `lbl_clock` 创建的完整对象创建语句移入 `create_portrait_ui(lv_obj_t *scr)`，保留所有对象名、尺寸和事件回调；只把硬编码背景、面板、正文和次要文字颜色改读 `ThemePalette`。新的入口在本任务中仍只创建竖屏，确保中间提交不引用尚未实现的横屏函数：

```cpp
void create_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_scroll_to(scr, 0, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  create_portrait_ui(scr);
}
```

- [ ] **步骤 4：让启动页和模态表面使用主题并提交**

`wifi_splash_screen()`、设置窗口、消息框边框/背景和校准页背景全部从 `ThemePalette` 取色；夜间模式继续只负责背光，不覆盖主题。

```bash
python3 -m unittest tests.test_theme_settings tests.test_settings_layout_and_sound \
  tests.test_click_sound tests.test_qweather_integration -v
git add aura/weather.ino tests/test_theme_settings.py tests/test_settings_layout_and_sound.py
git commit -m "应用五主题并拆分横竖屏布局"
```

---

### 任务 7：实现横屏七天和逐小时折线图

**文件：**
- 新建：`tests/test_landscape_weather_ui.py`
- 修改：`aura/weather.ino`
- 修改：`tests/test_click_sound.py`

- [ ] **步骤 1：先写横屏 UI 契约测试**

```python
def test_landscape_has_two_line_daily_and_one_line_hourly_charts(self):
    landscape = WEATHER[WEATHER.index("static void create_landscape_ui") :
                        WEATHER.index("void create_ui()")]
    self.assertGreaterEqual(landscape.count("lv_chart_create"), 2)
    self.assertIn("daily_high_series", landscape)
    self.assertIn("daily_low_series", landscape)
    self.assertIn("hourly_temperature_series", landscape)
    self.assertGreaterEqual(landscape.count("lv_chart_set_point_count"), 2)
    self.assertGreaterEqual(landscape.count("FORECAST_POINT_COUNT"), 2)

def test_create_ui_dispatches_by_rotation(self):
    create = WEATHER[WEATHER.index("void create_ui()") :
                     WEATHER.index("void populate_results_dropdown")]
    self.assertIn("geometry_for_rotation(current_rotation).landscape", create)
    self.assertIn("create_landscape_ui(scr);", create)
    self.assertIn("create_portrait_ui(scr);", create)

def test_each_forecast_column_has_temperature_icon_and_condition(self):
    for symbol in ("landscape_daily_icons", "landscape_hourly_icons",
                   "landscape_daily_conditions", "landscape_hourly_conditions",
                   "daily_high_labels", "daily_low_labels", "hourly_temperature_labels"):
        self.assertIn(symbol, WEATHER)

def test_render_binds_condition_text_and_hides_invalid_columns(self):
    start = WEATHER.index("static void render_landscape_snapshot() {")
    render = WEATHER[start : WEATHER.index("static void set_object_hidden(", start)]
    self.assertGreaterEqual(render.count("weather_condition_name("), 2)
    self.assertGreaterEqual(render.count("LV_CHART_POINT_NONE"), 3)
    for symbol in ("landscape_daily_dates", "landscape_daily_icons",
                   "landscape_daily_conditions", "daily_high_labels",
                   "daily_low_labels", "landscape_hourly_times",
                   "landscape_hourly_icons", "landscape_hourly_conditions",
                   "hourly_temperature_labels"):
        self.assertIn(f"set_object_hidden({symbol}[i], !", render)
```

- [ ] **步骤 2：创建横屏对象树和稳定尺寸**

使用以下固定布局，所有尺寸均对应真实 320 x 240：

```cpp
static constexpr int LANDSCAPE_HEADER_HEIGHT = 58;
static constexpr int LANDSCAPE_CHART_X = 6;
static constexpr int LANDSCAPE_CHART_Y = 62;
static constexpr int LANDSCAPE_CHART_WIDTH = 308;
static constexpr int LANDSCAPE_CHART_HEIGHT = 112;
static constexpr int LANDSCAPE_COLUMN_Y = 176;
static constexpr int LANDSCAPE_COLUMN_WIDTH = 44;

static void create_landscape_ui(lv_obj_t *scr) {
  apply_root_theme(scr);
  create_landscape_header(scr);
  create_forecast_segmented_control(scr);
  create_daily_chart(scr);
  create_hourly_chart(scr);
  set_forecast_view(active_forecast_view);
}
```

本任务同时把统一入口和快照渲染改为方向分派：

```cpp
static void render_landscape_snapshot();

void create_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_scroll_to(scr, 0, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  if (geometry_for_rotation(current_rotation).landscape) create_landscape_ui(scr);
  else create_portrait_ui(scr);
}

static void render_weather_snapshot() {
  if (geometry_for_rotation(current_rotation).landscape) render_landscape_snapshot();
  else render_portrait_snapshot();
}
```

顶部左侧显示位置、来源/更新时间、当前温度、天气状态和体感；右侧是 `7天 / 小时` 两个按钮和设置图标。图表不放入装饰卡片，根屏幕背景直接承载网格。

- [ ] **步骤 3：创建两张 LVGL 线图和外部定长数组**

```cpp
static int32_t daily_high_values[FORECAST_POINT_COUNT];
static int32_t daily_low_values[FORECAST_POINT_COUNT];
static int32_t hourly_temperature_values[FORECAST_POINT_COUNT];

daily_chart = lv_chart_create(scr);
lv_obj_set_pos(daily_chart, LANDSCAPE_CHART_X, LANDSCAPE_CHART_Y);
lv_obj_set_size(daily_chart, LANDSCAPE_CHART_WIDTH, LANDSCAPE_CHART_HEIGHT);
lv_chart_set_type(daily_chart, LV_CHART_TYPE_LINE);
lv_chart_set_point_count(daily_chart, FORECAST_POINT_COUNT);
lv_chart_set_div_line_count(daily_chart, 3, 7);
daily_high_series = lv_chart_add_series(daily_chart,
    theme_color(theme_palette(current_theme).high_temperature), LV_CHART_AXIS_PRIMARY_Y);
daily_low_series = lv_chart_add_series(daily_chart,
    theme_color(theme_palette(current_theme).low_temperature), LV_CHART_AXIS_PRIMARY_Y);
lv_chart_set_ext_y_array(daily_chart, daily_high_series, daily_high_values);
lv_chart_set_ext_y_array(daily_chart, daily_low_series, daily_low_values);

hourly_chart = lv_chart_create(scr);
lv_obj_set_pos(hourly_chart, LANDSCAPE_CHART_X, LANDSCAPE_CHART_Y);
lv_obj_set_size(hourly_chart, LANDSCAPE_CHART_WIDTH, LANDSCAPE_CHART_HEIGHT);
lv_chart_set_type(hourly_chart, LV_CHART_TYPE_LINE);
lv_chart_set_point_count(hourly_chart, FORECAST_POINT_COUNT);
lv_chart_set_div_line_count(hourly_chart, 3, 7);
hourly_temperature_series = lv_chart_add_series(hourly_chart,
    theme_color(theme_palette(current_theme).accent), LV_CHART_AXIS_PRIMARY_Y);
lv_chart_set_ext_y_array(hourly_chart, hourly_temperature_series,
                         hourly_temperature_values);
```

隐藏 chart 的滚动、边框和背景；通过 `LV_PART_ITEMS` 设置圆点大小，通过 `LV_PART_MAIN` 设置网格线颜色。

- [ ] **步骤 4：创建七列日期/时间、图标和天气状态**

每天和每小时各创建 7 组固定宽度对象：

```cpp
for (int i = 0; i < FORECAST_POINT_COUNT; ++i) {
  const int x = 6 + i * LANDSCAPE_COLUMN_WIDTH;
  landscape_daily_dates[i] = lv_label_create(scr);
  lv_obj_set_size(landscape_daily_dates[i], 42, 13);
  lv_obj_set_pos(landscape_daily_dates[i], x, LANDSCAPE_COLUMN_Y);
  lv_obj_set_style_text_align(landscape_daily_dates[i], LV_TEXT_ALIGN_CENTER, 0);
  landscape_daily_icons[i] = lv_img_create(scr);
  lv_obj_set_pos(landscape_daily_icons[i], x + 11, LANDSCAPE_COLUMN_Y + 13);
  landscape_daily_conditions[i] = lv_label_create(scr);
  lv_obj_set_size(landscape_daily_conditions[i], 42, 14);
  lv_obj_set_pos(landscape_daily_conditions[i], x, LANDSCAPE_COLUMN_Y + 34);
  lv_label_set_long_mode(landscape_daily_conditions[i], LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(landscape_daily_conditions[i], LV_TEXT_ALIGN_CENTER, 0);
}
```

小时列使用相同 x 坐标，并在状态行显示 `天气 40%`；无降水数据只显示天气状态。

- [ ] **步骤 5：渲染折线值、节点标签、图标与缺失数据**

```cpp
static void set_object_hidden(lv_obj_t *object, bool hidden);
static void position_chart_temperature_labels();

static void render_landscape_snapshot() {
  const LocalizedStrings *strings = get_strings(current_language);
  const char unit = use_fahrenheit ? 'F' : 'C';
  int range_min = 0, range_max = 0;
  if (daily_chart_range(weather_snapshot, &range_min, &range_max))
    lv_chart_set_range(daily_chart, LV_CHART_AXIS_PRIMARY_Y, range_min, range_max);
  if (hourly_chart_range(weather_snapshot, &range_min, &range_max))
    lv_chart_set_range(hourly_chart, LV_CHART_AXIS_PRIMARY_Y, range_min, range_max);

  for (int i = 0; i < FORECAST_POINT_COUNT; ++i) {
    const DailyForecastPoint &daily = weather_snapshot.daily[i];
    daily_high_values[i] = daily.valid ? lroundf(daily.maximum) : LV_CHART_POINT_NONE;
    daily_low_values[i] = daily.valid ? lroundf(daily.minimum) : LV_CHART_POINT_NONE;
    set_object_hidden(landscape_daily_dates[i], !daily.valid);
    set_object_hidden(landscape_daily_icons[i], !daily.valid);
    set_object_hidden(landscape_daily_conditions[i], !daily.valid);
    set_object_hidden(daily_high_labels[i], !daily.valid);
    set_object_hidden(daily_low_labels[i], !daily.valid);
    if (daily.valid) {
      lv_label_set_text_fmt(landscape_daily_dates[i], "%02u/%02u", daily.month, daily.day);
      lv_label_set_text(landscape_daily_conditions[i],
                        weather_condition_name(daily.weather_code));
      lv_label_set_text_fmt(daily_high_labels[i], "%.0f°%c", daily.maximum, unit);
      lv_label_set_text_fmt(daily_low_labels[i], "%.0f°%c", daily.minimum, unit);
      lv_img_set_src(landscape_daily_icons[i], choose_icon(daily.weather_code, 1));
    }

    const HourlyForecastPoint &hourly = weather_snapshot.hourly[i];
    hourly_temperature_values[i] = hourly.valid
        ? lroundf(hourly.temperature) : LV_CHART_POINT_NONE;
    set_object_hidden(landscape_hourly_times[i], !hourly.valid);
    set_object_hidden(landscape_hourly_icons[i], !hourly.valid);
    set_object_hidden(landscape_hourly_conditions[i], !hourly.valid);
    set_object_hidden(hourly_temperature_labels[i], !hourly.valid);
    if (hourly.valid) {
      String hour_name = i == 0 ? String(strings->now) : hour_of_day(hourly.hour);
      lv_label_set_text(landscape_hourly_times[i], hour_name.c_str());
      if (hourly.has_precipitation) {
        lv_label_set_text_fmt(landscape_hourly_conditions[i], "%s %.0f%%",
                              weather_condition_name(hourly.weather_code),
                              hourly.precipitation_probability);
      } else {
        lv_label_set_text(landscape_hourly_conditions[i],
                          weather_condition_name(hourly.weather_code));
      }
      lv_label_set_text_fmt(hourly_temperature_labels[i], "%.0f°%c",
                            hourly.temperature, unit);
      lv_img_set_src(landscape_hourly_icons[i],
                     choose_icon(hourly.weather_code, hourly.is_day));
    }
  }
  lv_chart_refresh(daily_chart);
  lv_chart_refresh(hourly_chart);
  position_chart_temperature_labels();
}
```

隐藏 helper 使用 LVGL 9 已存在的 add/clear API：

```cpp
static void set_object_hidden(lv_obj_t *object, bool hidden) {
  if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}
```

温度标签和 chart 都是根屏幕的直接子对象。`lv_chart_get_point_pos_by_id()` 返回 chart 内部坐标，因此定位时必须显式加上 chart 原点，不能把内部坐标直接写给根屏幕子对象：

```cpp
static void place_chart_label(lv_obj_t *label, lv_obj_t *chart,
                              lv_chart_series_t *series, uint32_t index,
                              int y_offset) {
  lv_point_t point{};
  lv_chart_get_point_pos_by_id(chart, series, index, &point);
  const int x = constrain(LANDSCAPE_CHART_X + point.x - 15,
                          0, display_width() - 30);
  const int y = LANDSCAPE_CHART_Y + point.y + y_offset;
  lv_obj_set_pos(label, x, y);
}

static void position_chart_temperature_labels() {
  lv_obj_update_layout(daily_chart);
  lv_obj_update_layout(hourly_chart);
  for (uint32_t i = 0; i < FORECAST_POINT_COUNT; ++i) {
    if (weather_snapshot.daily[i].valid) {
      place_chart_label(daily_high_labels[i], daily_chart, daily_high_series, i, -14);
      place_chart_label(daily_low_labels[i], daily_chart, daily_low_series, i, 2);
    }
    if (weather_snapshot.hourly[i].valid) {
      place_chart_label(hourly_temperature_labels[i], hourly_chart,
                        hourly_temperature_series, i, -14);
    }
  }
}
```

三个温度标签数组中的每个标签固定宽 30、高 12、居中对齐；标签位置不得改变 chart 尺寸。日期/时间、图标、天气文字、温度标签必须按同一个 `valid` 值整组显示或隐藏，避免残留上一次快照内容。

- [ ] **步骤 6：连接分段按钮、保留点击音并提交**

```cpp
void daily_cb(lv_event_t *e) {  // 竖屏：点击七天容器后切换到小时
  play_click_sound();
  active_forecast_view = FORECAST_HOURLY;
  set_forecast_view(active_forecast_view);
}

void hourly_cb(lv_event_t *e) {  // 竖屏：点击小时容器后切回七天
  play_click_sound();
  active_forecast_view = FORECAST_DAILY;
  set_forecast_view(active_forecast_view);
}
```

保持函数名以兼容现有点击音测试；竖屏 forecast 容器继续使用上述切换函数。横屏两个分段按钮使用独立的 `select_daily_cb` / `select_hourly_cb`，分别把状态明确设为 `FORECAST_DAILY` / `FORECAST_HOURLY`，不能复用会翻转状态的竖屏回调。

```bash
python3 -m unittest tests.test_landscape_weather_ui tests.test_click_sound \
  tests.test_qweather_integration tests.test_settings_layout_and_sound -v
git add aura/weather.ino tests/test_landscape_weather_ui.py tests/test_click_sound.py
git commit -m "实现横屏七天与小时折线图"
```

---

### 任务 8：实现五主题与四角度直选设置

**文件：**
- 修改：`aura/weather.ino`
- 修改：`tests/test_theme_settings.py`
- 修改：`tests/test_settings_layout_and_sound.py`
- 修改：`tests/test_touch_calibration.py`

- [ ] **步骤 1：先写设置交互测试**

```python
def test_settings_has_five_theme_swatches_and_four_rotation_choices(self):
    settings = WEATHER[WEATHER.index("void create_settings_window()") :]
    self.assertIn("theme_buttons[THEME_COUNT]", WEATHER)
    self.assertIn("rotation_buttonmatrix", WEATHER)
    self.assertIn('static const char *rotation_map[]', WEATHER)
    for angle in ('"0°"', '"90°"', '"180°"', '"270°"'):
        self.assertIn(angle, WEATHER)
    self.assertIn('prefs.putUInt("theme"', settings)
    self.assertIn('prefs.putUInt("screenRotation"', settings)
    self.assertIn("lv_async_call(apply_display_preferences_async", settings)

def test_calibration_temporarily_uses_zero_degree_and_restores_rotation(self):
    self.assertIn("calibration_previous_rotation", WEATHER)
    start = WEATHER[WEATHER.index("static void start_touch_calibration") :]
    finish = WEATHER[WEATHER.index("static void finish_touch_calibration") :]
    self.assertIn("SCREEN_ROTATION_0", start)
    self.assertIn("calibration_previous_rotation", finish)
    self.assertIn("restore_rotation_after_calibration(success);", finish)
    self.assertIn("finish_touch_calibration(false);", WEATHER)
    self.assertIn("finish_touch_calibration(true);", WEATHER)
```

- [ ] **步骤 2：增加五个色板按钮和四角度按钮矩阵**

主题行高 68，五个按钮各 38 x 42、间距 3；按钮背景使用各主题 accent，文字使用 `strings->theme_names[i]`，选中项加 2 像素白色或深色边框。旋转行使用：

```cpp
static const char *rotation_map[] = {"0°", "90°", "180°", "270°", ""};
rotation_buttonmatrix = lv_buttonmatrix_create(rotation_row);
lv_buttonmatrix_set_map(rotation_buttonmatrix, rotation_map);
lv_buttonmatrix_set_one_checked(rotation_buttonmatrix, true);
lv_buttonmatrix_set_button_ctrl_all(rotation_buttonmatrix,
                                    LV_BUTTONMATRIX_CTRL_CHECKABLE);
lv_buttonmatrix_set_button_ctrl(rotation_buttonmatrix,
                                static_cast<uint32_t>(current_rotation),
                                LV_BUTTONMATRIX_CTRL_CHECKED);
```

“自动校正触摸”使用 checked 且 disabled 的 switch，明确这是旋转必需行为，不提供关闭状态。

- [ ] **步骤 3：用 `lv_async_call` 安全应用并持久保存**

```cpp
struct PendingDisplayPreferences {
  ThemeId theme;
  ScreenRotation rotation;
  bool reopen_settings;
};
static PendingDisplayPreferences pending_display_preferences;

static void apply_display_preferences_async(void *) {
  current_theme = validated_theme(pending_display_preferences.theme);
  current_rotation = validated_rotation(pending_display_preferences.rotation);
  prefs.putUInt("theme", current_theme);
  prefs.putUInt("screenRotation", current_rotation);
  lv_display_set_rotation(display, lv_rotation_for(current_rotation));
  rebuild_ui(pending_display_preferences.reopen_settings);
}
```

主题或角度事件只更新 `pending_display_preferences` 并调用一次 `lv_async_call`；回调执行前不删除当前事件目标。切换后设置窗口重新打开，当前日/小时 tab 和 `weather_snapshot` 保持不变。

- [ ] **步骤 4：校准时临时回到 0°，结束后恢复**

```cpp
static ScreenRotation calibration_previous_rotation = SCREEN_ROTATION_0;

static void start_touch_calibration() {
  if (calibration_active || !settings_win) return;
  calibration_previous_rotation = current_rotation;
  if (kb) lv_keyboard_set_textarea(kb, nullptr);
  kb = nullptr;
  settings_win = nullptr;
  location_win = nullptr;
  current_rotation = SCREEN_ROTATION_0;
  lv_display_set_rotation(display, LV_DISPLAY_ROTATION_0);
  lv_obj_clean(lv_scr_act());
  create_touch_calibration_overlay();
}

static void restore_rotation_after_calibration(bool success) {
  current_rotation = calibration_previous_rotation;
  lv_display_set_rotation(display, lv_rotation_for(current_rotation));
  rebuild_ui(false);
  show_calibration_result(success);
}
```

把现有校准 UI 的对象创建提取为 `create_touch_calibration_overlay()`。`finish_touch_calibration(bool success)` 负责拟合、保存成功结果、停止 timer、清理 overlay 和复位校准状态，最后只调用一次 `restore_rotation_after_calibration(success)`；取消按钮、超时回调、拟合失败和五点成功均统一进入 `finish_touch_calibration()`。校准过程中不保存临时 0°。结果弹窗必须在恢复原角度并重建 UI 后创建；目标坐标和仿射系数继续以 240 x 320 竖屏基准表示，流程结束后不得保留旧 `settings_win`、`location_win` 或 `kb` 指针。

- [ ] **步骤 5：运行设置、触摸和全量测试并提交**

```bash
python3 -m unittest discover -s tests -v
git add aura/weather.ino tests/test_theme_settings.py tests/test_settings_layout_and_sound.py \
  tests/test_touch_calibration.py
git commit -m "新增五主题与四角度显示设置"
```

预期：新增和现有测试全部通过；主题/旋转事件及校准恢复路径都不请求天气。

---

### 任务 9：完整回归、固件编译和实机验收准备

**文件：**
- 修改：`README.md`
- 验证：`aura/weather.ino`
- 验证：`aura/display_config.h`
- 验证：`aura/forecast_model.h`
- 验证：全部 `tests/`

- [ ] **步骤 1：补充中文使用说明**

在 README 的语言说明后加入中文小节，内容固定为：

```markdown
### 显示方向与主题

在 Aura 设置的“显示设置”中可选择 0°、90°、180° 或 270°。0°/180°
使用竖屏列表，90°/270° 使用横屏折线图；触摸坐标会自动随屏幕方向校正。

主题提供深海、晴空、雨林、晚霞和高对比五种选择，并对主页、设置和弹窗全局生效。
七天横屏图显示最高温、最低温及逐日天气图标；小时横屏图显示逐小时温度、天气图标和可用的降水概率。
```

- [ ] **步骤 2：运行所有主机与静态测试**

```bash
python3 -m unittest discover -s tests -v
```

预期：全部测试通过；基线 43 项测试不得减少，新测试应增加旋转、主题、快照和横屏图覆盖。

- [ ] **步骤 3：检查变更和编译固件**

```bash
git diff --check
sketch_dir="$(mktemp -d /tmp/aura-sketch.XXXXXX)"
build_dir="$(mktemp -d /tmp/aura-build.XXXXXX)"
library_dir="$(mktemp -d /tmp/aura-libs.XXXXXX)"
cp -R aura "$sketch_dir/aura"
mv "$sketch_dir/aura/weather.ino" "$sketch_dir/aura/aura.ino"
cp -R /Users/luckmiracle/Documents/Arduino/libraries/TFT_eSPI "$library_dir/TFT_eSPI"
cp TFT_eSPI/User_Setup.h "$library_dir/TFT_eSPI/User_Setup.h"
arduino-cli compile \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
  --libraries "$library_dir" \
  --build-path "$build_dir" \
  --build-property "compiler.cpp.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -I/Users/luckmiracle/Documents/ChatGPT/CYD/lvgl/src" \
  "$sketch_dir/aura"
```

预期：Arduino 编译成功，无缺少字形、LVGL chart API、重复符号或程序空间溢出错误。记录固件大小，但本任务不自动上传设备。

- [ ] **步骤 4：按视觉与交互矩阵做实机验收**

在获得用户明确刷写授权后，逐项验证：

| 角度 | 布局 | 必验交互 |
| --- | --- | --- |
| 0° | 竖屏列表 | 四角触摸、设置滚动、日/小时切换 |
| 90° | 横屏折线 | 七天双线、小时单线、雨雪雷雨图标、设置按钮 |
| 180° | 倒置竖屏列表 | 四角触摸、关闭按钮、位置输入 |
| 270° | 倒置横屏折线 | 七天/小时切换、节点标签、设置滚动 |

每个角度再切换五个主题，确认正文、次要文字、两条温度线、选中按钮和天气图标可辨；重启后确认主题、角度和当前触摸方向一致。缺失天气点必须隐藏，不得落到 0° 形成假折线。

- [ ] **步骤 5：提交文档和最终验证状态**

```bash
git add README.md
git commit -m "记录横屏天气与主题使用方式"
git status --short
git log --oneline -9
```

预期：工作树干净；最近提交按计划分别覆盖配置模型、旋转、快照、翻译字体、渲染、横屏图、设置和文档。

---

## 自检结果

- [x] 四个角度、逻辑尺寸、Preferences 默认与非法回退均有任务和测试。
- [x] 触摸先以 240 x 320 校准再旋转，校准期间临时回 0° 并在所有退出路径恢复。
- [x] 五个主题的精确色值、八语种名称、全局应用范围和字体再生均有任务。
- [x] 竖屏保留七行列表，横屏提供七天双折线与小时单折线。
- [x] 七天和小时的每个索引都绑定温度、天气代码、图标与对应日期/时间；小时额外绑定降水概率。
- [x] 缺失点使用 `LV_CHART_POINT_NONE`，缺失降水不显示百分比。
- [x] 主题、旋转和 tab 切换复用快照，不触发网络请求。
- [x] 现有设置、声音、天气源、中文和触摸校准测试均纳入回归。
- [x] 计划只编译固件；刷写设备需要用户再次明确授权。
