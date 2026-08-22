#!/usr/bin/env python3
"""主题与天气状态翻译的回归测试。"""

from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER_DIR = ROOT / "aura"
TRANSLATIONS = (HEADER_DIR / "translations.h").read_text(encoding="utf-8")


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


if __name__ == "__main__":
    unittest.main()
