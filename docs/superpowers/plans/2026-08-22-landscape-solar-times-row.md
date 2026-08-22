# Aura Landscape Solar Times Row Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace landscape feels-like and humidity with a single horizontal row showing localized sunrise, sunset, and update times, then build and flash the firmware.

**Architecture:** Extend the dependency-free weather snapshot with validated `HH:mm` solar times. Populate those optional fields from the existing Open-Meteo and QWeather daily responses, then render three fixed-width LVGL labels across the landscape header while leaving portrait UI and forecast charts unchanged.

**Tech Stack:** Arduino C++17, ESP32 Arduino core, ArduinoJson, LVGL, Python `unittest`, `arduino-cli`

---

## File Map

- `aura/forecast_model.h`: owns the snapshot solar-time fields and dependency-free `HH:mm` parser.
- `aura/translations.h`: owns localized `Sunrise`/`Sunset` labels.
- `aura/weather.ino`: parses provider responses and creates/renders the landscape labels.
- `tests/test_forecast_model.py`: host-compiled parser and snapshot reset coverage.
- `tests/test_qweather_integration.py`: static contracts for both provider parsers.
- `tests/test_landscape_weather_ui.py`: landscape object, layout, rendering, and portrait-isolation contracts.
- `tests/test_chinese_language.py`: translation field coverage.

### Task 1: Add Validated Solar Times To The Snapshot

**Files:**
- Modify: `tests/test_forecast_model.py`
- Modify: `aura/forecast_model.h`

- [ ] **Step 1: Write failing model tests**

Add host tests that require a trailing `SolarTimes solar` member, verify zero-initialization and reset, and exercise a parser with both provider formats:

```python
def test_solar_time_parser_accepts_provider_formats_and_rejects_invalid_values(self):
    self.run_cpp(r'''
  char output[6] = {};
  if (!parse_hh_mm("2026-08-22T06:15", output)) return 1;
  if (strcmp(output, "06:15") != 0) return 2;
  if (!parse_hh_mm("18:42", output)) return 3;
  if (strcmp(output, "18:42") != 0) return 4;
  if (parse_hh_mm("24:00", output)) return 5;
  if (parse_hh_mm("06:60", output)) return 6;
  if (parse_hh_mm("", output)) return 7;
  if (parse_hh_mm(nullptr, output)) return 8;
  if (parse_hh_mm("06:15", nullptr)) return 9;
  return 0;
''')

def test_clear_resets_optional_solar_times(self):
    self.run_cpp(r'''
  WeatherSnapshot snapshot = {};
  strcpy(snapshot.solar.sunrise, "06:15");
  strcpy(snapshot.solar.sunset, "18:42");
  snapshot.solar.has_sunrise = true;
  snapshot.solar.has_sunset = true;
  clear_weather_snapshot(&snapshot);
  if (snapshot.solar.sunrise[0] != '\0' || snapshot.solar.sunset[0] != '\0') return 1;
  if (snapshot.solar.has_sunrise || snapshot.solar.has_sunset) return 2;
  return 0;
''')
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```bash
python3 -m unittest \
  tests.test_forecast_model.ForecastModelTests.test_solar_time_parser_accepts_provider_formats_and_rejects_invalid_values \
  tests.test_forecast_model.ForecastModelTests.test_clear_resets_optional_solar_times -v
```

Expected: FAIL because `parse_hh_mm` and `WeatherSnapshot::solar` do not exist.

- [ ] **Step 3: Implement the model and parser**

Add `<string.h>`, append the following aggregate-safe member after `hourly`, and implement a strict parser that copies exactly five characters plus a terminator:

```cpp
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

