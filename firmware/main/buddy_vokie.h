// firmware/main/buddy_vokie.h
// Vokie AI 语音助手模块接口定义
#pragma once

#include "esp_err.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include <stdbool.h>
#include <stdint.h>

#define BUDDY_VOKIE_SERVICE_UUID_BYTES \
    0x00, 0x45, 0x49, 0x4b, 0x4f, 0x56, 0x1a, 0x9d, 0x6f, 0x4b, 0x7b, 0x6a, 0x01, 0x00, 0x0e, 0x7f

#define BUDDY_VOKIE_CONTROL_UUID_BYTES \
    0x00, 0x45, 0x49, 0x4b, 0x4f, 0x56, 0x1a, 0x9d, 0x6f, 0x4b, 0x7b, 0x6a, 0x02, 0x00, 0x0e, 0x7f

#define BUDDY_VOKIE_AUDIO_UUID_BYTES \
    0x00, 0x45, 0x49, 0x4b, 0x4f, 0x56, 0x1a, 0x9d, 0x6f, 0x4b, 0x7b, 0x6a, 0x03, 0x00, 0x0e, 0x7f

#define BUDDY_VOKIE_INFO_UUID_BYTES \
    0x00, 0x45, 0x49, 0x4b, 0x4f, 0x56, 0x1a, 0x9d, 0x6f, 0x4b, 0x7b, 0x6a, 0x04, 0x00, 0x0e, 0x7f

typedef enum {
    BUDDY_VOKIE_STATE_OFFLINE = 0, /* 未连接主机 */
    BUDDY_VOKIE_STATE_READY,       /* 已连接主机并完成握手，待命中 */
    BUDDY_VOKIE_STATE_LISTENING,   /* 录音采集语音中 (Listening) */
    BUDDY_VOKIE_STATE_THINKING,    /* 主机转写处理中 (Processing/Thinking) */
    BUDDY_VOKIE_STATE_SENT,        /* 识别发送成功 (Sent) */
    BUDDY_VOKIE_STATE_ERROR,       /* 错误重试 (Error) */
} buddy_vokie_status_t;

typedef struct {
    buddy_vokie_status_t status;
    char message[64];
    bool recording;
    bool connected;
    bool host_ready;
    uint32_t active_tick;
} buddy_vokie_state_t;

/* 初始化 Vokie 模块（注册音频与后台任务） */
esp_err_t buddy_vokie_init(void);

/* 获取当前 Vokie 状态快照供 UI 渲染使用 */
const buddy_vokie_state_t *buddy_vokie_get_state(void);

/* 用户按键定制接口：
 * UP 单击：空闲时开始捕捉，采集中停止捕获并提交；
 * DOWN 单击：发送进入回车；
 * OK 单击：请求中取消请求，空闲时删除字符；
 * OK 双击：请求中取消请求，空闲时清空输入；
 * OK 长按：停止录音并退出返回 Launcher。
 */
void buddy_vokie_key_up(void);
void buddy_vokie_key_down(void);
void buddy_vokie_key_ok_click(uint32_t duration_ms);
void buddy_vokie_key_ok_double(void);
void buddy_vokie_stop_and_reset(void);

/* BLE 与 GATT 接入接口 */
int buddy_vokie_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);
void buddy_vokie_on_connect(uint16_t conn_handle);
void buddy_vokie_on_disconnect(void);
void buddy_vokie_on_subscribe(uint16_t attr_handle, uint8_t cur_notify);

/* GATT 服务 UUID 与 Handle 引用 */
extern const ble_uuid128_t g_vokie_service_uuid;
extern const ble_uuid128_t g_vokie_control_uuid;
extern const ble_uuid128_t g_vokie_audio_uuid;
extern const ble_uuid128_t g_vokie_info_uuid;
extern uint16_t g_vokie_control_handle;
extern uint16_t g_vokie_audio_handle;
extern uint16_t g_vokie_info_handle;
