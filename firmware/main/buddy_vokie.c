// firmware/main/buddy_vokie.c
// Vokie AI 语音助手模块实现
#include "buddy_vokie.h"

#include "bsp_audio.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "os/os_mbuf.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "buddy_vokie";

#define AUDIO_SAMPLES 320
#define ADPCM_BYTES 166
#define MAX_CONTROL 182
#define EMPTY_FINAL_SEQUENCE 0xffffffffUL

const ble_uuid128_t g_vokie_service_uuid = BLE_UUID128_INIT(BUDDY_VOKIE_SERVICE_UUID_BYTES);
const ble_uuid128_t g_vokie_control_uuid = BLE_UUID128_INIT(BUDDY_VOKIE_CONTROL_UUID_BYTES);
const ble_uuid128_t g_vokie_audio_uuid   = BLE_UUID128_INIT(BUDDY_VOKIE_AUDIO_UUID_BYTES);
const ble_uuid128_t g_vokie_info_uuid    = BLE_UUID128_INIT(BUDDY_VOKIE_INFO_UUID_BYTES);

uint16_t g_vokie_control_handle = 0;
uint16_t g_vokie_audio_handle   = 0;
uint16_t g_vokie_info_handle    = 0;

static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_control_subscribed = false;
static volatile bool s_audio_subscribed = false;
static volatile bool s_host_ready = false;
static volatile bool s_stop_requested = false;
static volatile uint32_t s_session = 0;
static volatile uint32_t s_control_seq = 0;
static volatile uint32_t s_button_seq = 0;
static TaskHandle_t s_audio_task = NULL;
static bool s_initialized = false;

static buddy_vokie_state_t s_vokie_state = {
    .status = BUDDY_VOKIE_STATE_OFFLINE,
    .message = "等待 Vokie 连接",
    .recording = false,
    .connected = false,
    .host_ready = false,
    .active_tick = 0,
};

static void set_vokie_status(buddy_vokie_status_t status, const char *msg)
{
    s_vokie_state.status = status;
    s_vokie_state.active_tick = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (msg != NULL) {
        strncpy(s_vokie_state.message, msg, sizeof(s_vokie_state.message) - 1);
        s_vokie_state.message[sizeof(s_vokie_state.message) - 1] = '\0';
    }
}

const buddy_vokie_state_t *buddy_vokie_get_state(void)
{
    s_vokie_state.connected = (s_conn != BLE_HS_CONN_HANDLE_NONE);
    s_vokie_state.host_ready = s_host_ready;
    return &s_vokie_state;
}

static int notify_bytes(uint16_t handle, const uint8_t *data, size_t len)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || handle == 0) {
        return BLE_HS_ENOTCONN;
    }
    if (len > ble_att_mtu(s_conn) - 3) {
        return BLE_HS_EMSGSIZE;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return BLE_HS_ENOMEM;
    }
    return ble_gatts_notify_custom(s_conn, handle, om);
}

static int notify_control(const char *json)
{
    size_t len = strlen(json);
    if (len == 0 || len > MAX_CONTROL) {
        return BLE_HS_EMSGSIZE;
    }
    return notify_bytes(g_vokie_control_handle, (const uint8_t *)json, len);
}

static void send_button_event(const char *button, const char *event, uint32_t duration_ms)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_host_ready) {
        return;
    }
    char json[MAX_CONTROL + 1];
    snprintf(json, sizeof(json),
             "{\"v\":1,\"type\":\"button_event\",\"button\":\"%s\",\"event\":\"%s\",\"durationMs\":%lu,\"seq\":%lu}",
             button, event, (unsigned long)duration_ms, (unsigned long)++s_button_seq);
    (void)notify_control(json);
}

static void send_device_error(uint32_t session, const char *message)
{
    char json[MAX_CONTROL + 1];
    snprintf(json, sizeof(json),
             "{\"v\":1,\"type\":\"device_error\",\"sessionId\":%lu,\"message\":\"%s\"}",
             (unsigned long)session, message);
    (void)notify_control(json);
}

