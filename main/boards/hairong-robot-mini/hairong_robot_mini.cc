// HaiRong Robot Mini 板级实现。
//
// 与 main/boards/eda-robot-pro 的差异仅三处：
//   1. 板型名 BOARD_NAME = "HaiRong Robot Mini"；
//   2. 新增 KEY2（IO11）长按 3 秒进入 WiFi 配网；
//   3. 自研代码按 2 空格 / 行宽 80 编写（Google C++ Style），
//      从 eda-robot-pro 原样复制的文件保持 4 空格不动。
//
// 其余（I2S 引脚、OLED、舵机、MCP 工具）与 eda-robot-pro 完全一致。

#include "wifi_board.h"

#include "application.h"
#include "assets/lang_config.h"
#include "button.h"
#include "codecs/no_audio_codec.h"
#include "config.h"
#include "display/oled_display.h"
#include "mcp_server.h"

#include <driver/i2c_master.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_log.h>
#include <ssid_manager.h>
#include <wifi_station.h>

namespace {
constexpr char kTag[] = "HaiRongRobotMini";

// WiFi 单次等待连接的时长；超时后只刷新屏显提示，不进入配网模式
constexpr int kWifiConnectTimeoutMs = 60 * 1000;

// 扫描中 / 连接中 / 已连接 这类状态提示的显示时长
constexpr int kNotifyDurationMs = 30 * 1000;

// 无法连接时的屏幕提示：两行，适配 128x64 OLED
constexpr char kNoWifiHint[] = "Wi-Fi 未连接\n长按K2进入配网";

// 提示保持时长略长于超时周期，避免上一条到期后闪空一瞬
constexpr int kNoWifiHintDurationMs = 70 * 1000;

// SSD1306 的 I2C 从机地址
constexpr uint8_t kOledI2cAddr = 0x3C;

// OLED I2C 时钟频率：400 kHz
constexpr uint32_t kOledI2cSclHz = 400 * 1000;
}  // namespace

// 定义在 eda_dog_controller.cc，负责注册机器狗 MCP 工具
extern void InitializeEDARobotDogController();

class HaiRongRobotMini : public WifiBoard {
 public:
  HaiRongRobotMini()
      : boot_button_(BOOT_BUTTON_GPIO),
        touch_button_(TOUCH_BUTTON_GPIO),
        key2_button_(KEY2_BUTTON_GPIO, /*active_high=*/false,
                     KEY2_CONFIG_MODE_LONG_PRESS_MS) {
    InitializeDisplayI2c();
    InitializeSsd1306Display();
    InitializeRobotDogController();
    InitializeButtons();
  }

