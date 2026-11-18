#include "dog_web_server.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>

#include "board.h"
#include "display.h"
#include "settings.h"

#include "dog_web_api.h"
#include "dog_web_config.h"
#include "dog_web_page.h"

#define TAG "DogWeb"

namespace {

constexpr size_t kQueryBufLen = 128;

/* 从 URL 查询串里取一个字段。取不到返回 false。 */
bool GetQueryValue(httpd_req_t* req, const char* key, char* out, size_t out_len) {
  size_t len = httpd_req_get_url_query_len(req);
  if (len == 0 || len + 1 > kQueryBufLen) return false;

  char query[kQueryBufLen];
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
    return false;
  }
  return httpd_query_key_value(query, key, out, out_len) == ESP_OK;
}

int GetQueryInt(httpd_req_t* req, const char* key, int default_value) {
  char buf[16];
  if (!GetQueryValue(req, key, buf, sizeof(buf))) return default_value;
  return std::atoi(buf);
}

esp_err_t SendJson(httpd_req_t* req, const char* status, const char* json) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "application/json; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, json);
}

esp_err_t SendError(httpd_req_t* req, const char* status, const char* msg) {
  char json[256];
  std::snprintf(json, sizeof(json), "{\"ok\":false,\"msg\":\"%s\"}", msg);
  return SendJson(req, status, json);
}

std::string GetApIp() {
  esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (netif == nullptr) return "192.168.4.1";

  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(netif, &info) != ESP_OK) return "192.168.4.1";

  char buf[16];
  std::snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
  return std::string(buf);
}

bool ParseMotion(const char* act, DogMotion* motion, int* direction, int* steps,
                 int* period) {
  if (std::strcmp(act, "fwd") == 0) {
    *motion = DogMotion::kWalk;
    *direction = 1;
  } else if (std::strcmp(act, "back") == 0) {
    *motion = DogMotion::kWalk;
    *direction = -1;
  } else if (std::strcmp(act, "left") == 0) {
    *motion = DogMotion::kTurn;
    *direction = 1;
  } else if (std::strcmp(act, "right") == 0) {
    *motion = DogMotion::kTurn;
    *direction = -1;
  } else if (std::strcmp(act, "sit") == 0) {
    *motion = DogMotion::kSit;
    *steps = 1;
  } else if (std::strcmp(act, "stand") == 0) {
    *motion = DogMotion::kStand;
    *steps = 1;
  } else if (std::strcmp(act, "stretch") == 0) {
    *motion = DogMotion::kStretch;
    *steps = 1;
  } else if (std::strcmp(act, "shake") == 0) {
    *motion = DogMotion::kShake;
    *steps = 1;
  } else if (std::strcmp(act, "home") == 0) {
    *motion = DogMotion::kHome;
    *steps = 1;
  } else {
    return false;
  }
  return true;
}

int ClampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

DogWebServer& DogWebServer::GetInstance() {
  static DogWebServer instance;
  return instance;
}

esp_err_t DogWebServer::Start() {
  if (server_ != nullptr) return ESP_OK;

  LoadToken();

  esp_err_t err = StartAccessPoint();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "热点启动失败: %s", esp_err_to_name(err));
    return err;
  }

  err = StartHttpd();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP 服务启动失败: %s", esp_err_to_name(err));
    return err;
  }

  url_ = "http://" + GetApIp();
  ESP_LOGI(TAG, "热点: %s", ssid_.c_str());
  ESP_LOGI(TAG, "控制页: %s", url_.c_str());
  ESP_LOGI(TAG, "访问令牌: %s", token_.c_str());

  auto display = Board::GetInstance().GetDisplay();
  if (display != nullptr) {
    std::string hint = "热点 " + ssid_ + "\n" + url_ + "\n令牌 " + token_;
    display->ShowNotification(hint.c_str(), 30000);
  }

  return ESP_OK;
}

