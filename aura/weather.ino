#include <Arduino.h>
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
#include "translations.h"
#include "touch_calibration.h"

#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS
#define LCD_BACKLIGHT_PIN 21
#define SPEAKER_PIN 26   // On-board speaker/buzzer on ESP32-2432S028R
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

// Screen dimming variables
static bool night_mode_active = false;
static bool temp_screen_wakeup_active = false;
static lv_timer_t *temp_screen_wakeup_timer = nullptr;
static lv_timer_t *startup_weather_timer = nullptr;

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
static lv_obj_t *lbl_loc;
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
static lv_obj_t *touch_calibration_btn;
static lv_obj_t *sound_enabled_switch;
static lv_obj_t *sound_effect_dropdown;
static lv_obj_t *qweather_config_btn;

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
void fetch_and_update_weather();
void create_settings_window();
void play_click_sound();
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

void touchscreen_read(lv_indev_t *indev, lv_indev_data_t *data) {
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    TS_Point p = touchscreen.getPoint();

    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
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
                                  SCREEN_WIDTH, SCREEN_HEIGHT,
                                  &calibrated_x, &calibrated_y)) {
        x = calibrated_x;
        y = calibrated_y;
      }
    }

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

void setup() {
  Serial.begin(115200);
  delay(100);

  TFT_eSPI tft = TFT_eSPI();
  tft.init();
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  lv_init();

  // Init touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  touchscreen.setRotation(0);

  lv_display_t *disp = lv_tft_espi_create(SCREEN_WIDTH, SCREEN_HEIGHT, draw_buf, sizeof(draw_buf));
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touchscreen_read);

  // Load saved prefs
  prefs.begin("weather", false);
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
  lv_obj_set_style_border_width(qweather_portal_prompt, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(qweather_portal_prompt, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(qweather_portal_prompt, LV_OPA_COVER, LV_PART_MAIN);
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

void wifi_splash_screen() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x4c8cb9), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0xa6cdec), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_t *lbl = lv_label_create(scr);
  lv_label_set_text(lbl, strings->wifi_config);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(lbl);
  lv_scr_load(scr);
}

