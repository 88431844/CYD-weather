#ifndef TRANSLATIONS_H
#define TRANSLATIONS_H

#include "display_config.h"

// Language support
enum Language { LANG_EN = 0, LANG_ZH = 1 };

inline Language validated_language(uint32_t value) {
  return value == LANG_EN ? LANG_EN : LANG_ZH;
}

struct LocalizedStrings {
  const char* temp_placeholder;
  const char* feels_like_temp;
  const char* humidity;
  const char* seven_day_forecast;
  const char* hourly_forecast;
  const char* daily_tab;
  const char* hourly_tab;
  const char* today;
  const char* now;
  const char* am;
  const char* pm;
  const char* noon;
  const char* invalid_hour;
  const char* brightness;
  const char* location;
  const char* use_fahrenheit;
  const char* use_24hr;
  const char* save;
  const char* cancel;
  const char* close;
  const char* location_btn;
  const char* reset_wifi;
  const char* reset;
  const char* change_location;
  const char* aura_settings;
  const char* city;
  const char* search_results;
  const char* city_placeholder;
  const char* wifi_config;
  const char* reset_confirmation;
  const char* language_label;
  const char* weekdays[7];
  const char* use_night_mode;
  const char* touch_calibration;
  const char* calibration_instructions;
  const char* calibration_progress;
  const char* calibration_cancel;
  const char* calibration_success;
  const char* calibration_failed;
  const char* sound_enabled;
  const char* sound_effect;
  const char* sound_effect_options;
  const char* weather_provider;
  const char* qweather_config;
  const char* qweather_config_status;
  const char* device_ip;
  const char* weather_source;
  const char* weather_updated;
  const char* sunrise;
  const char* sunset;
  const char* qweather_name;
  const char* open_meteo_name;
  const char* display_settings;
  const char* theme;
  const char* screen_orientation;
  const char* touch_rotation;
  const char* theme_names[THEME_COUNT];
  const char* weather_conditions[10];
};

#define DEFAULT_CAPTIVE_SSID "Aura"

static const LocalizedStrings strings_en = {
  "--°C", "Feels Like", "Humidity", "SEVEN DAY FORECAST", "HOURLY FORECAST",
  "7 days", "Hours",
  "Today", "Now", "am", "pm", "Noon", "Invalid hour",
  "Brightness:", "Location:", "Use °F:", "24hr:",
  "Save", "Cancel", "Close", "Location", "Reset Wi-Fi",
  "Reset", "Change Location", "Aura Settings",
  "City:", "Search Results", "e.g. London",
  "Wi-Fi Configuration:\n\n"
  "Please connect your\n"
  "phone or laptop to the\n"
  "temporary Wi-Fi access\n point "
  DEFAULT_CAPTIVE_SSID
  "\n"
  "to configure.\n\n"
  "If you don't see a \n"
  "configuration screen \n"
  "after connecting,\n"
  "visit http://192.168.4.1\n"
  "in your web browser.",
  "Are you sure you want to reset "
  "Wi-Fi credentials?\n\n"
  "You'll need to reconnect to the Wifi SSID " DEFAULT_CAPTIVE_SSID
  " with your phone or browser to "
  "reconfigure Wi-Fi credentials.",
  "Language:",
  {"Sun", "Mon", "Tues", "Wed", "Thurs", "Fri", "Sat"},
  "Dim screen at night",
  "Touch Calibration", "Press each target in order.\nHold until accepted.",
  "Point %d/5", "Cancel Calibration", "Touch calibration complete.",
  "Touch calibration failed; previous settings kept.",
  "Sound:", "Effect:", "Classic\nSoft\nDouble\nLow", "Weather provider:", "Configure QWeather",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1.",
  "IP:", "Source:", "Updated:", "Sunrise", "Sunset", "QWeather", "Open-Meteo",
  "Display", "Theme", "Orientation", "Correct touch",
  {"Deep Sea", "Clear Sky", "Rainforest", "Sunset", "High Contrast"},
  {"Clear", "Partly cloudy", "Cloudy", "Fog", "Drizzle", "Light rain", "Heavy rain", "Sleet", "Snow", "Thunderstorm"}
};

static const LocalizedStrings strings_zh = {
  "--°C", "体感温度", "湿度", "七日天气预报", "小时天气预报",
  "7天", "小时",
  "今天", "现在", "上午", "下午", "中午", "无效时间",
  "亮度:", "位置:", "使用 °F:", "24小时:",
  "保存", "取消", "关闭", "位置", "重置 Wi-Fi",
  "重置", "更改位置", "Aura 设置",
  "城市:", "搜索结果", "例如: 北京",
  "Wi-Fi 配置:\n\n"
  "请使用手机或电脑\n"
  "连接临时 Wi-Fi 热点\n"
  DEFAULT_CAPTIVE_SSID
  "\n"
  "进行配置。\n\n"
  "如果连接后没有出现\n"
  "配置页面，请在浏览器中\n"
  "访问 http://192.168.4.1",
  "确定要重置 Wi-Fi 凭据吗?\n\n"
  "你需要使用手机或电脑\n"
  "重新连接到 Wi-Fi 网络 " DEFAULT_CAPTIVE_SSID
  "，然后重新配置 Wi-Fi。",
  "语言:",
  {"周日", "周一", "周二", "周三", "周四", "周五", "周六"},
  "夜间调暗",
  "屏幕校验", "请长按每个校验点，直到确认通过。",
  "校验点 %d/5", "取消", "屏幕校验",
  "重调，设置",
  "声音:", "效果:", "经典\n柔和\n双音\n低沉", "天气源:", "和风天气API Key配置",
  "正在配置天气 API Key。\n请连接 Aura 热点，访问 192.168.4.1 完成配置。",
  "IP:", "天气源:", "更新时间:", "日出", "日落", "和风天气", "Open-Meteo",
  "显示设置", "主题", "屏幕方向", "自动校正触摸",
  {"深海", "晴空", "雨林", "晚霞", "高对比"},
  {"晴", "多云", "阴", "雾", "毛毛雨", "小雨", "大雨", "雨夹雪", "雪", "雷雨"}
};

static const LocalizedStrings* get_strings(Language current_language) {
  switch (current_language) {
    case LANG_ZH: return &strings_zh;
    default: return &strings_en;
  }
}

#endif // TRANSLATIONS_H