void DogWebServer::Stop() {
  if (server_ != nullptr) {
    httpd_stop(server_);
    server_ = nullptr;
  }
  // 只在确实由本模块拉起过热点时才关射频，
  // 否则会把 AI 模式的 STA 连接一起断掉。
  if (ap_up_) {
    esp_wifi_stop();
    ap_up_ = false;
    ESP_LOGI(TAG, "热点已关闭");
  }
}

void DogWebServer::LoadToken() {
  Settings settings(DOG_WEB_TOKEN_NVS_NS, true);
  token_ = settings.GetString(DOG_WEB_TOKEN_NVS_KEY, "");
  if (token_.empty()) {
    token_ = DOG_WEB_TOKEN_DEFAULT;
    settings.SetString(DOG_WEB_TOKEN_NVS_KEY, token_);
  }
}

esp_err_t DogWebServer::StartAccessPoint() {
  // 三个 init 都是「一次成功、二次返回 INVALID_STATE」，按非致命处理。
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  if (esp_netif_create_default_wifi_ap() == nullptr) {
    ESP_LOGW(TAG, "AP netif 可能已存在，继续");
  }

  wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init_cfg);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;

  uint8_t mac[6] = {};
  err = esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "读取 MAC 失败，SSID 退化为固定后缀");
  }

  char ssid[33] = {};
  std::snprintf(ssid, sizeof(ssid), "%s-%02X%02X", DOG_WEB_AP_SSID_PREFIX, mac[4],
                mac[5]);
  ssid_ = ssid;

  wifi_config_t ap_cfg = {};
  std::memcpy(ap_cfg.ap.ssid, ssid_.data(), ssid_.size());
  ap_cfg.ap.ssid_len = static_cast<uint8_t>(ssid_.size());
  ap_cfg.ap.channel = 1;
  ap_cfg.ap.max_connection = DOG_WEB_AP_MAX_CONNECTION;
  ap_cfg.ap.authmode = WIFI_AUTH_OPEN;
  ap_cfg.ap.pmf_cfg.required = false;

  err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (err != ESP_OK) return err;

  err = esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
  if (err != ESP_OK) return err;

  err = esp_wifi_start();
  if (err != ESP_OK) return err;
  ap_up_ = true;

  // 关闭省电，保证网页响应及时。
  esp_wifi_set_ps(WIFI_PS_NONE);
  return ESP_OK;
}