void create_ui() {
  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x4c8cb9), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0xa6cdec), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

  // Trigger settings screen on touch
  lv_obj_add_event_cb(scr, screen_event_cb, LV_EVENT_CLICKED, NULL);

  img_today_icon = lv_img_create(scr);
  lv_img_set_src(img_today_icon, &image_partly_cloudy);
  lv_obj_align(img_today_icon, LV_ALIGN_TOP_MID, -64, 4);

  static lv_style_t default_label_style;
  lv_style_init(&default_label_style);
  lv_style_set_text_color(&default_label_style, lv_color_hex(0xFFFFFF));
  lv_style_set_text_opa(&default_label_style, LV_OPA_COVER);

  const LocalizedStrings* strings = get_strings(current_language);

  lbl_today_temp = lv_label_create(scr);
  lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
  lv_obj_set_style_text_font(lbl_today_temp, get_font_42(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_temp, LV_ALIGN_TOP_MID, 45, 25);
  lv_obj_add_style(lbl_today_temp, &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);

  lbl_today_feels_like = lv_label_create(scr);
  lv_label_set_text(lbl_today_feels_like, strings->feels_like_temp);
  lv_obj_set_style_text_font(lbl_today_feels_like, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_today_feels_like, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_today_feels_like, LV_ALIGN_TOP_MID, 45, 75);

  lbl_forecast = lv_label_create(scr);
  lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
  lv_obj_set_style_text_font(lbl_forecast, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_forecast, lv_color_hex(0xe4ffff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl_forecast, LV_ALIGN_TOP_LEFT, 20, 110);

  box_daily = lv_obj_create(scr);
  lv_obj_set_size(box_daily, 220, 180);
  lv_obj_align(box_daily, LV_ALIGN_TOP_LEFT, 10, 135);
  lv_obj_set_style_bg_color(box_daily, lv_color_hex(0x5e9bc8), LV_PART_MAIN | LV_STATE_DEFAULT);
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
    lv_obj_add_style(lbl_daily_day[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_day[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_day[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_label_set_text(lbl_daily_high[i], "--");
    lv_obj_add_style(lbl_daily_high[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_high[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_high[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_daily_low[i], "--");
    lv_obj_set_style_text_color(lbl_daily_low[i], lv_color_hex(0xb9ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_daily_low[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_daily_low[i], LV_ALIGN_TOP_RIGHT, -50, i * 24);

    lv_img_set_src(img_daily[i], &icon_partly_cloudy);
    lv_obj_align(img_daily[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  box_hourly = lv_obj_create(scr);
  lv_obj_set_size(box_hourly, 220, 180);
  lv_obj_align(box_hourly, LV_ALIGN_TOP_LEFT, 10, 135);
  lv_obj_set_style_bg_color(box_hourly, lv_color_hex(0x5e9bc8), LV_PART_MAIN | LV_STATE_DEFAULT);
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
    lv_obj_add_style(lbl_hourly[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly[i], LV_ALIGN_TOP_LEFT, 2, i * 24);

    lv_label_set_text(lbl_hourly_temp[i], "--");
    lv_obj_add_style(lbl_hourly_temp[i], &default_label_style, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_hourly_temp[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_hourly_temp[i], LV_ALIGN_TOP_RIGHT, 0, i * 24);

    lv_label_set_text(lbl_precipitation_probability[i], "--");
    lv_obj_set_style_text_color(lbl_precipitation_probability[i], lv_color_hex(0xb9ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_precipitation_probability[i], get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_precipitation_probability[i], LV_ALIGN_TOP_RIGHT, -55, i * 24);

    lv_img_set_src(img_hourly[i], &icon_partly_cloudy);
    lv_obj_align(img_hourly[i], LV_ALIGN_TOP_LEFT, 72, i * 24);
  }

  lv_obj_add_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);

  // Create clock label in the top-right corner
  lbl_clock = lv_label_create(scr);
  lv_obj_set_style_text_font(lbl_clock, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(lbl_clock, lv_color_hex(0xb9ecff), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_text(lbl_clock, "");
  lv_obj_align(lbl_clock, LV_ALIGN_TOP_RIGHT, -10, 2);
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
    lv_obj_set_style_bg_color(btn_close_loc, lv_palette_main(LV_PALETTE_GREEN), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_close_loc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn_close_loc, lv_palette_darken(LV_PALETTE_GREEN, 1), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);
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
  lv_label_set_text(lbl_loc, opts.c_str());
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
  if (calibration_target) {
    lv_obj_del(calibration_target);
    calibration_target = nullptr;
  }

  calibration_target = lv_obj_create(calibration_overlay);
  lv_obj_set_size(calibration_target, 24, 24);
  lv_obj_set_pos(calibration_target,
                 static_cast<int>(TOUCH_CALIBRATION_TARGETS[calibration_target_index].x) - 12,
                 static_cast<int>(TOUCH_CALIBRATION_TARGETS[calibration_target_index].y) - 12);
  lv_obj_set_style_pad_all(calibration_target, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(calibration_target, lv_palette_main(LV_PALETTE_RED),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(calibration_target, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(calibration_target, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
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
  lv_msgbox_add_close_button(mbox);
  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);
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
  lv_obj_add_flag(settings_win, LV_OBJ_FLAG_HIDDEN);
  calibration_active = true;
  calibration_raw_pressed = false;
  calibration_started_ms = millis();
  calibration_last_touch_ms = calibration_started_ms;
  calibration_sample_count = 0;
  calibration_target_index = 0;
  calibration_state = TOUCH_CALIBRATION_WAIT_PRESS;

  calibration_overlay = lv_obj_create(lv_scr_act());
  lv_obj_set_size(calibration_overlay, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_set_pos(calibration_overlay, 0, 0);
  lv_obj_set_style_bg_color(calibration_overlay, lv_color_hex(0x183246),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(calibration_overlay, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(calibration_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(calibration_overlay, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(calibration_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(calibration_overlay, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *instructions = lv_label_create(calibration_overlay);
  lv_label_set_text(instructions, strings->calibration_instructions);
  lv_obj_set_width(instructions, 170);
  lv_obj_set_style_text_font(instructions, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(instructions, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(instructions, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(instructions, LV_ALIGN_TOP_MID, 0, 3);

  calibration_progress_label = lv_label_create(calibration_overlay);
  lv_obj_set_style_text_font(calibration_progress_label, get_font_12(),
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(calibration_progress_label, lv_color_white(),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(calibration_progress_label, LV_ALIGN_TOP_MID, 0, 25);

  lv_obj_t *cancel = lv_btn_create(calibration_overlay);
  lv_obj_set_size(cancel, 108, 32);
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
  play_click_sound();
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_add_flag(box_daily, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_forecast, strings->hourly_forecast);
  lv_obj_clear_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);
}

void hourly_cb(lv_event_t *e) {
  play_click_sound();
  const LocalizedStrings* strings = get_strings(current_language);
  lv_obj_add_flag(box_hourly, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(lbl_forecast, strings->seven_day_forecast);
  lv_obj_clear_flag(box_daily, LV_OBJ_FLAG_HIDDEN);
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
  lv_msgbox_add_close_button(mbox);

  lv_obj_t *btn_no = lv_msgbox_add_footer_button(mbox, strings->cancel);
  lv_obj_set_style_text_font(btn_no, get_font_12(), 0);
  lv_obj_t *btn_yes = lv_msgbox_add_footer_button(mbox, strings->reset);
  lv_obj_set_style_text_font(btn_yes, get_font_12(), 0);

  lv_obj_set_style_bg_color(btn_yes, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_yes, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_yes, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

  lv_obj_set_width(mbox, 230);
  lv_obj_center(mbox);

  lv_obj_set_style_border_width(mbox, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(mbox, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_border_opa(mbox, LV_OPA_COVER,   LV_PART_MAIN);
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
  location_win = lv_win_create(lv_scr_act());
  lv_obj_t *title = lv_win_add_title(location_win, strings->change_location);
  lv_obj_t *header = lv_win_get_header(location_win);
  lv_obj_set_style_height(header, 30, 0);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);
  lv_obj_set_size(location_win, 240, 320);
  lv_obj_center(location_win);

  lv_obj_t *cont = lv_win_get_content(location_win);

  lv_obj_t *lbl = lv_label_create(cont);
  lv_label_set_text(lbl, strings->city);
  lv_obj_set_style_text_font(lbl, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 5, 10);

  loc_ta = lv_textarea_create(cont);
  lv_textarea_set_one_line(loc_ta, true);
  lv_textarea_set_placeholder_text(loc_ta, strings->city_placeholder);
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

  lv_dropdown_set_options(results_dd, "");
  lv_obj_clear_flag(results_dd, LV_OBJ_FLAG_CLICKABLE);

  btn_close_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_close_loc, 80, 40);
  lv_obj_align(btn_close_loc, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  lv_obj_add_event_cb(btn_close_loc, location_save_event_cb, LV_EVENT_CLICKED, &geoResults);
  lv_obj_set_style_bg_color(btn_close_loc, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(btn_close_loc, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_close_loc, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_clear_flag(btn_close_loc, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *lbl_close = lv_label_create(btn_close_loc);
  lv_label_set_text(lbl_close, strings->save);
  lv_obj_set_style_text_font(lbl_close, get_font_14(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_close);

  lv_obj_t *btn_cancel_loc = lv_btn_create(cont);
  lv_obj_set_size(btn_cancel_loc, 80, 40);
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
  settings_win = lv_win_create(lv_scr_act());
  lv_obj_set_size(settings_win, SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_obj_center(settings_win);
  lv_obj_t *header = lv_win_get_header(settings_win);
  lv_obj_set_style_height(header, 30, 0);
  lv_obj_t *title = lv_win_add_title(settings_win, strings->aura_settings);
  lv_obj_set_style_text_font(title, get_font_16(), 0);
  lv_obj_set_style_margin_left(title, 10, 0);

  btn_close_obj = lv_btn_create(header);
  lv_obj_set_size(btn_close_obj, 42, LV_PCT(100));
  lv_obj_set_style_bg_opa(btn_close_obj, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(btn_close_obj, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(btn_close_obj, settings_event_handler, LV_EVENT_PRESSED, NULL);
  lv_obj_t *close_label = lv_label_create(btn_close_obj);
  lv_label_set_text(close_label, "X");
  lv_obj_set_style_text_font(close_label, get_font_16(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(close_label, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(close_label);

  lv_obj_t *cont = lv_win_get_content(settings_win);

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
    lv_obj_set_width(row, 214);
    lv_obj_set_height(row, height);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
  };

  auto style_label = [&](lv_obj_t *label) {
    lv_obj_set_style_text_font(label, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
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
  lv_obj_align(night_mode_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (use_night_mode) lv_obj_add_state(night_mode_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(night_mode_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Fahrenheit
  lv_obj_t *fahrenheit_row = create_row(34);
  lv_obj_t *lbl_u = lv_label_create(fahrenheit_row);
  lv_label_set_text(lbl_u, strings->use_fahrenheit);
  style_label(lbl_u);
  unit_switch = lv_switch_create(fahrenheit_row);
  lv_obj_align(unit_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (use_fahrenheit) lv_obj_add_state(unit_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(unit_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // 24-hour clock
  lv_obj_t *clock_row = create_row(34);
  lv_obj_t *lbl_24hr = lv_label_create(clock_row);
  lv_label_set_text(lbl_24hr, strings->use_24hr);
  style_label(lbl_24hr);
  clock_24hr_switch = lv_switch_create(clock_row);
  lv_obj_align(clock_24hr_switch, LV_ALIGN_RIGHT_MID, 0, 0);
  if (use_24_hour) lv_obj_add_state(clock_24hr_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(clock_24hr_switch, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Current Location label
  lv_obj_t *location_row = create_row(34);
  lv_obj_t *lbl_loc_l = lv_label_create(location_row);
  lv_label_set_text(lbl_loc_l, strings->location);
  style_label(lbl_loc_l);
  lbl_loc = lv_label_create(location_row);
  lv_label_set_text(lbl_loc, location.c_str());
  lv_obj_set_style_text_font(lbl_loc, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_label_set_long_mode(lbl_loc, LV_LABEL_LONG_DOT);
  lv_obj_set_width(lbl_loc, 135);
  lv_obj_align(lbl_loc, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_text_align(lbl_loc, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);

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
  lv_obj_align(language_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(language_dropdown, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // Sound enable
  lv_obj_t *sound_row = create_row(34);
  lv_obj_t *lbl_sound = lv_label_create(sound_row);
  lv_label_set_text(lbl_sound, strings->sound_enabled);
  style_label(lbl_sound);
  sound_enabled_switch = lv_switch_create(sound_row);
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
  lv_obj_align(sound_effect_dropdown, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(sound_effect_dropdown, settings_event_handler, LV_EVENT_VALUE_CHANGED, NULL);

  // QWeather configuration portal
  lv_obj_t *qweather_row = create_row(38);
  qweather_config_btn = lv_btn_create(qweather_row);
  lv_obj_set_size(qweather_config_btn, 204, 34);
  lv_obj_add_event_cb(qweather_config_btn, settings_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_qweather = lv_label_create(qweather_config_btn);
  lv_label_set_text(lbl_qweather, strings->qweather_config);
  lv_obj_set_style_text_font(lbl_qweather, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_qweather);

  // Touch calibration button
  lv_obj_t *calibration_row = create_row(38);
  touch_calibration_btn = lv_btn_create(calibration_row);
  lv_obj_set_size(touch_calibration_btn, 204, 34);
  lv_obj_add_event_cb(touch_calibration_btn, settings_event_handler, LV_EVENT_CLICKED, NULL);
  lv_obj_t *lbl_calibrate = lv_label_create(touch_calibration_btn);
  lv_label_set_text(lbl_calibrate, strings->touch_calibration);
  lv_obj_set_style_text_font(lbl_calibrate, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_center(lbl_calibrate);

  // Location search button
  lv_obj_t *location_button_row = create_row(38);
  lv_obj_t *btn_change_loc = lv_btn_create(location_button_row);
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
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, NULL);
  }

  // Reset WiFi button
  lv_obj_t *reset_row = create_row(38);
  lv_obj_t *btn_reset = lv_btn_create(reset_row);
  lv_obj_set_style_bg_color(btn_reset, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn_reset, lv_palette_darken(LV_PALETTE_RED, 1), LV_PART_MAIN | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btn_reset, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
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
  }

  if (tgt == clock_24hr_switch && code == LV_EVENT_VALUE_CHANGED) {
    use_24_hour = lv_obj_has_state(clock_24hr_switch, LV_STATE_CHECKED);
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
    // Update the UI immediately to reflect language change
    lv_obj_del(settings_win);
    settings_win = nullptr;
    
    // Save preferences and recreate UI with new language
    prefs.putBool("useFahrenheit", use_fahrenheit);
    prefs.putBool("use24Hour", use_24_hour);
    prefs.putBool("useNightMode", use_night_mode);
    prefs.putBool("soundEnabled", sound_enabled);
    prefs.putUInt("soundEffect", sound_effect);
    prefs.putUInt("language", current_language);

    lv_keyboard_set_textarea(kb, nullptr);
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    
    // Recreate the main UI with the new language
    lv_obj_clean(lv_scr_act());
    create_ui();
    fetch_and_update_weather();
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

    fetch_and_update_weather();
  }
}

void play_click_sound() {
  if (!sound_enabled) return;

  switch (sound_effect) {
    case 1:
      tone(SPEAKER_PIN, 1500, 35);
      break;
    case 2:
      tone(SPEAKER_PIN, 2200, 18);
      delay(25);
      tone(SPEAKER_PIN, 3000, 18);
      break;
    case 3:
      tone(SPEAKER_PIN, 900, 45);
      break;
    default:
      tone(SPEAKER_PIN, 2200, 25);
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
    Serial.println("Updated weather from open-meteo: " + url);

    String payload = http.getString();
    DynamicJsonDocument doc(32 * 1024);
    if (deserializeJson(doc, payload) == DeserializationError::Ok) {
      float t_now = doc["current"]["temperature_2m"].as<float>();
      float t_ap = doc["current"]["apparent_temperature"].as<float>();
      int code_now = doc["current"]["weather_code"].as<int>();
      int is_day = doc["current"]["is_day"].as<int>();
      if (use_fahrenheit) {
        t_now = t_now * 9.0 / 5.0 + 32.0;
        t_ap = t_ap * 9.0 / 5.0 + 32.0;
      }

      const LocalizedStrings* strings = get_strings(current_language);
      int utc_offset_seconds = doc["utc_offset_seconds"].as<int>();
      configTime(utc_offset_seconds, 0, "pool.ntp.org", "time.nist.gov");

      char unit = use_fahrenheit ? 'F' : 'C';
      lv_label_set_text_fmt(lbl_today_temp, "%.0f°%c", t_now, unit);
      lv_label_set_text_fmt(lbl_today_feels_like, "%s %.0f°%c", strings->feels_like_temp, t_ap, unit);
      lv_img_set_src(img_today_icon, choose_image(code_now, is_day));

      JsonArray times = doc["daily"]["time"].as<JsonArray>();
      JsonArray tmin = doc["daily"]["temperature_2m_min"].as<JsonArray>();
      JsonArray tmax = doc["daily"]["temperature_2m_max"].as<JsonArray>();
      JsonArray weather_codes = doc["daily"]["weather_code"].as<JsonArray>();
      for (int i = 0; i < 7; i++) {
        const char *date = times[i];
        int year = atoi(date);
        int mon = atoi(date + 5);
        int dayd = atoi(date + 8);
        int dow = day_of_week(year, mon, dayd);
        const char *day_str = (i == 0 && current_language != LANG_FR)
                                ? strings->today
                                : strings->weekdays[dow];

        float mn = tmin[i].as<float>();
        float mx = tmax[i].as<float>();
        if (use_fahrenheit) {
          mn = mn * 9.0 / 5.0 + 32.0;
          mx = mx * 9.0 / 5.0 + 32.0;
        }
        lv_label_set_text_fmt(lbl_daily_day[i], "%s", day_str);
        lv_label_set_text_fmt(lbl_daily_high[i], "%.0f°%c", mx, unit);
        lv_label_set_text_fmt(lbl_daily_low[i], "%.0f°%c", mn, unit);
        lv_img_set_src(img_daily[i], choose_icon(weather_codes[i].as<int>(), (i == 0) ? is_day : 1));
      }

      JsonArray hours = doc["hourly"]["time"].as<JsonArray>();
      JsonArray hourly_temps = doc["hourly"]["temperature_2m"].as<JsonArray>();
      JsonArray precipitation_probabilities = doc["hourly"]["precipitation_probability"].as<JsonArray>();
      JsonArray hourly_weather_codes = doc["hourly"]["weather_code"].as<JsonArray>();
      JsonArray hourly_is_day = doc["hourly"]["is_day"].as<JsonArray>();
      for (int i = 0; i < 7; i++) {
        const char *date_time = hours[i];
        int hour = atoi(date_time + 11);
        String hour_name = hour_of_day(hour);
        float precipitation_probability = precipitation_probabilities[i].as<float>();
        float temp = hourly_temps[i].as<float>();
        if (use_fahrenheit) temp = temp * 9.0 / 5.0 + 32.0;

        if (i == 0 && current_language != LANG_FR) {
          lv_label_set_text(lbl_hourly[i], strings->now);
        } else {
          lv_label_set_text(lbl_hourly[i], hour_name.c_str());
        }
        lv_label_set_text_fmt(lbl_precipitation_probability[i], "%.0f%%", precipitation_probability);
        lv_label_set_text_fmt(lbl_hourly_temp[i], "%.0f°%c", temp, unit);
        lv_img_set_src(img_hourly[i], choose_icon(hourly_weather_codes[i].as<int>(), hourly_is_day[i].as<int>()));
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

  DynamicJsonDocument doc(32 * 1024);
  String location_query = String(longitude) + "," + latitude;
  const LocalizedStrings* strings = get_strings(current_language);
  const char unit = use_fahrenheit ? 'F' : 'C';

  if (!request_qweather(String("/v7/weather/now?location=") + location_query, doc)) {
    Serial.println("QWeather current weather unavailable; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  float t_now = doc["now"]["temp"].as<float>();
  float t_ap = doc["now"]["feelsLike"].as<float>();
  int q_icon_now = doc["now"]["icon"].as<int>();
  int code_now = qweather_icon_to_wmo(q_icon_now);
  int is_day = qweather_icon_is_day(q_icon_now);
  if (use_fahrenheit) {
    t_now = t_now * 9.0 / 5.0 + 32.0;
    t_ap = t_ap * 9.0 / 5.0 + 32.0;
  }

  configTime(8 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  lv_label_set_text_fmt(lbl_today_temp, "%.0f°%c", t_now, unit);
  lv_label_set_text_fmt(lbl_today_feels_like, "%s %.0f°%c", strings->feels_like_temp, t_ap, unit);
  lv_img_set_src(img_today_icon, choose_image(code_now, is_day));

  if (!request_qweather(String("/v7/weather/7d?location=") + location_query, doc)) {
    Serial.println("QWeather daily forecast unavailable; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  JsonArray daily = doc["daily"].as<JsonArray>();
  for (int i = 0; i < 7 && i < static_cast<int>(daily.size()); i++) {
    const char *date = daily[i]["fxDate"] | "";
    if (strlen(date) < 10) continue;

    int year = atoi(date);
    int mon = atoi(date + 5);
    int dayd = atoi(date + 8);
    int dow = day_of_week(year, mon, dayd);
    const char *day_str = (i == 0 && current_language != LANG_FR)
                            ? strings->today
                            : strings->weekdays[dow];
    float mn = daily[i]["tempMin"].as<float>();
    float mx = daily[i]["tempMax"].as<float>();
    int daily_icon = qweather_icon_to_wmo(daily[i]["iconDay"].as<int>());
    if (use_fahrenheit) {
      mn = mn * 9.0 / 5.0 + 32.0;
      mx = mx * 9.0 / 5.0 + 32.0;
    }

    lv_label_set_text_fmt(lbl_daily_day[i], "%s", day_str);
    lv_label_set_text_fmt(lbl_daily_high[i], "%.0f°%c", mx, unit);
    lv_label_set_text_fmt(lbl_daily_low[i], "%.0f°%c", mn, unit);
    lv_img_set_src(img_daily[i], choose_icon(daily_icon, 1));
  }

  if (!request_qweather(String("/v7/weather/24h?location=") + location_query, doc)) {
    Serial.println("QWeather hourly forecast unavailable; using Open-Meteo fallback.");
    fetch_open_meteo_weather();
    return;
  }

  JsonArray hourly = doc["hourly"].as<JsonArray>();
  for (int i = 0; i < 7 && i < static_cast<int>(hourly.size()); i++) {
    const char *date_time = hourly[i]["fxTime"] | "";
    if (strlen(date_time) < 16) continue;

    int hour = atoi(date_time + 11);
    String hour_name = hour_of_day(hour);
    float precipitation_probability = hourly[i]["pop"].as<float>();
    float temp = hourly[i]["temp"].as<float>();
    int hourly_icon = hourly[i]["icon"].as<int>();
    if (use_fahrenheit) temp = temp * 9.0 / 5.0 + 32.0;

    if (i == 0 && current_language != LANG_FR) {
      lv_label_set_text(lbl_hourly[i], strings->now);
    } else {
      lv_label_set_text(lbl_hourly[i], hour_name.c_str());
    }
    lv_label_set_text_fmt(lbl_precipitation_probability[i], "%.0f%%", precipitation_probability);
    lv_label_set_text_fmt(lbl_hourly_temp[i], "%.0f°%c", temp, unit);
    lv_img_set_src(img_hourly[i], choose_icon(
        qweather_icon_to_wmo(hourly_icon), qweather_icon_is_day(hourly_icon)));
  }
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
