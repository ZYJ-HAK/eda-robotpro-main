#ifndef _DOG_WEB_CONFIG_H_
#define _DOG_WEB_CONFIG_H_

/*
 * 内置网页控制 —— 全部可调参数集中在此文件。
 *
 * 与项目「引脚唯一真源」同一条纪律：任何 .cc 里都不许出现这些数值的字面量。
 * 标注「待实测」的项尚未在真机上验证过，改动前请先读设计文档第 8 节。
 */

// ==================== 热点 ====================

// 热点名称前缀。实际 SSID 会拼上 MAC 末两字节，例如 DogCtl-3F2A，
// 这样六台设备同场不会重名，也不需要为每台单独编译。
#define DOG_WEB_AP_SSID_PREFIX "DogCtl"

// HTTP 端口。独占 80，不复用小智配网页，避免用户被两个页面搞混。
#define DOG_WEB_HTTP_PORT 80

// 同时允许关联的客户端数。
#define DOG_WEB_AP_MAX_CONNECTION 4

// 1 = 开机直接进入热点模式（满足「通电即热点」的要求）。
//     此模式下 STA 关闭，云端语音不可用。
// 0 = 走原小智流程（连 WiFi、语音可用），需要时再进热点。
#ifndef DOG_WEB_AP_AT_BOOT
#define DOG_WEB_AP_AT_BOOT 1
#endif

// ==================== 访问控制 ====================

// 热点评测为开放网络（无密码），令牌是唯一的访问闸门。
#define DOG_WEB_TOKEN_NVS_NS "dog_web"
#define DOG_WEB_TOKEN_NVS_KEY "token"
#define DOG_WEB_TOKEN_DEFAULT "1234"
#define DOG_WEB_TOKEN_MAX_LEN 16

// ==================== 运动限幅 ====================

// 单次动作的步数上限。超过 4 步，四足在水磨石地面上会明显打滑。
#define DOG_WEB_STEPS_MIN 1
#define DOG_WEB_STEPS_MAX 4

// 步态周期（毫秒）。数值越大越慢。上下限取自官方 MCP 工具的参数范围。
#define DOG_WEB_PERIOD_MIN 500
#define DOG_WEB_PERIOD_MAX 2000

// 相邻两条运动指令的最小间隔，防止网页连点把动作队列灌满。
#define DOG_WEB_MOTION_COOLDOWN_MS 600

// ==================== httpd ====================

#define DOG_WEB_HTTP_MAX_URI_HANDLERS 8

// 待实测：8192 是按「单页 HTML 约 9 KB + JSON 1 KB」估的，
// 若出现 httpd_resp_send 返回 ESP_ERR_HTTPD_RESP_HDR 之外的截断，
// 优先把它提到 10240 再测。
#define DOG_WEB_HTTP_STACK_SIZE 8192

#define DOG_WEB_HTTP_RECV_TIMEOUT_S 5
#define DOG_WEB_HTTP_SEND_TIMEOUT_S 5

#endif  // _DOG_WEB_CONFIG_H_
