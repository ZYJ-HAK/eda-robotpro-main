#include "dog_mode.h"

#include <esp_log.h>

#include "application.h"
#include "board.h"
#include "config.h"
#include "display.h"
#include "dog_ble_speaker.h"
#include "dog_web_server.h"
#include "settings.h"

#define TAG "DogMode"

namespace {
constexpr const char* kNvsNamespace = "dog_mode";
constexpr const char* kNvsKey = "mode";
constexpr int kModeCount = 3;
}  // namespace

DogModeManager& DogModeManager::GetInstance() {
  static DogModeManager instance;
  return instance;
}

DogModeManager::DogModeManager()
    : key2_(DOG_KEY2_GPIO, false, DOG_KEY2_LONG_PRESS_MS) {}

DogModeManager::~DogModeManager() = default;

void DogModeManager::SetProvisioningHandler(std::function<void()> handler) {
  provisioning_handler_ = std::move(handler);
}

void DogModeManager::Init() {
  if (initialized_) return;
  initialized_ = true;

  key2_.OnClick([]() { DogModeManager::GetInstance().SwitchNext(); });
  key2_.OnLongPress([]() { DogModeManager::GetInstance().EnterProvisioning(); });

  mode_ = Load();
  ESP_LOGI(TAG, "初始模式: %s", GetModeName());
  Apply(mode_);
}

const char* DogModeManager::GetModeName() const {
  switch (mode_) {
    case DogMode::kAi:
      return "AI 聊天模式";
    case DogMode::kWeb:
      return "WiFi 遥控模式";
    case DogMode::kBle:
      return "BLE 音箱模式";
  }
  return "未知模式";
}

void DogModeManager::SwitchNext() {
  const int next = (static_cast<int>(mode_) + 1) % kModeCount;
  ESP_LOGI(TAG, "KEY2 短按，切换到模式 %d", next);
  Apply(static_cast<DogMode>(next));
}

void DogModeManager::EnterProvisioning() {
  if (mode_ != DogMode::kAi) {
    ESP_LOGW(TAG, "当前非 AI 模式，长按配网已忽略");
    Announce("长按配网仅在 AI 模式可用");
    return;
  }
  if (!provisioning_handler_) {
    ESP_LOGW(TAG, "未注入配网回调");
    return;
  }
  Announce("进入配网模式");
  provisioning_handler_();
}

void DogModeManager::Apply(DogMode mode) {
  // 1. 先让所有外接模式释放资源，保证同一时刻只有一个持有 I2S / 射频。
  DogBleSpeaker::GetInstance().Stop();
  DogWebServer::GetInstance().Stop();
  if (external_active_) {
    Application::GetInstance().ExitExternalMode();
    external_active_ = false;
  }

  mode_ = mode;

  // 2. 再启动目标模式。
  switch (mode) {
    case DogMode::kAi:
      break;

    case DogMode::kWeb:
      Application::GetInstance().EnterExternalMode();
      external_active_ = true;
      DogWebServer::GetInstance().Start();
      break;

    case DogMode::kBle:
      Application::GetInstance().EnterExternalMode();
      external_active_ = true;
      DogBleSpeaker::GetInstance().Start();
      break;
  }

  Save(mode);
  Announce(GetModeName());
  ESP_LOGI(TAG, "已切换到 %s", GetModeName());
}

void DogModeManager::Announce(const char* text) const {
  auto* display = Board::GetInstance().GetDisplay();
  if (display != nullptr) {
    display->ShowNotification(text, 2000);
  }
}

void DogModeManager::Save(DogMode mode) const {
  Settings settings(kNvsNamespace, true);
  settings.SetInt(kNvsKey, static_cast<int>(mode));
}

DogMode DogModeManager::Load() const {
  Settings settings(kNvsNamespace, false);
  int value = settings.GetInt(kNvsKey, static_cast<int>(DogMode::kAi));
  if (value < 0 || value >= kModeCount) value = static_cast<int>(DogMode::kAi);
  return static_cast<DogMode>(value);
}