static inline bool parse_hh_mm(const char *value, char output[6]) {
  if (!value || !output) return false;
  const char *clock = strchr(value, 'T');
  clock = clock ? clock + 1 : value;
  if (strlen(clock) < 5 || clock[2] != ':' ||
      clock[0] < '0' || clock[0] > '9' ||
      clock[1] < '0' || clock[1] > '9' ||
      clock[3] < '0' || clock[3] > '9' ||
      clock[4] < '0' || clock[4] > '9') return false;
  const int hour = (clock[0] - '0') * 10 + clock[1] - '0';
  const int minute = (clock[3] - '0') * 10 + clock[4] - '0';
  if (hour > 23 || minute > 59) return false;
  memcpy(output, clock, 5);
  output[5] = '\0';
  return true;
}
```

- [ ] **Step 4: Run the model suite and verify GREEN**

Run: `python3 -m unittest tests.test_forecast_model -v`

Expected: all forecast model tests pass with `-Wall -Werror`.

- [ ] **Step 5: Commit the model change**

```bash
git add aura/forecast_model.h tests/test_forecast_model.py
git commit -m "添加天气快照日出日落时间"
```

### Task 2: Populate Solar Times From Both Providers

**Files:**
- Modify: `tests/test_qweather_integration.py`
- Modify: `aura/weather.ino`

- [ ] **Step 1: Write failing provider parser contracts**

Add tests asserting that Open-Meteo requests and parses daily solar arrays and QWeather parses the first daily object without making the fields mandatory:

```python
def test_open_meteo_populates_optional_solar_times(self):
    parser = function_body(
        "static void fetch_open_meteo_weather() {",
        "void fetch_and_update_weather()",
    )
    self.assertIn(
        "&daily=temperature_2m_min,temperature_2m_max,weather_code,sunrise,sunset",
        parser,
    )
    self.assertIn('JsonArray sunrises = doc["daily"]["sunrise"].as<JsonArray>();', parser)
    self.assertIn('JsonArray sunsets = doc["daily"]["sunset"].as<JsonArray>();', parser)
    self.assertIn("candidate.solar.has_sunrise = parse_hh_mm(", parser)
    self.assertIn("candidate.solar.has_sunset = parse_hh_mm(", parser)
    current_guard = parser[parser.index("bool current_complete") : parser.index("if (!current_complete)")]
    self.assertNotIn("sunrise", current_guard)
    self.assertNotIn("sunset", current_guard)

def test_qweather_populates_optional_solar_times_from_today(self):
    parser = function_body(
        "void fetch_and_update_weather() {",
        "const lv_img_dsc_t* choose_image",
    )
    self.assertIn('daily[0]["sunrise"]', parser)
    self.assertIn('daily[0]["sunset"]', parser)
    self.assertIn("candidate.solar.has_sunrise = parse_hh_mm(", parser)
    self.assertIn("candidate.solar.has_sunset = parse_hh_mm(", parser)
    daily_required = parser[parser.index('const char *date = daily[i]["fxDate"]') : parser.index("continue;", parser.index('const char *date = daily[i]["fxDate"]'))]
    self.assertNotIn("sunrise", daily_required)
    self.assertNotIn("sunset", daily_required)
```

- [ ] **Step 2: Run provider tests and verify RED**

Run:

```bash
python3 -m unittest \
  tests.test_qweather_integration.QWeatherIntegrationTests.test_open_meteo_populates_optional_solar_times \
  tests.test_qweather_integration.QWeatherIntegrationTests.test_qweather_populates_optional_solar_times_from_today -v
```

Expected: FAIL because neither provider currently extracts solar times.

- [ ] **Step 3: Parse optional Open-Meteo values**

Add `sunrise,sunset` to the existing `daily` query. Read the arrays and parse element zero independently before validating forecast point counts:

```cpp
JsonArray sunrises = doc["daily"]["sunrise"].as<JsonArray>();
JsonArray sunsets = doc["daily"]["sunset"].as<JsonArray>();