esp_err_t DogWebServer::StartHttpd() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = DOG_WEB_HTTP_PORT;
  config.max_uri_handlers = DOG_WEB_HTTP_MAX_URI_HANDLERS;
  config.stack_size = DOG_WEB_HTTP_STACK_SIZE;
  config.recv_wait_timeout = DOG_WEB_HTTP_RECV_TIMEOUT_S;
  config.send_wait_timeout = DOG_WEB_HTTP_SEND_TIMEOUT_S;
  config.lru_purge_enable = true;
  config.uri_match_fn = httpd_uri_match_wildcard;

  esp_err_t err = httpd_start(&server_, &config);
  if (err != ESP_OK) {
    server_ = nullptr;
    return err;
  }

  httpd_uri_t index = {};
  index.uri = "/";
  index.method = HTTP_GET;
  index.handler = [](httpd_req_t* req) -> esp_err_t {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, kDogWebPage, HTTPD_RESP_USE_STRLEN);
  };
  httpd_register_uri_handler(server_, &index);

  httpd_uri_t status = {};
  status.uri = "/api/status";
  status.method = HTTP_GET;
  status.user_ctx = this;
  status.handler = [](httpd_req_t* req) -> esp_err_t {
    auto* self = static_cast<DogWebServer*>(req->user_ctx);
    if (!self->Authorized(req)) {
      return SendError(req, "401 Unauthorized", "token");
    }

    int clients = 0;
    wifi_sta_list_t sta_list = {};
    if (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) {
      clients = sta_list.num;
    }

    char json[320];
    std::snprintf(
        json, sizeof(json),
        "{\"ok\":true,\"busy\":%s,\"ssid\":\"%s\",\"ip\":\"%s\","
        "\"clients\":%d,\"uptime_s\":%lld,\"heap\":%u,"
        "\"limit\":{\"steps_max\":%d,\"period_min\":%d,\"period_max\":%d}}",
        DogWebIsBusy() ? "true" : "false", self->ssid_.c_str(),
        GetApIp().c_str(), clients,
        static_cast<long long>(esp_timer_get_time() / 1000000),
        static_cast<unsigned>(esp_get_free_heap_size()), DOG_WEB_STEPS_MAX,
        DOG_WEB_PERIOD_MIN, DOG_WEB_PERIOD_MAX);
    return SendJson(req, "200 OK", json);
  };
  httpd_register_uri_handler(server_, &status);

  httpd_uri_t motion = {};
  motion.uri = "/api/motion";
  motion.method = HTTP_POST;
  motion.user_ctx = this;
  motion.handler = [](httpd_req_t* req) -> esp_err_t {
    auto* self = static_cast<DogWebServer*>(req->user_ctx);
    if (!self->Authorized(req)) {
      return SendError(req, "401 Unauthorized", "token");
    }

    char act[16] = {};
    if (!GetQueryValue(req, "act", act, sizeof(act))) {
      return SendError(req, "400 Bad Request", "missing act");
    }

    DogMotion type = DogMotion::kHome;
    int direction = 1;
    int steps = GetQueryInt(req, "steps", 2);
    int period = GetQueryInt(req, "period", 1000);

    if (!ParseMotion(act, &type, &direction, &steps, &period)) {
      return SendError(req, "400 Bad Request", "unknown act");
    }

    steps = ClampInt(steps, DOG_WEB_STEPS_MIN, DOG_WEB_STEPS_MAX);
    period = ClampInt(period, DOG_WEB_PERIOD_MIN, DOG_WEB_PERIOD_MAX);

    // 冷却只作用于连续运动，姿态指令不受限。
    const bool is_motion =
        (type == DogMotion::kWalk || type == DogMotion::kTurn);
    if (is_motion) {
      const int64_t now = esp_timer_get_time();
      if (self->last_motion_us_ != 0 &&
          now - self->last_motion_us_ <
              static_cast<int64_t>(DOG_WEB_MOTION_COOLDOWN_MS) * 1000) {
        return SendError(req, "429 Too Many Requests", "cooldown");
      }
      self->last_motion_us_ = now;
    }

    esp_err_t err = DogWebSubmitMotion(type, steps, period, direction);
    if (err == ESP_ERR_INVALID_STATE) {
      return SendError(req, "409 Conflict", "busy");
    }
    if (err != ESP_OK) {
      return SendError(req, "400 Bad Request", "rejected");
    }

    char json[96];
    std::snprintf(json, sizeof(json), "{\"ok\":true,\"act\":\"%s\"}", act);
    return SendJson(req, "200 OK", json);
  };
  httpd_register_uri_handler(server_, &motion);

  httpd_uri_t stop = {};
  stop.uri = "/api/stop";
  stop.method = HTTP_POST;
  stop.user_ctx = this;
  stop.handler = [](httpd_req_t* req) -> esp_err_t {
    auto* self = static_cast<DogWebServer*>(req->user_ctx);
    if (!self->Authorized(req)) {
      return SendError(req, "401 Unauthorized", "token");
    }
    DogWebEmergencyStop();
    return SendJson(req, "200 OK", "{\"ok\":true}");
  };
  httpd_register_uri_handler(server_, &stop);

  return ESP_OK;
}

bool DogWebServer::Authorized(httpd_req_t* req) {
  size_t len = httpd_req_get_hdr_value_len(req, "X-Dog-Token");
  if (len == 0 || len >= DOG_WEB_TOKEN_MAX_LEN) return false;

  char buf[DOG_WEB_TOKEN_MAX_LEN] = {};
  if (httpd_req_get_hdr_value_str(req, "X-Dog-Token", buf, sizeof(buf)) !=
      ESP_OK) {
    return false;
  }
  return token_ == buf;
}
