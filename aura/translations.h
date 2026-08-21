#ifndef TRANSLATIONS_H
#define TRANSLATIONS_H

// Language support
enum Language { LANG_EN = 0, LANG_ES = 1, LANG_DE = 2, LANG_FR = 3, LANG_TR = 4, LANG_SV = 5, LANG_IT = 6, LANG_ZH = 7 };

struct LocalizedStrings {
  const char* temp_placeholder;
  const char* feels_like_temp;
  const char* seven_day_forecast;
  const char* hourly_forecast;
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
  const char* qweather_config;
  const char* qweather_config_status;
};

#define DEFAULT_CAPTIVE_SSID "Aura"

static const LocalizedStrings strings_en = {
  "--°C", "Feels Like", "SEVEN DAY FORECAST", "HOURLY FORECAST",
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
  "Sound:", "Effect:", "Classic\nSoft\nDouble\nLow", "Configure QWeather",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_es = {
  "--°C", "Sensación", "PRONÓSTICO 7 DÍAS", "PRONÓSTICO POR HORAS",
  "Hoy", "Ahora", "am", "pm", "Mediodía", "Hora inválida",
  "Brillo:", "Ubicación:", "Usar °F:", "24h:",
  "Guardar", "Cancelar", "Cerrar", "Ubicación", "Wi-Fi",
  "Restablecer", "Cambiar Ubicación", "Configuración Aura",
  "Ciudad:", "Resultados de Búsqueda", "ej. Madrid",
  "Configuración Wi-Fi:\n\n"
  "Conecte su teléfono\n"
  "o portátil al punto de\n"
  "acceso Wi-Fi temporal\n"
  DEFAULT_CAPTIVE_SSID
  "\n"
  "para configurar.\n\n"
  "Si no ve una pantalla\n"
  "de configuración después\n"
  "de conectarse, visite\n"
  "http://192.168.4.1\n"
  "en su navegador.",
  "¿Está seguro de que desea\n"
  "restablecer las credenciales\n"
  "Wi-Fi?\n\n"
  "Deberá reconectarse al SSID " DEFAULT_CAPTIVE_SSID
  " con su teléfono o navegador\n"
  "para reconfigurar las\n"
  "credenciales Wi-Fi.",
  "Idioma:",
  {"Dom", "Lun", "Mar", "Mié", "Jue", "Vie", "Sáb"},
  "Pantalla noche",
  "Calibración táctil", "Pulse cada objetivo en orden.\nMantenga pulsado hasta aceptar.",
  "Punto %d/5", "Cancelar calibración", "Calibración táctil completa.",
  "Calibración fallida; se conservaron los ajustes anteriores.",
  "Sonido:", "Efecto:", "Classic\nSoft\nDouble\nLow", "Configurar QWeather",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_de = {
  "--°C", "Gefühlt", "7-TAGE VORHERSAGE", "STÜNDLICHE VORHERSAGE",
  "Heute", "Jetzt", "", "", "Mittag", "Ungültige Stunde",
  "Helligkeit:", "Standort:", "°F:", "24h:",
  "Speichern", "Abbrechen", "Schließen", "Standort", "Wi-Fi",
  "Zurücksetzen", "Standort ändern", "Aura Einstellungen",
  "Stadt:", "Suchergebnisse", "z.B. Berlin",
  "Wi-Fi Konfiguration:\n\n"
  "Verbinden Sie Ihr Telefon\n"
  "oder Laptop mit dem\n"
  "temporären Wi-Fi\n"
  "Zugangspunkt "
  DEFAULT_CAPTIVE_SSID
  "\n"
  "zum Konfigurieren.\n\n"
  "Wenn Sie keinen\n"
  "Konfigurationsbildschirm\n"
  "sehen, besuchen Sie\n"
  "http://192.168.4.1\n"
  "in Ihrem Browser.",
  "Sind Sie sicher, dass Sie\n"
  "die Wi-Fi Zugangsdaten\n"
  "zurücksetzen möchten?\n\n"
  "Sie müssen sich erneut mit\n"
  "der SSID " DEFAULT_CAPTIVE_SSID
  " verbinden, um die\n"
  "Wi-Fi Zugangsdaten\n"
  "neu zu konfigurieren.",
  "Sprache:",
  {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"},
  "Nacht-Dimmen",
  "Touchscreen kalibrieren", "Drücken Sie die Ziele der Reihe nach.\nHalten Sie bis zur Bestätigung.",
  "Punkt %d/5", "Kalibrierung abbrechen", "Touchscreen kalibriert.",
  "Kalibrierung fehlgeschlagen; alte Einstellungen behalten.",
  "Ton:", "Effekt:", "Classic\nSoft\nDouble\nLow", "QWeather konfigurieren",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_fr = {
  "--°C", "Ressenti", "PRÉVISIONS 7 JOURS", "PRÉVISIONS HORAIRES",
  "Aujourd'hui", "Maintenant", "h", "h", "Midi", "Heure invalide",
  "Luminosité:", "Lieu:", "Utiliser °F:", "24h:",
  "Sauvegarder", "Annuler", "Fermer", "Lieu", "Wi-Fi",
  "Réinitialiser", "Changer de lieu", "Paramètres Aura",
  "Ville:", "Résultats de recherche", "ex. Paris",
  "Configuration Wi-Fi:\n\n"
  "Connectez votre téléphone\n"
  "ou ordinateur portable au\n"
  "point d'accès Wi-Fi\n"
  "temporaire "
  DEFAULT_CAPTIVE_SSID
  "\n"
  "pour configurer.\n\n"
  "Si vous ne voyez pas\n"
  "d'écran de configuration\n"
  "après connexion, visitez\n"
  "http://192.168.4.1\n"
  "dans votre navigateur.",
  "Êtes-vous sûr de vouloir\n"
  "réinitialiser les\n"
  "identifiants Wi-Fi?\n\n"
  "Vous devrez vous reconnecter\n"
  "au SSID " DEFAULT_CAPTIVE_SSID
  " avec votre téléphone ou\n"
  "navigateur pour reconfigurer\n"
  "les identifiants Wi-Fi.",
  "Langue:",
  {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"},
  "Nuit écran",
  "Calibration tactile", "Touchez chaque cible dans l'ordre.\nMaintenez jusqu'à validation.",
  "Point %d/5", "Annuler la calibration", "Calibration tactile terminée.",
  "Échec de la calibration; anciens réglages conservés.",
  "Son:", "Effet:", "Classic\nSoft\nDouble\nLow", "Configurer QWeather",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_tr = {
  "--°C", "Hissedilen", "YEDI GÜNLÜK TAHMIN", "SAATLIK TAHMIN",
  "Bugün", "Simdi", "öö", "ös", "Öğle", "Geçersiz saat",
  "Parlaklik:", "Konum:", "°F Kullan:", "24 Saat:",
  "Kaydet", "İptal", "Kapat", "Konum", "Wi-Fi Sifirla",
  "Sifirla", "Konumu Değiştir", "Aura Ayarlari",
  "Şehir:", "Arama Sonuçları", "örn. Londra",
  "Wi-Fi Yapilandirmasi:\n\n"
  "Lütfen telefonunuzu veya\n"
  "bilgisayarinizi geçici Wi-Fi\n"
  "erişim noktasina bağlayin "
  DEFAULT_CAPTIVE_SSID
  "\n"
  "yapilandirmak için.\n\n"
  "Bağlandiktan sonra bir\n"
  "yapilandirma ekrani görmezseniz,\n"
  "web tarayicinizda\n"
  "http://192.168.4.1 adresine gidin.",
  "Wi-Fi kimlik bilgilerini sifirlamak\n"
  "istediğinizden emin misiniz?\n\n"
  "Wi-Fi kimlik bilgilerini yeniden\n"
  "yapilandirmak için telefonunuz veya\n"
  "tarayiciniz ile " DEFAULT_CAPTIVE_SSID
  " SSID'sine tekrar bağlanmaniz\n"
  "gerekecek.",
  "Dil:",
  {"Paz", "Pzt", "Sal", "Çar", "Per", "Cum", "Cmt"},
  "Gece kısık",
  "Dokunmatik kalibrasyonu", "Hedeflere sırayla basın.\nKabul edilene kadar basılı tutun.",
  "Nokta %d/5", "Kalibrasyonu iptal et", "Dokunmatik kalibrasyonu tamamlandı.",
  "Kalibrasyon başarısız; önceki ayarlar korundu.",
  "Ses:", "Efekt:", "Classic\nSoft\nDouble\nLow", "QWeather yapılandır",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_sv = {
  "--°C", "Känns som", "7-DAGARS PROGNOS", "TIMPROGNOS",
  "Idag", "Nu", "", "", "Middag", "Ogiltig timme",
  "Ljusstyrka:", "Plats:", "Använd °F:", "24h:",
  "Spara", "Avbryt", "Stäng", "Plats", "Aterställ Wi-Fi",
  "Aterställ", "Andra plats", "Aura-inställningar",
  "Stad:", "Sökresultat", "t.ex. Stockholm",
  "Wi-Fi-konfiguration:\n\n"
  "Anslut din telefon\n"
  "eller laptop till den\n"
  "tillfälliga Wi-Fi-\n"
  "atkomstpunkten "
  DEFAULT_CAPTIVE_SSID
  "\n"
  "för att konfigurera.\n\n"
  "Om du inte ser en\n"
  "konfigurationsskärm\n"
  "efter anslutning, besök\n"
  "http://192.168.4.1\n"
  "i din webbläsare.",
  "Ar du säker pa att du vill\n"
  "aterställa Wi-Fi-\n"
  "autentiseringsuppgifter?\n\n"
  "Du maste ateransluta till\n"
  "SSID " DEFAULT_CAPTIVE_SSID
  " med din telefon eller\n"
  "webbläsare för att\n"
  "omkonfigurera Wi-Fi-\n"
  "autentiseringsuppgifter.",
  "Sprak:",
  {"Sön", "Man", "Tis", "Ons", "Tor", "Fre", "Lör"},
  "Nattdämpning",
  "Pekskärmskalibrering", "Tryck på varje mål i ordning.\nHåll tills det godkänns.",
  "Punkt %d/5", "Avbryt kalibrering", "Pekskärmen är kalibrerad.",
  "Kalibrering misslyckades; tidigare inställningar behölls.",
  "Ljud:", "Effekt:", "Classic\nSoft\nDouble\nLow", "Konfigurera QWeather",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_it = {
  "--°C", "Percepita", "PREVISIONI A 7 GIORNI", "PREVISIONI ORARIE",
  "Oggi", "Ora", "am", "pm", "Mezzog.", "Ora non valida",
  "Luminosità:", "Posizione:", "Utilizzo °F:", "24hr:",
  "Salva", "Cancellare", "Close", "Posizione", "Resetta Wi-Fi",
  "Reset", "Cambia posizione", "Impostazioni aura",
  "Città:", "Risultati di ricerca", "e.s. Londra",
  "Configurazione Wi-Fi:\n\n"
  "Per favore collega il tuo\n"
  "smartphone o laptop\n"
  "al Wi-Fi temporaneo\n "
  DEFAULT_CAPTIVE_SSID
  "\n"
  "per configurare la rete.\n\n"
  "Se non vedi la \n"
  "Schermata di configurazione \n"
  "dopo il collegamento,\n"
  "visita http://192.168.4.1\n"
  "sul tuo web browser.",
  "Sei sicuro di voler ripristinare "
  "le credenzili Wi-Fi ?\n\n"
  "Dovrai riconnetterti al WiFi con SSID " DEFAULT_CAPTIVE_SSID
  "con il tuo telefono o browser a "
  "riconfigurare le credenziali Wi-Fi.",
  "Lingua:",
  {"Dom", "Lun", "Mar", "Mer", "Gio", "Ven", "Sab"},
  "Schermo notte",
  "Calibrazione touch", "Premi ogni obiettivo in ordine.\nTieni premuto fino all'accettazione.",
  "Punto %d/5", "Annulla calibrazione", "Calibrazione touch completata.",
  "Calibrazione fallita; impostazioni precedenti mantenute.",
  "Suono:", "Effetto:", "Classic\nSoft\nDouble\nLow", "Configura QWeather",
  "QWeather API Key configuration is active.\nConnect to the Aura hotspot and open 192.168.4.1."
};

static const LocalizedStrings strings_zh = {
  "--°C", "体感温度", "七日天气预报", "小时天气预报",
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
  "Sound:", "效果:", "Classic\nSoft\nDouble\nLow", "和风天气API Key配置",
  "正在配置天气 API Key。\n请连接 Aura 热点，访问 192.168.4.1 完成配置。"
};

static const LocalizedStrings* get_strings(Language current_language) {
  switch (current_language) {
    case LANG_ES: return &strings_es;
    case LANG_DE: return &strings_de;
    case LANG_FR: return &strings_fr;
    case LANG_TR: return &strings_tr;
    case LANG_SV: return &strings_sv;
    case LANG_IT: return &strings_it;
    case LANG_ZH: return &strings_zh;
    default: return &strings_en;
  }
}

#endif // TRANSLATIONS_H
