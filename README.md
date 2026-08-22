# Aura

Aura is a simple weather widget that runs on ESP32-2432S028R ILI9341 devices with a 2.8" screen. These devices are sometimes called a "CYD" or Cheap Yellow Display.

This is just the source code for the project. This project includes a case design and assembly instructions. The complete instructions are available
here: https://makerworld.com/en/models/1382304-aura-smart-weather-forecast-display

### License

You can use the weather.ino code here under the terms of the GPL 3.0 license.

The icons are not included in that license. See "Thanks" below for details on the icons.

The Simplified Chinese interface uses glyphs generated from [Noto Sans CJK SC](https://github.com/notofonts/noto-cjk), licensed under the SIL Open Font License 1.1.

### How to compile:

1. Configure Arduino IDE 
    1. for "esp32" board with a device type of "ESP32 Dev Module" and
    1. set "Tools -> Partition Scheme" to "Huge App (3MB No OTA/1MB SPIFFS)"
1. Install the libraries below in Arduino IDE
1. Put the source code folders that are in this folder in ~/Documents/Arduino/
    1. Note the included config files for lvgl and TFT_eSPI need to be dropped in their respective folders
1. Install and run

### Libraries required to compile:

- ArduinoJson 7.4.1
- HttpClient 2.2.0
- TFT_eSPI 2.5.43_
- WifiManager 2.0.17
- XPT2046_Touchscreen 1.4
- lvgl 9.2.2

### Languages

The interface is available in English, Spanish, German, French, Turkish, Swedish, Italian, and Simplified Chinese.

### 显示方向与主题

在 Aura 设置的“显示设置”中可选择 0°、90°、180° 或 270°。0°/180°
使用竖屏列表，90°/270° 使用横屏折线图；触摸坐标会自动随屏幕方向校正。

主题提供深海、晴空、雨林、晚霞和高对比五种选择，并对主页、设置和弹窗全局生效。
七天横屏图显示最高温、最低温及逐日天气图标；小时横屏图显示逐小时温度、天气图标和可用的降水概率。

### Thanks & Credits

- Weather icons from https://github.com/mrdarrengriffin/google-weather-icons/tree/main/v2
- Thanks to [lvgl](https://lvgl.io/), a great library for UIs on ESP32 devices that made this much easier
- Thanks to [witnessmenow](https://github.com/witnessmenow/)'s [CYD Github repo](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) for dev board reference information
- Double thanks to [witnessmenow](https://github.com/witnessmenow/) for the [ESP32 web flashing tutorial](https://github.com/witnessmenow/ESP-Web-Tools-Tutorial)
- Thanks to [Random Nerd Tutorials](https://randomnerdtutorials.com/) for helpful ESP32 / CYD information, especially with [setting up LVGL](https://randomnerdtutorials.com/esp32-cyd-lvgl-line-chart/)
- Thanks to these sweet libraries that made this possible:
	- [ArduinoJson](https://arduinojson.org/)
	- [HttpClient](https://github.com/amcewen/HttpClient)
	- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
	- [WifiManager](https://github.com/tzapu/WiFiManager)
	- [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
	- [lvgl](https://lvgl.io/)
