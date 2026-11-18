#include "dog_ble_speaker.h"

#include <cstring>
#include <vector>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/task.h>

#include <host/ble_hs.h>
#include <host/util/util.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_port_freertos.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>

#include "audio/audio_codec.h"
#include "board.h"
#include "dog_ble_config.h"

#define TAG "DogBle"

namespace {

/* 私有 16 位 UUID，避开标准服务；手机端用同样的数值过滤即可。 */
const ble_uuid16_t kServiceUuid = BLE_UUID16_INIT(DOG_BLE_SERVICE_UUID);
const ble_uuid16_t kPcmUuid = BLE_UUID16_INIT(DOG_BLE_PCM_UUID);

struct ble_gatt_chr_def kCharacteristics[2];
struct ble_gatt_svc_def kServices[2];

uint8_t own_addr_type = 0;
uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;

int OnGattAccess(uint16_t handle, uint16_t attr_handle,
                 struct ble_gatt_access_ctxt* ctxt, void* arg) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  const uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0 || len > DOG_BLE_MAX_PACKET) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  unsigned char buffer[DOG_BLE_MAX_PACKET];
  if (os_mbuf_copydata(ctxt->om, 0, len, buffer) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  DogBleSpeaker::GetInstance().PushPcm(buffer, len);
  return 0;
}

void BuildGattTable() {
  std::memset(kCharacteristics, 0, sizeof(kCharacteristics));
  kCharacteristics[0].uuid = &kPcmUuid.u;
  kCharacteristics[0].access_cb = OnGattAccess;
  kCharacteristics[0].flags =
      BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP;

  std::memset(kServices, 0, sizeof(kServices));
  kServices[0].type = BLE_GATT_SVC_TYPE_PRIMARY;
  kServices[0].uuid = &kServiceUuid.u;
  kServices[0].characteristics = kCharacteristics;
}

int OnGapEvent(struct ble_gap_event* event, void* arg);

void StartAdvertising() {
  if (DogBleSpeaker::GetInstance().IsAdvertising()) return;

  struct ble_hs_adv_fields fields = {};
  const char* name = DOG_BLE_DEVICE_NAME;
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids16 = const_cast<ble_uuid16_t*>(&kServiceUuid);
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;
  fields.name = reinterpret_cast<const uint8_t*>(name);
  fields.name_len = static_cast<uint8_t>(std::strlen(name));
  fields.name_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "设置广播内容失败 rc=%d", rc);
    return;
  }

  struct ble_gap_adv_params params = {};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  // 30 ms 广播间隔：手机侧发现更快，代价是功耗略高。
  params.itvl_min = 0x30;
  params.itvl_max = 0x30;

  rc = ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &params,
                         OnGapEvent, nullptr);
  if (rc != 0) {
    ESP_LOGE(TAG, "开始广播失败 rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "已开始广播: %s", DOG_BLE_DEVICE_NAME);
}

int OnGapEvent(struct ble_gap_event* event, void* arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        conn_handle = event->connect.conn_handle;
        ESP_LOGI(TAG, "手机已连接");
        // 请求更大的 MTU，减小每包开销。
        ble_att_set_preferred_mtu(DOG_BLE_PREFERRED_MTU);
      } else {
        StartAdvertising();
      }
      return 0;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "手机已断开");
      conn_handle = BLE_HS_CONN_HANDLE_NONE;
      DogBleSpeaker::GetInstance().OnLinkDisconnect();
      return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
      StartAdvertising();
      return 0;

    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "MTU 协商为 %d", event->mtu.value);
      return 0;

    default:
      return 0;
  }
}

void OnStackSync() {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &own_addr_type);
  DogBleSpeaker::GetInstance().OnStackSync();
}

void OnStackReset(int reason) {
  ESP_LOGW(TAG, "协议栈复位 reason=%d", reason);
}

void HostTask(void* param) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

}  // namespace

DogBleSpeaker& DogBleSpeaker::GetInstance() {
  static DogBleSpeaker instance;
  return instance;
}

esp_err_t DogBleSpeaker::Start() {
  if (ring_buffer_ == nullptr) {
    ring_buffer_ = xRingbufferCreate(DOG_BLE_RING_BUFFER_BYTES,
                                     RINGBUF_TYPE_BYTEBUF);
    if (ring_buffer_ == nullptr) {
      ESP_LOGE(TAG, "环形缓冲创建失败");
      return ESP_ERR_NO_MEM;
    }
  }

  if (!stack_ready_) {
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "NimBLE 初始化失败: %s", esp_err_to_name(err));
      return err;
    }
    ble_hs_cfg.sync_cb = OnStackSync;
    ble_hs_cfg.reset_cb = OnStackReset;

    BuildGattTable();
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(kServices);
    ble_gatts_add_svcs(kServices);
    ble_svc_gap_device_name_set(DOG_BLE_DEVICE_NAME);

    nimble_port_freertos_init(HostTask);
    stack_ready_ = true;
  }

  StartPlaybackTask();
  return ESP_OK;
}

void DogBleSpeaker::Stop() {
  if (!stack_ready_) return;
  if (advertising_) {
    ble_gap_adv_stop();
    advertising_ = false;
  }
  if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }
}

void DogBleSpeaker::OnStackSync() {
  ESP_LOGI(TAG, "协议栈就绪");
  advertising_ = true;
  StartAdvertising();
}

void DogBleSpeaker::OnLinkDisconnect() {
  advertising_ = true;
  StartAdvertising();
}

void DogBleSpeaker::PushPcm(const unsigned char* data, unsigned short len) {
  if (ring_buffer_ == nullptr || data == nullptr || len == 0) return;

  // 丢旧保新：缓冲满时丢弃最旧数据，宁可断音也不让延迟累积。
  size_t free_bytes = xRingbufferGetCurFreeSize(ring_buffer_);
  if (free_bytes < len) {
    size_t dropped = 0;
    void* stale = xRingbufferReceiveUpTo(ring_buffer_, &dropped,
                                         pdMS_TO_TICKS(0), len);
    if (stale != nullptr) vRingbufferReturnItem(ring_buffer_, stale);
  }
  xRingbufferSend(ring_buffer_, data, len, 0);
  bytes_received_ += len;
}

void DogBleSpeaker::StartPlaybackTask() {
  if (playback_started_) return;
  playback_started_ = true;
  xTaskCreate(
      [](void* arg) {
        auto* self = static_cast<DogBleSpeaker*>(arg);
        AudioCodec* codec = Board::GetInstance().GetAudioCodec();
        if (codec == nullptr) {
          ESP_LOGE(TAG, "无音频输出设备，BLE 播放线程退出");
          vTaskDelete(nullptr);
          return;
        }
        std::vector<int16_t> pcm;
        pcm.reserve(DOG_BLE_CHUNK_SAMPLES);
        for (;;) {
          size_t item_size = 0;
          void* item = xRingbufferReceiveUpTo(ring_buffer_, &item_size,
                                              pdMS_TO_TICKS(20),
                                              DOG_BLE_CHUNK_BYTES);
          if (item == nullptr) {
            continue;  // 没数据就静音，不写 I2S
          }
          const size_t samples = item_size / 2;
          pcm.resize(samples);
          std::memcpy(pcm.data(), item, samples * sizeof(int16_t));
          vRingbufferReturnItem(ring_buffer_, item);
          codec->OutputData(pcm);
        }
      },
      "ble_playback", DOG_BLE_PLAYBACK_STACK, this, 5, nullptr);
}
