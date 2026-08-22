# Default Weather Provider Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Open-Meteo Aura's zero-configuration default while keeping QWeather as a persisted, explicitly selected provider with Open-Meteo fallback.

**Architecture:** Add a validated provider preference beside the existing weather source status, then make `fetch_and_update_weather()` dispatch before entering the QWeather request path. Reuse the normalized `WeatherSnapshot`, existing Open-Meteo parser, QWeather portal, settings row patterns, and source status rendering.

**Tech Stack:** ESP32 Arduino, Preferences, LVGL 9, WiFiManager, ArduinoJson, Python `unittest` static contracts.

---

### Task 1: Lock Down Provider Preference And Dispatch

**Files:**
- Modify: `tests/test_qweather_integration.py`
- Modify: `aura/weather.ino`

- [x] **Step 1: Write failing provider preference tests**

Add tests requiring a two-value enum, Open-Meteo default, validated preference load, and early Open-Meteo dispatch:

```python
def test_open_meteo_is_the_validated_default_provider(self):
    self.assertRegex(
        WEATHER,
        r"enum\s+WeatherProvider\s*:\s*uint8_t\s*\{\s*"
        r"WEATHER_PROVIDER_OPEN_METEO\s*=\s*0\s*,\s*"
        r"WEATHER_PROVIDER_QWEATHER\s*=\s*1\s*\}",
    )
    self.assertRegex(
        WEATHER,
        r"static\s+WeatherProvider\s+weather_provider\s*=\s*"
        r"WEATHER_PROVIDER_OPEN_METEO",
    )
    self.assertIn(
        'prefs.getUInt("weatherProvider", WEATHER_PROVIDER_OPEN_METEO)',
        WEATHER,
    )
    self.assertRegex(
        WEATHER,
        r"validated_weather_provider\s*\([^)]*\)[\s\S]*?"
        r"return WEATHER_PROVIDER_OPEN_METEO;",
    )

def test_fetch_dispatches_open_meteo_before_qweather(self):
    fetch = WEATHER[
        WEATHER.rindex("void fetch_and_update_weather() {") :
        WEATHER.index("const lv_img_dsc_t* choose_image")
    ]
    dispatch = "weather_provider == WEATHER_PROVIDER_OPEN_METEO"
    self.assertIn(dispatch, fetch)
    self.assertLess(fetch.index(dispatch), fetch.index("strlen(qweather_key)"))
```

- [x] **Step 2: Run the focused tests and verify they fail**

Run `python3 -m unittest tests.test_qweather_integration -v`.

Expected: the new provider tests fail because the enum, preference, and dispatch do not exist.

- [x] **Step 3: Implement the validated provider preference**

Add near `WeatherSource`:

```cpp
enum WeatherProvider : uint8_t {
  WEATHER_PROVIDER_OPEN_METEO = 0,
  WEATHER_PROVIDER_QWEATHER = 1
};

static WeatherProvider validated_weather_provider(uint32_t value) {
  return value == WEATHER_PROVIDER_QWEATHER
      ? WEATHER_PROVIDER_QWEATHER
      : WEATHER_PROVIDER_OPEN_METEO;
}

static WeatherProvider weather_provider = WEATHER_PROVIDER_OPEN_METEO;
```

Load it during setup independently from the saved API key:

```cpp
weather_provider = validated_weather_provider(
    prefs.getUInt("weatherProvider", WEATHER_PROVIDER_OPEN_METEO));
```

Dispatch immediately after the Wi-Fi guard:

```cpp
if (weather_provider == WEATHER_PROVIDER_OPEN_METEO) {
  fetch_open_meteo_weather();
  return;
}
```

Keep every existing QWeather missing-key or request-failure branch calling `fetch_open_meteo_weather()`.

- [x] **Step 4: Run the focused tests and verify they pass**

Run `python3 -m unittest tests.test_qweather_integration -v`.

Expected: all QWeather/Open-Meteo integration tests pass.

### Task 2: Add The Persisted Settings Selector

**Files:**
- Modify: `tests/test_qweather_integration.py`
- Modify: `tests/test_settings_layout_and_sound.py`
- Modify: `aura/weather.ino`
- Modify: `aura/translations.h`

- [x] **Step 1: Write failing settings and localization tests**

Add contracts for the dropdown, label, persistence, refresh, and missing-key portal:

```python
def test_settings_selects_and_persists_weather_provider(self):
    for symbol in (
        "weather_provider_dropdown",
        "strings->weather_provider",
        "strings->open_meteo_name",
        "strings->qweather_name",
        'prefs.putUInt("weatherProvider", weather_provider)',
        "lv_dropdown_get_selected(weather_provider_dropdown)",
    ):
        self.assertIn(symbol, WEATHER)

def test_weather_provider_label_is_localized(self):
    self.assertIn("const char* weather_provider;", TRANSLATIONS)
    self.assertIn('"Weather provider:"', TRANSLATIONS)
    self.assertIn('"天气源:"', TRANSLATIONS)
```

- [x] **Step 2: Run focused tests and verify they fail**

Run `python3 -m unittest tests.test_qweather_integration tests.test_settings_layout_and_sound tests.test_chinese_language -v`.

Expected: failures for the absent dropdown, persistence call, and localized field.

- [x] **Step 3: Add the localized settings label**

Add `const char* weather_provider;` before `qweather_config` in `LocalizedStrings`. Add one value at the same position in all eight initializers: `Weather provider:`, `Proveedor:`, `Wetterdienst:`, `Service météo :`, `Hava durumu:`, `Väderkälla:`, `Servizio meteo:`, and `天气源:`.