/* IMA ADPCM 编码查找表 */
static const int s_ima_index_table[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static const int s_ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
    60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499,
    2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
    18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static size_t encode_adpcm(const int16_t *pcm, uint8_t out[ADPCM_BYTES])
{
    int predictor = pcm[0];
    int index = 0;
    out[0] = (uint8_t)predictor;
    out[1] = (uint8_t)(predictor >> 8);
    out[2] = 0;
    out[3] = AUDIO_SAMPLES & 0xff;
    out[4] = AUDIO_SAMPLES >> 8;
    out[5] = 0;
    int nibble = 0;
    for (int i = 1; i < AUDIO_SAMPLES; ++i) {
        int diff = pcm[i] - predictor;
        int sign = diff < 0 ? 8 : 0;
        if (diff < 0) diff = -diff;
        int step = s_ima_step_table[index];
        int delta = 0;
        int vpdiff = step >> 3;
        if (diff >= step) {
            delta |= 4;
            diff -= step;
            vpdiff += step;
        }
        if (diff >= (step >> 1)) {
            delta |= 2;
            diff -= (step >> 1);
            vpdiff += (step >> 1);
        }
        if (diff >= (step >> 2)) {
            delta |= 1;
            vpdiff += (step >> 2);
        }
        int code = delta | sign;
        predictor += sign ? -vpdiff : vpdiff;
        if (predictor > 32767) predictor = 32767;
        if (predictor < -32768) predictor = -32768;
        index += s_ima_index_table[code];
        if (index < 0) index = 0;
        if (index > 88) index = 88;
        if (nibble == 0) {
            out[6 + (i - 1) / 2] = (uint8_t)code;
            nibble = 1;
        } else {
            out[6 + (i - 1) / 2] |= (uint8_t)(code << 4);
            nibble = 0;
        }
    }
    return ADPCM_BYTES;
}

static int notify_audio(uint32_t session, uint32_t sequence, const uint8_t *payload, size_t len)
{
    const size_t mtu_payload = ble_att_mtu(s_conn) - 3;
    if (mtu_payload <= 16) return BLE_HS_EMSGSIZE;
    const size_t chunk = mtu_payload - 16;
    uint8_t packet[16 + ADPCM_BYTES];
    uint8_t count = (uint8_t)((len + chunk - 1) / chunk);
    if (count == 0 || count > 64) return BLE_HS_EMSGSIZE;
    for (uint8_t part = 0; part < count; ++part) {
        size_t offset = part * chunk;
        size_t amount = len - offset < chunk ? len - offset : chunk;
        packet[0] = 0x41;
        packet[1] = 0x50;
        packet[2] = 1;
        packet[3] = 0;
        memcpy(packet + 4, &session, 4);
        memcpy(packet + 8, &sequence, 4);
        packet[12] = part;
        packet[13] = count;
        packet[14] = amount & 0xff;
        packet[15] = amount >> 8;
        memcpy(packet + 16, payload + offset, amount);
        int rc = notify_bytes(g_vokie_audio_handle, packet, 16 + amount);
        if (rc != 0) return rc;
    }
    return 0;
}

static void vokie_audio_task(void *arg)
{
    (void)arg;
    int16_t pcm[AUDIO_SAMPLES];
    uint8_t encoded[ADPCM_BYTES];
    uint32_t sequence = 0;

    for (;;) {
        if (!s_vokie_state.recording) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        if (bsp_audio_read(pcm, sizeof(pcm)) != ESP_OK) {
            s_stop_requested = true;
            send_device_error(s_session, "audio_read");
            s_vokie_state.recording = false;
            set_vokie_status(BUDDY_VOKIE_STATE_ERROR, "麦克风读取异常");
            continue;
        }

        if (!s_audio_subscribed || s_conn == BLE_HS_CONN_HANDLE_NONE || ble_att_mtu(s_conn) < 185) {
            s_stop_requested = true;
            send_device_error(s_session, "mtu");
            s_vokie_state.recording = false;
            set_vokie_status(BUDDY_VOKIE_STATE_ERROR, "MTU 或连接不足");
            continue;
        }

        size_t encoded_len = encode_adpcm(pcm, encoded);
        if (notify_audio(s_session, sequence++, encoded, encoded_len) != 0) {
            s_stop_requested = true;
            send_device_error(s_session, "transport");
            s_vokie_state.recording = false;
            set_vokie_status(BUDDY_VOKIE_STATE_ERROR, "蓝牙传输中断");
        }

        if (s_stop_requested) {
            char json[MAX_CONTROL + 1];
            snprintf(json, sizeof(json),
                     "{\"v\":1,\"type\":\"ptt_up\",\"sessionId\":%lu,\"seq\":%lu,\"finalSequence\":%lu}",
                     (unsigned long)s_session, (unsigned long)++s_control_seq,
                     sequence ? (unsigned long)(sequence - 1) : EMPTY_FINAL_SEQUENCE);
            (void)notify_control(json);
            s_vokie_state.recording = false;
            s_stop_requested = false;
            sequence = 0;
            set_vokie_status(BUDDY_VOKIE_STATE_THINKING, "正在转写处理...");
        }
    }
}

/* 用户定制的 5 类按键控制函数 */

void buddy_vokie_key_up(void)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !s_host_ready) {
        set_vokie_status(s_vokie_state.status, "主机未就绪");
        return;
    }

    if (!s_vokie_state.recording) {
        /* 空闲时单击 UP：开始语音捕捉 */
        s_session = esp_random();
        if (s_session == 0) s_session = 1;
        s_control_seq = 0;
        s_vokie_state.recording = true;
        set_vokie_status(BUDDY_VOKIE_STATE_LISTENING, "正在聆听语音...");

        char json[MAX_CONTROL + 1];
        snprintf(json, sizeof(json),
                 "{\"v\":1,\"type\":\"ptt_down\",\"sessionId\":%lu,\"seq\":0}",
                 (unsigned long)s_session);
        if (notify_control(json) != 0) {
            s_vokie_state.recording = false;
            set_vokie_status(BUDDY_VOKIE_STATE_ERROR, "BLE 发送失败");
        }
    } else {
        /* 采集中单击 UP：完成当前帧并停止捕获提交 */
        s_stop_requested = true;
        set_vokie_status(BUDDY_VOKIE_STATE_THINKING, "停止捕获，提交中...");
    }
}

