# Aura 横屏当前天气标题区实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在横屏首页隐藏地点、设备 IP 和天气源名称，并用 42px 温度与 20px 天气状态填充释放的顶部空间，最后编译并烧录到连接的 CYD 设备。

**Architecture:** 保留现有天气快照、横屏图表和右侧控制，只调整 `create_landscape_header()` 创建的标签和 `update_home_status()` 的横屏文本格式。竖屏仍创建地点、IP 与数据源状态；横屏只创建更新时间标签，因此状态刷新函数按标签是否存在及当前方向分别更新。

**Tech Stack:** Arduino ESP32、LVGL 9、Python `unittest` 静态契约、Arduino CLI 1.5.1、ESP32-2432S028R。

---

## 文件职责

- 修改 `tests/test_landscape_weather_ui.py`：锁定横屏标题标签、字体、位置、状态文本与图表坐标不变的契约。
- 修改 `aura/weather.ino`：实现横屏标题布局与方向相关的状态文本刷新。
- 构建产物仅写入 `/private/tmp`：使用仓库中的 TFT_eSPI 配置编译和烧录，不提交二进制。

### Task 1: 用失败测试锁定 A 方案

**Files:**
- Modify: `tests/test_landscape_weather_ui.py:106`
- Test: `tests/test_landscape_weather_ui.py`

- [ ] **Step 1: 替换横屏标题契约测试**

将 `test_header_has_current_weather_status_segmented_controls_and_settings` 的标题区断言改为：

```python
def test_landscape_header_prioritizes_large_current_weather(self):
    header = function_body(
        "static void create_landscape_header(lv_obj_t *scr) {",
        "static void create_forecast_segmented_control",
    )
    self.assertNotIn("lbl_home_location = lv_label_create(scr);", header)
    self.assertNotIn("lbl_network_status = lv_label_create(scr);", header)
    self.assertIn("lbl_update_status = lv_label_create(scr);", header)
    self.assertIn("lv_obj_set_pos(lbl_update_status, 6, 44);", header)
    self.assertIn("lv_obj_set_size(lbl_today_temp, 76, 46);", header)
    self.assertIn("lv_obj_set_pos(lbl_today_temp, 6, 0);", header)
    self.assertIn("lbl_today_temp, get_font_42()", header)
    self.assertIn("lv_obj_set_size(landscape_current_condition, 96, 24);", header)
    self.assertIn("lv_obj_set_pos(landscape_current_condition, 84, 4);", header)
    self.assertIn("landscape_current_condition, get_font_20()", header)
    self.assertIn("lv_obj_set_size(lbl_today_feels_like, 100, 16);", header)
    self.assertIn("lv_obj_set_pos(lbl_today_feels_like, 84, 32);", header)

def test_landscape_update_status_omits_provider_and_ip(self):
    update = function_body(
        "void update_home_status(uint8_t source, const char *updated_at) {",
        "static void update_clock",
    )
    self.assertIn("if (lbl_network_status)", update)
    self.assertIn("if (lbl_update_status)", update)
    self.assertIn("geometry_for_rotation(current_rotation).landscape", update)
    self.assertIn(
        'lv_label_set_text_fmt(lbl_update_status, "%s %s",\n'
        "                          strings->weather_updated, compact_updated.c_str());",
        update,
    )
    self.assertIn(
        'lv_label_set_text_fmt(lbl_update_status, "%s %s",\n'
        "                          source_name.c_str(), compact_updated.c_str());",
        update,
    )
```

保留同一测试中的分段按钮和设置按钮断言，但将其放到独立的 `test_landscape_segmented_controls_and_settings_remain_unchanged`，继续检查 `strings->daily_tab`、`strings->hourly_tab`、`LV_SYMBOL_SETTINGS` 和原坐标。

- [ ] **Step 2: 运行测试并确认红灯**

Run:

```bash
python3 -m unittest tests.test_landscape_weather_ui.LandscapeWeatherUiTests.test_landscape_header_prioritizes_large_current_weather tests.test_landscape_weather_ui.LandscapeWeatherUiTests.test_landscape_update_status_omits_provider_and_ip -v
```

Expected: 两项都因旧横屏标题仍创建地点/IP、温度仍使用 20px、天气仍使用 12px、状态仍显示数据源而失败。

- [ ] **Step 3: 提交测试红灯基线**

```bash
git add tests/test_landscape_weather_ui.py
git commit -m "测试横屏当前天气标题布局"
```

### Task 2: 实现温度主导的横屏标题区

**Files:**
- Modify: `aura/weather.ino:873-888`
- Modify: `aura/weather.ino:2085-2159`
- Test: `tests/test_landscape_weather_ui.py`

- [ ] **Step 1: 让状态刷新支持横屏仅显示更新时间**

在 `update_home_status()` 中保留数据缓存，去掉要求两个标签同时存在的早退条件。设备 IP 只在 `lbl_network_status` 存在时更新；更新时间在横屏使用本地化 `weather_updated` 前缀，在竖屏继续使用天气源名称：

```cpp
if (!lbl_network_status && !lbl_update_status) return;

const LocalizedStrings *strings = get_strings(current_language);
String updated = weather_updated_at.length() > 0 ? weather_updated_at : String("--");
String compact_updated = updated.length() >= 16 ? updated.substring(11, 16) : updated;

if (lbl_network_status) {
  String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--");
  lv_label_set_text_fmt(
      lbl_network_status, "%s %s", strings->device_ip, ip.c_str());
}
if (lbl_update_status) {
  if (geometry_for_rotation(current_rotation).landscape) {
    lv_label_set_text_fmt(lbl_update_status, "%s %s",
                          strings->weather_updated, compact_updated.c_str());
  } else {
    String source_name = weather_source_name(weather_source, strings);
    lv_label_set_text_fmt(lbl_update_status, "%s %s",
                          source_name.c_str(), compact_updated.c_str());
  }
}
```

