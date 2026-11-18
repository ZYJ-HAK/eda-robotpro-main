#ifndef _DOG_BLE_SPEAKER_H_
#define _DOG_BLE_SPEAKER_H_

#include <esp_err.h>

/*
 * BLE 音箱模式：手机通过 BLE 把 PCM 推过来，本模块写进 I2S 播放。
 *
 * 为什么不是真正的蓝牙音箱：ESP32-S3 没有经典蓝牙（BR/EDR），
 * A2DP 依赖 BR/EDR，硬件层不成立。这里退化为自定义 BLE GATT 音频通道，
 * 手机端必须用本项目的发送端页面（Web Bluetooth），不能用任意播放器。
 *
 * 格式约定：24 kHz / 单声道 / s16le，与板级 AUDIO_OUTPUT_SAMPLE_RATE 对齐。
 *
 * 与 xiaozhi 的互斥：进入本模式前必须由模式管理器停掉小智的音频服务，
 * 否则两边同时写 I2S 会爆音。本模块不负责停小智。
 */
class DogBleSpeaker {
 public:
  static DogBleSpeaker& GetInstance();

  /* 首次调用初始化 NimBLE 并开始广播；重复调用只重新广播。 */
  esp_err_t Start();

  /* 停止广播并断开已连接设备。BLE 协议栈保持初始化，便于快速恢复。 */
  void Stop();

  bool IsAdvertising() const { return advertising_; }

  /* 已收到的音频字节数，供状态展示与掉流判断。 */
  unsigned long long BytesReceived() const { return bytes_received_; }

  /* 由 GATT 写回调调用，把一包 PCM 放进环形缓冲。 */
  void PushPcm(const unsigned char* data, unsigned short len);

  /* 由协议栈回调调用。 */
  void OnStackSync();
  void OnLinkDisconnect();

  DogBleSpeaker(const DogBleSpeaker&) = delete;
  DogBleSpeaker& operator=(const DogBleSpeaker&) = delete;

 private:
  DogBleSpeaker() = default;

  void StartPlaybackTask();

  bool stack_ready_ = false;
  bool playback_started_ = false;
  bool advertising_ = false;
  void* ring_buffer_ = nullptr;
  volatile unsigned long long bytes_received_ = 0;
};

/* 手机 BLE 列表里显示的名字。 */
#define DOG_BLE_DEVICE_NAME "DogCtl-BT"

#endif  // _DOG_BLE_SPEAKER_H_
