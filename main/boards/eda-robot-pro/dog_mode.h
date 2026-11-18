#ifndef _DOG_MODE_H_
#define _DOG_MODE_H_

#include <esp_err.h>

#include <functional>

#include "button.h"

/*
 * 三种运行模式与切换。
 *
 * kAi  —— 小智原生的语音对话模式，云端已连接，按住说话与唤醒词可用。
 * kWeb —— 自带热点 + 网页遥控，手机连上即可操控，不需要装任何东西。
 * kBle —— BLE 音频接收（替代无法实现的 A2DP 蓝牙音箱），需手机端发送页面。
 *
 * 三种模式共用同一组舵机与 I2S，任意时刻只允许一种模式持有音频输出。
 * 切换为热切换：先让上一模式释放资源，再启动下一模式，不重启。
 *
 * 按键 KEY2（GPIO 见板级 config.h）：
 *   短按 -> 顺序切换 kAi -> kWeb -> kBle -> kAi
 *   长按 -> 仅 kAi 模式下进入配网；其余模式忽略
 */

/* 长按判定阈值。待实测：800 ms 与触摸键的默认手感接近，可按需调整。 */
#define DOG_KEY2_LONG_PRESS_MS 800

enum class DogMode : int {
  kAi = 0,
  kWeb = 1,
  kBle = 2,
};

class DogModeManager {
 public:
  static DogModeManager& GetInstance();

  /*
   * 进入配网模式的回调。由板级注入，避免在这里对 Board 做向下转型
   * （项目禁用 dynamic_cast，且 Board 基类没有该接口）。
   */
  void SetProvisioningHandler(std::function<void()> handler);

  /* 读取上次模式、注册 KEY2、应用该模式。只需调用一次。 */
  void Init();

  DogMode GetMode() const { return mode_; }
  const char* GetModeName() const;

  /* KEY2 短按：切到下一模式。 */
  void SwitchNext();

  /* KEY2 长按：kAi 模式下降级为配网，其他模式忽略。 */
  void EnterProvisioning();

  DogModeManager(const DogModeManager&) = delete;
  DogModeManager& operator=(const DogModeManager&) = delete;

 private:
  DogModeManager();
  ~DogModeManager();

  void Apply(DogMode mode);
  void Save(DogMode mode) const;
  DogMode Load() const;
  void Announce(const char* text) const;

  DogMode mode_ = DogMode::kAi;
  bool initialized_ = false;
  bool external_active_ = false;  // 小智是否已让出音频通道
  std::function<void()> provisioning_handler_;
  Button key2_;
};

#endif  // _DOG_MODE_H_
