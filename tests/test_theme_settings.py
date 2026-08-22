#!/usr/bin/env python3
"""主题与天气状态翻译的回归测试。"""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "aura"
TRANSLATIONS = (HEADER_DIR / "translations.h").read_text(encoding="utf-8")
WEATHER = (HEADER_DIR / "weather.ino").read_text(encoding="utf-8")


class ThemeSettingsTranslationTests(unittest.TestCase):
    def run_cpp(self, body):
        source = f'''#include "translations.h"
#include <cstring>
#include <type_traits>

int main() {{
  const LocalizedStrings *default_strings = get_strings(LANG_EN);
  (void)default_strings;
{body}
}}
'''
        with tempfile.TemporaryDirectory() as temp_dir:
            source_path = Path(temp_dir) / "test.cpp"
            binary_path = Path(temp_dir) / "test"
            source_path.write_text(source, encoding="utf-8")
            compiled = subprocess.run(
                [
                    "c++", "-std=c++17", "-Wall", "-Werror", "-I", str(HEADER_DIR),
                    str(source_path), "-o", str(binary_path),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(binary_path)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)

    def test_header_uses_theme_count_for_new_arrays(self):
        self.assertIn('#include "display_config.h"', TRANSLATIONS)
        for field in (
            "display_settings",
            "theme",
            "screen_orientation",
            "touch_rotation",
            "theme_names[THEME_COUNT]",
            "weather_conditions[10]",
        ):
            self.assertIn(field, TRANSLATIONS)

    def test_all_languages_provide_ordered_theme_and_weather_strings(self):
        self.run_cpp(r'''
  static_assert(std::extent<decltype(strings_en.theme_names)>::value == THEME_COUNT);
  static_assert(std::extent<decltype(strings_en.weather_conditions)>::value == 10);
  const LocalizedStrings *languages[] = {
      &strings_en, &strings_es, &strings_de, &strings_fr,
      &strings_tr, &strings_sv, &strings_it, &strings_zh};
  struct ExpectedStrings {
    const char *settings[4];
    const char *themes[THEME_COUNT];
    const char *weather[10];
  };
  const ExpectedStrings expected[] = {
      {{"Display", "Theme", "Orientation", "Correct touch"},
       {"Deep Sea", "Clear Sky", "Rainforest", "Sunset", "High Contrast"},
       {"Clear", "Partly cloudy", "Cloudy", "Fog", "Drizzle", "Light rain", "Heavy rain", "Sleet", "Snow", "Thunderstorm"}},
      {{"Pantalla", "Tema", "Orientación", "Corregir toque"},
       {"Mar profundo", "Cielo claro", "Selva", "Atardecer", "Alto contraste"},
       {"Despejado", "Parcialmente nublado", "Nublado", "Niebla", "Llovizna", "Lluvia ligera", "Lluvia fuerte", "Aguanieve", "Nieve", "Tormenta"}},
      {{"Anzeige", "Thema", "Ausrichtung", "Touch korrigieren"},
       {"Tiefsee", "Klarer Himmel", "Regenwald", "Abendrot", "Hoher Kontrast"},
       {"Klar", "Teilweise bewölkt", "Bewölkt", "Nebel", "Nieselregen", "Leichter Regen", "Starkregen", "Schneeregen", "Schnee", "Gewitter"}},
      {{"Affichage", "Thème", "Orientation", "Corriger le tactile"},
       {"Haute mer", "Ciel clair", "Forêt", "Crépuscule", "Contraste élevé"},
       {"Dégagé", "Peu nuageux", "Nuageux", "Brouillard", "Bruine", "Pluie faible", "Forte pluie", "Grésil", "Neige", "Orage"}},
      {{"Ekran", "Tema", "Yön", "Dokunmayı düzelt"},
       {"Derin deniz", "Açık gökyüzü", "Yağmur ormanı", "Gün batımı", "Yüksek kontrast"},
       {"Açık", "Parçalı bulutlu", "Bulutlu", "Sis", "Çiseleme", "Hafif yağmur", "Şiddetli yağmur", "Sulu kar", "Kar", "Fırtına"}},
      {{"Skärm", "Tema", "Riktning", "Korrigera touch"},
       {"Djuphav", "Klar himmel", "Regnskog", "Solnedgång", "Hög kontrast"},
       {"Klart", "Delvis molnigt", "Molnigt", "Dimma", "Duggregn", "Lätt regn", "Kraftigt regn", "Snöblandat", "Snö", "Åska"}},
      {{"Schermo", "Tema", "Orientamento", "Correggi tocco"},
       {"Mare profondo", "Cielo sereno", "Foresta", "Tramonto", "Alto contrasto"},
       {"Sereno", "Parzialmente nuvoloso", "Nuvoloso", "Nebbia", "Pioviggine", "Pioggia leggera", "Pioggia forte", "Nevischio", "Neve", "Temporale"}},
      {{"显示设置", "主题", "屏幕方向", "自动校正触摸"},
       {"深海", "晴空", "雨林", "晚霞", "高对比"},
       {"晴", "多云", "阴", "雾", "毛毛雨", "小雨", "大雨", "雨夹雪", "雪", "雷雨"}},
  };
  for (int language = 0; language < 8; language++) {
    const LocalizedStrings *actual = languages[language];
    const char *settings[] = {
        actual->display_settings, actual->theme,
        actual->screen_orientation, actual->touch_rotation};
    for (int index = 0; index < 4; index++) {
      if (std::strcmp(settings[index], expected[language].settings[index])) return 10 + language;
    }
    for (int index = 0; index < THEME_COUNT; index++) {
      if (std::strcmp(actual->theme_names[index], expected[language].themes[index])) return 20 + language;
    }
    for (int index = 0; index < 10; index++) {
      if (std::strcmp(actual->weather_conditions[index], expected[language].weather[index])) return 30 + language;
    }
  }
  return 0;
''')

    def test_create_ui_only_dispatches_to_orientation_builders(self):
        create_start = WEATHER.index("void create_ui() {")
        create_end = WEATHER.index("void populate_results_dropdown()", create_start)
        create = WEATHER[create_start:create_end]
        self.assertIn("static void create_portrait_ui(lv_obj_t *scr) {", WEATHER)
        self.assertIn("static void create_landscape_ui(lv_obj_t *scr) {", WEATHER)
        self.assertEqual(create.count("create_portrait_ui(scr);"), 1)
        self.assertEqual(create.count("create_landscape_ui(scr);"), 1)
        self.assertIn("geometry_for_rotation(current_rotation).landscape", create)
        for object_name in (
            "img_today_icon",
            "lbl_today_temp",
            "lbl_network_status",
            "box_daily",
            "box_hourly",
            "lbl_clock",
        ):
            self.assertNotIn(f"{object_name} =", create)

    def test_theme_helpers_apply_current_palette_to_root(self):
        helpers_start = WEATHER.index("static lv_color_t theme_color(uint32_t rgb) {")
        helpers_end = WEATHER.index("void wifi_splash_screen()", helpers_start)
        helpers = WEATHER[helpers_start:helpers_end]
        self.assertIn("return lv_color_hex(rgb);", helpers)
        self.assertIn("static void apply_root_theme(lv_obj_t *root)", helpers)
        self.assertIn("theme_palette(current_theme)", helpers)
        self.assertIn("theme_color(palette.background)", helpers)
        self.assertIn("theme_color(palette.text)", helpers)

    def test_theme_palette_is_used_by_all_required_surfaces(self):
        function_ranges = {
            "启动页": ("void wifi_splash_screen() {", "static void create_portrait_ui(lv_obj_t *scr) {", "theme_palette(current_theme)"),
            "竖屏主页": ("static void create_portrait_ui(lv_obj_t *scr) {", "void create_ui() {", "theme_palette(current_theme)"),
            "和风天气配置提示": ("static void open_qweather_config_portal() {", "static void qweather_cancel_event_cb", "apply_msgbox_theme("),
            "校准结果消息框": ("static void finish_touch_calibration(bool success) {", "static void calibration_timer_cb", "apply_msgbox_theme("),
            "触摸校准页": ("static void start_touch_calibration() {", "void daily_cb", "theme_palette(current_theme)"),
            "重置消息框": ("static void reset_wifi_event_handler(lv_event_t *e) {", "static void reset_confirm_yes_cb", "apply_msgbox_theme("),
            "位置窗口": ("void create_location_dialog() {", "void create_settings_window()", "theme_palette(current_theme)"),
            "设置窗": ("void create_settings_window() {", "static void settings_event_handler", "theme_palette(current_theme)"),
        }
        for surface, (start_marker, end_marker, theme_marker) in function_ranges.items():
            start = WEATHER.index(start_marker)
            end = WEATHER.index(end_marker, start)
            body = WEATHER[start:end]
            self.assertIn(theme_marker, body, surface)

    def test_night_mode_only_controls_backlight(self):
        start = WEATHER.index("bool night_mode_should_be_active() {")
        end = WEATHER.index("void do_geocode_query", start)
        night_mode = WEATHER[start:end]
        self.assertIn("analogWrite(LCD_BACKLIGHT_PIN", night_mode)
        self.assertNotIn("theme_palette", night_mode)
        self.assertNotIn("lv_obj_set_style", night_mode)

    def test_portrait_builder_uses_object_local_label_styles(self):
        start = WEATHER.index("static void create_portrait_ui(lv_obj_t *scr) {")
        end = WEATHER.index("void create_ui() {", start)
        portrait = WEATHER[start:end]
        self.assertNotIn("lv_style_init(", portrait)
        self.assertNotIn("lv_obj_add_style(", portrait)
        for label in (
            "lbl_today_temp",
            "lbl_daily_day[i]",
            "lbl_daily_high[i]",
            "lbl_hourly[i]",
            "lbl_hourly_temp[i]",
        ):
            self.assertIn(f"lv_obj_set_style_text_color({label}", portrait)
            self.assertIn(f"lv_obj_set_style_text_opa({label}", portrait)

    def test_control_theme_helpers_cover_lvgl_parts_and_states(self):
        helpers_start = WEATHER.index("static void apply_button_theme(lv_obj_t *button, bool destructive) {")
        helpers_end = WEATHER.index("void wifi_splash_screen()", helpers_start)
        helpers = WEATHER[helpers_start:helpers_end]
        for helper in (
            "apply_button_theme",
            "apply_slider_theme",
            "apply_switch_theme",
            "apply_dropdown_theme",
            "apply_textarea_theme",
            "apply_keyboard_theme",
            "apply_msgbox_theme",
        ):
            self.assertIn(f"static void {helper}(", helpers)
        for selector in (
            "LV_PART_INDICATOR",
            "LV_PART_KNOB",
            "LV_PART_SELECTED",
            "LV_PART_ITEMS",
            "LV_STATE_PRESSED",
            "LV_STATE_CHECKED",
            "LV_STATE_DISABLED",
        ):
            self.assertIn(selector, helpers)
        for getter in (
            "lv_msgbox_get_header(mbox)",
            "lv_msgbox_get_content(mbox)",
            "lv_msgbox_get_footer(mbox)",
        ):
            self.assertIn(getter, helpers)

    def test_settings_and_location_controls_use_theme_helpers(self):
        settings_start = WEATHER.index("void create_settings_window() {")
        settings_end = WEATHER.index("static void settings_event_handler", settings_start)
        settings = WEATHER[settings_start:settings_end]
        for call in (
            "apply_button_theme(btn_close_obj",
            "apply_slider_theme(slider);",
            "apply_switch_theme(night_mode_switch);",
            "apply_switch_theme(unit_switch);",
            "apply_switch_theme(clock_24hr_switch);",
            "apply_switch_theme(sound_enabled_switch);",
            "apply_dropdown_theme(language_dropdown);",
            "apply_dropdown_theme(sound_effect_dropdown);",
            "apply_button_theme(qweather_config_btn",
            "apply_button_theme(touch_calibration_btn",
            "apply_button_theme(btn_change_loc",
            "apply_keyboard_theme(kb);",
            "apply_button_theme(btn_reset",
        ):
            self.assertIn(call, settings)

        location_start = WEATHER.index("void create_location_dialog() {")
        location_end = WEATHER.index("void create_settings_window()", location_start)
        location = WEATHER[location_start:location_end]
        for call in (
            "apply_textarea_theme(loc_ta);",
            "apply_dropdown_theme(results_dd);",
            "apply_button_theme(btn_close_loc",
            "apply_button_theme(btn_cancel_loc",
            "lv_obj_add_state(btn_close_loc, LV_STATE_DISABLED);",
        ):
            self.assertIn(call, location)

    def test_message_boxes_theme_containers_and_buttons(self):
        cases = (
            ("static void open_qweather_config_portal() {", "static void qweather_cancel_event_cb", ("cancel",)),
            ("static void finish_touch_calibration(bool success) {", "static void calibration_timer_cb", ("close",)),
            ("static void reset_wifi_event_handler(lv_event_t *e) {", "static void reset_confirm_yes_cb", ("close", "btn_no", "btn_yes")),
        )
        for start_marker, end_marker, buttons in cases:
            start = WEATHER.index(start_marker)
            end = WEATHER.index(end_marker, start)
            body = WEATHER[start:end]
            self.assertIn("apply_msgbox_theme(", body)
            for button in buttons:
                self.assertIn(f"apply_button_theme({button}", body)


if __name__ == "__main__":
    unittest.main()