- [ ] **Step 2: 用 A 方案替换横屏标题对象布局**

`create_landscape_header()` 不再创建 `lbl_home_location` 和 `lbl_network_status`，并使用以下稳定尺寸：

```cpp
lbl_update_status = lv_label_create(scr);
lv_obj_set_size(lbl_update_status, 76, 13);
lv_obj_set_pos(lbl_update_status, 6, 44);
lv_label_set_long_mode(lbl_update_status, LV_LABEL_LONG_DOT);
lv_obj_set_style_text_font(
    lbl_update_status, get_font_12(), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_text_color(
    lbl_update_status, theme_color(palette.muted),
    LV_PART_MAIN | LV_STATE_DEFAULT);

lbl_today_temp = lv_label_create(scr);
lv_obj_set_size(lbl_today_temp, 76, 46);
lv_obj_set_pos(lbl_today_temp, 6, 0);
lv_label_set_text(lbl_today_temp, strings->temp_placeholder);
lv_obj_set_style_text_font(
    lbl_today_temp, get_font_42(), LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_text_color(
    lbl_today_temp, theme_color(palette.text),
    LV_PART_MAIN | LV_STATE_DEFAULT);

landscape_current_condition = lv_label_create(scr);
lv_obj_set_size(landscape_current_condition, 96, 24);
lv_obj_set_pos(landscape_current_condition, 84, 4);
lv_label_set_long_mode(landscape_current_condition, LV_LABEL_LONG_DOT);
lv_label_set_text(landscape_current_condition, "--");
lv_obj_set_style_text_font(
    landscape_current_condition, get_font_20(),
    LV_PART_MAIN | LV_STATE_DEFAULT);
lv_obj_set_style_text_color(
    landscape_current_condition, theme_color(palette.accent),
    LV_PART_MAIN | LV_STATE_DEFAULT);

lbl_today_feels_like = lv_label_create(scr);
lv_obj_set_size(lbl_today_feels_like, 100, 16);
lv_obj_set_pos(lbl_today_feels_like, 84, 32);
```

其余体感标签样式、隐藏时钟和 `update_home_status()` 调用保持现有实现。

- [ ] **Step 3: 运行目标测试并确认绿灯**

```bash
python3 -m unittest tests.test_landscape_weather_ui -v
```

Expected: 横屏测试全部通过。

- [ ] **Step 4: 运行完整回归测试**

```bash
python3 -m unittest discover -s tests -v
git diff --check
```

Expected: 全部测试通过，`git diff --check` 无输出。

- [ ] **Step 5: 提交实现**

```bash
git add aura/weather.ino tests/test_landscape_weather_ui.py
git commit -m "放大横屏当前天气标题"
```

### Task 3: 编译、定位 CYD 串口并烧录

**Files:**
- Build only: `/private/tmp/cyd-landscape-header-*`

- [ ] **Step 1: 建立隔离构建目录并复制正确显示配置**

```bash
sketch_dir="$(mktemp -d /private/tmp/cyd-landscape-header-sketch.XXXXXX)"
build_dir="$(mktemp -d /private/tmp/cyd-landscape-header-build.XXXXXX)"
library_dir="$(mktemp -d /private/tmp/cyd-landscape-header-libs.XXXXXX)"
cp -R aura "$sketch_dir/aura"
mv "$sketch_dir/aura/weather.ino" "$sketch_dir/aura/aura.ino"
cp -R /Users/luckmiracle/Documents/Arduino/libraries/TFT_eSPI "$library_dir/TFT_eSPI"
cp TFT_eSPI/User_Setup.h "$library_dir/TFT_eSPI/User_Setup.h"
```

- [ ] **Step 2: 编译 ESP32 huge_app 固件**

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
  --libraries "$library_dir" \
  --build-path "$build_dir" \
  --build-property "compiler.cpp.extra_flags=-DLV_CONF_INCLUDE_SIMPLE -I/Users/luckmiracle/Documents/ChatGPT/CYD/lvgl/src" \
  "$sketch_dir/aura"
```

Expected: 编译退出码为 0，并输出程序存储空间与动态内存用量。

- [ ] **Step 3: 确认 CYD 串口**

```bash
arduino-cli board list
ioreg -r -c IOSerialBSDClient -l
```

选择与 CYD 的 USB-UART 芯片和当前 USB 连接位置对应的 `/dev/cu.usbserial-*`。若存在多个候选且无法从设备树唯一判定，不尝试轮流烧录，先请用户确认。

- [ ] **Step 4: 上传并校验固件**

```bash
arduino-cli upload \
  --fqbn esp32:esp32:esp32:PartitionScheme=huge_app \
  --port "$cyd_port" \
  --input-dir "$build_dir" \
  --verify \
  "$sketch_dir/aura"
```

Expected: esptool 完成写入、校验并复位设备，退出码为 0。

- [ ] **Step 5: 读取启动日志**

```bash
arduino-cli monitor --port "$cyd_port" --config baudrate=115200
```

读取一次完整启动序列后退出监视器。确认没有反复复位、Guru Meditation、显示初始化失败或天气任务崩溃。

## 自检结果

- [x] 地点、IP、天气源名称的横屏隐藏均有测试和实现步骤。
- [x] 42px 温度、20px 天气状态和体感/更新时间位置均有精确契约。
- [x] 竖屏状态文本保持天气源名称，横屏状态文本仅保留本地化更新时间。
- [x] 图表尺寸与右侧控制不修改，并由现有横屏测试继续保护。
- [x] 烧录前必须通过全套测试与 CYD 配置编译，且多个串口无法唯一判断时不会盲目写入。