void buddy_vokie_key_down(void)
{
    /* 单击 DOWN：发送 进入 (send_enter) */
    send_button_event("down", "click", 0);
    set_vokie_status(s_vokie_state.status, "发送回车");
}

void buddy_vokie_key_ok_click(uint32_t duration_ms)
{
    /* 单击 OK：若请求进行中则取消请求；若空闲则删除一个字符 */
    if (s_vokie_state.recording || s_vokie_state.status == BUDDY_VOKIE_STATE_THINKING) {
        if (s_vokie_state.recording) {
            s_stop_requested = true;
        }
        send_button_event("ok", "click", duration_ms);
        set_vokie_status(BUDDY_VOKIE_STATE_READY, "已取消当前请求");
    } else {
        send_button_event("ok", "click", duration_ms);
        set_vokie_status(BUDDY_VOKIE_STATE_READY, "删除字符");
    }
}

void buddy_vokie_key_ok_double(void)
{
    /* 双击 OK：若请求进行中则取消请求；若空闲则清空输入 (清晰输入) */
    if (s_vokie_state.recording || s_vokie_state.status == BUDDY_VOKIE_STATE_THINKING) {
        if (s_vokie_state.recording) {
            s_stop_requested = true;
        }
        send_button_event("ok", "click", 200);
        set_vokie_status(BUDDY_VOKIE_STATE_READY, "已取消当前请求");
    } else {
        /* 主机协议规定 event: long 对应 clear_input 清空输入 */
        send_button_event("ok", "long", 650);
        set_vokie_status(BUDDY_VOKIE_STATE_READY, "清空输入");
    }
}

void buddy_vokie_stop_and_reset(void)
{
    /* OK 长按退出 Vokie 助手，安全关闭录音任务 */
    if (s_vokie_state.recording) {
        s_stop_requested = true;
        s_vokie_state.recording = false;
    }
    if (s_conn != BLE_HS_CONN_HANDLE_NONE && s_host_ready) {
        set_vokie_status(BUDDY_VOKIE_STATE_READY, "就绪待命");
    } else {
        set_vokie_status(BUDDY_VOKIE_STATE_OFFLINE, "等待 Vokie 连接");
    }
}

