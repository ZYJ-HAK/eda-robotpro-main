#ifndef _DOG_WEB_SERVER_H_
#define _DOG_WEB_SERVER_H_

#include <esp_err.h>
#include <esp_http_server.h>

#include <string>

/*
 * 内置网页控制服务。
 *
 * 职责只有三件：起热点、挂 HTTP 服务、把网页请求翻译成动作层调用。
 * 不直接碰 ledc，也不持有舵机对象 —— 所有动作都经 dog_web_api.h 转交。
 *
 * 与原有「小智配网页」的关系：两者互不干扰。本服务独占 80 端口，
 * 不复用 WifiConfigurationAp 的 httpd（其句柄为私有，无对外接口）。
 */
class DogWebServer {
 public:
  static DogWebServer& GetInstance();

  /* 起热点 + 起 HTTP 服务。可重复调用，第二次直接返回 ESP_OK。 */
  esp_err_t Start();

  const char* GetSsid() const { return ssid_.c_str(); }
  const char* GetUrl() const { return url_.c_str(); }
  bool IsRunning() const { return server_ != nullptr; }

  DogWebServer(const DogWebServer&) = delete;
  DogWebServer& operator=(const DogWebServer&) = delete;

 private:
  DogWebServer() = default;

  esp_err_t StartAccessPoint();
  esp_err_t StartHttpd();
  void LoadToken();
  bool Authorized(httpd_req_t* req);

  httpd_handle_t server_ = nullptr;
  std::string ssid_;
  std::string url_;
  std::string token_;
  int64_t last_motion_us_ = 0;
};

#endif  // _DOG_WEB_SERVER_H_
