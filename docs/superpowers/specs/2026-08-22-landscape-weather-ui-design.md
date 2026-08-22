# Aura Landscape Weather UI Design

## Goal

Add a landscape weather experience to the 320 x 240 CYD display while preserving the existing 240 x 320 portrait experience. Users can choose one of four screen angles and one of five color themes from settings. Landscape daily and hourly forecasts use annotated line charts with a weather icon for every forecast point.

The approved visual direction is the B "immersive trend" layout shown in the local browser prototype. The graph is the main visual surface, with current conditions in a compact header and forecast metadata aligned beneath each graph point.

## Scope

This design covers:

- Four explicit display angles: 0, 90, 180, and 270 degrees.
- Portrait layouts at 0 and 180 degrees.
- Landscape layouts at 90 and 270 degrees.
- Touch-coordinate correction for every angle.
- Five global color themes.
- Seven-day high/low temperature charts with daily weather icons.
- Seven-hour temperature charts with hourly weather icons and precipitation probability.
- Immediate preview and persistence of theme and rotation settings.

This design does not change weather providers, forecast lengths, Wi-Fi setup, location search, sound behavior, or the existing touch-calibration flow.

## Orientation Behavior

The saved display angle is the single source of truth:

| Angle | Logical size | Layout | Physical relationship |
| --- | --- | --- | --- |
| 0 degrees | 240 x 320 | Portrait | Existing upright orientation |
| 90 degrees | 320 x 240 | Landscape | Clockwise from 0 degrees |
| 180 degrees | 240 x 320 | Portrait | Upside down from 0 degrees |
| 270 degrees | 320 x 240 | Landscape | Upside down from 90 degrees |

Settings use four direct-selection controls rather than a single rotate button. The selected angle applies immediately and is saved. The UI is recreated at the new logical size without fetching weather again. Existing in-memory forecast data repopulates the new layout.

Touch input is converted from calibrated raw coordinates into normalized portrait coordinates, then transformed for the selected angle. Calibration values remain orientation-independent, so changing rotation does not require another calibration. The display and touch transforms must be applied as one operation to prevent a visible orientation with stale touch mapping.

If the stored rotation value is invalid, Aura falls back to 0 degrees. The initial default is 0 degrees to preserve current installations.

## Theme System

Themes replace color tokens only. Layout, fonts, dimensions, icon assets, and interaction behavior remain identical. The theme applies to portrait, landscape, settings, splash, and modal surfaces.

| Theme | Background | Panel | Text | Muted | High temperature | Low temperature | Accent |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Deep Sea | `#101820` | `#1B2932` | `#F8FBFC` | `#8FA5AF` | `#FFD25F` | `#63C6FF` | `#73E1D5` |
| Clear Sky | `#F6FAFB` | `#E5EEF1` | `#18333D` | `#647981` | `#DF633D` | `#197FAD` | `#087B73` |
| Rainforest | `#132019` | `#21362C` | `#F2F8F4` | `#9AB5A6` | `#FFC857` | `#7BCBE6` | `#65D49E` |
| Sunset | `#2B1B29` | `#462839` | `#FFF6F7` | `#C8A8B4` | `#FFBA62` | `#6FD1D8` | `#FF7C79` |
| High Contrast | `#050606` | `#202323` | `#FFFFFF` | `#C8CCCC` | `#FFE100` | `#00D9FF` | `#FFFFFF` |

Deep Sea is the default when no saved theme exists. An invalid stored theme also falls back to Deep Sea.

Theme names and new display-setting labels are added to every existing language in `translations.h`. The Simplified Chinese labels are the primary visual-fit reference because they exercise the bundled CJK fonts.

## Landscape Home Layout

The landscape home screen uses the approved immersive trend layout at 320 x 240:

- The top-left header shows location, weather source, and compact update time.
- The top-right header contains a two-option `7 days / Hourly` segmented control and a settings icon button.
- Current temperature, condition, feels-like temperature, and humidity occupy a compact summary row below the location.
- The remaining surface is an unframed chart region. Subtle grid lines provide scale without enclosing the chart in a card.
- Daily/hourly metadata is arranged in seven equal-width columns aligned with the seven graph points.

The layout uses stable tracks and fixed chart bounds. Dynamic labels may truncate with an ellipsis, but graph points, icons, temperatures, and time/date labels must never shift the chart geometry.

### Seven-Day View

The daily view plots two lines:

- Daily maximum temperature uses the theme's high-temperature color.
- Daily minimum temperature uses the theme's low-temperature color.

Each of the seven date columns contains:

- `Today` or a compact `MM/DD` date.
- A 20 x 20 existing Aura weather icon.
- A compact condition label such as `Light rain`, `Snow`, or `Thunderstorm`.

Every high and low point has a numeric degree label. For example, the 08/23 column shows a 30-degree high point, a 26-degree low point, the light-rain icon, and `Light rain` beneath it. The icon derives from the same weather code already used by the portrait daily list.

### Hourly View

The hourly view plots one temperature line across the existing seven forecast hours. Each point has a numeric degree label.

