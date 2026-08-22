#include <Arduino.h>
#include "esp_arduino_version.h"
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <lvgl.h>

// TFT_eSPI/User_Setup.h is shared by this sketch and the LVGL/TFT library
// translation units. Keeping the setup in one header prevents class-layout
// mismatches that can corrupt the setup stack during display initialization.
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <miniz.h>
#include "esp_system.h"
#include "display_config.h"
#include "forecast_model.h"
#include "translations.h"
#include "touch_calibration.h"

#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS
#define LCD_BACKLIGHT_PIN 21
#define SPEAKER_PIN 26   // On-board speaker/buzzer on ESP32-2432S028R
#define SPEAKER_LEDC_CHANNEL 0
#define SPEAKER_DUTY 24
#define CLICK_SOUND_REFRESH_DELAY_MS 60
#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 320
#define DRAW_BUF_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 10 * (LV_COLOR_DEPTH / 8))

#define LATITUDE_DEFAULT "22.5431"
#define LONGITUDE_DEFAULT "114.0579"
#define LOCATION_DEFAULT "Shenzhen"
#define DEFAULT_CAPTIVE_SSID "Aura"
#define QWEATHER_API_BASE "https://devapi.qweather.com"
#define QWEATHER_API_KEY_MAX_LENGTH 64
#define QWEATHER_MAX_DECOMPRESSED_SIZE (64 * 1024UL)
#define WEATHER_HTTP_TIMEOUT_MS 5000
#define WIFI_CONNECT_TIMEOUT_MS 15000UL
#define UPDATE_INTERVAL 600000UL  // 10 minutes

// Night mode starts at 10pm and ends at 6am
#define NIGHT_MODE_START_HOUR 22
#define NIGHT_MODE_END_HOUR 6

#define TOUCH_CALIBRATION_SAMPLE_COUNT 12
#define TOUCH_CALIBRATION_MIN_SAMPLES 8
#define TOUCH_CALIBRATION_TIMEOUT_MS 120000UL
#define TOUCH_CALIBRATION_RELEASE_MS 80UL
#define TOUCH_CALIBRATION_RAW_MIN 0
#define TOUCH_CALIBRATION_RAW_MAX 4095
#define TOUCH_CALIBRATION_VERSION 2

static const TouchScreenPoint TOUCH_CALIBRATION_TARGETS[] = {
  {18, 18}, {222, 18}, {120, 160}, {18, 302}, {222, 302}
};

enum TouchCalibrationState {
  TOUCH_CALIBRATION_WAIT_PRESS,
  TOUCH_CALIBRATION_WAIT_RELEASE
};

LV_FONT_DECLARE(lv_font_montserrat_latin_12);
LV_FONT_DECLARE(lv_font_montserrat_latin_14);
LV_FONT_DECLARE(lv_font_montserrat_latin_16);
LV_FONT_DECLARE(lv_font_montserrat_latin_20);
LV_FONT_DECLARE(lv_font_montserrat_latin_42);
LV_FONT_DECLARE(lv_font_noto_sans_sc_12);
LV_FONT_DECLARE(lv_font_noto_sans_sc_14);
LV_FONT_DECLARE(lv_font_noto_sans_sc_16);
LV_FONT_DECLARE(lv_font_noto_sans_sc_20);

static Language current_language = LANG_ZH;

// Font selection based on language
const lv_font_t* get_font_12() {
  return current_language == LANG_ZH ? &lv_font_noto_sans_sc_12 : &lv_font_montserrat_latin_12;
}

const lv_font_t* get_font_14() {
  return current_language == LANG_ZH ? &lv_font_noto_sans_sc_14 : &lv_font_montserrat_latin_14;
}

const lv_font_t* get_font_16() {
  return current_language == LANG_ZH ? &lv_font_noto_sans_sc_16 : &lv_font_montserrat_latin_16;
}

const lv_font_t* get_font_20() {
  return current_language == LANG_ZH ? &lv_font_noto_sans_sc_20 : &lv_font_montserrat_latin_20;
}

const lv_font_t* get_font_42() {
  // The large temperature label only contains ASCII digits and the degree sign.
  return &lv_font_montserrat_latin_42;
}

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);
uint32_t draw_buf[DRAW_BUF_SIZE / 4];
int x, y, z;
static lv_display_t *display = nullptr;
static lv_indev_t *touch_indev = nullptr;
static ScreenRotation current_rotation = SCREEN_ROTATION_0;
static ThemeId current_theme = THEME_DEEP_SEA;

// Preferences
static Preferences prefs;
static bool use_fahrenheit = false;
static bool use_24_hour = false; 
static bool use_night_mode = false;
static bool sound_enabled = true;
static uint8_t sound_effect = 0;
static char qweather_key[QWEATHER_API_KEY_MAX_LENGTH] = "";
static WiFiManagerParameter qweather_key_param(
    "qweatherKey", "QWeather API Key", "", QWEATHER_API_KEY_MAX_LENGTH);
static WiFiManager wifi_manager;
static bool wifi_manager_configured = false;
static bool wifi_connection_started = false;
static bool wifi_config_portal_started = false;
static bool wifi_splash_active = false;
static uint32_t wifi_connect_started_ms = 0;
static WiFiManager qweather_portal_manager;
static bool qweather_portal_manager_configured = false;
static const char qweather_menu_html[] =
    "<p><a class='button' href='/param'>QWeather API Key</a></p>";
static const char *qweather_menu[] = {"wifi", "param", "custom", "info", "exit"};
static bool qweather_portal_active = false;
static bool qweather_portal_start_requested = false;
static bool qweather_portal_params_saved = false;
static bool qweather_portal_cancel_requested = false;
static bool qweather_portal_timed_out = false;
static lv_obj_t *qweather_portal_prompt = nullptr;
static char latitude[16] = LATITUDE_DEFAULT;
static char longitude[16] = LONGITUDE_DEFAULT;
static String location = String(LOCATION_DEFAULT);
static char dd_opts[512];
static DynamicJsonDocument geoDoc(8 * 1024);
static JsonArray geoResults;

enum WeatherSource {
  WEATHER_SOURCE_UNKNOWN,
  WEATHER_SOURCE_QWEATHER,
  WEATHER_SOURCE_OPEN_METEO
};

enum ForecastView : uint8_t { FORECAST_DAILY, FORECAST_HOURLY };

static uint8_t weather_source = WEATHER_SOURCE_UNKNOWN;
static String weather_updated_at;
static WeatherSnapshot weather_snapshot{};
static ForecastView active_forecast_view = FORECAST_DAILY;

// Screen dimming variables
static bool night_mode_active = false;
static bool temp_screen_wakeup_active = false;
static lv_timer_t *temp_screen_wakeup_timer = nullptr;
static lv_timer_t *startup_weather_timer = nullptr;
static lv_timer_t *speaker_timer = nullptr;
static uint8_t speaker_sequence_step = 0;

// UI components
static lv_obj_t *lbl_today_temp;
static lv_obj_t *lbl_today_feels_like;
static lv_obj_t *img_today_icon;
static lv_obj_t *lbl_forecast;
static lv_obj_t *box_daily;
static lv_obj_t *box_hourly;
static lv_obj_t *lbl_daily_day[7];
static lv_obj_t *lbl_daily_high[7];
static lv_obj_t *lbl_daily_low[7];
static lv_obj_t *img_daily[7];
static lv_obj_t *lbl_hourly[7];
static lv_obj_t *lbl_precipitation_probability[7];
static lv_obj_t *lbl_hourly_temp[7];
static lv_obj_t *img_hourly[7];
static lv_obj_t *lbl_home_location;
static lv_obj_t *lbl_settings_location;
static lv_obj_t *loc_ta;
static lv_obj_t *results_dd;
static lv_obj_t *btn_close_loc;
static lv_obj_t *btn_close_obj;
static lv_obj_t *kb;
static lv_obj_t *settings_win;
static lv_obj_t *location_win = nullptr;
static lv_obj_t *unit_switch;
static lv_obj_t *clock_24hr_switch;
static lv_obj_t *night_mode_switch;
static lv_obj_t *language_dropdown;
static lv_obj_t *lbl_clock;
static lv_obj_t *lbl_network_status;
static lv_obj_t *lbl_update_status;
static lv_obj_t *touch_calibration_btn;
static lv_obj_t *sound_enabled_switch;
static lv_obj_t *sound_effect_dropdown;
static lv_obj_t *qweather_config_btn;

static constexpr int LANDSCAPE_HEADER_HEIGHT = 58;
static constexpr int LANDSCAPE_CHART_X = 6;
static constexpr int LANDSCAPE_CHART_Y = 62;
static constexpr int LANDSCAPE_CHART_WIDTH = 308;
static constexpr int LANDSCAPE_CHART_HEIGHT = 108;
static constexpr int LANDSCAPE_COLUMN_Y = 174;
static constexpr int LANDSCAPE_COLUMN_WIDTH = 44;

