# Five-Point Touch Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a five-point XPT2046 resistive touchscreen calibration flow to the Aura firmware, verify it on the ESP32-2432S028R, and flash it without committing or pushing until the user confirms the device works.

**Architecture:** Keep the affine fitting and coordinate application in `aura/touch_calibration.h`, a small Arduino-independent header that can be exercised by host tests. `aura/weather.ino` owns the modal LVGL calibration UI, raw sample state machine, Preferences persistence, and integration with `touchscreen_read()`. The existing fixed `map()` conversion remains the runtime fallback whenever six valid coefficients are not loaded.

**Tech Stack:** Arduino ESP32, LVGL 9, XPT2046_Touchscreen, Preferences, C++17 host math tests, Python `unittest` static regression tests, Arduino CLI build/upload.

---

### Task 1: Add failing calibration contract and math tests

**Files:**
- Create: `tests/test_touch_calibration.py`
- Create: `tests/test_touch_calibration_math.py`

- [ ] **Step 1: Write static contract tests before production code**

Assert that the firmware declares the five target order, calibration state, six persisted coefficient keys, calibrated coordinate application, fallback `map()` calls, localized settings entry, release gate, timeout, and click sound. These checks must fail because none of those symbols exist yet.

- [ ] **Step 2: Write host-side affine behavior tests**

Use five raw points and screen points generated from:

```text
screen_x = 0.50 * raw_x + 0.10 * raw_y + 4
screen_y = -0.05 * raw_x + 0.80 * raw_y + 8
```

The tests cover exact coefficient recovery within `1e-5`, conversion of a new raw point, rejection of collinear raw points, rejection of raw values outside `0..4095`, and fallback mapping when calibration is invalid.

- [ ] **Step 3: Run the focused tests and verify the expected red failure**

Run:

```bash
python3 -m unittest tests.test_touch_calibration tests.test_touch_calibration_math -v
```

Expected: failures identifying missing calibration symbols/implementation, while the three existing regression test modules remain untouched.

### Task 2: Implement tested affine math

**Files:**
- Create: `aura/touch_calibration.h`

- [ ] **Step 1: Implement the minimal pure C++ API**

Define `TouchRawPoint`, `TouchScreenPoint`, and `TouchCalibration` with six coefficients and a `valid` flag. Implement `fit_touch_calibration(raw, screen, count, out)` using the normal equations for `[raw_x, raw_y, 1]` and partial-pivot 3x3 Gaussian elimination. Reject fewer than three points, non-finite/out-of-range raw samples, singular matrices, non-finite coefficients, or a near-zero affine determinant.

Implement `apply_touch_calibration(calibration, raw_x, raw_y, width, height, out_x, out_y)` to return `false` for invalid coefficients, calculate both axes, reject non-finite results, and clamp valid results to the display rectangle.

- [ ] **Step 2: Run the host math tests**

Run:

```bash
python3 -m unittest tests.test_touch_calibration_math -v
```

Expected: PASS for fit, conversion, invalid samples, singular samples, and fallback behavior.

- [ ] **Step 3: Commit only if explicitly requested**

Do not commit at this checkpoint because the user requested a flash-and-confirm workflow.

### Task 3: Add localized calibration UI strings

**Files:**
- Modify: `aura/translations.h`

- [ ] **Step 1: Extend `LocalizedStrings`**

Add fields named `touch_calibration`, `calibration_instructions`, `calibration_progress`, `calibration_cancel`, `calibration_success`, and `calibration_failed` after `use_night_mode` so every language initializer must provide all six values.

- [ ] **Step 2: Add translations for all eight languages**

Include Simplified Chinese text such as `触摸校准`, `请依次点击屏幕上的五个目标点`, `校准点 %d/5`, `取消校准`, `触摸校准完成`, and `触摸校准失败，已保留原设置`; use equivalent concise text for English, Spanish, German, French, Turkish, Swedish, and Italian.

- [ ] **Step 3: Run localization regression tests**

Run:

```bash
python3 -m unittest tests.test_chinese_language -v
```