if (sunrises.size() > 0) {
  candidate.solar.has_sunrise = parse_hh_mm(
      sunrises[0] | "", candidate.solar.sunrise);
}
if (sunsets.size() > 0) {
  candidate.solar.has_sunset = parse_hh_mm(
      sunsets[0] | "", candidate.solar.sunset);
}
```

- [ ] **Step 4: Parse optional QWeather values**

Immediately after obtaining the `daily` array, parse only `daily[0]` when present; do not add solar fields to the required daily-point guard:

```cpp
if (daily.size() > 0) {
  candidate.solar.has_sunrise = parse_hh_mm(
      daily[0]["sunrise"] | "", candidate.solar.sunrise);
  candidate.solar.has_sunset = parse_hh_mm(
      daily[0]["sunset"] | "", candidate.solar.sunset);
}
```

- [ ] **Step 5: Run provider and model suites and verify GREEN**

Run:

```bash
python3 -m unittest tests.test_forecast_model tests.test_qweather_integration -v
```

Expected: all tests pass; optional missing solar fields do not alter fallback guards.

- [ ] **Step 6: Commit provider parsing**

```bash
git add aura/weather.ino tests/test_qweather_integration.py
git commit -m "解析天气源日出日落时间"
```

### Task 3: Render The Three-Column Landscape Status Row

**Files:**
- Modify: `tests/test_landscape_weather_ui.py`
- Modify: `tests/test_chinese_language.py`
- Modify: `aura/translations.h`
- Modify: `aura/weather.ino`

- [ ] **Step 1: Write failing localization and landscape contracts**

Update the landscape header test to forbid a feels-like label and require two new labels plus the approved geometry:

```python
self.assertNotIn("lbl_today_feels_like = lv_label_create(scr);", header)
self.assertIn("landscape_sunrise = lv_label_create(scr);", header)
self.assertIn("lv_obj_set_size(landscape_sunrise, 94, 13);", header)
self.assertIn("lv_obj_set_pos(landscape_sunrise, 6, 44);", header)
self.assertIn("landscape_sunset = lv_label_create(scr);", header)
self.assertIn("lv_obj_set_size(landscape_sunset, 88, 13);", header)
self.assertIn("lv_obj_set_pos(landscape_sunset, 104, 44);", header)
self.assertIn("lv_obj_set_size(lbl_update_status, 118, 13);", header)
self.assertIn("lv_obj_set_pos(lbl_update_status, 196, 44);", header)
```

Replace the old landscape feels-like renderer test with:

```python
def test_landscape_summary_renders_localized_solar_placeholders(self):
    renderer = function_body(
        "static void render_landscape_snapshot() {",
        "static void set_object_hidden(",
    )
    self.assertNotIn("lbl_today_feels_like", renderer)
    self.assertIn("strings->sunrise", renderer)
    self.assertIn("strings->sunset", renderer)
    self.assertIn("weather_snapshot.solar.has_sunrise", renderer)
    self.assertIn("weather_snapshot.solar.has_sunset", renderer)
    self.assertGreaterEqual(renderer.count('"--:--"'), 2)
```

Extend the Chinese-language test to require `"Sunrise", "Sunset"` and `"日出", "日落"` initializers.

- [ ] **Step 2: Run UI tests and verify RED**

Run:

```bash
python3 -m unittest tests.test_landscape_weather_ui tests.test_chinese_language -v
```

Expected: FAIL because the old feels-like label still exists and solar labels/translations do not.

- [ ] **Step 3: Add localized labels and LVGL object references**

Append `sunrise` and `sunset` fields next to `weather_updated` in `LocalizedStrings`, initialize them as `"Sunrise"`, `"Sunset"` and `"日出"`, `"日落"`, and add/reset these globals in `weather.ino`:

```cpp
static lv_obj_t *landscape_sunrise;
static lv_obj_t *landscape_sunset;
```

- [ ] **Step 4: Replace the landscape feels-like label with three fixed columns**

Keep temperature, condition, buttons, settings, and chart geometry unchanged. Use the existing 12px font and muted theme color for all three labels:

```cpp
landscape_sunrise = lv_label_create(scr);
lv_obj_set_size(landscape_sunrise, 94, 13);
lv_obj_set_pos(landscape_sunrise, 6, 44);

landscape_sunset = lv_label_create(scr);
lv_obj_set_size(landscape_sunset, 88, 13);
lv_obj_set_pos(landscape_sunset, 104, 44);

lbl_update_status = lv_label_create(scr);
lv_obj_set_size(lbl_update_status, 118, 13);
lv_obj_set_pos(lbl_update_status, 196, 44);
```

Each label uses `LV_LABEL_LONG_DOT`, `get_font_12()`, and `palette.muted`. Do not create `lbl_today_feels_like` in `create_landscape_header`; portrait creation remains unchanged.

- [ ] **Step 5: Render solar values with independent placeholders**

In `render_landscape_snapshot()`, stop formatting feels-like/humidity and update the two new labels from `weather_snapshot.solar` regardless of current-condition validity:

```cpp
const char *sunrise = weather_snapshot.solar.has_sunrise
    ? weather_snapshot.solar.sunrise : "--:--";