static lv_obj_t *daily_chart;
static lv_obj_t *hourly_chart;
static lv_chart_series_t *daily_high_series;
static lv_chart_series_t *daily_low_series;
static lv_chart_series_t *hourly_temperature_series;
static int32_t daily_high_values[FORECAST_POINT_COUNT];
static int32_t daily_low_values[FORECAST_POINT_COUNT];
static int32_t hourly_temperature_values[FORECAST_POINT_COUNT];
static float daily_high_display_temperatures[FORECAST_POINT_COUNT];
static float daily_low_display_temperatures[FORECAST_POINT_COUNT];
static float hourly_display_temperatures[FORECAST_POINT_COUNT];
static bool daily_point_renderable[FORECAST_POINT_COUNT];
static bool hourly_point_renderable[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_daily_dates[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_daily_icons[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_daily_conditions[FORECAST_POINT_COUNT];
static lv_obj_t *daily_high_labels[FORECAST_POINT_COUNT];
static lv_obj_t *daily_low_labels[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_hourly_times[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_hourly_icons[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_hourly_conditions[FORECAST_POINT_COUNT];
static lv_obj_t *hourly_temperature_labels[FORECAST_POINT_COUNT];
static lv_obj_t *landscape_current_condition;
static lv_obj_t *landscape_daily_button;
static lv_obj_t *landscape_hourly_button;

// Touch calibration state is kept separate from the last saved transform.
static TouchCalibration touch_calibration = {};
static TouchRawPoint calibration_samples[TOUCH_CALIBRATION_SAMPLE_COUNT];
static TouchRawPoint calibration_points[5];
static uint8_t calibration_sample_count = 0;
static uint8_t calibration_target_index = 0;
static TouchCalibrationState calibration_state = TOUCH_CALIBRATION_WAIT_PRESS;
static bool calibration_active = false;
static bool calibration_raw_pressed = false;
static uint32_t calibration_last_touch_ms = 0;
static uint32_t calibration_started_ms = 0;
static lv_obj_t *calibration_overlay = nullptr;
static lv_obj_t *calibration_target = nullptr;
static lv_obj_t *calibration_progress_label = nullptr;
static lv_timer_t *calibration_timer = nullptr;

// Weather icons
LV_IMG_DECLARE(icon_blizzard);
LV_IMG_DECLARE(icon_blowing_snow);
LV_IMG_DECLARE(icon_clear_night);
LV_IMG_DECLARE(icon_cloudy);
LV_IMG_DECLARE(icon_drizzle);
LV_IMG_DECLARE(icon_flurries);
LV_IMG_DECLARE(icon_haze_fog_dust_smoke);
LV_IMG_DECLARE(icon_heavy_rain);
LV_IMG_DECLARE(icon_heavy_snow);
LV_IMG_DECLARE(icon_isolated_scattered_tstorms_day);
LV_IMG_DECLARE(icon_isolated_scattered_tstorms_night);
LV_IMG_DECLARE(icon_mostly_clear_night);
LV_IMG_DECLARE(icon_mostly_cloudy_day);
LV_IMG_DECLARE(icon_mostly_cloudy_night);
LV_IMG_DECLARE(icon_mostly_sunny);
LV_IMG_DECLARE(icon_partly_cloudy);
LV_IMG_DECLARE(icon_partly_cloudy_night);
LV_IMG_DECLARE(icon_scattered_showers_day);
LV_IMG_DECLARE(icon_scattered_showers_night);
LV_IMG_DECLARE(icon_showers_rain);
LV_IMG_DECLARE(icon_sleet_hail);
LV_IMG_DECLARE(icon_snow_showers_snow);
LV_IMG_DECLARE(icon_strong_tstorms);
LV_IMG_DECLARE(icon_sunny);
LV_IMG_DECLARE(icon_tornado);
LV_IMG_DECLARE(icon_wintry_mix_rain_snow);

// Weather Images
LV_IMG_DECLARE(image_blizzard);
LV_IMG_DECLARE(image_blowing_snow);
LV_IMG_DECLARE(image_clear_night);
LV_IMG_DECLARE(image_cloudy);
LV_IMG_DECLARE(image_drizzle);
LV_IMG_DECLARE(image_flurries);
LV_IMG_DECLARE(image_haze_fog_dust_smoke);
LV_IMG_DECLARE(image_heavy_rain);
LV_IMG_DECLARE(image_heavy_snow);
LV_IMG_DECLARE(image_isolated_scattered_tstorms_day);
LV_IMG_DECLARE(image_isolated_scattered_tstorms_night);
LV_IMG_DECLARE(image_mostly_clear_night);
LV_IMG_DECLARE(image_mostly_cloudy_day);
LV_IMG_DECLARE(image_mostly_cloudy_night);
LV_IMG_DECLARE(image_mostly_sunny);
LV_IMG_DECLARE(image_partly_cloudy);
LV_IMG_DECLARE(image_partly_cloudy_night);
LV_IMG_DECLARE(image_scattered_showers_day);
LV_IMG_DECLARE(image_scattered_showers_night);
LV_IMG_DECLARE(image_showers_rain);
LV_IMG_DECLARE(image_sleet_hail);
LV_IMG_DECLARE(image_snow_showers_snow);
LV_IMG_DECLARE(image_strong_tstorms);
LV_IMG_DECLARE(image_sunny);
LV_IMG_DECLARE(image_tornado);
LV_IMG_DECLARE(image_wintry_mix_rain_snow);

void create_ui();
static lv_color_t theme_color(uint32_t rgb);
static void apply_root_theme(lv_obj_t *root);
static void apply_button_theme(lv_obj_t *button, bool destructive);
static void apply_slider_theme(lv_obj_t *slider);
static void apply_switch_theme(lv_obj_t *switch_obj);
static void apply_dropdown_theme(lv_obj_t *dropdown);
static void apply_textarea_theme(lv_obj_t *textarea);
static void apply_keyboard_theme(lv_obj_t *keyboard);
static void apply_msgbox_theme(lv_obj_t *mbox);
static void create_portrait_ui(lv_obj_t *scr);
static void create_landscape_ui(lv_obj_t *scr);
static void render_landscape_snapshot();
static void set_forecast_view(ForecastView view);
static void set_object_hidden(lv_obj_t *object, bool hidden);
static void position_chart_temperature_labels();
static int display_width();
static int display_height();
void fetch_and_update_weather();
void create_settings_window();
void play_click_sound();
static void stop_click_sound(lv_timer_t *timer);
static void schedule_weather_refresh_after_click();
static void start_touch_calibration();
static void finish_touch_calibration(bool success);
static void calibration_timer_cb(lv_timer_t *timer);
static void screen_event_cb(lv_event_t *e);
static void settings_event_handler(lv_event_t *e);
const lv_img_dsc_t *choose_image(int wmo_code, int is_day);
const lv_img_dsc_t *choose_icon(int wmo_code, int is_day);

// Screen dimming functions
bool night_mode_should_be_active();
void activate_night_mode();
void deactivate_night_mode();
void check_for_night_mode();
void handle_temp_screen_wakeup_timeout(lv_timer_t *timer);
void apModeCallback(WiFiManager *mgr);
static void save_qweather_params();
static void configure_wifi_manager(WiFiManager &wm);
static void open_qweather_config_portal();
static void qweather_cancel_event_cb(lv_event_t *e);
static void qweather_portal_timeout_cb();
static void finish_qweather_config_portal(bool saved);
static void process_qweather_config_portal();
static void process_initial_wifi();
static void fetch_open_meteo_weather();
static bool decode_qweather_payload(String &payload);
static bool request_qweather(const String &path, DynamicJsonDocument &doc);
static int qweather_icon_to_wmo(int icon);
static bool qweather_icon_is_day(int icon);
static void startup_weather_timer_cb(lv_timer_t *timer);
static void rebuild_ui(bool reopen_settings);
static void render_weather_snapshot();


int day_of_week(int y, int m, int d) {
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

String hour_of_day(int hour) {
  const LocalizedStrings* strings = get_strings(current_language);
  if(hour < 0 || hour > 23) return String(strings->invalid_hour);

  if (use_24_hour) {
    if (hour < 10)
      return String("0") + String(hour);
    else
      return String(hour);
  } else {
    if(hour == 0)   return String("12") + strings->am;
    if(hour == 12)  return String(strings->noon);

    bool isMorning = (hour < 12);
    String suffix = isMorning ? strings->am : strings->pm;

    int displayHour = hour % 12;

    return String(displayHour) + suffix;
  }
}

static const char *weather_condition_name(int code) {
  const char *const *conditions =
      get_strings(current_language)->weather_conditions;
  if (code >= 51 && code <= 57) return conditions[4];
  if (code >= 71 && code <= 77) return conditions[8];

  switch (code) {
    case 0:
    case 1:
      return conditions[0];
    case 2:
      return conditions[1];
    case 3:
      return conditions[2];
    case 45:
    case 48:
      return conditions[3];
    case 61:
    case 63:
    case 80:
    case 81:
      return conditions[5];
    case 65:
    case 82:
      return conditions[6];
    case 66:
    case 67:
      return conditions[7];
    case 85:
    case 86:
      return conditions[8];
    case 95:
    case 96:
    case 99:
      return conditions[9];
    default:
      return conditions[2];
  }
}

static void publish_weather_snapshot(const WeatherSnapshot &candidate) {
  weather_snapshot = candidate;
  render_weather_snapshot();
}

static void render_portrait_snapshot() {
  const LocalizedStrings *strings = get_strings(current_language);
  const char unit = use_fahrenheit ? 'F' : 'C';
  const CurrentConditions &current = weather_snapshot.current;

  if (current.valid) {
    float temperature = current.temperature;
    float feels_like = current.feels_like;
    if (use_fahrenheit) {
      temperature = temperature * 9.0f / 5.0f + 32.0f;
      feels_like = feels_like * 9.0f / 5.0f + 32.0f;
    }
    lv_label_set_text_fmt(lbl_today_temp, "%.0f°%c", temperature, unit);
    lv_label_set_text_fmt(
        lbl_today_feels_like, "%s %.0f°%c",
        strings->feels_like_temp, feels_like, unit);
    lv_img_set_src(
        img_today_icon, choose_image(current.weather_code, current.is_day));
    lv_obj_clear_flag(img_today_icon, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
    lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
    lv_img_set_src(img_today_icon, &image_partly_cloudy);
    lv_obj_add_flag(img_today_icon, LV_OBJ_FLAG_HIDDEN);
  }

  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    const DailyForecastPoint &point = weather_snapshot.daily[i];
    if (point.valid) {
      float minimum = point.minimum;
      float maximum = point.maximum;
      if (use_fahrenheit) {
        minimum = minimum * 9.0f / 5.0f + 32.0f;
        maximum = maximum * 9.0f / 5.0f + 32.0f;
      }
      lv_label_set_text_fmt(
          lbl_daily_day[i], "%02u/%02u",
          static_cast<unsigned>(point.month),
          static_cast<unsigned>(point.day));
      lv_label_set_text_fmt(lbl_daily_high[i], "%.0f°%c", maximum, unit);
      lv_label_set_text_fmt(lbl_daily_low[i], "%.0f°%c", minimum, unit);
      lv_img_set_src(img_daily[i], choose_icon(point.weather_code, 1));
      lv_obj_clear_flag(img_daily[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_label_set_text(lbl_daily_day[i], "--");
      lv_label_set_text(lbl_daily_high[i], "--");
      lv_label_set_text(lbl_daily_low[i], "--");
      lv_img_set_src(img_daily[i], &icon_partly_cloudy);
      lv_obj_add_flag(img_daily[i], LV_OBJ_FLAG_HIDDEN);
    }

    const HourlyForecastPoint &hourly_point = weather_snapshot.hourly[i];
    if (hourly_point.valid) {
      float temperature = hourly_point.temperature;
      if (use_fahrenheit) {
        temperature = temperature * 9.0f / 5.0f + 32.0f;
      }
      if (i == 0) {
        lv_label_set_text(lbl_hourly[i], strings->now);
      } else {
        String hour_name = hour_of_day(hourly_point.hour);
        lv_label_set_text(lbl_hourly[i], hour_name.c_str());
      }
      lv_label_set_text_fmt(
          lbl_hourly_temp[i], "%.0f°%c", temperature, unit);
      lv_img_set_src(
          img_hourly[i],
          choose_icon(hourly_point.weather_code, hourly_point.is_day));
      lv_obj_clear_flag(img_hourly[i], LV_OBJ_FLAG_HIDDEN);
      if (hourly_point.has_precipitation) {
        lv_label_set_text_fmt(
            lbl_precipitation_probability[i], "%.0f%%",
            hourly_point.precipitation_probability);
      } else {
        lv_label_set_text(lbl_precipitation_probability[i], "");
      }
    } else {
      lv_label_set_text(lbl_hourly[i], "--");
      lv_label_set_text(lbl_hourly_temp[i], "--");
      lv_label_set_text(lbl_precipitation_probability[i], "");
      lv_img_set_src(img_hourly[i], &icon_partly_cloudy);
      lv_obj_add_flag(img_hourly[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

static float temperature_for_display(float celsius) {
  return use_fahrenheit ? celsius * 9.0f / 5.0f + 32.0f : celsius;
}

static bool safe_chart_temperature(
    float celsius, float *out_display, int32_t *out_chart_value) {
  if (!out_display || !out_chart_value) return false;

  const double source = static_cast<double>(celsius);
  if (!isfinite(source)) return false;
  const double display = use_fahrenheit
      ? source * 9.0 / 5.0 + 32.0 : source;
  if (!isfinite(display)) return false;
  const double rounded = round(display);
  if (!isfinite(rounded) ||
      rounded < static_cast<double>(INT32_MIN) ||
      rounded > static_cast<double>(INT32_MAX)) {
    return false;
  }

  const int32_t chart_value = static_cast<int32_t>(rounded);
  if (chart_value == LV_CHART_POINT_NONE) return false;
  *out_display = static_cast<float>(display);
  *out_chart_value = chart_value;
  return true;
}

static void prepare_landscape_chart_points() {
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    daily_point_renderable[i] = false;
    hourly_point_renderable[i] = false;
    daily_high_values[i] = LV_CHART_POINT_NONE;
    daily_low_values[i] = LV_CHART_POINT_NONE;
    hourly_temperature_values[i] = LV_CHART_POINT_NONE;

    const DailyForecastPoint &daily = weather_snapshot.daily[i];
    daily_point_renderable[i] = daily.valid &&
        safe_chart_temperature(
            daily.maximum, &daily_high_display_temperatures[i],
            &daily_high_values[i]) &&
        safe_chart_temperature(
            daily.minimum, &daily_low_display_temperatures[i],
            &daily_low_values[i]);
    if (!daily_point_renderable[i]) {
      daily_high_values[i] = LV_CHART_POINT_NONE;
      daily_low_values[i] = LV_CHART_POINT_NONE;
    }

    const HourlyForecastPoint &hourly = weather_snapshot.hourly[i];
    hourly_point_renderable[i] = hourly.valid &&
        safe_chart_temperature(
            hourly.temperature, &hourly_display_temperatures[i],
            &hourly_temperature_values[i]);
    if (!hourly_point_renderable[i]) {
      hourly_temperature_values[i] = LV_CHART_POINT_NONE;
    }
  }
}

static bool daily_display_chart_range(int *out_minimum, int *out_maximum) {
  bool has_valid_point = false;
  float minimum = 0.0f;
  float maximum = 0.0f;
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    if (!daily_point_renderable[i]) continue;
    float point_minimum = daily_low_display_temperatures[i];
    float point_maximum = daily_high_display_temperatures[i];
    if (point_minimum > point_maximum) {
      const float swapped = point_minimum;
      point_minimum = point_maximum;
      point_maximum = swapped;
    }
    if (!has_valid_point) {
      minimum = point_minimum;
      maximum = point_maximum;
      has_valid_point = true;
    } else {
      minimum = min(minimum, point_minimum);
      maximum = max(maximum, point_maximum);
    }
  }
  return has_valid_point &&
         padded_chart_range(minimum, maximum, out_minimum, out_maximum);
}

static bool hourly_display_chart_range(int *out_minimum, int *out_maximum) {
  bool has_valid_point = false;
  float minimum = 0.0f;
  float maximum = 0.0f;
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    if (!hourly_point_renderable[i]) continue;
    const float temperature = hourly_display_temperatures[i];
    if (!has_valid_point) {
      minimum = temperature;
      maximum = temperature;
      has_valid_point = true;
    } else {
      minimum = min(minimum, temperature);
      maximum = max(maximum, temperature);
    }
  }
  return has_valid_point &&
         padded_chart_range(minimum, maximum, out_minimum, out_maximum);
}

static void render_landscape_snapshot() {
  const LocalizedStrings *strings = get_strings(current_language);
  const char unit = use_fahrenheit ? 'F' : 'C';
  const CurrentConditions &current = weather_snapshot.current;

  if (current.valid) {
    const float current_temperature =
        temperature_for_display(current.temperature);
    const float current_feels_like =
        temperature_for_display(current.feels_like);
    lv_label_set_text_fmt(
        lbl_today_temp, "%.0f°%c", current_temperature, unit);
    lv_label_set_text(
        landscape_current_condition,
        weather_condition_name(current.weather_code));
    lv_label_set_text_fmt(
        lbl_today_feels_like, "%s %.0f°%c",
        strings->feels_like_temp, current_feels_like, unit);
  } else {
    lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
    lv_label_set_text(landscape_current_condition, "--");
    lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
  }

  prepare_landscape_chart_points();

  int range_min = 0;
  int range_max = 0;
  if (daily_display_chart_range(&range_min, &range_max)) {
    lv_chart_set_range(
        daily_chart, LV_CHART_AXIS_PRIMARY_Y, range_min, range_max);
  }
  if (hourly_display_chart_range(&range_min, &range_max)) {
    lv_chart_set_range(
        hourly_chart, LV_CHART_AXIS_PRIMARY_Y, range_min, range_max);
  }

  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    const DailyForecastPoint &daily = weather_snapshot.daily[i];
    set_object_hidden(
        landscape_daily_dates[i], !daily_point_renderable[i]);
    set_object_hidden(
        landscape_daily_icons[i], !daily_point_renderable[i]);
    set_object_hidden(
        landscape_daily_conditions[i], !daily_point_renderable[i]);
    set_object_hidden(daily_high_labels[i], !daily_point_renderable[i]);
    set_object_hidden(daily_low_labels[i], !daily_point_renderable[i]);
    if (daily_point_renderable[i]) {
      lv_label_set_text_fmt(
          landscape_daily_dates[i], "%02u/%02u",
          static_cast<unsigned>(daily.month),
          static_cast<unsigned>(daily.day));
      lv_label_set_text(
          landscape_daily_conditions[i],
          weather_condition_name(daily.weather_code));
      lv_label_set_text_fmt(
          daily_high_labels[i], "%.0f°%c",
          daily_high_display_temperatures[i], unit);
      lv_label_set_text_fmt(
          daily_low_labels[i], "%.0f°%c",
          daily_low_display_temperatures[i], unit);
      lv_img_set_src(
          landscape_daily_icons[i], choose_icon(daily.weather_code, 1));
    }

    const HourlyForecastPoint &hourly = weather_snapshot.hourly[i];
    set_object_hidden(
        landscape_hourly_times[i], !hourly_point_renderable[i]);
    set_object_hidden(
        landscape_hourly_icons[i], !hourly_point_renderable[i]);
    set_object_hidden(
        landscape_hourly_conditions[i], !hourly_point_renderable[i]);
    set_object_hidden(
        hourly_temperature_labels[i], !hourly_point_renderable[i]);
    if (hourly_point_renderable[i]) {
      if (i == 0) {
        lv_label_set_text(landscape_hourly_times[i], strings->now);
      } else {
        const String hour_name = hour_of_day(hourly.hour);
        lv_label_set_text(landscape_hourly_times[i], hour_name.c_str());
      }
      if (hourly.has_precipitation) {
        lv_label_set_text_fmt(
            landscape_hourly_conditions[i], "%s\n%.0f%%",
            weather_condition_name(hourly.weather_code),
            hourly.precipitation_probability);
      } else {
        lv_label_set_text(
            landscape_hourly_conditions[i],
            weather_condition_name(hourly.weather_code));
      }
      lv_label_set_text_fmt(
          hourly_temperature_labels[i], "%.0f°%c",
          hourly_display_temperatures[i], unit);
      lv_img_set_src(
          landscape_hourly_icons[i],
          choose_icon(hourly.weather_code, hourly.is_day));
    }
  }

  lv_chart_refresh(daily_chart);
  lv_chart_refresh(hourly_chart);
  position_chart_temperature_labels();
  set_forecast_view(active_forecast_view);
}

static void set_object_hidden(lv_obj_t *object, bool hidden) {
  if (!object) return;
  if (hidden) lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
}

static void place_chart_label(
    lv_obj_t *label, lv_obj_t *chart, lv_chart_series_t *series,
    uint32_t index, int y_offset) {
  lv_point_t point{};
  lv_chart_get_point_pos_by_id(chart, series, index, &point);
  const int x = constrain(
      LANDSCAPE_CHART_X + point.x - 17, 0, display_width() - 34);
  const int maximum_y = min(display_height() - 13, LANDSCAPE_COLUMN_Y - 13);
  const int y = constrain(
      LANDSCAPE_CHART_Y + point.y + y_offset,
      LANDSCAPE_HEADER_HEIGHT, maximum_y);
  lv_obj_set_pos(label, x, y);
}

static void position_chart_temperature_labels() {
  lv_obj_update_layout(daily_chart);
  lv_obj_update_layout(hourly_chart);
  for (uint32_t i = 0; i < FORECAST_POINT_COUNT; i++) {
    if (daily_point_renderable[i]) {
      place_chart_label(
          daily_high_labels[i], daily_chart, daily_high_series, i, -14);
      place_chart_label(
          daily_low_labels[i], daily_chart, daily_low_series, i, 2);
    }
    if (hourly_point_renderable[i]) {
      place_chart_label(
          hourly_temperature_labels[i], hourly_chart,
          hourly_temperature_series, i, -14);
    }
  }
}

static void render_weather_snapshot() {
  if (geometry_for_rotation(current_rotation).landscape) {
    render_landscape_snapshot();
  } else {
    render_portrait_snapshot();
  }
}

String urlencode(const String &str) {
  String encoded = "";
  char buf[5];
  for (size_t i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    // Unreserved characters according to RFC 3986
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      // Percent-encode others
      sprintf(buf, "%%%02X", (unsigned char)c);
      encoded += buf;
    }
  }
  return encoded;
}

static String format_weather_timestamp(const char *timestamp) {
  if (!timestamp || timestamp[0] == '\0') return String();

  String formatted(timestamp);
  int separator = formatted.indexOf('T');
  if (separator < 0) separator = formatted.indexOf(' ');
  if (separator >= 0) formatted.setCharAt(separator, ' ');
  if (formatted.length() > 16) formatted = formatted.substring(0, 16);
  return formatted;
}

static const char *weather_source_name(uint8_t source,
                                       const LocalizedStrings *strings) {
  if (source == WEATHER_SOURCE_QWEATHER) return strings->qweather_name;
  if (source == WEATHER_SOURCE_OPEN_METEO) return strings->open_meteo_name;
  return "--";
}

void update_home_status(uint8_t source, const char *updated_at) {
  weather_source = source;
  weather_updated_at = format_weather_timestamp(updated_at);

  if (!lbl_network_status || !lbl_update_status) return;

  const LocalizedStrings *strings = get_strings(current_language);
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--");
  String source_name = weather_source_name(weather_source, strings);
  String updated = weather_updated_at.length() > 0 ? weather_updated_at : String("--");
  String compact_updated = updated.length() >= 16 ? updated.substring(11, 16) : updated;

  lv_label_set_text_fmt(lbl_network_status, "%s %s", strings->device_ip, ip.c_str());
  lv_label_set_text_fmt(lbl_update_status, "%s %s",
                        source_name.c_str(), compact_updated.c_str());
}

static void update_clock(lv_timer_t *timer) {
  struct tm timeinfo;

  check_for_night_mode();

  if (!getLocalTime(&timeinfo)) return;

  const LocalizedStrings* strings = get_strings(current_language);
  char buf[16];
  if (use_24_hour) {
    snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  } else {
    int hour = timeinfo.tm_hour % 12;
    if(hour == 0) hour = 12;
    const char *ampm = (timeinfo.tm_hour < 12) ? strings->am : strings->pm;
    snprintf(buf, sizeof(buf), "%d:%02d%s", hour, timeinfo.tm_min, ampm);
  }
  lv_label_set_text(lbl_clock, buf);
}

static void ta_event_cb(lv_event_t *e) {
  play_click_sound();
  lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(e);
  lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(e);

  // Show keyboard
  lv_keyboard_set_textarea(kb, ta);
  lv_obj_move_foreground(kb);
  lv_obj_clear_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

static void kb_event_cb(lv_event_t *e) {
  lv_obj_t *kb = static_cast<lv_obj_t *>(lv_event_get_target(e));
  lv_obj_add_flag((lv_obj_t *)lv_event_get_target(e), LV_OBJ_FLAG_HIDDEN);

  if (lv_event_get_code(e) == LV_EVENT_READY) {
    const char *loc = lv_textarea_get_text(loc_ta);
    if (strlen(loc) > 0) {
      do_geocode_query(loc);
    }
  }
}

static void ta_defocus_cb(lv_event_t *e) {
  lv_obj_add_flag((lv_obj_t *)lv_event_get_user_data(e), LV_OBJ_FLAG_HIDDEN);
}

static bool raw_touch_point_valid(int raw_x, int raw_y) {
  return raw_x >= TOUCH_CALIBRATION_RAW_MIN && raw_x <= TOUCH_CALIBRATION_RAW_MAX &&
         raw_y >= TOUCH_CALIBRATION_RAW_MIN && raw_y <= TOUCH_CALIBRATION_RAW_MAX;
}

static void load_touch_calibration() {
  touch_calibration.valid =
      prefs.getUInt("touchCalVersion", 0) == TOUCH_CALIBRATION_VERSION &&
      prefs.getBool("touchCalibrated", false);
  touch_calibration.a = prefs.getFloat("touchCalA", 0.0f);
  touch_calibration.b = prefs.getFloat("touchCalB", 0.0f);
  touch_calibration.c = prefs.getFloat("touchCalC", 0.0f);
  touch_calibration.d = prefs.getFloat("touchCalD", 0.0f);
  touch_calibration.e = prefs.getFloat("touchCalE", 0.0f);
  touch_calibration.f = prefs.getFloat("touchCalF", 0.0f);
}

static void save_touch_calibration(const TouchCalibration &calibration) {
  prefs.putFloat("touchCalA", calibration.a);
  prefs.putFloat("touchCalB", calibration.b);
  prefs.putFloat("touchCalC", calibration.c);
  prefs.putFloat("touchCalD", calibration.d);
  prefs.putFloat("touchCalE", calibration.e);
  prefs.putFloat("touchCalF", calibration.f);
  prefs.putUInt("touchCalVersion", TOUCH_CALIBRATION_VERSION);
  prefs.putBool("touchCalibrated", true);
}

static bool calibration_samples_are_stable() {
  if (calibration_sample_count < TOUCH_CALIBRATION_MIN_SAMPLES) return false;

  int min_x = TOUCH_CALIBRATION_RAW_MAX;
  int max_x = TOUCH_CALIBRATION_RAW_MIN;
  int min_y = TOUCH_CALIBRATION_RAW_MAX;
  int max_y = TOUCH_CALIBRATION_RAW_MIN;
  for (uint8_t i = 0; i < calibration_sample_count; i++) {
    min_x = min(min_x, static_cast<int>(calibration_samples[i].x));
    max_x = max(max_x, static_cast<int>(calibration_samples[i].x));
    min_y = min(min_y, static_cast<int>(calibration_samples[i].y));
    max_y = max(max_y, static_cast<int>(calibration_samples[i].y));
  }
  return (max_x - min_x) <= 100 && (max_y - min_y) <= 100;
}

static TouchRawPoint average_calibration_samples() {
  float sum_x = 0.0f;
  float sum_y = 0.0f;
  for (uint8_t i = 0; i < calibration_sample_count; i++) {
    sum_x += calibration_samples[i].x;
    sum_y += calibration_samples[i].y;
  }
  return {
    sum_x / calibration_sample_count,
    sum_y / calibration_sample_count
  };
}

static lv_display_rotation_t lv_rotation_for(ScreenRotation rotation) {
  switch (validated_rotation(static_cast<uint32_t>(rotation))) {
    case SCREEN_ROTATION_0:
      return LV_DISPLAY_ROTATION_0;
    case SCREEN_ROTATION_90:
      return LV_DISPLAY_ROTATION_90;
    case SCREEN_ROTATION_180:
      return LV_DISPLAY_ROTATION_180;
    case SCREEN_ROTATION_270:
      return LV_DISPLAY_ROTATION_270;
    default:
      return LV_DISPLAY_ROTATION_0;
  }
}

static int display_width() {
  return geometry_for_rotation(current_rotation).width;
}

static int display_height() {
  return geometry_for_rotation(current_rotation).height;
}

void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();

    int portrait_x = map(p.x, 200, 3700, 1, PORTRAIT_WIDTH);
    int portrait_y = map(p.y, 240, 3800, 1, PORTRAIT_HEIGHT);
    z = p.z;

    if (calibration_active) {
      calibration_raw_pressed = true;
      calibration_last_touch_ms = millis();
      if (calibration_state == TOUCH_CALIBRATION_WAIT_PRESS &&
          calibration_sample_count < TOUCH_CALIBRATION_SAMPLE_COUNT &&
          raw_touch_point_valid(p.x, p.y)) {
        calibration_samples[calibration_sample_count++] = {
          static_cast<float>(p.x), static_cast<float>(p.y)
        };
      }
    } else if (touch_calibration.valid) {
      int calibrated_x = 0;
      int calibrated_y = 0;
      if (apply_touch_calibration(touch_calibration, p.x, p.y,
                                  PORTRAIT_WIDTH, PORTRAIT_HEIGHT,
                                  &calibrated_x, &calibrated_y)) {
        portrait_x = calibrated_x;
        portrait_y = calibrated_y;
      }
    }

    portrait_x = constrain(portrait_x, 0, PORTRAIT_WIDTH - 1);
    portrait_y = constrain(portrait_y, 0, PORTRAIT_HEIGHT - 1);
    x = portrait_x;
    y = portrait_y;

    // Handle touch during dimmed screen
    if (!calibration_active && night_mode_active) {
      // Temporarily wake the screen for 15 seconds
      analogWrite(LCD_BACKLIGHT_PIN, prefs.getUInt("brightness", 128));
    
      if (temp_screen_wakeup_timer) {
        lv_timer_del(temp_screen_wakeup_timer);
      }
      temp_screen_wakeup_timer = lv_timer_create(handle_temp_screen_wakeup_timeout, 15000, NULL);
      lv_timer_set_repeat_count(temp_screen_wakeup_timer, 1); // Run only once
      Serial.println("Woke up screen. Setting timer to turn of screen after 15 seconds of inactivity.");

      if (!temp_screen_wakeup_active) {
          // If this is the wake-up tap, don't pass this touch to the UI - just undim the screen
          temp_screen_wakeup_active = true;
          data->state = LV_INDEV_STATE_RELEASED;
          return;
      }

      temp_screen_wakeup_active = true;
    }

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    if (calibration_active) calibration_raw_pressed = false;
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

static void rebuild_ui(bool reopen_settings) {
  if (calibration_active || qweather_portal_active) return;
  if (kb && lv_obj_is_valid(kb)) lv_keyboard_set_textarea(kb, nullptr);
  kb = nullptr;
  settings_win = nullptr;
  location_win = nullptr;
  lbl_home_location = nullptr;
  lbl_settings_location = nullptr;
  lv_obj_clean(lv_scr_act());
  create_ui();
  render_weather_snapshot();
  if (reopen_settings) create_settings_window();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  TFT_eSPI tft = TFT_eSPI();
  tft.init();
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  lv_init();

  // Load saved prefs before the display is created so its rotation is ready.
  prefs.begin("weather", false);
  current_rotation = validated_rotation(prefs.getUInt("screenRotation", SCREEN_ROTATION_0));
  current_theme = validated_theme(prefs.getUInt("theme", THEME_DEEP_SEA));
  String lat = prefs.getString("latitude", LATITUDE_DEFAULT);
  String lon = prefs.getString("longitude", LONGITUDE_DEFAULT);
  use_fahrenheit = prefs.getBool("useFahrenheit", false);
  location = prefs.getString("location", LOCATION_DEFAULT);
  // Move the original untouched London defaults to Shenzhen without changing
  // a city that the user selected explicitly.
  if (lat == "51.5074" && lon == "-0.1278" && location == "London") {
    lat = LATITUDE_DEFAULT;
    lon = LONGITUDE_DEFAULT;
    location = LOCATION_DEFAULT;
    prefs.putString("latitude", lat);
    prefs.putString("longitude", lon);
    prefs.putString("location", location);
  }
  lat.toCharArray(latitude, sizeof(latitude));
  lon.toCharArray(longitude, sizeof(longitude));
  use_night_mode = prefs.getBool("useNightMode", false);
  uint32_t brightness = prefs.getUInt("brightness", 255);
  use_24_hour = prefs.getBool("use24Hour", false);
  sound_enabled = prefs.getBool("soundEnabled", true);
  sound_effect = constrain(prefs.getUInt("soundEffect", 0), 0, 3);
  current_language = (Language)prefs.getUInt("language", LANG_ZH);
  String saved_qweather_key = prefs.getString("qweatherKey", "");
  saved_qweather_key.toCharArray(qweather_key, sizeof(qweather_key));
  qweather_key_param.setValue(qweather_key, sizeof(qweather_key));
  load_touch_calibration();

  // Init touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);

  display = lv_tft_espi_create(PORTRAIT_WIDTH, PORTRAIT_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_display_set_rotation(display, lv_rotation_for(current_rotation));
  touch_indev = lv_indev_create();
  lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
  // LVGL 9 applies display rotation to bound pointer input.
  lv_indev_set_display(touch_indev, display);
  lv_indev_set_read_cb(touch_indev, touchscreen_read);
  analogWrite(LCD_BACKLIGHT_PIN, brightness);

  // Start saved Wi-Fi credentials without blocking the display or touch loop.
  configure_wifi_manager(wifi_manager);
  wifi_manager.setConfigPortalBlocking(false);
  wifi_manager.setConfigPortalTimeout(300);
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  wifi_connection_started = true;
  wifi_connect_started_ms = millis();

  lv_timer_create(update_clock, 1000, NULL);

  lv_obj_clean(lv_scr_act());
  create_ui();
  startup_weather_timer = lv_timer_create(startup_weather_timer_cb, 500, nullptr);
}

static void startup_weather_timer_cb(lv_timer_t *timer) {
  if (wifi_connection_started || WiFi.status() != WL_CONNECTED) return;

  if (startup_weather_timer) {
    lv_timer_del(startup_weather_timer);
    startup_weather_timer = nullptr;
  }
  fetch_and_update_weather();
}

void flush_wifi_splashscreen(uint32_t ms = 200) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    lv_timer_handler();
    delay(5);
  }
}

void apModeCallback(WiFiManager *mgr) {
  if (qweather_portal_active) return;
  wifi_splash_active = true;
  wifi_splash_screen();
  flush_wifi_splashscreen();
}

static void save_qweather_params() {
  strncpy(qweather_key, qweather_key_param.getValue(), sizeof(qweather_key) - 1);
  qweather_key[sizeof(qweather_key) - 1] = '\0';
  prefs.putString("qweatherKey", qweather_key);
  if (qweather_portal_active) qweather_portal_params_saved = true;
}

static void configure_wifi_manager(WiFiManager &wm) {
  wm.setAPCallback(apModeCallback);
  bool needs_parameter = false;
  if (&wm == &qweather_portal_manager) {
    needs_parameter = !qweather_portal_manager_configured;
  } else if (&wm == &wifi_manager) {
    needs_parameter = !wifi_manager_configured;
  } else {
    needs_parameter = true;
  }
  if (needs_parameter) {
    wm.addParameter(&qweather_key_param);
    if (&wm == &qweather_portal_manager) {
      qweather_portal_manager_configured = true;
    } else if (&wm == &wifi_manager) {
      wifi_manager_configured = true;
    }
  }
  wm.setSaveParamsCallback(save_qweather_params);
  wm.setMenu(qweather_menu, sizeof(qweather_menu) / sizeof(qweather_menu[0]));
  wm.setCustomMenuHTML(qweather_menu_html);
}

static void restore_home_ui_after_wifi() {
  if (!wifi_splash_active) return;

  wifi_splash_active = false;
  lbl_home_location = nullptr;
  lbl_settings_location = nullptr;
  lv_obj_clean(lv_scr_act());
  create_ui();
}

static void process_initial_wifi() {
  if (!wifi_connection_started) return;

  if (wifi_manager.getConfigPortalActive()) {
    wifi_manager.process();
    if (wifi_manager.getConfigPortalActive()) return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connection_started = false;
    restore_home_ui_after_wifi();
    return;
  }

  if (!wifi_config_portal_started &&
      millis() - wifi_connect_started_ms >= WIFI_CONNECT_TIMEOUT_MS) {
    wifi_config_portal_started = true;
    Serial.println("Saved Wi-Fi connection timed out; starting configuration portal.");
    wifi_manager.startConfigPortal(DEFAULT_CAPTIVE_SSID);
  }
}

static void open_qweather_config_portal() {
  if (qweather_portal_active) return;

  if (kb) {
    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
  }
  if (settings_win) {
    lv_obj_del(settings_win);
    settings_win = nullptr;
    lbl_settings_location = nullptr;
  }

  const LocalizedStrings* strings = get_strings(current_language);
  qweather_portal_active = true;
  qweather_portal_start_requested = true;
  qweather_portal_params_saved = false;
  qweather_portal_cancel_requested = false;
  qweather_portal_timed_out = false;

  qweather_portal_prompt = lv_msgbox_create(lv_scr_act());
  lv_obj_t *title = lv_msgbox_add_title(qweather_portal_prompt, strings->qweather_config);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_t *text = lv_msgbox_add_text(qweather_portal_prompt, strings->qweather_config_status);
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_obj_t *cancel = lv_msgbox_add_footer_button(qweather_portal_prompt, strings->cancel);
  lv_obj_set_style_text_font(cancel, get_font_12(), 0);
  lv_obj_add_event_cb(cancel, qweather_cancel_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_set_width(qweather_portal_prompt, 230);
  lv_obj_center(qweather_portal_prompt);
  apply_msgbox_theme(qweather_portal_prompt);
  apply_button_theme(cancel, false);
  lv_obj_set_style_radius(qweather_portal_prompt, 4, LV_PART_MAIN);
}

static void qweather_cancel_event_cb(lv_event_t *e) {
  play_click_sound();
  qweather_portal_cancel_requested = true;
}

static void qweather_portal_timeout_cb() {
  qweather_portal_timed_out = true;
}

static void finish_qweather_config_portal(bool saved) {
  if (!qweather_portal_active) return;

  if (qweather_portal_manager.getConfigPortalActive()) {
    qweather_portal_manager.stopConfigPortal();
  }
  qweather_portal_active = false;
  qweather_portal_start_requested = false;
  qweather_portal_cancel_requested = false;
  qweather_portal_timed_out = false;

  if (qweather_portal_prompt) {
    lv_obj_del(qweather_portal_prompt);
    qweather_portal_prompt = nullptr;
  }

  if (saved) fetch_and_update_weather();
}

static void process_qweather_config_portal() {
  if (!qweather_portal_active) return;

  if (qweather_portal_cancel_requested) {
    finish_qweather_config_portal(false);
    return;
  }

  if (qweather_portal_start_requested) {
    qweather_portal_start_requested = false;
    lv_refr_now(nullptr);
    configure_wifi_manager(qweather_portal_manager);
    qweather_portal_manager.setConfigPortalBlocking(false);
    qweather_portal_manager.setConfigPortalTimeout(300);
    qweather_portal_manager.setConfigPortalTimeoutCallback(qweather_portal_timeout_cb);
    Serial.println("Starting QWeather configuration portal.");
    qweather_portal_manager.startConfigPortal(DEFAULT_CAPTIVE_SSID);
  }

  qweather_portal_manager.process();
  if (qweather_portal_cancel_requested || qweather_portal_params_saved ||
      qweather_portal_timed_out ||
      !qweather_portal_manager.getConfigPortalActive()) {
    finish_qweather_config_portal(qweather_portal_params_saved && !qweather_portal_timed_out &&
                                  !qweather_portal_cancel_requested);
  }
}

static bool decode_qweather_payload(String &payload) {
  const size_t input_size = payload.length();
  const uint8_t *input = reinterpret_cast<const uint8_t *>(payload.c_str());
  if (input_size < 2 || input[0] != 0x1f || input[1] != 0x8b) return true;

  if (input_size < 18 || input[2] != MZ_DEFLATED || (input[3] & 0xe0) != 0) {
    Serial.println("QWeather GZIP header is invalid.");
    return false;
  }

  const size_t trailer_offset = input_size - 8;
  size_t compressed_offset = 10;
  const uint8_t flags = input[3];

  if (flags & 0x04) {
    if (compressed_offset + 2 > trailer_offset) return false;
    const size_t extra_length = input[compressed_offset] |
                                (static_cast<size_t>(input[compressed_offset + 1]) << 8);
    compressed_offset += 2;
    if (extra_length > trailer_offset - compressed_offset) return false;
    compressed_offset += extra_length;
  }

  auto skip_zero_terminated_field = [&]() {
    while (compressed_offset < trailer_offset && input[compressed_offset] != 0) {
      compressed_offset++;
    }
    if (compressed_offset >= trailer_offset) return false;
    compressed_offset++;
    return true;
  };
  if ((flags & 0x08) && !skip_zero_terminated_field()) return false;
  if ((flags & 0x10) && !skip_zero_terminated_field()) return false;
  if (flags & 0x02) {
    if (compressed_offset + 2 > trailer_offset) return false;
    compressed_offset += 2;
  }
  if (compressed_offset >= trailer_offset) return false;

  const uint32_t expected_size =
      static_cast<uint32_t>(input[trailer_offset + 4]) |
      (static_cast<uint32_t>(input[trailer_offset + 5]) << 8) |
      (static_cast<uint32_t>(input[trailer_offset + 6]) << 16) |
      (static_cast<uint32_t>(input[trailer_offset + 7]) << 24);
  if (expected_size == 0 || expected_size > QWEATHER_MAX_DECOMPRESSED_SIZE) {
    Serial.printf("QWeather GZIP output size is invalid: %u.\n", expected_size);
    return false;
  }

  uint8_t *output = static_cast<uint8_t *>(malloc(expected_size + 1));
  if (!output) {
    Serial.println("QWeather GZIP output allocation failed.");
    return false;
  }

  tinfl_decompressor *decompressor = static_cast<tinfl_decompressor *>(
      malloc(sizeof(tinfl_decompressor)));
  if (!decompressor) {
    free(output);
    Serial.println("QWeather GZIP state allocation failed.");
    return false;
  }

  tinfl_init(decompressor);
  size_t compressed_size = trailer_offset - compressed_offset;
  size_t actual_size = expected_size;
  const tinfl_status status = tinfl_decompress(
      decompressor, input + compressed_offset, &compressed_size,
      output, output, &actual_size, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  free(decompressor);
  if (status != TINFL_STATUS_DONE || actual_size != expected_size) {
    free(output);
    Serial.printf("QWeather GZIP decompression failed: %d.\n", status);
    return false;
  }

  output[actual_size] = '\0';
  payload = reinterpret_cast<const char *>(output);
  free(output);
  return payload.length() == actual_size;
}

static bool request_qweather(const String &path, DynamicJsonDocument &doc) {
  if (strlen(qweather_key) == 0) {
    Serial.println("QWeather API key missing; configure it in the Aura AP portal.");
    return false;
  }

  String url = String(QWEATHER_API_BASE) + path + "&key=" + urlencode(String(qweather_key));
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("QWeather HTTPS connection setup failed.");
    return false;
  }
  http.setConnectTimeout(WEATHER_HTTP_TIMEOUT_MS);
  http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);

  int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("QWeather request failed: %d\n", status);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  if (!decode_qweather_payload(payload)) return false;

  doc.clear();
  DeserializationError json_error = deserializeJson(doc, payload);
  if (json_error != DeserializationError::Ok) {
    uint8_t prefix[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < sizeof(prefix) && i < payload.length(); i++) {
      prefix[i] = static_cast<uint8_t>(payload[i]);
    }
    Serial.printf("QWeather JSON parse failed: %s (length: %u, first bytes: %02X %02X %02X %02X).\n",
                  json_error.c_str(), static_cast<unsigned>(payload.length()),
                  prefix[0], prefix[1], prefix[2], prefix[3]);
    return false;
  }

  const char *api_code = doc["code"] | "";
  if (strcmp(api_code, "200") != 0) {
    Serial.printf("QWeather API returned code %s.\n", api_code);
    return false;
  }
  return true;
}

static int qweather_icon_to_wmo(int icon) {
  if (icon == 100 || icon == 150) return 0;
  if ((icon >= 101 && icon <= 103) || (icon >= 151 && icon <= 153)) return 2;
  if (icon == 104 || icon == 154) return 3;

  // Preserve the special precipitation types before the broad ranges.
  if (icon == 302) return 95;
  if (icon == 303) return 96;
  if (icon == 304) return 66;
  if (icon >= 300 && icon <= 399) return 63;
  if (icon >= 400 && icon <= 499) return 71;
  if (icon >= 500 && icon <= 515) return 45;

  if (icon == 900) return 0;
  if (icon == 901) return 3;
  return 3;
}

static bool qweather_icon_is_day(int icon) {
  return icon < 150 || icon >= 200;
}

void loop() {
  process_initial_wifi();
  lv_timer_handler();
  process_qweather_config_portal();
  static uint32_t last = millis();

  if (millis() - last >= UPDATE_INTERVAL) {
    fetch_and_update_weather();
    last = millis();
  }

  lv_tick_inc(5);
  delay(5);
}

static lv_color_t theme_color(uint32_t rgb) {
  return lv_color_hex(rgb);
}

static void apply_root_theme(lv_obj_t *root) {
  const ThemePalette &palette = theme_palette(current_theme);
  lv_obj_set_style_bg_color(root, theme_color(palette.background), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_NONE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_text_color(root, theme_color(palette.text), LV_PART_MAIN);
}

static void apply_control_part(lv_obj_t *obj, lv_style_selector_t selector,
                               uint32_t background, uint32_t text,
                               uint32_t border) {
  if (!obj) return;
  lv_obj_set_style_bg_color(obj, theme_color(background), selector);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, selector);
  lv_obj_set_style_text_color(obj, theme_color(text), selector);
  lv_obj_set_style_border_color(obj, theme_color(border), selector);
  lv_obj_set_style_border_opa(obj, LV_OPA_COVER, selector);
  lv_obj_set_style_border_width(obj, 1, selector);
}

static void apply_button_theme(lv_obj_t *button, bool destructive) {
  const ThemePalette &palette = theme_palette(current_theme);
  const uint32_t normal_background =
      destructive ? palette.high_temperature : palette.panel;
  const uint32_t normal_text =
      destructive ? palette.background : palette.text;
  apply_control_part(button, LV_PART_MAIN | LV_STATE_DEFAULT,
                     normal_background, normal_text, palette.grid);
  apply_control_part(button, LV_PART_MAIN | LV_STATE_PRESSED,
                     palette.accent, palette.background, palette.accent);
  apply_control_part(button, LV_PART_MAIN | LV_STATE_CHECKED,
                     palette.accent, palette.background, palette.accent);
  apply_control_part(button,
                     LV_PART_MAIN | LV_STATE_CHECKED | LV_STATE_PRESSED,
                     palette.low_temperature, palette.background,
                     palette.accent);
  apply_control_part(button, LV_PART_MAIN | LV_STATE_DISABLED,
                     palette.grid, palette.muted, palette.grid);
}

static void apply_slider_theme(lv_obj_t *slider) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_control_part(slider, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.grid, palette.text, palette.grid);
  apply_control_part(slider, LV_PART_INDICATOR | LV_STATE_DEFAULT,
                     palette.accent, palette.background, palette.accent);
  apply_control_part(slider, LV_PART_INDICATOR | LV_STATE_DISABLED,
                     palette.muted, palette.muted, palette.grid);
  apply_control_part(slider, LV_PART_KNOB | LV_STATE_DEFAULT,
                     palette.low_temperature, palette.background,
                     palette.low_temperature);
  apply_control_part(slider, LV_PART_KNOB | LV_STATE_PRESSED,
                     palette.high_temperature, palette.background,
                     palette.high_temperature);
  apply_control_part(slider, LV_PART_KNOB | LV_STATE_DISABLED,
                     palette.muted, palette.background, palette.grid);
}

static void apply_switch_theme(lv_obj_t *switch_obj) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_control_part(switch_obj, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(switch_obj, LV_PART_MAIN | LV_STATE_DISABLED,
                     palette.grid, palette.muted, palette.grid);
  apply_control_part(switch_obj, LV_PART_INDICATOR | LV_STATE_DEFAULT,
                     palette.grid, palette.muted, palette.grid);
  apply_control_part(switch_obj, LV_PART_INDICATOR | LV_STATE_CHECKED,
                     palette.accent, palette.background, palette.accent);
  apply_control_part(switch_obj,
                     LV_PART_INDICATOR | LV_STATE_CHECKED | LV_STATE_PRESSED,
                     palette.low_temperature, palette.background,
                     palette.accent);
  apply_control_part(switch_obj, LV_PART_KNOB | LV_STATE_DEFAULT,
                     palette.text, palette.background, palette.grid);
  apply_control_part(switch_obj, LV_PART_KNOB | LV_STATE_CHECKED,
                     palette.background, palette.text, palette.accent);
  apply_control_part(switch_obj, LV_PART_KNOB | LV_STATE_DISABLED,
                     palette.muted, palette.background, palette.grid);
}

static void apply_dropdown_theme(lv_obj_t *dropdown) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_control_part(dropdown, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(dropdown, LV_PART_MAIN | LV_STATE_PRESSED,
                     palette.grid, palette.text, palette.accent);
  apply_control_part(dropdown, LV_PART_MAIN | LV_STATE_CHECKED,
                     palette.panel, palette.text, palette.accent);
  apply_control_part(dropdown, LV_PART_MAIN | LV_STATE_DISABLED,
                     palette.grid, palette.muted, palette.grid);
  apply_control_part(dropdown, LV_PART_INDICATOR | LV_STATE_DEFAULT,
                     palette.panel, palette.accent, palette.grid);
  apply_control_part(dropdown, LV_PART_INDICATOR | LV_STATE_CHECKED,
                     palette.panel, palette.high_temperature,
                     palette.accent);

  lv_obj_t *list = lv_dropdown_get_list(dropdown);
  apply_control_part(list, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.background, palette.text, palette.grid);
  apply_control_part(list, LV_PART_SELECTED | LV_STATE_CHECKED,
                     palette.accent, palette.background, palette.accent);
  apply_control_part(list, LV_PART_SELECTED | LV_STATE_PRESSED,
                     palette.high_temperature, palette.background,
                     palette.high_temperature);
  apply_control_part(list,
                     LV_PART_SELECTED | LV_STATE_CHECKED | LV_STATE_PRESSED,
                     palette.low_temperature, palette.background,
                     palette.accent);
  apply_control_part(list, LV_PART_SELECTED | LV_STATE_DISABLED,
                     palette.grid, palette.muted, palette.grid);
}

static void apply_textarea_theme(lv_obj_t *textarea) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_control_part(textarea, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(textarea, LV_PART_MAIN | LV_STATE_FOCUSED,
                     palette.panel, palette.text, palette.accent);
  apply_control_part(textarea, LV_PART_MAIN | LV_STATE_DISABLED,
                     palette.grid, palette.muted, palette.grid);
  lv_obj_set_style_border_color(textarea, theme_color(palette.accent),
                                LV_PART_CURSOR | LV_STATE_FOCUSED);
}

static void apply_keyboard_theme(lv_obj_t *keyboard) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_control_part(keyboard, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.background, palette.text, palette.grid);
  apply_control_part(keyboard, LV_PART_ITEMS | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(keyboard, LV_PART_ITEMS | LV_STATE_PRESSED,
                     palette.accent, palette.background, palette.accent);
  apply_control_part(keyboard, LV_PART_ITEMS | LV_STATE_CHECKED,
                     palette.low_temperature, palette.background,
                     palette.low_temperature);
  apply_control_part(keyboard,
                     LV_PART_ITEMS | LV_STATE_CHECKED | LV_STATE_PRESSED,
                     palette.high_temperature, palette.background,
                     palette.accent);
  apply_control_part(keyboard, LV_PART_ITEMS | LV_STATE_DISABLED,
                     palette.grid, palette.muted, palette.grid);
}

static void apply_msgbox_theme(lv_obj_t *mbox) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_control_part(mbox, LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(lv_msgbox_get_header(mbox),
                     LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(lv_msgbox_get_content(mbox),
                     LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
  apply_control_part(lv_msgbox_get_footer(mbox),
                     LV_PART_MAIN | LV_STATE_DEFAULT,
                     palette.panel, palette.text, palette.grid);
}

void wifi_splash_screen() {
  lv_obj_t *scr = lv_scr_act();
  lbl_home_location = nullptr;
  lbl_settings_location = nullptr;
  lv_obj_clean(scr);
  const ThemePalette &palette = theme_palette(current_theme);
  apply_root_theme(scr);

  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, strings->wifi_config);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl, theme_color(palette.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl);
  lv_scr_load(scr);
}

static void create_portrait_ui(lv_obj_t *scr) {
  const ThemePalette &palette = theme_palette(current_theme);
  apply_root_theme(scr);

  // Trigger settings screen on touch
  lv_obj_add_event_cb(scr, screen_event_cb, LV_EVENT_CLICKED, NULL);

  img_today_icon = lv_img_create(scr);
  lv_img_set_src(img_today_icon, &image_partly_cloudy);
  lv_obj_add_flag(img_today_icon, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(img_today_icon, LV_ALIGN_TOP_MID, -64, 12);

  const LocalizedStrings* strings = get_strings(current_language);

  lbl_today_temp = lv_label_create(scr);
  lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
  lv_obj_set_style_text_font(lbl_today_temp, get_font_42(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_today_temp, theme_color(palette.text), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_opa(lbl_today_temp, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_temp, LV_ALIGN_TOP_MID, 45, 25);

  lbl_today_feels_like = lv_label_create(scr);
  lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
  lv_obj_set_style_text_font(lbl_today_feels_like, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_today_feels_like, theme_color(palette.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_feels_like, LV_ALIGN_TOP_MID, 45, 75);

  lbl_network_status = lv_label_create(scr);
  lv_obj_set_width(lbl_network_status, 150);
  lv_label_set_long_mode(lbl_network_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(lbl_network_status, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_network_status, theme_color(palette.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_network_status, LV_ALIGN_TOP_LEFT, 4, 2);

  lbl_update_status = lv_label_create(scr);
  lv_obj_set_width(lbl_update_status, 100);
  lv_label_set_long_mode(lbl_update_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(lbl_update_status, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_update_status, theme_color(palette.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(lbl_update_status, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_update_status, LV_ALIGN_TOP_RIGHT, -10, 118);
  update_home_status(weather_source, weather_updated_at.c_str());

  lbl_forecast = lv_label_create(scr);
  lv_obj_set_width(lbl_forecast, 120);
  lv_label_set_long_mode(lbl_forecast, LV_LABEL_LONG_DOT);
  lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
  lv_obj_set_style_text_font(lbl_forecast, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_forecast, theme_color(palette.muted), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_forecast, LV_ALIGN_TOP_LEFT, 10, 118);

  box_daily = lv_obj_create(scr);
  lv_obj_set_size(box_daily, 220, 180);
  lv_obj_align(box_daily, LV_ALIGN_TOP_LEFT, 10, 140);
  lv_obj_set_style_bg_color(box_daily, theme_color(palette.panel), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(box_daily, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(box_daily, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(box_daily, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(box_daily, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(box_daily, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(box_daily, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(box_daily, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(box_daily, daily_cb, LV_EVENT_CLICKED, NULL);

  for (int i = 0; i < 7; i++) {
    lbl_daily_day[i] = lv_label_create(box_daily);
    lbl_daily_high[i] = lv_label_create(box_daily);
    lbl_daily_low[i] = lv_label_create(box_daily);
    img_daily[i] = lv_img_create(box_daily);

    lv_label_set_text(lbl_daily_day[i], "--");
    lv_obj_set_style_text_color(lbl_daily_day[i], theme_color(palette.text), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl_daily_day[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_day[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_day[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_label_set_text(lbl_daily_high[i], "--");
    lv_obj_set_style_text_color(lbl_daily_high[i], theme_color(palette.high_temperature), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl_daily_high[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_high[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_high[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_daily_low[i], "--");
    lv_obj_set_style_text_color(lbl_daily_low[i], theme_color(palette.low_temperature), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_low[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_low[i], LV_ALIGN_TOP_RIGHT, -50, i * 24);

    lv_img_set_src(img_daily[i], &icon_partly_cloudy);
    lv_obj_add_flag(img_daily[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_daily[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  box_hourly = lv_obj_create(scr);
  lv_obj_set_size(box_hourly, 220, 180);
  lv_obj_align(box_hourly, LV_ALIGN_TOP_LEFT, 10, 140);
  lv_obj_set_style_bg_color(box_hourly, theme_color(palette.panel), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(box_hourly, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(box_hourly, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(box_hourly, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(box_hourly, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(box_hourly, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_pad_all(box_hourly, 10, LV_PART_MAIN);
  lv_obj_set_style_pad_gap(box_hourly, 0, LV_PART_MAIN);
  lv_obj_add_event_cb(box_hourly, hourly_cb, LV_EVENT_CLICKED, NULL);

  for (int i = 0; i < 7; i++) {
    lbl_hourly[i] = lv_label_create(box_hourly);
    lbl_precipitation_probability[i] = lv_label_create(box_hourly);
    lbl_hourly_temp[i] = lv_label_create(box_hourly);
    img_hourly[i] = lv_img_create(box_hourly);

    lv_label_set_text(lbl_hourly[i], "--");
    lv_obj_set_style_text_color(lbl_hourly[i], theme_color(palette.text), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl_hourly[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_label_set_text(lbl_hourly_temp[i], "--");
    lv_obj_set_style_text_color(lbl_hourly_temp[i], theme_color(palette.high_temperature), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(lbl_hourly_temp[i], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly_temp[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly_temp[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_precipitation_probability[i], "--");
    lv_obj_set_style_text_color(lbl_precipitation_probability[i], theme_color(palette.low_temperature), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_precipitation_probability[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_precipitation_probability[i], LV_ALIGN_TOP_RIGHT, -55, i * 24);

    lv_img_set_src(img_hourly[i], &icon_partly_cloudy);
    lv_obj_add_flag(img_hourly[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(img_hourly[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  if (active_forecast_view == FORECAST_HOURLY) {
    lv_obj_add_flag(box_daily, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(lbl_forecast, strings->hourly_forecast);
  } else {
    lv_obj_add_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);
  }

  // Create clock label in the top-right corner
  lbl_clock = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_clock, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_clock, theme_color(palette.low_temperature), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_clock, "");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -10, 2);
}

static void set_forecast_view(ForecastView view) {
  active_forecast_view = view;
  if (!geometry_for_rotation(current_rotation).landscape) {
    if (!box_daily || !box_hourly || !lbl_forecast) return;
    const bool show_daily = view == FORECAST_DAILY;
    set_object_hidden(box_daily, !show_daily);
    set_object_hidden(box_hourly, show_daily);
    lv_label_set_text(
        lbl_forecast,
        show_daily ? get_strings(current_language)->seven_day_forecast
                   : get_strings(current_language)->hourly_forecast);
    return;
  }

  const bool show_daily = view == FORECAST_DAILY;
  set_object_hidden(daily_chart, !show_daily);
  set_object_hidden(hourly_chart, show_daily);
  if (show_daily) {
    lv_obj_add_state(landscape_daily_button, LV_STATE_CHECKED);
    lv_obj_remove_state(landscape_hourly_button, LV_STATE_CHECKED);
  } else {
    lv_obj_remove_state(landscape_daily_button, LV_STATE_CHECKED);
    lv_obj_add_state(landscape_hourly_button, LV_STATE_CHECKED);
  }
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    const bool hide_daily = !show_daily || !daily_point_renderable[i];
    set_object_hidden(landscape_daily_dates[i], hide_daily);
    set_object_hidden(landscape_daily_icons[i], hide_daily);
    set_object_hidden(landscape_daily_conditions[i], hide_daily);
    set_object_hidden(daily_high_labels[i], hide_daily);
    set_object_hidden(daily_low_labels[i], hide_daily);

    const bool hide_hourly = show_daily || !hourly_point_renderable[i];
    set_object_hidden(landscape_hourly_times[i], hide_hourly);
    set_object_hidden(landscape_hourly_icons[i], hide_hourly);
    set_object_hidden(landscape_hourly_conditions[i], hide_hourly);
    set_object_hidden(hourly_temperature_labels[i], hide_hourly);
  }
}

static void select_daily_cb(lv_event_t *e) {
  (void)e;
  play_click_sound();
  active_forecast_view = FORECAST_DAILY;
  set_forecast_view(active_forecast_view);
}

static void select_hourly_cb(lv_event_t *e) {
  (void)e;
  play_click_sound();
  active_forecast_view = FORECAST_HOURLY;
  set_forecast_view(active_forecast_view);
}

static void create_landscape_header(lv_obj_t *scr) {
  const ThemePalette &palette = theme_palette(current_theme);
  const LocalizedStrings *strings = get_strings(current_language);

  lbl_home_location = lv_label_create(scr);
  lv_obj_set_size(lbl_home_location, 96, 15);
  lv_obj_set_pos(lbl_home_location, 6, 2);
  lv_label_set_long_mode(lbl_home_location, LV_LABEL_LONG_DOT);
  lv_label_set_text(lbl_home_location, location.c_str());
  lv_obj_set_style_text_font(
      lbl_home_location, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      lbl_home_location, theme_color(palette.text),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_network_status = lv_label_create(scr);
  lv_obj_set_size(lbl_network_status, 96, 13);
  lv_obj_set_pos(lbl_network_status, 6, 19);
  lv_label_set_long_mode(lbl_network_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(
      lbl_network_status, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      lbl_network_status, theme_color(palette.muted),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_update_status = lv_label_create(scr);
  lv_obj_set_size(lbl_update_status, 96, 13);
  lv_obj_set_pos(lbl_update_status, 6, 35);
  lv_label_set_long_mode(lbl_update_status, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(
      lbl_update_status, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      lbl_update_status, theme_color(palette.muted),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_today_temp = lv_label_create(scr);
  lv_obj_set_size(lbl_today_temp, 78, 23);
  lv_obj_set_pos(lbl_today_temp, 106, 0);
  lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
  lv_obj_set_style_text_font(
      lbl_today_temp, get_font_20(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      lbl_today_temp, theme_color(palette.text),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  landscape_current_condition = lv_label_create(scr);
  lv_obj_set_size(landscape_current_condition, 78, 14);
  lv_obj_set_pos(landscape_current_condition, 106, 24);
  lv_label_set_long_mode(landscape_current_condition, LV_LABEL_LONG_DOT);
  lv_label_set_text(landscape_current_condition, "--");
  lv_obj_set_style_text_font(
      landscape_current_condition, get_font_12(),
      LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      landscape_current_condition, theme_color(palette.accent),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_today_feels_like = lv_label_create(scr);
  lv_obj_set_size(lbl_today_feels_like, 78, 14);
  lv_obj_set_pos(lbl_today_feels_like, 106, 40);
  lv_label_set_long_mode(lbl_today_feels_like, LV_LABEL_LONG_DOT);
  lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
  lv_obj_set_style_text_font(
      lbl_today_feels_like, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      lbl_today_feels_like, theme_color(palette.muted),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_clock = lv_label_create(scr);
  lv_obj_set_size(lbl_clock, 1, 1);
  lv_label_set_text(lbl_clock, "");
  lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);

  update_home_status(weather_source, weather_updated_at.c_str());
}

static void create_forecast_segmented_control(lv_obj_t *scr) {
  const LocalizedStrings *strings = get_strings(current_language);

  landscape_daily_button = lv_btn_create(scr);
  lv_obj_set_size(landscape_daily_button, 46, 25);
  lv_obj_set_pos(landscape_daily_button, 188, 2);
  lv_obj_add_flag(landscape_daily_button, LV_OBJ_FLAG_CHECKABLE);
  apply_button_theme(landscape_daily_button, false);
  lv_obj_add_event_cb(
      landscape_daily_button, select_daily_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *daily_label = lv_label_create(landscape_daily_button);
  lv_obj_set_width(daily_label, 38);
  lv_label_set_long_mode(daily_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(daily_label, strings->daily_tab);
  lv_obj_set_style_text_font(
      daily_label, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(
      daily_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(daily_label);

  landscape_hourly_button = lv_btn_create(scr);
  lv_obj_set_size(landscape_hourly_button, 46, 25);
  lv_obj_set_pos(landscape_hourly_button, 236, 2);
  lv_obj_add_flag(landscape_hourly_button, LV_OBJ_FLAG_CHECKABLE);
  apply_button_theme(landscape_hourly_button, false);
  lv_obj_add_event_cb(
      landscape_hourly_button, select_hourly_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *hourly_label = lv_label_create(landscape_hourly_button);
  lv_obj_set_width(hourly_label, 38);
  lv_label_set_long_mode(hourly_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(hourly_label, strings->hourly_tab);
  lv_obj_set_style_text_font(
      hourly_label, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(
      hourly_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(hourly_label);

  lv_obj_t *settings_button = lv_btn_create(scr);
  lv_obj_set_size(settings_button, 30, 25);
  lv_obj_set_pos(settings_button, 284, 2);
  apply_button_theme(settings_button, false);
  lv_obj_add_event_cb(
      settings_button, screen_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *settings_label = lv_label_create(settings_button);
  lv_label_set_text(settings_label, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_font(
      settings_label, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(settings_label);
}

static void style_landscape_chart(lv_obj_t *chart) {
  const ThemePalette &palette = theme_palette(current_theme);
  lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(chart, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(chart, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(chart, 6, LV_PART_MAIN);
  lv_obj_set_style_line_color(
      chart, theme_color(palette.grid), LV_PART_MAIN);
  lv_obj_set_style_line_opa(chart, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
  lv_obj_set_style_width(chart, 5, LV_PART_INDICATOR);
  lv_obj_set_style_height(chart, 5, LV_PART_INDICATOR);
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(chart, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_scrollbar_mode(chart, LV_SCROLLBAR_MODE_OFF);
}

static void create_daily_chart(lv_obj_t *scr) {
  daily_chart = lv_chart_create(scr);
  lv_obj_set_pos(daily_chart, LANDSCAPE_CHART_X, LANDSCAPE_CHART_Y);
  lv_obj_set_size(
      daily_chart, LANDSCAPE_CHART_WIDTH, LANDSCAPE_CHART_HEIGHT);
  style_landscape_chart(daily_chart);
  lv_chart_set_type(daily_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(daily_chart, FORECAST_POINT_COUNT);
  lv_chart_set_div_line_count(daily_chart, 3, FORECAST_POINT_COUNT);
  daily_high_series = lv_chart_add_series(
      daily_chart,
      theme_color(theme_palette(current_theme).high_temperature),
      LV_CHART_AXIS_PRIMARY_Y);
  daily_low_series = lv_chart_add_series(
      daily_chart,
      theme_color(theme_palette(current_theme).low_temperature),
      LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_ext_y_array(
      daily_chart, daily_high_series, daily_high_values);
  lv_chart_set_ext_y_array(
      daily_chart, daily_low_series, daily_low_values);
}

static void create_hourly_chart(lv_obj_t *scr) {
  hourly_chart = lv_chart_create(scr);
  lv_obj_set_pos(hourly_chart, LANDSCAPE_CHART_X, LANDSCAPE_CHART_Y);
  lv_obj_set_size(
      hourly_chart, LANDSCAPE_CHART_WIDTH, LANDSCAPE_CHART_HEIGHT);
  style_landscape_chart(hourly_chart);
  lv_chart_set_type(hourly_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_point_count(hourly_chart, FORECAST_POINT_COUNT);
  lv_chart_set_div_line_count(hourly_chart, 3, FORECAST_POINT_COUNT);
  hourly_temperature_series = lv_chart_add_series(
      hourly_chart, theme_color(theme_palette(current_theme).accent),
      LV_CHART_AXIS_PRIMARY_Y);
  lv_chart_set_ext_y_array(
      hourly_chart, hourly_temperature_series, hourly_temperature_values);
}

static void create_landscape_forecast_columns(lv_obj_t *scr) {
  const ThemePalette &palette = theme_palette(current_theme);
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    const int x = LANDSCAPE_CHART_X + i * LANDSCAPE_COLUMN_WIDTH;

    landscape_daily_dates[i] = lv_label_create(scr);
    lv_obj_set_size(landscape_daily_dates[i], 42, 13);
    lv_obj_set_pos(landscape_daily_dates[i], x, LANDSCAPE_COLUMN_Y);
    lv_obj_set_style_text_font(
        landscape_daily_dates[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        landscape_daily_dates[i], theme_color(palette.text),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        landscape_daily_dates[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    landscape_daily_icons[i] = lv_img_create(scr);
    lv_img_set_src(landscape_daily_icons[i], &icon_partly_cloudy);
    lv_obj_set_pos(
        landscape_daily_icons[i], x + 11, LANDSCAPE_COLUMN_Y + 14);

    landscape_daily_conditions[i] = lv_label_create(scr);
    lv_obj_set_size(landscape_daily_conditions[i], 42, 14);
    lv_obj_set_pos(
        landscape_daily_conditions[i], x, LANDSCAPE_COLUMN_Y + 36);
    lv_label_set_long_mode(
        landscape_daily_conditions[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(
        landscape_daily_conditions[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        landscape_daily_conditions[i], theme_color(palette.muted),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        landscape_daily_conditions[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    daily_high_labels[i] = lv_label_create(scr);
    lv_obj_set_size(daily_high_labels[i], 34, 13);
    lv_obj_set_style_text_font(
        daily_high_labels[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        daily_high_labels[i], theme_color(palette.high_temperature),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        daily_high_labels[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    daily_low_labels[i] = lv_label_create(scr);
    lv_obj_set_size(daily_low_labels[i], 34, 13);
    lv_obj_set_style_text_font(
        daily_low_labels[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        daily_low_labels[i], theme_color(palette.low_temperature),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        daily_low_labels[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    landscape_hourly_times[i] = lv_label_create(scr);
    lv_obj_set_size(landscape_hourly_times[i], 42, 13);
    lv_obj_set_pos(landscape_hourly_times[i], x, LANDSCAPE_COLUMN_Y);
    lv_obj_set_style_text_font(
        landscape_hourly_times[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        landscape_hourly_times[i], theme_color(palette.text),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        landscape_hourly_times[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    landscape_hourly_icons[i] = lv_img_create(scr);
    lv_img_set_src(landscape_hourly_icons[i], &icon_partly_cloudy);
    lv_obj_set_pos(
        landscape_hourly_icons[i], x + 11, LANDSCAPE_COLUMN_Y + 14);

    landscape_hourly_conditions[i] = lv_label_create(scr);
    lv_obj_set_size(landscape_hourly_conditions[i], 42, 28);
    lv_obj_set_pos(
        landscape_hourly_conditions[i], x, LANDSCAPE_COLUMN_Y + 34);
    lv_label_set_long_mode(
        landscape_hourly_conditions[i], LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(
        landscape_hourly_conditions[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        landscape_hourly_conditions[i], theme_color(palette.muted),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        landscape_hourly_conditions[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);

    hourly_temperature_labels[i] = lv_label_create(scr);
    lv_obj_set_size(hourly_temperature_labels[i], 34, 13);
    lv_obj_set_style_text_font(
        hourly_temperature_labels[i], get_font_12(),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(
        hourly_temperature_labels[i], theme_color(palette.accent),
        LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(
        hourly_temperature_labels[i], LV_TEXT_ALIGN_CENTER,
        LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

static void create_landscape_ui(lv_obj_t *scr) {
  apply_root_theme(scr);
  create_landscape_header(scr);
  create_forecast_segmented_control(scr);
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    daily_high_values[i] = LV_CHART_POINT_NONE;
    daily_low_values[i] = LV_CHART_POINT_NONE;
    hourly_temperature_values[i] = LV_CHART_POINT_NONE;
    daily_point_renderable[i] = false;
    hourly_point_renderable[i] = false;
  }
  create_daily_chart(scr);
  create_hourly_chart(scr);
  create_landscape_forecast_columns(scr);
  set_forecast_view(active_forecast_view);
}

void create_ui() {
  lv_obj_t *scr = lv_scr_act();
  lbl_home_location = nullptr;
  lv_obj_scroll_to(scr, 0, 0, LV_ANIM_OFF);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  if (geometry_for_rotation(current_rotation).landscape) {
    create_landscape_ui(scr);
  } else {
    create_portrait_ui(scr);
  }
}

void populate_results_dropdown() {
  dd_opts[0] = '\0';
  for (JsonObject item : geoResults) {
    strcat(dd_opts, item["name"].as<const char *>());
    if (item["admin1"]) {
      strcat(dd_opts, ", ");
      strcat(dd_opts, item["admin1"].as<const char *>());
    }

    strcat(dd_opts, "\n");
  }

  if (geoResults.size() > 0) {
    lv_dropdown_set_options_static(results_dd, dd_opts);
    lv_obj_add_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_state(btn_close_loc, LV_STATE_DISABLED);
    lv_obj_add_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void update_location_labels() {
  if (lbl_home_location && lv_obj_is_valid(lbl_home_location)) {
    lv_label_set_text(lbl_home_location, location.c_str());
  }
  if (lbl_settings_location && lv_obj_is_valid(lbl_settings_location)) {
    lv_label_set_text(lbl_settings_location, location.c_str());
  }
}

static void location_save_event_cb(lv_event_t *e) {
  play_click_sound();
  JsonArray *pres = static_cast<JsonArray *>(lv_event_get_user_data(e));
  uint16_t idx = lv_dropdown_get_selected(results_dd);

  JsonObject obj = (*pres)[idx];
  double lat = obj["latitude"].as<double>();
  double lon = obj["longitude"].as<double>();

  snprintf(latitude, sizeof(latitude), "%.6f", lat);
  snprintf(longitude, sizeof(longitude), "%.6f", lon);
  prefs.putString("latitude", latitude);
  prefs.putString("longitude", longitude);

  String opts;
  const char *name = obj["name"];
  const char *admin = obj["admin1"];
  const char *country = obj["country_code"];
  opts += name;
  if (admin) {
    opts += ", ";
    opts += admin;
  }

  prefs.putString("location", opts);
  location = prefs.getString("location");

  // Re‐fetch weather immediately
  update_location_labels();
  fetch_and_update_weather();

  lv_obj_del(location_win);
  location_win = nullptr;
}

static void location_cancel_event_cb(lv_event_t *e) {
  play_click_sound();
  lv_obj_del(location_win);
  location_win = nullptr;
}

void screen_event_cb(lv_event_t *e) {
  play_click_sound();
  create_settings_window();
}

static void update_calibration_target() {
  const LocalizedStrings* strings = get_strings(current_language);
  const ThemePalette &palette = theme_palette(current_theme);
  if (calibration_target) {
    lv_obj_del(calibration_target);
    calibration_target = nullptr;
  }

  calibration_target = lv_obj_create(calibration_overlay);
  lv_obj_set_size(calibration_target, 24, 24);
  const TouchScreenPoint portrait_target = TOUCH_CALIBRATION_TARGETS[calibration_target_index];
  int target_x = 0;
  int target_y = 0;
  rotate_portrait_touch(current_rotation, portrait_target.x, portrait_target.y, &target_x, &target_y);
  lv_obj_set_pos(calibration_target,
                 target_x - 12,
                 target_y - 12);
  lv_obj_set_style_pad_all(calibration_target, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(calibration_target, theme_color(palette.high_temperature),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(calibration_target, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(calibration_target, theme_color(palette.accent), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(calibration_target, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(calibration_target, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(calibration_target, LV_OBJ_FLAG_CLICKABLE);
  lv_label_set_text_fmt(calibration_progress_label, strings->calibration_progress,
                        calibration_target_index + 1);
}

static void calibration_cancel_event_cb(lv_event_t *e) {
  play_click_sound();
  finish_touch_calibration(false);
}

static void finish_touch_calibration(bool success) {
  if (success) {
    TouchScreenPoint targets[5];
    for (uint8_t i = 0; i < 5; i++) targets[i] = TOUCH_CALIBRATION_TARGETS[i];

    TouchCalibration fitted = {};
    if (!fit_touch_calibration(calibration_points, targets, 5, &fitted)) {
      success = false;
    } else {
      touch_calibration = fitted;
      save_touch_calibration(touch_calibration);
    }
  }

  if (calibration_timer) {
    lv_timer_del(calibration_timer);
    calibration_timer = nullptr;
  }
  calibration_active = false;
  calibration_raw_pressed = false;
  calibration_sample_count = 0;

  if (calibration_overlay) {
    lv_obj_del(calibration_overlay);
    calibration_overlay = nullptr;
    calibration_target = nullptr;
    calibration_progress_label = nullptr;
  }
  if (settings_win) lv_obj_clear_flag(settings_win, LV_OBJ_FLAG_HIDDEN);

  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
  lv_obj_t *title = lv_msgbox_add_title(mbox, strings->touch_calibration);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_t *text = lv_msgbox_add_text(
      mbox, success ? strings->calibration_success : strings->calibration_failed);
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_obj_t *close = lv_msgbox_add_close_button(mbox);
  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);
  apply_msgbox_theme(mbox);
  apply_button_theme(close, false);
}

static void calibration_timer_cb(lv_timer_t *timer) {
  if (!calibration_active) return;
  if (millis() - calibration_started_ms >= TOUCH_CALIBRATION_TIMEOUT_MS) {
    finish_touch_calibration(false);
    return;
  }

  if (calibration_state == TOUCH_CALIBRATION_WAIT_PRESS) {
    if (calibration_sample_count >= TOUCH_CALIBRATION_MIN_SAMPLES &&
        calibration_sample_count >= TOUCH_CALIBRATION_SAMPLE_COUNT) {
      if (calibration_samples_are_stable()) {
        calibration_points[calibration_target_index] = average_calibration_samples();
        calibration_sample_count = 0;
        calibration_state = TOUCH_CALIBRATION_WAIT_RELEASE;
        play_click_sound();
      } else {
        calibration_sample_count = 0;
      }
    }
  } else if (!calibration_raw_pressed &&
             millis() - calibration_last_touch_ms >= TOUCH_CALIBRATION_RELEASE_MS) {
    if (calibration_target_index == 4) {
      finish_touch_calibration(true);
    } else {
      calibration_target_index++;
      calibration_state = TOUCH_CALIBRATION_WAIT_PRESS;
      update_calibration_target();
    }
  }
}

static void start_touch_calibration() {
  if (calibration_active || !settings_win) return;

  const LocalizedStrings* strings = get_strings(current_language);
  const ThemePalette &palette = theme_palette(current_theme);
  lv_obj_add_flag(settings_win, LV_OBJ_FLAG_HIDDEN);
  calibration_active = true;
  calibration_raw_pressed = false;
  calibration_started_ms = millis();
  calibration_last_touch_ms = calibration_started_ms;
  calibration_sample_count = 0;
  calibration_target_index = 0;
  calibration_state = TOUCH_CALIBRATION_WAIT_PRESS;

  calibration_overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(calibration_overlay, display_width(), display_height());
  lv_obj_set_pos(calibration_overlay, 0, 0);
  apply_root_theme(calibration_overlay);
  lv_obj_set_style_pad_all(calibration_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(calibration_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(calibration_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(calibration_overlay, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *instructions = lv_label_create(calibration_overlay);
  lv_label_set_text(instructions, strings->calibration_instructions);
  lv_obj_set_width(instructions, 170);
  lv_obj_set_style_text_font(instructions, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(instructions, theme_color(palette.text), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(instructions, LV_ALIGN_TOP_MID, 0, 3);

  calibration_progress_label = lv_label_create(calibration_overlay);
  lv_obj_set_style_text_font(calibration_progress_label, get_font_12(),
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(calibration_progress_label, theme_color(palette.muted),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(calibration_progress_label, LV_ALIGN_TOP_MID, 0, 25);

  lv_obj_t *cancel = lv_btn_create(calibration_overlay);
  lv_obj_set_size(cancel, 108, 32);
  apply_button_theme(cancel, false);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, 0, -44);
  lv_obj_add_event_cb(cancel, calibration_cancel_event_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *cancel_label = lv_label_create(cancel);
  lv_label_set_text(cancel_label, strings->calibration_cancel);
  lv_obj_set_style_text_font(cancel_label, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(cancel_label);

  update_calibration_target();
  calibration_timer = lv_timer_create(calibration_timer_cb, 20, nullptr);
}

void daily_cb(lv_event_t *e) {
  (void)e;
  play_click_sound();
  active_forecast_view = FORECAST_HOURLY;
  set_forecast_view(active_forecast_view);
}

void hourly_cb(lv_event_t *e) {
  (void)e;
  play_click_sound();
  active_forecast_view = FORECAST_DAILY;
  set_forecast_view(active_forecast_view);
}


static void reset_wifi_event_handler(lv_event_t *e) {
  play_click_sound();
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *mbox = lv_msgbox_create(lv_scr_act());
  lv_obj_t *title = lv_msgbox_add_title(mbox, strings->reset);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);

  lv_obj_t *text = lv_msgbox_add_text(mbox, strings->reset_confirmation);
  lv_obj_set_style_text_font(text, get_font_12(), 0);
  lv_obj_t *close = lv_msgbox_add_close_button(mbox);

  lv_obj_t *btn_no = lv_msgbox_add_footer_button(mbox, strings->cancel);
  lv_obj_set_style_text_font(btn_no, get_font_12(), 0);
  lv_obj_t *btn_yes = lv_msgbox_add_footer_button(mbox, strings->reset);
  lv_obj_set_style_text_font(btn_yes, get_font_12(), 0);

  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);

  apply_msgbox_theme(mbox);
  apply_button_theme(close, false);
  apply_button_theme(btn_no, false);
  apply_button_theme(btn_yes, true);
  lv_obj_set_style_radius(mbox, 4, LV_PART_MAIN);

  lv_obj_add_event_cb(btn_yes, reset_confirm_yes_cb, LV_EVENT_CLICKED, mbox);
  lv_obj_add_event_cb(btn_no, reset_confirm_no_cb, LV_EVENT_CLICKED, mbox);
}

static void reset_confirm_yes_cb(lv_event_t *e) {
  play_click_sound();
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  Serial.println("Clearing Wi-Fi creds and rebooting");
  WiFiManager wm;
  wm.resetSettings();
  delay(100);
  esp_restart();
}

static void reset_confirm_no_cb(lv_event_t *e) {
  play_click_sound();
  lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_del(mbox);
}

static void change_location_event_cb(lv_event_t *e) {
  play_click_sound();
  if (location_win) return;

  create_location_dialog();
}

void create_location_dialog() {
  const LocalizedStrings* strings = get_strings(current_language);
  const ThemePalette &palette = theme_palette(current_theme);
  location_win = lv_win_create(lv_scr_act());
  lv_obj_t *title = lv_win_add_title(location_win, strings->change_location);
  lv_obj_t *header = lv_win_get_header(location_win);
  lv_obj_set_style_bg_color(location_win, theme_color(palette.background), LV_PART_MAIN);
  lv_obj_set_style_text_color(location_win, theme_color(palette.text), LV_PART_MAIN);
  lv_obj_set_style_border_color(location_win, theme_color(palette.grid), LV_PART_MAIN);
  lv_obj_set_style_bg_color(header, theme_color(palette.panel), LV_PART_MAIN);
  lv_obj_set_style_text_color(header, theme_color(palette.text), LV_PART_MAIN);
  lv_obj_set_style_height(header, 30, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_size(location_win, display_width(), display_height());
  lv_obj_center(location_win);

  lv_obj_t *cont = lv_win_get_content(location_win);
  lv_obj_set_style_bg_color(cont, theme_color(palette.background), LV_PART_MAIN);
  lv_obj_set_style_text_color(cont, theme_color(palette.text), LV_PART_MAIN);

  lv_obj_t *lbl = lv_label_create(cont);
  lv_label_set_text(lbl, strings->city);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 10);

  loc_ta = lv_textarea_create(cont);
  lv_textarea_set_one_line(loc_ta, true);
  lv_textarea_set_placeholder_text(loc_ta, strings->city_placeholder);
  apply_textarea_theme(loc_ta);
  lv_obj_set_width(loc_ta, 170);
  lv_obj_align_to(loc_ta, lbl, LV_ALIGN_OUT_RIGHT_MID, 5, 0);

  lv_obj_add_event_cb(loc_ta, ta_event_cb, LV_EVENT_CLICKED, kb);
  lv_obj_add_event_cb(loc_ta, ta_defocus_cb, LV_EVENT_DEFOCUSED, kb);

  lv_obj_t *lbl2 = lv_label_create(cont);
  lv_label_set_text(lbl2, strings->search_results);
  lv_obj_set_style_text_font(lbl2, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 5, 50);

  results_dd = lv_dropdown_create(cont);
  lv_obj_set_width(results_dd, 200);
  lv_obj_align(results_dd, LV_ALIGN_TOP_LEFT, 5, 70);
  lv_obj_set_style_text_font(results_dd, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(results_dd, get_font_14(), LV_PART_SELECTED | LV_STATE_DEFAULT);

  lv_obj_t *list = lv_dropdown_get_list(results_dd);
  lv_obj_set_style_text_font(list, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  apply_dropdown_theme(results_dd);

  lv_dropdown_set_options(results_dd, "");
  lv_obj_clear_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);

  btn_close_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_close_loc, 80, 40);
  lv_obj_align(btn_close_loc, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_add_event_cb(btn_close_loc, location_save_event_cb, LV_EVENT_CLICKED, &geoResults);
  apply_button_theme(btn_close_loc, false);
  lv_obj_add_state(btn_close_loc, LV_STATE_DISABLED);
  lv_obj_clear_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl_close = lv_label_create(btn_close_loc);
  lv_label_set_text(lbl_close, strings->save);
  lv_obj_set_style_text_font(lbl_close, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_close);

  lv_obj_t *btn_cancel_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_cancel_loc, 80, 40);
  apply_button_theme(btn_cancel_loc, false);
  lv_obj_align_to(btn_cancel_loc, btn_close_loc, LV_ALIGN_OUT_LEFT_MID, -5, 0);
  lv_obj_add_event_cb(btn_cancel_loc, location_cancel_event_cb, LV_EVENT_CLICKED, &geoResults);

  lv_obj_t *lbl_cancel = lv_label_create(btn_cancel_loc);
  lv_label_set_text(lbl_cancel, strings->cancel);
  lv_obj_set_style_text_font(lbl_cancel, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_cancel);
}

void create_settings_window() {
  if (settings_win) return;

  const LocalizedStrings* strings = get_strings(current_language);
  const ThemePalette &palette = theme_palette(current_theme);
  lbl_settings_location = nullptr;
  settings_win = lv_win_create(lv_scr_act());
  lv_obj_set_size(settings_win, display_width(), display_height());
  lv_obj_center(settings_win);
  lv_obj_clear_flag(settings_win, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(settings_win, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *header = lv_win_get_header(settings_win);
  lv_obj_set_style_bg_color(settings_win, theme_color(palette.background), LV_PART_MAIN);
  lv_obj_set_style_text_color(settings_win, theme_color(palette.text), LV_PART_MAIN);
  lv_obj_set_style_border_color(settings_win, theme_color(palette.grid), LV_PART_MAIN);
  lv_obj_set_style_bg_color(header, theme_color(palette.panel), LV_PART_MAIN);
  lv_obj_set_style_text_color(header, theme_color(palette.text), LV_PART_MAIN);
  lv_obj_set_style_height(header, 30, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
  lv_obj_t *title = lv_win_add_title(settings_win, strings->aura_settings);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);

  btn_close_obj = lv_btn_create(header);
  lv_obj_set_size(btn_close_obj, 42, LV_PCT(100));
  apply_button_theme(btn_close_obj, false);
  lv_obj_set_style_bg_opa(btn_close_obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(btn_close_obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(btn_close_obj, settings_event_handler, LV_EVENT_PRESSED, NULL);
  lv_obj_t *close_label = lv_label_create(btn_close_obj);
  lv_label_set_text(close_label, "X");
  lv_obj_set_style_text_font(close_label, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(close_label, theme_color(palette.text), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(close_label);

  lv_obj_t *cont = lv_win_get_content(settings_win);
  lv_obj_set_style_bg_color(cont, theme_color(palette.background), LV_PART_MAIN);
  lv_obj_set_style_text_color(cont, theme_color(palette.text), LV_PART_MAIN);

  // The content is intentionally taller than the display. Flex rows keep
  // each control in its own lane, while the window provides vertical scrolling.
  lv_obj_set_scroll_dir(cont, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_layout(cont, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_all(cont, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_row(cont, 4, LV_PART_MAIN);

  auto create_row = [&](int height) {
    lv_obj_t *row = lv_obj_create(cont);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, height);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
  };

  auto style_label = [&](lv_obj_t *label) {
    lv_obj_set_style_text_font(label, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(label, theme_color(palette.text), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  };

  // Brightness
  lv_obj_t *brightness_row = create_row(38);
  lv_obj_t *lbl_b = lv_label_create(brightness_row);
  lv_label_set_text(lbl_b, strings->brightness);
  style_label(lbl_b);
  lv_obj_t *slider = lv_slider_create(brightness_row);
  lv_slider_set_range(slider, 1, 255);
  uint32_t saved_b = prefs.getUInt("brightness", 128);
  lv_slider_set_value(slider, saved_b, LV_ANIM_OFF);
  lv_obj_set_width(slider, 100);
  apply_slider_theme(slider);
  lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_add_event_cb(slider, [](lv_event_t *e){
    lv_obj_t *s = (lv_obj_t*)lv_event_get_target(e);
    uint32_t v = lv_slider_get_value(s);
    analogWrite(LCD_BACKLIGHT_PIN, v);
    prefs.putUInt("brightness", v);
  }, LV_EVENT_VALUE_CHANGED, NULL);

  // Night mode
  lv_obj_t *night_row = create_row(34);
  lv_obj_t *lbl_night_mode = lv_label_create(night_row);
  lv_label_set_text(lbl_night_mode, strings->use_night_mode);
  style_label(lbl_night_mode);
  night_mode_switch = lv_switch_create(night_row);
  apply_switch_theme(night_mode_switch);
  lv_obj_align(night_mode_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (use_night_mode) lv_obj_add_state(night_mode_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(night_mode_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Fahrenheit
  lv_obj_t *fahrenheit_row = create_row(34);
  lv_obj_t *lbl_u = lv_label_create(fahrenheit_row);
  lv_label_set_text(lbl_u, strings->use_fahrenheit);
  style_label(lbl_u);
  unit_switch = lv_switch_create(fahrenheit_row);
  apply_switch_theme(unit_switch);
  lv_obj_align(unit_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (use_fahrenheit) lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(unit_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // 24-hour clock
  lv_obj_t *clock_row = create_row(34);
  lv_obj_t *lbl_24hr = lv_label_create(clock_row);
  lv_label_set_text(lbl_24hr, strings->use_24hr);
  style_label(lbl_24hr);
  clock_24hr_switch = lv_switch_create(clock_row);
  apply_switch_theme(clock_24hr_switch);
  lv_obj_align(clock_24hr_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (use_24_hour) lv_obj_add_state(clock_24hr_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(clock_24hr_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Current Location label
  lv_obj_t *location_row = create_row(34);
  lv_obj_t *lbl_loc_l = lv_label_create(location_row);
  lv_label_set_text(lbl_loc_l, strings->location);
  style_label(lbl_loc_l);
  lbl_settings_location = lv_label_create(location_row);
  lv_label_set_text(lbl_settings_location, location.c_str());
  lv_obj_set_style_text_font(
      lbl_settings_location, get_font_12(),
      LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_long_mode(lbl_settings_location, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl_settings_location, 135);
  lv_obj_align(lbl_settings_location, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_text_align(
      lbl_settings_location, LV_TEXT_ALIGN_RIGHT,
      LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(
      lbl_settings_location, theme_color(palette.muted),
      LV_PART_MAIN | LV_STATE_DEFAULT);

  // Language selection
  lv_obj_t *language_row = create_row(42);
  lv_obj_t *lbl_lang = lv_label_create(language_row);
  lv_label_set_text(lbl_lang, strings->language_label);
  style_label(lbl_lang);

  language_dropdown = lv_dropdown_create(language_row);
  lv_dropdown_set_options(language_dropdown, "English\nEspañol\nDeutsch\nFrançais\nTürkçe\nSvenska\nItaliano\n简体中文");
  lv_dropdown_set_selected(language_dropdown, current_language);
  lv_obj_set_width(language_dropdown, 132);
  lv_obj_set_style_text_font(language_dropdown, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(language_dropdown, get_font_12(), LV_PART_SELECTED | LV_STATE_DEFAULT);
  lv_obj_t *list = lv_dropdown_get_list(language_dropdown);
  lv_obj_set_style_text_font(list, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  apply_dropdown_theme(language_dropdown);
  lv_obj_align(language_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(language_dropdown, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Sound enable
  lv_obj_t *sound_row = create_row(34);
  lv_obj_t *lbl_sound = lv_label_create(sound_row);
  lv_label_set_text(lbl_sound, strings->sound_enabled);
  style_label(lbl_sound);
  sound_enabled_switch = lv_switch_create(sound_row);
  apply_switch_theme(sound_enabled_switch);
  lv_obj_align(sound_enabled_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (sound_enabled) lv_obj_add_state(sound_enabled_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sound_enabled_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Sound effect
  lv_obj_t *effect_row = create_row(42);
  lv_obj_t *lbl_effect = lv_label_create(effect_row);
  lv_label_set_text(lbl_effect, strings->sound_effect);
  style_label(lbl_effect);
  sound_effect_dropdown = lv_dropdown_create(effect_row);
  lv_dropdown_set_options(sound_effect_dropdown, strings->sound_effect_options);
  lv_dropdown_set_selected(sound_effect_dropdown, sound_effect);
  lv_obj_set_width(sound_effect_dropdown, 132);
  lv_obj_set_style_text_font(sound_effect_dropdown, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(sound_effect_dropdown, get_font_12(), LV_PART_SELECTED | LV_STATE_DEFAULT);
  lv_obj_t *effect_list = lv_dropdown_get_list(sound_effect_dropdown);
  lv_obj_set_style_text_font(effect_list, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  apply_dropdown_theme(sound_effect_dropdown);
  lv_obj_align(sound_effect_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(sound_effect_dropdown, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // QWeather configuration portal
  lv_obj_t *qweather_row = create_row(38);
  qweather_config_btn = lv_btn_create(qweather_row);
  apply_button_theme(qweather_config_btn, false);
  lv_obj_set_size(qweather_config_btn, 204, 34);
  lv_obj_add_event_cb(qweather_config_btn, settings_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_qweather = lv_label_create(qweather_config_btn);
  lv_label_set_text(lbl_qweather, strings->qweather_config);
  lv_obj_set_style_text_font(lbl_qweather, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_qweather);

  // Touch calibration button
  lv_obj_t *calibration_row = create_row(38);
  touch_calibration_btn = lv_btn_create(calibration_row);
  apply_button_theme(touch_calibration_btn, false);
  lv_obj_set_size(touch_calibration_btn, 204, 34);
  lv_obj_add_event_cb(touch_calibration_btn, settings_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_calibrate = lv_label_create(touch_calibration_btn);
  lv_label_set_text(lbl_calibrate, strings->touch_calibration);
  lv_obj_set_style_text_font(lbl_calibrate, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_calibrate);

  // Location search button
  lv_obj_t *location_button_row = create_row(38);
  lv_obj_t *btn_change_loc = lv_btn_create(location_button_row);
  apply_button_theme(btn_change_loc, false);
  lv_obj_set_size(btn_change_loc, 204, 34);
  lv_obj_add_event_cb(btn_change_loc, change_location_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_chg = lv_label_create(btn_change_loc);
  lv_label_set_text(lbl_chg, strings->location_btn);
  lv_obj_set_style_text_font(lbl_chg, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_chg);

  // Hidden keyboard object
  if (!kb) {
    kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    apply_keyboard_theme(kb);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
  }

  // Reset WiFi button
  lv_obj_t *reset_row = create_row(38);
  lv_obj_t *btn_reset = lv_btn_create(reset_row);
  apply_button_theme(btn_reset, true);
  lv_obj_set_size(btn_reset, 204, 34);

  lv_obj_add_event_cb(btn_reset, reset_wifi_event_handler, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *lbl_reset = lv_label_create(btn_reset);
  lv_label_set_text(lbl_reset, strings->reset_wifi);
  lv_obj_set_style_text_font(lbl_reset, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_reset);

}

static void settings_event_handler(lv_event_t *e) {
  play_click_sound();
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t *tgt = (lv_obj_t *)lv_event_get_target(e);

  if (tgt == touch_calibration_btn && code == LV_EVENT_CLICKED) {
    start_touch_calibration();
    return;
  }

  if (tgt == qweather_config_btn && code == LV_EVENT_CLICKED) {
    open_qweather_config_portal();
    return;
  }

  if (tgt == unit_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_fahrenheit = lv_obj_has_state(unit_switch, LV_STATE_CHECKED);
    render_weather_snapshot();
    return;
  }

  if (tgt == clock_24hr_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_24_hour = lv_obj_has_state(clock_24hr_switch, LV_STATE_CHECKED);
    render_weather_snapshot();
    return;
  }

  if (tgt == night_mode_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_night_mode = lv_obj_has_state(night_mode_switch, LV_STATE_CHECKED);
  }

  if (tgt == sound_enabled_switch && code == LV_EVENT_VALUE_CHANGED) {
    sound_enabled = lv_obj_has_state(sound_enabled_switch, LV_STATE_CHECKED);
  }

  if (tgt == sound_effect_dropdown && code == LV_EVENT_VALUE_CHANGED) {
    sound_effect = lv_dropdown_get_selected(sound_effect_dropdown);
  }

  if (tgt == language_dropdown && code == LV_EVENT_VALUE_CHANGED) {
    current_language = (Language)lv_dropdown_get_selected(language_dropdown);
    prefs.putBool("useFahrenheit", use_fahrenheit);
    prefs.putBool("use24Hour", use_24_hour);
    prefs.putBool("useNightMode", use_night_mode);
    prefs.putBool("soundEnabled", sound_enabled);
    prefs.putUInt("soundEffect", sound_effect);
    prefs.putUInt("language", current_language);

    rebuild_ui(true);
    return;
  }

  if (tgt == btn_close_obj &&
      (code == LV_EVENT_PRESSED || code == LV_EVENT_CLICKED)) {
    prefs.putBool("useFahrenheit", use_fahrenheit);
    prefs.putBool("use24Hour", use_24_hour);
    prefs.putBool("useNightMode", use_night_mode);
    prefs.putBool("soundEnabled", sound_enabled);
    prefs.putUInt("soundEffect", sound_effect);
    prefs.putUInt("language", current_language);

    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);

    lv_obj_del(settings_win);
    settings_win = nullptr;
    lbl_settings_location = nullptr;

    schedule_weather_refresh_after_click();
  }
}

static void refresh_weather_after_click_sound(lv_timer_t *timer) {
  (void)timer;
  fetch_and_update_weather();
}

static void schedule_weather_refresh_after_click() {
  lv_timer_t *timer = lv_timer_create(
      refresh_weather_after_click_sound, CLICK_SOUND_REFRESH_DELAY_MS, nullptr);
  lv_timer_set_repeat_count(timer, 1);
}

static void configure_click_tone(uint32_t frequency) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(SPEAKER_PIN, frequency, 8);
  ledcWrite(SPEAKER_PIN, SPEAKER_DUTY);
#else
  ledcSetup(SPEAKER_LEDC_CHANNEL, frequency, 8);
  ledcAttachPin(SPEAKER_PIN, SPEAKER_LEDC_CHANNEL);
  ledcWrite(SPEAKER_LEDC_CHANNEL, SPEAKER_DUTY);
#endif
}

static void start_click_tone(uint32_t frequency, uint32_t duration_ms) {
  configure_click_tone(frequency);
  speaker_timer = lv_timer_create(stop_click_sound, duration_ms, nullptr);
  // The callback owns deletion because sound effect 2 chains a second tone.
  lv_timer_set_repeat_count(speaker_timer, -1);
}

static void release_click_tone() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(SPEAKER_PIN, 0);
  ledcDetach(SPEAKER_PIN);
#else
  ledcWrite(SPEAKER_LEDC_CHANNEL, 0);
  ledcDetachPin(SPEAKER_PIN);
#endif
}

static void stop_click_sound(lv_timer_t *timer) {
  if (speaker_sequence_step == 1) {
    speaker_sequence_step = 2;
    configure_click_tone(3000);
    lv_timer_set_period(timer, 18);
    return;
  }

  release_click_tone();
  speaker_sequence_step = 0;
  speaker_timer = nullptr;
  lv_timer_del(timer);
}

void play_click_sound() {
  if (!sound_enabled) return;

  if (speaker_timer) {
    release_click_tone();
    lv_timer_del(speaker_timer);
    speaker_timer = nullptr;
    speaker_sequence_step = 0;
  }

  switch (sound_effect) {
    case 1:
      start_click_tone(1500, 35);
      break;
    case 2:
      speaker_sequence_step = 1;
      start_click_tone(2200, 18);
      break;
    case 3:
      start_click_tone(900, 45);
      break;
    default:
      start_click_tone(2200, 25);
      break;
  }
}

// Screen dimming functions implementation
bool night_mode_should_be_active() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return false;

  if (!use_night_mode) return false;
  
  int hour = timeinfo.tm_hour;
  return (hour >= NIGHT_MODE_START_HOUR || hour < NIGHT_MODE_END_HOUR);
}

void activate_night_mode() {
  analogWrite(LCD_BACKLIGHT_PIN, 0);
  night_mode_active = true;
}

void deactivate_night_mode() {
  analogWrite(LCD_BACKLIGHT_PIN, prefs.getUInt("brightness", 128));
  night_mode_active = false;
}

void check_for_night_mode() {
  bool night_mode_time = night_mode_should_be_active();

  if (night_mode_time && !night_mode_active && !temp_screen_wakeup_active) {
    activate_night_mode();
  } else if (!night_mode_time && night_mode_active) {
    deactivate_night_mode();
  }
}

void handle_temp_screen_wakeup_timeout(lv_timer_t *timer) {
  if (temp_screen_wakeup_active) {
    temp_screen_wakeup_active = false;

    if (night_mode_should_be_active()) {
      activate_night_mode();
    }
  }
  
  if (temp_screen_wakeup_timer) {
    lv_timer_del(temp_screen_wakeup_timer);
    temp_screen_wakeup_timer = nullptr;
  }
}

void do_geocode_query(const char *q) {
  geoDoc.clear();
  String url = String("https://geocoding-api.open-meteo.com/v1/search?name=") + urlencode(q) + "&count=15";

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(WEATHER_HTTP_TIMEOUT_MS);
  http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);
  if (http.GET() == HTTP_CODE_OK) {
    Serial.println("Completed location search at open-meteo: " + url);
    auto err = deserializeJson(geoDoc, http.getString());
    if (!err) {
      geoResults = geoDoc["results"].as<JsonArray>();
      populate_results_dropdown();
    } else {
        Serial.println("Failed to parse search response from open-meteo: " + url);
    }
  } else {
      Serial.println("Failed location search at open-meteo: " + url);
  }
  http.end();
}

static void fetch_open_meteo_weather() {
  String url = String("http://api.open-meteo.com/v1/forecast?latitude=")
               + latitude + "&longitude=" + longitude
               + "&current=temperature_2m,apparent_temperature,is_day,weather_code"
               + "&daily=temperature_2m_min,temperature_2m_max,weather_code"
               + "&hourly=temperature_2m,precipitation_probability,is_day,weather_code"
               + "&forecast_hours=7"
               + "&timezone=auto";

  HTTPClient http;
  http.begin(url);
  http.setConnectTimeout(WEATHER_HTTP_TIMEOUT_MS);
  http.setTimeout(WEATHER_HTTP_TIMEOUT_MS);
  if (http.GET() == HTTP_CODE_OK) {
    WeatherSnapshot candidate{};
    Serial.println("Updated weather from open-meteo: " + url);

    String payload = http.getString();
    DynamicJsonDocument doc(32 * 1024);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      JsonObject current = doc["current"].as<JsonObject>();
      JsonArray times = doc["daily"]["time"].as<JsonArray>();
      JsonArray tmin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
      JsonArray tmax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
      JsonArray weather_codes = doc["daily"]["weather_code"].as<JsonArray>();
      JsonArray hours = doc["hourly"]["time"].as<JsonArray>();
      JsonArray hourly_temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
      JsonArray precipitation_probabilities = doc["hourly"]["precipitation_probability"].as<JsonArray>();
      JsonArray hourly_weather_codes = doc["hourly"]["weather_code"].as<JsonArray>();
      JsonArray hourly_is_day = doc["hourly"]["is_day"].as<JsonArray>();

      bool arrays_complete =
          !current.isNull() &&
          !current["temperature_2m"].isNull() &&
          !current["apparent_temperature"].isNull() &&
          !current["weather_code"].isNull() &&
          !current["is_day"].isNull() &&
          times.size() >= FORECAST_POINT_COUNT &&
          tmin.size() >= FORECAST_POINT_COUNT &&
          tmax.size() >= FORECAST_POINT_COUNT &&
          weather_codes.size() >= FORECAST_POINT_COUNT &&
          hours.size() >= FORECAST_POINT_COUNT &&
          hourly_temps.size() >= FORECAST_POINT_COUNT &&
          hourly_weather_codes.size() >= FORECAST_POINT_COUNT &&
          hourly_is_day.size() >= FORECAST_POINT_COUNT;

      if (!arrays_complete) {
        Serial.println("Open-Meteo weather data incomplete.");
      } else {
        candidate.current.temperature = current["temperature_2m"].as<float>();
        candidate.current.feels_like = current["apparent_temperature"].as<float>();
        candidate.current.weather_code = current["weather_code"].as<int>();
        candidate.current.is_day = current["is_day"].as<int>() != 0;
        candidate.current.valid = true;

        bool points_complete = true;
        for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
          const char *date = times[i] | "";
          const char *date_time = hours[i] | "";
          if (strlen(date) < 10 || strlen(date_time) < 13 ||
              tmin[i].isNull() || tmax[i].isNull() ||
              weather_codes[i].isNull() || hourly_temps[i].isNull() ||
              hourly_weather_codes[i].isNull() || hourly_is_day[i].isNull()) {
            points_complete = false;
            break;
          }

          candidate.daily[i].minimum = tmin[i].as<float>();
          candidate.daily[i].maximum = tmax[i].as<float>();
          candidate.daily[i].weather_code = weather_codes[i].as<int>();
          candidate.daily[i].month = static_cast<uint8_t>(atoi(date + 5));
          candidate.daily[i].day = static_cast<uint8_t>(atoi(date + 8));
          candidate.daily[i].valid = true;

          candidate.hourly[i].temperature = hourly_temps[i].as<float>();
          candidate.hourly[i].weather_code = hourly_weather_codes[i].as<int>();
          candidate.hourly[i].hour = static_cast<uint8_t>(atoi(date_time + 11));
          candidate.hourly[i].is_day = hourly_is_day[i].as<int>() != 0;
          candidate.hourly[i].has_precipitation =
              i < precipitation_probabilities.size() &&
              !precipitation_probabilities[i].isNull();
          if (candidate.hourly[i].has_precipitation) {
            candidate.hourly[i].precipitation_probability =
                precipitation_probabilities[i].as<float>();
          }
          candidate.hourly[i].valid = true;
        }

        if (points_complete) {
          int utc_offset_seconds = doc["utc_offset_seconds"].as<int>();
          configTime(utc_offset_seconds, 0, "pool.ntp.org", "time.nist.gov");
          String updated_at = doc["current"]["time"] | "";
          publish_weather_snapshot(candidate);
          update_home_status(WEATHER_SOURCE_OPEN_METEO, updated_at.c_str());
        } else {
          Serial.println("Open-Meteo forecast points incomplete.");
        }
      }
    } else {
      Serial.println("Open-Meteo JSON parse failed.");
    }
  } else {
    Serial.println("Open-Meteo HTTP request failed.");
  }
  http.end();
}

void fetch_and_update_weather() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi no longer connected; reconnecting asynchronously.");
    WiFi.reconnect();
    return;
  }

  if (strlen(qweather_key) == 0) {
    Serial.println("QWeather API key missing; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  WeatherSnapshot candidate{};
  DynamicJsonDocument doc(32 * 1024);
  String location_query = String(longitude) + "," + latitude;

  if (!request_qweather(String("/v7/weather/now?location=") + location_query, doc)) {
    Serial.println("QWeather current weather unavailable; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  String qweather_updated_at = doc["updateTime"] | "";
  if (doc["now"]["temp"].isNull() || doc["now"]["feelsLike"].isNull() ||
      doc["now"]["icon"].isNull()) {
    Serial.println("QWeather current weather incomplete; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  int q_icon_now = doc["now"]["icon"].as<int>();
  candidate.current.temperature = doc["now"]["temp"].as<float>();
  candidate.current.feels_like = doc["now"]["feelsLike"].as<float>();
  candidate.current.weather_code = qweather_icon_to_wmo(q_icon_now);
  candidate.current.is_day = qweather_icon_is_day(q_icon_now);
  candidate.current.valid = true;

  if (!request_qweather(String("/v7/weather/7d?location=") + location_query, doc)) {
    Serial.println("QWeather daily forecast unavailable; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  JsonArray daily = doc["daily"].as<JsonArray>();
  if (daily.size() < FORECAST_POINT_COUNT) {
    Serial.println("QWeather daily forecast incomplete; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    const char *date = daily[i]["fxDate"] | "";
    if (strlen(date) < 10 || daily[i]["tempMin"].isNull() ||
        daily[i]["tempMax"].isNull() || daily[i]["iconDay"].isNull()) {
      Serial.println("QWeather daily point incomplete; using Open-Meteo fallback.");
      fetch_open_meteo_weather();
      return;
    }

    candidate.daily[i].minimum = daily[i]["tempMin"].as<float>();
    candidate.daily[i].maximum = daily[i]["tempMax"].as<float>();
    candidate.daily[i].weather_code = qweather_icon_to_wmo(
        daily[i]["iconDay"].as<int>());
    candidate.daily[i].month = static_cast<uint8_t>(atoi(date + 5));
    candidate.daily[i].day = static_cast<uint8_t>(atoi(date + 8));
    candidate.daily[i].valid = true;
  }

  if (!request_qweather(String("/v7/weather/24h?location=") + location_query, doc)) {
    Serial.println("QWeather hourly forecast unavailable; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  JsonArray hourly = doc["hourly"].as<JsonArray>();
  if (hourly.size() < FORECAST_POINT_COUNT) {
    Serial.println("QWeather hourly forecast incomplete; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }
  for (int i = 0; i < FORECAST_POINT_COUNT; i++) {
    const char *date_time = hourly[i]["fxTime"] | "";
    if (strlen(date_time) < 13 || hourly[i]["temp"].isNull() ||
        hourly[i]["icon"].isNull()) {
      Serial.println("QWeather hourly point incomplete; using Open-Meteo fallback.");
      fetch_open_meteo_weather();
      return;
    }

    int hourly_icon = hourly[i]["icon"].as<int>();
    candidate.hourly[i].temperature = hourly[i]["temp"].as<float>();
    candidate.hourly[i].weather_code = qweather_icon_to_wmo(hourly_icon);
    candidate.hourly[i].hour = static_cast<uint8_t>(atoi(date_time + 11));
    candidate.hourly[i].is_day = qweather_icon_is_day(hourly_icon);
    candidate.hourly[i].has_precipitation = !hourly[i]["pop"].isNull();
    if (candidate.hourly[i].has_precipitation) {
      candidate.hourly[i].precipitation_probability =
          hourly[i]["pop"].as<float>();
    }
    candidate.hourly[i].valid = true;
  }

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  publish_weather_snapshot(candidate);
  update_home_status(WEATHER_SOURCE_QWEATHER, qweather_updated_at.c_str());
}

const lv_img_dsc_t* choose_image(int code, int is_day) {
  switch (code) {
    // Clear sky
    case  0:
      return is_day
        ? &image_sunny
        : &image_clear_night;

    // Mainly clear
    case  1:
      return is_day
        ? &image_mostly_sunny
        : &image_mostly_clear_night;

    // Partly cloudy
    case  2:
      return is_day
        ? &image_partly_cloudy
        : &image_partly_cloudy_night;

    // Overcast
    case  3:
      return &image_cloudy;

    // Fog / mist
    case 45:
    case 48:
      return &image_haze_fog_dust_smoke;

    // Drizzle (light → dense)
    case 51:
    case 53:
    case 55:
      return &image_drizzle;

    // Freezing drizzle
    case 56:
    case 57:
      return &image_sleet_hail;

    // Rain: slight showers
    case 61:
      return is_day
        ? &image_scattered_showers_day
        : &image_scattered_showers_night;

    // Rain: moderate
    case 63:
      return &image_showers_rain;

    // Rain: heavy
    case 65:
      return &image_heavy_rain;

    // Freezing rain
    case 66:
    case 67:
      return &image_wintry_mix_rain_snow;

    // Snow fall (light, moderate, heavy) & snow showers (light)
    case 71:
    case 73:
    case 75:
    case 85:
      return &image_snow_showers_snow;

    // Snow grains
    case 77:
      return &image_flurries;

    // Rain showers (slight → moderate)
    case 80:
    case 81:
      return is_day
        ? &image_scattered_showers_day
        : &image_scattered_showers_night;

    // Rain showers: violent
    case 82:
      return &image_heavy_rain;

    // Heavy snow showers
    case 86:
      return &image_heavy_snow;

    // Thunderstorm (light)
    case 95:
      return is_day
        ? &image_isolated_scattered_tstorms_day
        : &image_isolated_scattered_tstorms_night;

    // Thunderstorm with hail
    case 96:
    case 99:
      return &image_strong_tstorms;

    // Fallback for any other code
    default:
      return is_day
        ? &image_mostly_cloudy_day
        : &image_mostly_cloudy_night;
  }
}

const lv_img_dsc_t* choose_icon(int code, int is_day) {
  switch (code) {
    // Clear sky
    case  0:
      return is_day
        ? &icon_sunny
        : &icon_clear_night;

    // Mainly clear
    case  1:
      return is_day
        ? &icon_mostly_sunny
        : &icon_mostly_clear_night;

    // Partly cloudy
    case  2:
      return is_day
        ? &icon_partly_cloudy
        : &icon_partly_cloudy_night;

    // Overcast
    case  3:
      return &icon_cloudy;

    // Fog / mist
    case 45:
    case 48:
      return &icon_haze_fog_dust_smoke;

    // Drizzle (light → dense)
    case 51:
    case 53:
    case 55:
      return &icon_drizzle;

    // Freezing drizzle
    case 56:
    case 57:
      return &icon_sleet_hail;

    // Rain: slight showers
    case 61:
      return is_day
        ? &icon_scattered_showers_day
        : &icon_scattered_showers_night;

    // Rain: moderate
    case 63:
      return &icon_showers_rain;

    // Rain: heavy
    case 65:
      return &icon_heavy_rain;

    // Freezing rain
    case 66:
    case 67:
      return &icon_wintry_mix_rain_snow;

    // Snow fall (light, moderate, heavy) & snow showers (light)
    case 71:
    case 73:
    case 75:
    case 85:
      return &icon_snow_showers_snow;

    // Snow grains
    case 77:
      return &icon_flurries;

    // Rain showers (slight → moderate)
    case 80:
    case 81:
      return is_day
        ? &icon_scattered_showers_day
        : &icon_scattered_showers_night;

    // Rain showers: violent
    case 82:
      return &icon_heavy_rain;

    // Heavy snow showers
    case 86:
      return &icon_heavy_snow;

    // Thunderstorm (light)
    case 95:
      return is_day
        ? &icon_isolated_scattered_tstorms_day
        : &icon_isolated_scattered_tstorms_night;

    // Thunderstorm with hail
    case 96:
    case 99:
      return &icon_strong_tstorms;

    // Fallback for any other code
    default:
      return is_day
        ? &icon_mostly_cloudy_day
        : &icon_mostly_cloudy_night;
  }
}