  AudioCodec* GetAudioCodec() override {
    static NoAudioCodecSimplex audio_codec(
        AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK,
        AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK,
        AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
    return &audio_codec;
  }

  Display* GetDisplay() override { return display_; }

  // 覆写上游 StartNetwork()：连不上 WiFi 时不进配网，原地等待。
  //
  // 上游实现（boards/common/wifi_board.cc）在 WaitForConnected(60 s) 失败后
  // 调用 EnterWifiConfigMode()，而该函数末尾是 while(true)，设备会永久停在
  // 配网模式，无法等"预设网络"进入信号范围后自动连接。
  //
  // 本实现改为：超时只刷新屏显提示，然后继续等待。WifiStation 内部会周期性
  // 重扫（扫描失败的 SSID 不进队列，扫到即连），连上后本函数返回，
  // Application::Start() 继续走激活与协议初始化。
  void StartNetwork() override {
    // 长按 K2 触发配网后重启：构造函数已把 force_ap 读入 wifi_config_mode_
    if (wifi_config_mode_) {
      EnterWifiConfigMode();
      return;
    }

    auto& ssid_manager = SsidManager::GetInstance();
    if (ssid_manager.GetSsidList().empty()) {
      // 一条凭据都没有，只能进配网
      EnterWifiConfigMode();
      return;
    }

    auto* display = GetDisplay();
    auto& wifi_station = WifiStation::GetInstance();

    wifi_station.OnScanBegin([display]() {
      display->ShowNotification(Lang::Strings::SCANNING_WIFI, kNotifyDurationMs);
    });
    wifi_station.OnConnect([display](const std::string& ssid) {
      std::string notification = Lang::Strings::CONNECT_TO;
      notification += ssid;
      notification += "...";
      display->ShowNotification(notification.c_str(), kNotifyDurationMs);
    });
    wifi_station.OnConnected([display](const std::string& ssid) {
      std::string notification = Lang::Strings::CONNECTED_TO;
      notification += ssid;
      display->ShowNotification(notification.c_str(), kNotifyDurationMs);
    });

    ESP_LOGI(kTag, "Starting station with %d saved ssid(s)",
             static_cast<int>(ssid_manager.GetSsidList().size()));
    wifi_station.Start();

    // 超时不放弃，只刷新提示后继续等待；WifiStation 会自己重扫重连。
    while (!wifi_station.WaitForConnected(kWifiConnectTimeoutMs)) {
      ESP_LOGW(kTag, "WiFi not connected within %d ms, keep waiting",
               kWifiConnectTimeoutMs);
      display->ShowNotification(kNoWifiHint, kNoWifiHintDurationMs);
    }
    ESP_LOGI(kTag, "WiFi connected, continue startup");
  }

 private:
  i2c_master_bus_handle_t display_i2c_bus_ = nullptr;
  esp_lcd_panel_io_handle_t panel_io_ = nullptr;
  esp_lcd_panel_handle_t panel_ = nullptr;
  Display* display_ = nullptr;
  Button boot_button_;
  Button touch_button_;
  Button key2_button_;

  void InitializeDisplayI2c() {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = static_cast<i2c_port_t>(0),
        .sda_io_num = DISPLAY_SDA_PIN,
        .scl_io_num = DISPLAY_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags =
            {
                .enable_internal_pullup = 1,
            },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &display_i2c_bus_));
  }

  void InitializeSsd1306Display() {
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = kOledI2cAddr,
        .on_color_trans_done = nullptr,
        .user_ctx = nullptr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags =
            {
                .dc_low_on_data = 0,
                .disable_control_phase = 0,
            },
        .scl_speed_hz = kOledI2cSclHz,
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_i2c_v2(display_i2c_bus_, &io_config, &panel_io_));

    ESP_LOGI(kTag, "Install SSD1306 driver");
    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.bits_per_pixel = 1;

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {
        .height = static_cast<uint8_t>(DISPLAY_HEIGHT),
    };
    panel_config.vendor_config = &ssd1306_config;

    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(panel_io_, &panel_config,
                                             &panel_));
    ESP_LOGI(kTag, "SSD1306 driver installed");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_));
    if (esp_lcd_panel_init(panel_) != ESP_OK) {
      ESP_LOGE(kTag, "Failed to initialize display");
      display_ = new NoDisplay();
      return;
    }
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_, true));

    display_ = new OledDisplay(panel_io_, panel_, DISPLAY_WIDTH, DISPLAY_HEIGHT,
                               DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
  }

  void InitializeRobotDogController() {
    ESP_LOGI(kTag, "Initialize robot dog MCP controller");
    ::InitializeEDARobotDogController();
  }

  void InitializeButtons() {
    touch_button_.OnPressDown(
        []() { Application::GetInstance().StartListening(); });
    touch_button_.OnPressUp(
        []() { Application::GetInstance().StopListening(); });

    // KEY2 短按不绑定任何功能，只响应长按。
    key2_button_.OnLongPress([this]() { OnKey2LongPress(); });
  }

  // KEY2 长按 3 秒 → 进入 WiFi 配网。
  //
  // ResetWifiConfiguration() 的行为（见 boards/common/wifi_board.cc）：
  //   1. 在 NVS 命名空间 "wifi" 写入 force_ap = 1；
  //   2. 调用 GetDisplay()->ShowNotification(ENTERING_WIFI_CONFIG_MODE)；
  //   3. 延时 1000 ms 后 esp_restart()；
  //   4. 重启后 StartNetwork() 读到 force_ap = 1，清零标志并调用
  //      EnterWifiConfigMode()，开启热点 + 配置网页。
  //
  // 注意：该路径会重启设备，但不清除已保存的 SSID 列表（不清 NVS），
  // 与 SystemReset::ResetNvsFlash() 的整片 erase 完全不同。
  void OnKey2LongPress() {
    ESP_LOGI(kTag, "KEY2 held for %d ms, entering wifi config mode",
             KEY2_CONFIG_MODE_LONG_PRESS_MS);
    ResetWifiConfiguration();
  }
};

DECLARE_BOARD(HaiRongRobotMini);
