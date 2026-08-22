# Aura

Aura 是一款简洁的天气小组件，运行在配备 2.8 英寸 ILI9341 屏幕的 ESP32-2432S028R 设备上。这类设备有时也称为“CYD”或“廉价黄色显示屏”（Cheap Yellow Display）。

本仓库仅包含项目源代码。项目另有外壳设计和组装说明，完整说明位于：
https://makerworld.com/en/models/1382304-aura-smart-weather-forecast-display

### 许可

本仓库中的 weather.ino 代码按照 GPL 3.0 许可证使用。

图标不在该许可证范围内，详情请参阅下方“致谢”。

简体中文界面使用由 [Noto Sans CJK SC](https://github.com/notofonts/noto-cjk) 生成的字形，该字体采用 SIL Open Font License 1.1 许可证。

### 编译方法

1. 配置 Arduino IDE：
   1. 安装“esp32”开发板支持，并将设备类型设为“ESP32 Dev Module”。
   1. 将“Tools -> Partition Scheme”设为“Huge App (3MB No OTA/1MB SPIFFS)”。
1. 在 Arduino IDE 中安装下列依赖库。
1. 将当前目录中的源代码文件夹放入 `~/Documents/Arduino/`。
   1. 随附的 lvgl 和 TFT_eSPI 配置文件需要放入各自的库目录。
1. 编译并运行。

### 编译所需库

- ArduinoJson 7.4.1
- HttpClient 2.2.0
- TFT_eSPI 2.5.43_
- WifiManager 2.0.17
- XPT2046_Touchscreen 1.4
- lvgl 9.2.2

### 支持语言

界面支持英语和简体中文，默认使用简体中文。

### 天气数据源

Open-Meteo 是默认天气源，无需 API Key，可提供当前天气、七日预报和小时级天气。
和风天气作为可选天气源，可在设置中选择并配置 API Key；缺少 Key 或请求失败时，
Aura 会自动使用 Open-Meteo 完成当前刷新。

### 显示方向与主题

在 Aura 设置的“显示设置”中可选择 0°、90°、180° 或 270°。0°/180°
使用竖屏列表，90°/270° 使用横屏折线图；触摸坐标会自动随屏幕方向校正。

主题提供深海、晴空、雨林、晚霞和高对比五种选择，并对主页、设置和弹窗全局生效。
七天横屏图显示最高温、最低温及逐日天气图标；小时横屏图显示逐小时温度、天气图标和可用的降水概率。

### 致谢

- 天气图标来自 https://github.com/mrdarrengriffin/google-weather-icons/tree/main/v2
- 感谢 [lvgl](https://lvgl.io/)，它让在 ESP32 设备上构建界面变得容易许多。
- 感谢 [witnessmenow](https://github.com/witnessmenow/) 的 [CYD GitHub 仓库](https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display) 提供开发板参考资料。
- 再次感谢 [witnessmenow](https://github.com/witnessmenow/) 提供 [ESP32 网页刷写教程](https://github.com/witnessmenow/ESP-Web-Tools-Tutorial)。
- 感谢 [Random Nerd Tutorials](https://randomnerdtutorials.com/) 提供实用的 ESP32/CYD 资料，尤其是[配置 LVGL](https://randomnerdtutorials.com/esp32-cyd-lvgl-line-chart/)的说明。
- 感谢以下优秀的依赖库：
  - [ArduinoJson](https://arduinojson.org/)
  - [HttpClient](https://github.com/amcewen/HttpClient)
  - [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
  - [WifiManager](https://github.com/tzapu/WiFiManager)
  - [XPT2046_Touchscreen](https://github.com/PaulStoffregen/XPT2046_Touchscreen)
  - [lvgl](https://lvgl.io/)
