#ifndef _DOG_BLE_CONFIG_H_
#define _DOG_BLE_CONFIG_H_

/*
 * BLE 音箱模式参数。
 *
 * 采样率不在此处定义 —— 它由板级 AUDIO_OUTPUT_SAMPLE_RATE 决定（当前 24000），
 * 手机端必须按同一个值发送，否则变速播放。改这里之前先看 config.h。
 */

/* 自定义 16 位 UUID（私有段）。手机端用同样的数值过滤服务与特征。 */
#define DOG_BLE_SERVICE_UUID 0xFF00
#define DOG_BLE_PCM_UUID 0xFF01

/* 单包上限。MTU 517 时有效载荷约 509 字节，留一点余量。 */
#define DOG_BLE_MAX_PACKET 512

/* 期望 MTU。越大每包开销越小，吞吐越高。 */
#define DOG_BLE_PREFERRED_MTU 517

/*
 * 环形缓冲。24 kHz × 2 字节 = 48 KB/s，
 * 8192 字节约 170 ms，足够吸收 BLE 抖动。
 * 待实测：若频繁丢包说明手机侧发送节奏不稳，可加大到 16384。
 */
#define DOG_BLE_RING_BUFFER_BYTES 8192

/* 每次写 I2S 的块大小：240 样点 = 10 ms，480 字节。 */
#define DOG_BLE_CHUNK_SAMPLES 240
#define DOG_BLE_CHUNK_BYTES (DOG_BLE_CHUNK_SAMPLES * 2)

/*
 * 播放线程栈。待实测：只用了一个 int16 向量与 memcpy，4096 应足够；
 * 若出现栈溢出回溯，先提到 6144。
 */
#define DOG_BLE_PLAYBACK_STACK 4096

#endif  // _DOG_BLE_CONFIG_H_