Expected: PASS, including embedded glyph coverage for the newly added Chinese characters.

### Task 4: Integrate five-point sampling and persistence into firmware

**Files:**
- Modify: `aura/weather.ino`

- [ ] **Step 1: Add calibration constants, state, and declarations**

Include `touch_calibration.h`. Add five screen targets in this exact order: `(24, 44)`, `(216, 44)`, `(120, 160)`, `(24, 276)`, `(216, 276)`. Add a 12-sample average buffer, `WAIT_PRESS`/`WAIT_RELEASE` state, a 120-second timeout, raw range `0..4095`, and overlay/timer pointers. Keep the active saved calibration separate from pending raw samples.

- [ ] **Step 2: Load and save coefficients atomically**

Load `touchCalibrated`, `touchCalA`, `touchCalB`, `touchCalC`, `touchCalD`, `touchCalE`, and `touchCalF` after `prefs.begin()`. Save the six floats and the validity flag only after all five points fit successfully. On cancel, timeout, invalid sample, or invalid fit, leave the active calibration and Preferences unchanged.

- [ ] **Step 3: Apply calibration in `touchscreen_read()` with fallback**

Capture raw XPT2046 points while the calibration overlay is active, expose fallback-mapped coordinates to LVGL so the cancel button remains clickable, and otherwise use `apply_touch_calibration()` when the saved calibration is valid. Preserve the current mapping exactly when calibration is invalid or application fails.

- [ ] **Step 4: Implement the modal LVGL workflow**

Add a Settings button. Starting calibration hides Settings and creates a full-size overlay with instruction text, progress text, a visible target, and a Cancel button. Accept a target only after at least 8 valid samples from a stable press; average the raw samples, play the existing click sound, require a release before advancing, and update the target order. On success, fit/store the affine transform, restore Settings, and show the localized success message. On failure or timeout, restore Settings and show the localized failure message.

- [ ] **Step 5: Run all host tests and static checks**

Run:

```bash
python3 -m unittest discover -s tests -v
```

Expected: all existing display, language, speaker, calibration contract, and math tests pass.

### Task 5: Compile, inspect, and flash the uncommitted firmware

**Files:**
- Build only: temporary Arduino/TFT_eSPI staging directories outside the repository

- [ ] **Step 1: Compile with the CYD display configuration**

Use the ESP32 board target `esp32:esp32:esp32:PartitionScheme=huge_app`, LVGL flags `-DLV_CONF_INCLUDE_SIMPLE -I/Users/luckmiracle/Documents/ChatGPT/CYD/lvgl/src`, and a temporary full TFT_eSPI library copy containing the repository `TFT_eSPI/User_Setup.h`, because TFT_eSPI implementation files compile separately from `.ino` macros.

- [ ] **Step 2: Confirm the generated firmware is for `/dev/cu.usbserial-1140`**

Check the serial device exists and record the build artifact and size before upload. Do not upload if compilation fails or the port is absent.

- [ ] **Step 3: Upload and verify at 115200 baud**

Upload with `--verify` to `/dev/cu.usbserial-1140`. Capture the uploader result. The implementation remains uncommitted and unpushed after this step.

- [ ] **Step 4: Hand off for physical confirmation**

Tell the user the firmware was flashed and ask them to verify the five-point flow, normal weather display, touch response, and click sound. Stop before `git commit` or `git push` until the user confirms.

### Self-review checklist

- [ ] Five targets are presented in the approved order and require release between points.
- [ ] Multiple raw samples are averaged and invalid raw values are rejected.
- [ ] Affine fit corrects scale, offset, rotation, and cross-axis error.
- [ ] Six coefficients persist under named Preferences keys and are applied before LVGL receives coordinates.
- [ ] No valid calibration preserves the existing fixed map behavior.
- [ ] Cancel, timeout, bad samples, and bad transforms preserve the previous calibration.
- [ ] All eight languages have strings and Chinese glyph coverage is tested.
- [ ] Static tests and host math tests pass before compile/upload.
- [ ] The code is flashed but not committed/pushed before user confirmation.