/* BLE GATT 访问回调 */
int buddy_vokie_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    int kind = (int)(intptr_t)arg;

    /* Device Info 特征值读取 */
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR && kind == 3) {
        const char info[] =
            "{\"v\":1,\"type\":\"hello\",\"device\":\"ai-passport\",\"fw\":\"0.1.0\","
            "\"codec\":\"ima-adpcm\",\"sampleRate\":16000,\"channels\":1,\"frameMs\":20}";
        return os_mbuf_append(ctxt->om, info, strlen(info));
    }

    /* Control 特征值有响应写入 */
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR || kind != 1) {
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    char value[MAX_CONTROL + 1] = {0};
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(ctxt->om, value, MAX_CONTROL, &len) != 0 || len == 0 || len > MAX_CONTROL) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    cJSON *root = cJSON_ParseWithLength(value, len);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return BLE_ATT_ERR_UNLIKELY;
    }

    const cJSON *v = cJSON_GetObjectItem(root, "v");
    if (!cJSON_IsNumber(v) || v->valueint != 1) {
        cJSON_Delete(root);
        return BLE_ATT_ERR_UNLIKELY;
    }

    const cJSON *type = cJSON_GetObjectItem(root, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "host_ready") == 0) {
        s_host_ready = true;
        set_vokie_status(BUDDY_VOKIE_STATE_READY, "Vokie 已就绪");
    } else if (cJSON_IsString(type) && strcmp(type->valuestring, "host_state") == 0) {
        const cJSON *state = cJSON_GetObjectItem(root, "state");
        if (cJSON_IsString(state)) {
            if (strcmp(state->valuestring, "ready") == 0) {
                s_host_ready = true;
                set_vokie_status(BUDDY_VOKIE_STATE_READY, "就绪待命");
            } else if (strcmp(state->valuestring, "recording") == 0) {
                set_vokie_status(BUDDY_VOKIE_STATE_LISTENING, "正在聆听...");
            } else if (strcmp(state->valuestring, "processing") == 0) {
                set_vokie_status(BUDDY_VOKIE_STATE_THINKING, "正在思考转写...");
            } else if (strcmp(state->valuestring, "success") == 0) {
                set_vokie_status(BUDDY_VOKIE_STATE_SENT, "发送成功");
            } else if (strcmp(state->valuestring, "error") == 0) {
                set_vokie_status(BUDDY_VOKIE_STATE_ERROR, "识别出错，请重试");
            }
        }
    }

    cJSON_Delete(root);
    return 0;
}

void buddy_vokie_on_connect(uint16_t conn_handle)
{
    s_conn = conn_handle;
    s_host_ready = false;
    s_control_subscribed = false;
    s_audio_subscribed = false;
    set_vokie_status(BUDDY_VOKIE_STATE_OFFLINE, "主机已连接，握手中...");
}

void buddy_vokie_on_disconnect(void)
{
    s_vokie_state.recording = false;
    s_stop_requested = false;
    s_host_ready = false;
    s_control_subscribed = false;
    s_audio_subscribed = false;
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    set_vokie_status(BUDDY_VOKIE_STATE_OFFLINE, "等待 Vokie 连接");
}

void buddy_vokie_on_subscribe(uint16_t attr_handle, uint8_t cur_notify)
{
    if (attr_handle == g_vokie_control_handle) {
        s_control_subscribed = (cur_notify != 0);
    }
    if (attr_handle == g_vokie_audio_handle) {
        s_audio_subscribed = (cur_notify != 0);
    }

    static bool s_hello_sent = false;
    if (!s_control_subscribed || !s_audio_subscribed) {
        s_hello_sent = false;
    } else if (!s_hello_sent) {
        const char hello[] =
            "{\"v\":1,\"type\":\"hello\",\"device\":\"ai-passport\",\"fw\":\"0.1.0\","
            "\"codec\":\"ima-adpcm\",\"sampleRate\":16000,\"channels\":1,\"frameMs\":20}";
        s_hello_sent = (notify_control(hello) == 0);
        if (s_hello_sent) {
            set_vokie_status(BUDDY_VOKIE_STATE_OFFLINE, "已发送握手包");
        }
    }
}

esp_err_t buddy_vokie_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    esp_err_t err = bsp_audio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "初始化音频硬件失败: %d", err);
        return err;
    }

    err = bsp_audio_set_format(16000, 16, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "配置音频采样格式失败: %d", err);
        return err;
    }

    if (xTaskCreate(vokie_audio_task, "vokie_audio", 4096, NULL, 5, &s_audio_task) != pdPASS) {
        ESP_LOGE(TAG, "创建 Vokie 音频任务失败");
        return ESP_ERR_NO_MEM;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Vokie 模块初始化成功");
    return ESP_OK;
}