const char *sunset = weather_snapshot.solar.has_sunset
    ? weather_snapshot.solar.sunset : "--:--";
lv_label_set_text_fmt(landscape_sunrise, "%s %s", strings->sunrise, sunrise);
lv_label_set_text_fmt(landscape_sunset, "%s %s", strings->sunset, sunset);
```

Keep `update_home_status()` responsible only for the third label, preserving `更新时间 22:30` in landscape and the existing provider/time format in portrait.

- [ ] **Step 6: Run UI, translation, and rotation suites and verify GREEN**

Run:

```bash
python3 -m unittest \
  tests.test_landscape_weather_ui \
  tests.test_chinese_language \
  tests.test_display_rotation \
  tests.test_settings_layout_and_sound -v
```

Expected: all tests pass; portrait feels-like/humidity contracts remain present.

- [ ] **Step 7: Commit landscape rendering**

```bash
git add aura/weather.ino aura/translations.h \
  tests/test_landscape_weather_ui.py tests/test_chinese_language.py
git commit -m "横屏平铺日出日落更新时间"
```

### Task 4: Full Verification, Build, Flash, And Boot Check

**Files:**
- Build only: `/private/tmp/cyd-landscape-solar-*`

- [ ] **Step 1: Run the complete test suite**

Run: `python3 -m unittest discover -s tests -v`

Expected: every test passes with zero failures and zero errors.

- [ ] **Step 2: Create isolated sketch, build, and library directories**

```bash
sketch_dir="$(mktemp -d /private/tmp/cyd-landscape-solar-sketch.XXXXXX)"
build_dir="$(mktemp -d /private/tmp/cyd-landscape-solar-build.XXXXXX)"
library_dir="$(mktemp -d /private/tmp/cyd-landscape-solar-libs.XXXXXX)"
cp -R aura "$sketch_dir/aura"
mv "$sketch_dir/aura/weather.ino" "$sketch_dir/aura/aura.ino"
cp -R /Users/luckmiracle/Documents/Arduino/libraries/TFT_eSPI "$library_dir/TFT_eSPI"
cp TFT_eSPI/User_Setup.h "$library_dir/TFT_eSPI/User_Setup.h"
```

- [ ] **Step 3: Compile the ESP32 huge_app firmware at the proven upload speed**

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200 \
  --libraries "$library_dir" \
  --build-path "$build_dir" \
  --build-property "compiler.cpp.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -I/Users/luckmiracle/Documents/ChatGPT/CYD/lvgl/src" \
  "$sketch_dir/aura"
```

Expected: exit code 0 with flash and dynamic-memory usage reported.

- [ ] **Step 4: Validate the known CYD serial device**

Run:

```bash
arduino-cli board list
ioreg -r -c IOSerialBSDClient -l
```

Expected: `/dev/cu.usbserial-1130` is present and corresponds to the ESP32 previously identified by MAC `b4:bf:e9:0d:df:14`. Stop for confirmation if the port is absent or ambiguous.

- [ ] **Step 5: Upload with hash verification**

```bash
arduino-cli upload \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app,UploadSpeed=115200 \
  --port /dev/cu.usbserial-1130 \
  --input-dir "$build_dir" \
  --verify \
  "$sketch_dir/aura"
```

Expected: esptool writes every segment, verifies hashes, and resets the ESP32 with exit code 0.

- [ ] **Step 6: Read one complete boot and weather-refresh sequence**

Run: `arduino-cli monitor --port /dev/cu.usbserial-1130 --config baudrate=115200`

Expected: normal boot, display initialization, and a successful Open-Meteo or QWeather refresh with no reset loop, Guru Meditation, or weather-task crash. Exit the monitor after the first complete sequence.

## Self-Review

- [x] The plan covers both providers without adding a request or making solar times mandatory.
- [x] The parser validates both timestamp and `HH:mm` inputs before copying into fixed-size storage.
- [x] Landscape feels-like/humidity removal and portrait preservation have explicit tests.
- [x] All three labels have exact, non-overlapping `320 x 240` geometry and independent missing-value behavior.
- [x] Full regression, fresh compile, 115200 upload, hash verification, and serial boot checks are required before completion.
