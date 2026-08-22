# Default Weather Provider Design

## Goal

Make Open-Meteo the default Aura weather provider without requiring an API key. Keep QWeather as an explicitly selected optional provider for users who configure a key.

## Scope

- Add a persisted weather-provider preference with Open-Meteo as its default.
- Let users select Open-Meteo or QWeather from settings.
- Keep the existing QWeather API-key configuration flow.
- Continue using Open-Meteo when QWeather is selected but unavailable.
- Update localized UI strings, tests, and user documentation.

This change does not alter location search, forecast lengths, normalized weather models, chart layouts, refresh intervals, or weather icons.

## Provider Selection

Add a `WeatherProvider` enum with `OPEN_METEO` and `QWEATHER` values. Store the selected value in the existing `weather` preferences namespace. A missing, invalid, or out-of-range saved value resolves to Open-Meteo.

Open-Meteo remains the default after a firmware upgrade even when an older device already has a saved QWeather key. A saved key is credentials only; it does not imply provider selection. The user must explicitly select QWeather.

The settings screen exposes a compact provider selector containing `Open-Meteo` and the localized QWeather name. The existing QWeather API-key configuration action remains available. Changing the selector saves immediately and triggers a weather refresh when Wi-Fi is connected.

## Data Flow

`fetch_and_update_weather()` dispatches according to the validated provider preference:

1. When Open-Meteo is selected, call the existing Open-Meteo request and parser directly.
2. When QWeather is selected and a key exists, call the existing QWeather current, daily, and hourly requests.
3. When QWeather is selected without a key, report the missing credential and call Open-Meteo.
4. When any QWeather request or required response section fails, call Open-Meteo.

Both providers continue publishing the existing `WeatherSnapshot` model. Rendering remains provider-independent. The home status displays the provider that produced the successfully published snapshot, so a QWeather-to-Open-Meteo fallback is shown as Open-Meteo.

## Failure Handling

Open-Meteo request, HTTP, or parsing failure does not publish a partially initialized snapshot. The last successfully published snapshot remains visible. Diagnostics are written to the serial log.

Selecting QWeather without a configured key does not leave the home screen empty. Aura logs the missing key and uses Open-Meteo for that refresh. The QWeather key can be added through the existing non-blocking configuration portal.

No automatic fallback from Open-Meteo to QWeather is added. This keeps the default provider independent of credentials and avoids surprising API usage.

## Compatibility

The existing saved `qweatherKey` value and portal remain unchanged. Existing devices gain the new provider preference with Open-Meteo as the default. Location coordinates and all other settings retain their current storage keys and behavior.

## Testing

Static integration tests will verify:

- The provider enum and persisted preference exist.
- Missing or invalid provider preferences resolve to Open-Meteo.
- A stored QWeather key alone does not select QWeather.
- Open-Meteo selection dispatches directly to Open-Meteo.
- QWeather selection uses QWeather only when a key is present.
- Missing-key and QWeather request failures fall back to Open-Meteo.
- Provider selection is exposed in settings and localized.
- The home status continues to show the provider that returned the displayed data.

The full Python test suite and firmware compilation will be run after implementation. README instructions will describe Open-Meteo as the zero-configuration default and QWeather as optional.

## Acceptance Criteria

- A fresh or upgraded device fetches current, daily, and hourly weather from Open-Meteo without an API key.
- A user can explicitly select QWeather and configure its API key.
- QWeather failures never prevent an Open-Meteo refresh attempt.
- A previously stored QWeather key does not override the new Open-Meteo default.
- The displayed source matches the provider that supplied the visible snapshot.