- [x] **Step 4: Build and handle the provider dropdown**

Declare and clear `weather_provider_dropdown`. Insert a 42-pixel settings row before the QWeather configuration button:

```cpp
lv_obj_t *provider_row = create_row(42);
lv_obj_t *provider_label = lv_label_create(provider_row);
lv_label_set_text(provider_label, strings->weather_provider);
style_label(provider_label);
weather_provider_dropdown = lv_dropdown_create(provider_row);
String provider_options = String(strings->open_meteo_name) + "\n" +
                          strings->qweather_name;
lv_dropdown_set_options(weather_provider_dropdown, provider_options.c_str());
lv_dropdown_set_selected(weather_provider_dropdown, weather_provider);
lv_obj_set_width(weather_provider_dropdown, 132);
apply_dropdown_theme(weather_provider_dropdown);
lv_obj_align(weather_provider_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
lv_obj_add_event_cb(weather_provider_dropdown, settings_event_handler,
                    LV_EVENT_VALUE_CHANGED, nullptr);
```

Handle selection immediately while preserving QWeather as the saved selection if its key is missing:

```cpp
if (tgt == weather_provider_dropdown && code == LV_EVENT_VALUE_CHANGED) {
  weather_provider = validated_weather_provider(
      lv_dropdown_get_selected(weather_provider_dropdown));
  prefs.putUInt("weatherProvider", weather_provider);
  fetch_and_update_weather();
  if (weather_provider == WEATHER_PROVIDER_QWEATHER &&
      strlen(qweather_key) == 0) {
    open_qweather_config_portal();
  }
  return;
}
```

- [x] **Step 5: Run focused tests and verify they pass**

Run `python3 -m unittest tests.test_qweather_integration tests.test_settings_layout_and_sound tests.test_chinese_language -v`.

Expected: all focused tests pass.

### Task 3: Document The Default And Verify The Firmware

**Files:**
- Modify: `tests/test_build_documentation.py`
- Modify: `README.md`
- Verify: `aura/weather.ino`

- [x] **Step 1: Write a failing README contract**

```python
def test_readme_documents_weather_provider_behavior(self):
    for fact in (
        "Open-Meteo", "无需 API Key", "默认天气源",
        "和风天气", "可选天气源",
    ):
        self.assertIn(fact, README)
```

- [x] **Step 2: Run the documentation test and verify it fails**

Run `python3 -m unittest tests.test_build_documentation -v`.

Expected: failure because the README does not explain provider selection.

- [x] **Step 3: Add the weather source documentation**

Add a `### 天气数据源` section explaining that Open-Meteo is the default, needs no API key, supplies current/daily/hourly data, and that QWeather is optional and falls back to Open-Meteo when its key or request is unavailable.

- [x] **Step 4: Run all tests and static checks**

Run:

```bash
python3 -m unittest discover -s tests -v
git diff --check
```

Expected: all tests pass and `git diff --check` produces no output.

- [x] **Step 5: Compile the firmware in an isolated temporary sketch directory**

Run:

```bash
sketch_dir="$(mktemp -d /private/tmp/cyd-default-provider.XXXXXX)"
build_dir="$(mktemp -d /private/tmp/cyd-default-provider-build.XXXXXX)"
library_dir="$(mktemp -d /private/tmp/cyd-default-provider-libs.XXXXXX)"
cp -R aura "$sketch_dir/aura"
mv "$sketch_dir/aura/weather.ino" "$sketch_dir/aura/aura.ino"
cp -R /Users/luckmiracle/Documents/Arduino/libraries/TFT_eSPI "$library_dir/TFT_eSPI"
cp TFT_eSPI/User_Setup.h "$library_dir/TFT_eSPI/User_Setup.h"
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
  --libraries "$library_dir" \
  --build-path "$build_dir" \
  --build-property "compiler.cpp.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -I/Users/luckmiracle/Documents/ChatGPT/CYD/lvgl/src" \
  "$sketch_dir/aura"
```

Expected: Arduino CLI exits successfully with flash and RAM usage summaries. The
temporary TFT_eSPI copy must contain the repository `TFT_eSPI/User_Setup.h`; using
the installed library configuration directly can compile a binary for the wrong
display driver and pins.

- [x] **Step 6: Review the final diff**

Run `git diff --stat` and inspect the diffs for `aura/weather.ino`, `aura/translations.h`, `README.md`, and the three modified test files.

Expected: only the approved provider preference, selector, localization, tests, and documentation changes are present.

### Task 4: Address Review Findings

**Files:**
- Modify: `tests/test_qweather_integration.py`
- Modify: `aura/weather.ino`

- [x] **Step 1: Reject wholly unusable forecast sections**

Count valid daily and hourly points while retaining per-point tolerance. Open-Meteo keeps the previous snapshot when either section has no valid points; QWeather falls back to Open-Meteo.

- [x] **Step 2: Defer provider refresh outside the LVGL event**

The settings event persists the selection, opens the missing-key portal when needed, and sets `weather_refresh_requested`. The main loop processes the portal before consuming the queued refresh.

- [x] **Step 3: Re-run verification**

Run `python3 -m unittest discover -s tests -v`, `git diff --check`, and the isolated `arduino-cli compile` command from Task 3.

Expected: all 150 tests pass, static diff checks are clean, and firmware compilation succeeds.
