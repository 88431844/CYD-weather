# Touch Calibration Design

## Goal

Add a five-point calibration workflow for the XPT2046 resistive touchscreen used by the ESP32-2432S028R. The workflow must improve coordinate accuracy without making an uncalibrated device unusable.

## User Flow

1. The user opens Settings and selects Touch Calibration.
2. The app displays five targets in this order: top-left, top-right, center, bottom-left, and bottom-right.
3. The user presses and holds each target until a sample is accepted. A short click sound confirms each accepted point.
4. The app validates the samples, saves the calibration, and returns to Settings with a success message.
5. Cancel, timeout, invalid samples, or an invalid transform leave the previous calibration unchanged.

The calibration screen is modal and temporarily consumes touchscreen input so normal weather controls cannot react to calibration taps.

## Coordinate Model

The touchscreen driver provides raw `(x, y)` samples. Five target points provide enough redundancy to fit two affine equations:

```text
screen_x = a * raw_x + b * raw_y + c
screen_y = d * raw_x + e * raw_y + f
```

The six coefficients are calculated with a least-squares solve over the five samples. This corrects offset, scale, minor rotation, and X/Y cross-axis error. The coefficients are stored as Preferences values and applied in `touchscreen_read()` before LVGL receives the point.

If no valid coefficients are stored, the existing fixed mapping remains the fallback.

## Sampling and Validation

- Each target accepts a stable press after collecting multiple raw samples and averaging them.
- A release is required before advancing to the next target, preventing one long press from completing multiple points.
- Samples are rejected when raw values are outside the expected XPT2046 range or when the five target points do not produce a numerically valid transform.
- Calibration parameters are written only after all points and validation succeed.

## UI and Localization

Settings receives a Touch Calibration button. Calibration instructions, target progress, cancel, success, and failure messages are added to every supported language, including Simplified Chinese. Existing settings layout, language persistence, and click sounds remain unchanged.

## Testing

- Static tests verify the settings entry, calibration state machine, persistence keys, and use of calibrated coordinates.
- Host-side tests cover affine fitting, coordinate conversion, invalid sample rejection, and fallback behavior.
- The firmware is compiled with the verified CYD TFT_eSPI configuration before any device upload.