Each time column contains:

- `Now` or a compact hour label.
- A 20 x 20 weather icon derived from that hour's weather code and day/night value.
- A compact condition label.
- Precipitation probability when supplied by the provider.

The weather icon and precipitation value belong to the same forecast index as the graph point. Missing precipitation probability displays no percentage rather than a placeholder percentage.

### Forecast Switching

The segmented control changes between daily and hourly data without navigation or animation that delays reading. The selected view remains in memory while the home screen is active. Changing theme or rotation preserves the selected forecast view.

## Portrait Home Layout

Portrait mode retains the existing current-weather header and seven-row daily/hourly forecast presentation because 240 pixels is too narrow for seven annotated graph columns. The five theme palettes apply to the portrait screen, and 180-degree rotation fully inverts display and touch input.

No chart is added to portrait mode in this scope.

## Settings Layout

The settings window adds a `Display` group near the existing brightness and night-mode controls:

- `Theme`: five compact swatch controls with translated names.
- `Screen orientation`: direct controls for 0, 90, 180, and 270 degrees.
- `Auto-correct touch`: shown as enabled and kept enabled because display rotation without touch correction is not a supported state.

Theme changes recolor the open settings window immediately. Rotation changes recreate the settings window in the new orientation and keep the user within settings. Both values are written to preferences as soon as they are applied.

The settings close button, scrolling behavior, minimum touch targets, and existing controls remain available in all four orientations. Landscape settings may use two columns where space permits, but control order must match portrait mode.

## Components And Ownership

The implementation should separate these responsibilities:

- `DisplayPreferences`: validates, loads, and saves theme and rotation values.
- `ThemePalette`: returns all semantic colors for the selected theme.
- `DisplayTransform`: applies display rotation, exposes logical width/height, and maps normalized touch coordinates.
- `PortraitWeatherView`: creates and updates the existing list-based layout.
- `LandscapeWeatherView`: creates and updates the daily and hourly chart layout.
- `ForecastChart`: owns chart geometry, lines, nodes, temperature labels, aligned time/date columns, and weather icons.

The current provider parsing remains responsible for normalized daily and hourly values. UI components consume those values and do not call weather services directly.

## Data Flow

1. Boot loads and validates saved rotation and theme before creating the LVGL display and root UI.
2. The display transform establishes the logical resolution and touch mapping.
3. The selected theme initializes semantic UI colors.
4. `create_ui()` chooses the portrait or landscape view from the logical resolution.
5. Weather providers populate the same seven daily and seven hourly normalized entries used today.
6. The active view updates labels, images, and chart series from those entries.
7. Theme changes restyle/recreate the active UI using cached weather data.
8. Rotation changes apply the display and touch transform together, recreate the appropriate layout, and repopulate it from cached weather data.

## Failure Handling

- Invalid preference values fall back to rotation 0 and Deep Sea.
- Missing forecast entries remain hidden rather than drawing false zero-degree points.
- Missing weather icons use the existing partly-cloudy fallback.
- Missing precipitation values omit the percentage.
- A theme or rotation change must not trigger network access. If no cached data exists, the normal placeholders remain until the next scheduled fetch.
- Rotation must not leave settings or the keyboard attached to deleted LVGL objects. Any active keyboard is detached before recreating the UI.

## Performance And Memory

The implementation reuses existing 20 x 20 forecast icon assets. Chart series store at most seven values and use fixed-size arrays. Theme palettes are static constants. UI recreation is permitted for theme and rotation changes because these are infrequent settings actions; per-frame allocation or animation is not required.

Grid lines, graph lines, and point markers should use LVGL primitives. New full-screen bitmap backgrounds are not part of this design.

## Verification

Automated checks cover:

- Preference defaults, validation, and persistence.
- Logical display dimensions for all four angles.
- Touch-coordinate transforms at corners, center, and calibration edges for all four angles.
- Orientation-specific selection of portrait and landscape views.
- Seven daily high/low values and seven hourly values reaching chart series.
- Daily and hourly icon assignment by matching forecast index.
- Theme token presence and translation keys.
- Existing settings, sound, provider, and touch-calibration tests remaining green.

Visual checks cover all four rotations, both forecast tabs, settings scrolling, all five themes, rain/snow/thunderstorm icons, long translated labels, and missing-data placeholders. At 320 x 240, no temperature, icon, date/time, settings control, or status text may overlap or resize the chart.

## Acceptance Criteria

- Users can select and persist all five themes.
- Users can select and persist 0, 90, 180, or 270 degrees.
- Display and touch orientation stay synchronized immediately after selection and after reboot.
- Landscape daily mode shows seven high and seven low points with numeric labels and seven weather icons.
- Landscape hourly mode shows seven temperature points with numeric labels, seven weather icons, and available precipitation probabilities.
- Portrait mode remains list-based and works at both 0 and 180 degrees.
- Changing theme or rotation does not fetch weather and does not lose the currently displayed forecast data.
- The approved B visual hierarchy and color themes are preserved without clipped or overlapping content.
