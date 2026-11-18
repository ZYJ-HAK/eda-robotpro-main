#ifndef _DOG_WEB_API_H_
#define _DOG_WEB_API_H_

#include <esp_err.h>

/*
 * 网页控制层与动作层之间唯一的接口。
 *
 * 实现放在 eda_dog_controller.cc，直接复用其动作队列与动作任务，
 * 绝不新建第二条 PWM 控制路径 —— 两个任务同时写同一组 ledc 通道
 * 会让舵机瞬间收到矛盾占空比，轻则抖动重则堵转烧毁。
 *
 * 分层位置：网页层与 MCP 工具层同级，都在步态层之上。
 */

enum class DogMotion : int {
  kWalk = 1,
  kTurn = 2,
  kSit = 3,
  kStand = 4,
  kStretch = 5,
  kShake = 6,
  kHome = 11,
};

/*
 * 入队一个动作。
 *
 * @param steps     步数，调用方已限幅，此处再夹一次
 * @param period    周期毫秒，同上
 * @param direction +1 前进/左转，-1 后退/右转
 *
 * @return ESP_OK                 已入队
 *         ESP_ERR_INVALID_ARG    动作类型非法
 *         ESP_ERR_INVALID_STATE  当前仍有动作在执行，拒绝排队
 */
esp_err_t DogWebSubmitMotion(DogMotion motion, int steps, int period,
                             int direction);

/* 立即停止：清空队列、结束动作任务、归中位。任何时候都允许调用。 */
esp_err_t DogWebEmergencyStop();

/* 是否有动作正在执行。 */
bool DogWebIsBusy();

#endif  // _DOG_WEB_API_H_
