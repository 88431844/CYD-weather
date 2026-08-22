# QWeather Portal And Status Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make QWeather API-key configuration responsive and cancellable, expose a clear API-key entry on the Aura AP page, and show IP, weather source, and last update time on the home screen.

**Architecture:** Replace the blocking on-demand WiFiManager portal with one persistent non-blocking manager processed from `loop()`. A device-side LVGL message box owns the cancel action; WiFiManager callbacks signal save/timeout completion back to the main loop. Home status labels are updated from the active WiFi connection and each successful Open-Meteo or QWeather response.

**Tech Stack:** ESP32 Arduino, WiFiManager 2.0.17, LVGL 9, ArduinoJson, Python `unittest` static contracts.

---

### Task 1: Lock down portal, page, and status contracts

**Files:**
- Modify: `tests/test_qweather_integration.py`
- Modify: `tests/test_settings_layout_and_sound.py`
- Modify: `tests/test_chinese_language.py`

- [ ] **Step 1: Add failing assertions** for non-blocking portal processing, a cancel callback, the `/param` menu link, the renamed Chinese button, status labels, and update-time fields.
- [ ] **Step 2: Run `python3 -m unittest tests.test_qweather_integration tests.test_settings_layout_and_sound tests.test_chinese_language -v` and confirm the new assertions fail against the current blocking implementation.

### Task 2: Make the QWeather portal non-blocking and cancellable

**Files:**
- Modify: `aura/weather.ino`
- Modify: `aura/translations.h`

- [ ] **Step 1: Add a global `WiFiManager qweather_portal_manager`, portal-active flags, and an LVGL prompt pointer.** Configure this manager with `setConfigPortalBlocking(false)` and process it from `loop()`.
- [ ] **Step 2: Add `finish_qweather_config_portal(bool saved)` and a cancel event callback.** The callback calls `stopConfigPortal()` through the main loop, closes the AP, restores the home UI, and refreshes weather only after a saved key.
- [ ] **Step 3: Add the localized in-progress prompt and rename the Chinese settings label to `和风天气API Key配置`.** Keep the prompt visible while the AP is running and show the AP instructions plus `取消`.
- [ ] **Step 4: Add a WiFiManager menu array containing `wifi`, `param`, `custom`, `info`, and `exit`, and custom menu HTML linking to `/param` with a visible `QWeather API Key` button.** This makes the key field a dedicated page while retaining WiFiManager's standard save handling.
- [ ] **Step 5: Run the focused tests and confirm they pass.**

### Task 3: Add home-screen network/source/update status

**Files:**
- Modify: `aura/weather.ino`
- Modify: `aura/translations.h`

- [ ] **Step 1: Add two compact status labels below the forecast heading and move the forecast card down only enough to preserve the existing seven rows.** Show the local IP, `和风天气` or `Open-Meteo`, and `更新时间: YYYY-MM-DD HH:MM`.
- [ ] **Step 2: Parse QWeather `updateTime` and Open-Meteo `current.time` into the displayed timestamp after each successful response.** Refresh the IP/source labels after UI creation and after portal close.
- [ ] **Step 3: Add localized labels for IP, weather source, updated time, QWeather, and Open-Meteo; regenerate the four embedded Chinese fonts for every Chinese character used in source strings.**

### Task 4: Verify firmware and device behavior

**Files:**
- Verify: `aura/weather.ino`, `aura/translations.h`, `aura/lv_font_noto_sans_sc_12.c`, `aura/lv_font_noto_sans_sc_14.c`, `aura/lv_font_noto_sans_sc_16.c`, `aura/lv_font_noto_sans_sc_20.c`

- [ ] **Step 1: Run `python3 -m unittest discover -s tests -v` and `git diff --check`.
- [ ] **Step 2: Compile the `esp32:esp32:esp32:PartitionScheme=huge_app` firmware in `/private/tmp/cyd-build.ieKQmQ`.
- [ ] **Step 3: Upload at 115200 baud to `/dev/cu.usbserial-1130` and require esptool hash verification.
- [ ] **Step 4: Report the QWeather capability comparison separately; do not add extra weather fields until the user selects which ones to display.
