#include "buddy_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "buddy_i4.h"
#include "buddy_font_zh.h"
#include "buddy_sprite.h"
#include "buddy_text_layout.h"
#include "lvgl.h"

#define UI_W 240
#define UI_H 320
#define COL_BG lv_color_hex(0x080A0C)
#define COL_INK lv_color_hex(0xF7E9D7)
#define COL_DIM lv_color_hex(0x8B8178)
#define COL_LINE lv_color_hex(0x39332F)
#define COL_ORANGE lv_color_hex(0xE17B52)
#define COL_RED lv_color_hex(0xEF4B38)
#define COL_GREEN lv_color_hex(0x64C987)
#define COL_YELLOW lv_color_hex(0xF1C75B)
#define COL_BLUE lv_color_hex(0x72A7D8)

static lv_obj_t *s_screen;
static lv_obj_t *s_canvas;
static LV_ATTRIBUTE_MEM_ALIGN uint8_t s_canvas_buffer[
    LV_DRAW_BUF_SIZE(UI_W, UI_H, LV_COLOR_FORMAT_I4)];
static buddy_ui_snapshot_t s_snapshot;
static bool s_have_snapshot;
static uint32_t s_tick;
static uint64_t s_elapsed_ms;
static int s_scroll;
static buddy_i4_surface_t s_surface;

#define I4_PALETTE_BYTES (16U * sizeof(lv_color32_t))

static const uint32_t s_palette_rgb[] = {0x080A0C, 0xF7E9D7, 0x8B8178, 0x39332F,
    0xE17B52, 0xEF4B38, 0x64C987, 0xF1C75B, 0x72A7D8, 0xFFFFFF,
    0xD97757, 0xA96349, 0xB48EAD, 0x81A1C1, 0xEBCB8B, 0x151719};

static uint8_t color_index(lv_color_t color)
{
    uint32_t rgb = lv_color_to_int(color);
    uint32_t best_distance = UINT32_MAX;
    uint8_t best = 0;
    uint8_t i;
    for (i = 0; i < sizeof(s_palette_rgb) / sizeof(s_palette_rgb[0]); ++i) {
        int dr = (int)((rgb >> 16) & 0xffU) - (int)((s_palette_rgb[i] >> 16) & 0xffU);
        int dg = (int)((rgb >> 8) & 0xffU) - (int)((s_palette_rgb[i] >> 8) & 0xffU);
        int db = (int)(rgb & 0xffU) - (int)(s_palette_rgb[i] & 0xffU);
        uint32_t distance = (uint32_t)(dr * dr + dg * dg + db * db);
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

static void pixel(int x, int y, uint8_t index)
{
    if ((unsigned)x >= UI_W || (unsigned)y >= UI_H) return;
    buddy_i4_set_pixel(s_canvas_buffer + I4_PALETTE_BYTES, UI_W,
                       (uint16_t)x, (uint16_t)y, index);
}

static size_t utf8_decode(const char *text, size_t remaining, uint32_t *codepoint)
{
    const uint8_t *s = (const uint8_t *)text;
    if (remaining == 0U) return 0U;
    if (s[0] < 0x80U) {
        *codepoint = s[0];
        return 1U;
    }
    if (remaining >= 2U && (s[0] & 0xe0U) == 0xc0U && (s[1] & 0xc0U) == 0x80U) {
        *codepoint = ((uint32_t)(s[0] & 0x1fU) << 6) | (uint32_t)(s[1] & 0x3fU);
        return 2U;
    }
    if (remaining >= 3U && (s[0] & 0xf0U) == 0xe0U &&
        (s[1] & 0xc0U) == 0x80U && (s[2] & 0xc0U) == 0x80U) {
        *codepoint = ((uint32_t)(s[0] & 0x0fU) << 12) |
                     ((uint32_t)(s[1] & 0x3fU) << 6) | (uint32_t)(s[2] & 0x3fU);
        return 3U;
    }
    *codepoint = '?';
    return 1U;
}

static void glyph(int x, int y, uint8_t index, const lv_font_t *font, uint32_t codepoint)
{
    lv_font_glyph_dsc_t dsc;
    const uint8_t *bitmap;
    unsigned row;
    unsigned col;
    if (!lv_font_get_glyph_dsc(font, &dsc, codepoint, 0) || dsc.box_w == 0 || dsc.box_h == 0) return;
    dsc.req_raw_bitmap = 1;
    bitmap = dsc.resolved_font->get_glyph_bitmap(&dsc, NULL);
    if (!bitmap || (dsc.format != LV_FONT_GLYPH_FORMAT_A1 &&
                    dsc.format != LV_FONT_GLYPH_FORMAT_A4)) return;
    y += font->line_height - font->base_line - dsc.box_h - dsc.ofs_y;
    x += dsc.ofs_x;
    for (row = 0; row < dsc.box_h; ++row) {
        for (col = 0; col < dsc.box_w; ++col) {
            uint32_t sample = row * dsc.box_w + col;
            bool visible = dsc.format == LV_FONT_GLYPH_FORMAT_A1
                               ? (bitmap[sample >> 3] & (0x80U >> (sample & 7U))) != 0
                               : (((sample & 1U) == 0U ? bitmap[sample >> 1] >> 4
                                                       : bitmap[sample >> 1] & 0x0fU) >= 4U);
            if (visible) pixel(x + col, y + row, index);
        }
    }
}

static void text_limited(lv_layer_t *layer, int x, int y, int width, lv_color_t color,
                         const char *value, bool large, lv_text_align_t align,
                         unsigned max_lines)
{
    const lv_font_t *font = large ? &buddy_font_zh_16 : &buddy_font_zh_14;
    int spacing = large ? 1 : 0;
    int line_step = font->line_height + (large ? 2 : 1);
    uint8_t index = color_index(color);
    const char *cursor = value;
    unsigned lines = 0;
    (void)layer;
    while (*cursor && y < UI_H && lines < max_lines) {
        const char *end = strchr(cursor, '\n');
        size_t length = end ? (size_t)(end - cursor) : strlen(cursor);
        size_t fit = 0;
        size_t offset = 0;
        int measured = 0;
        while (offset < length) {
            lv_font_glyph_dsc_t dsc;
            uint32_t codepoint;
            size_t consumed = utf8_decode(cursor + offset, length - offset, &codepoint);
            int advance = lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)
                              ? dsc.adv_w + spacing : 0;
            if (fit > 0U && measured + advance > width) break;
            measured += advance;
            offset += consumed;
            fit = offset;
        }
        if (measured > 0) measured -= spacing;
        int pen = x;
        if (align == LV_TEXT_ALIGN_CENTER) pen += (width - measured) / 2;
        else if (align == LV_TEXT_ALIGN_RIGHT) pen += width - measured;
        offset = 0;
        while (offset < fit) {
            lv_font_glyph_dsc_t dsc;
            uint32_t codepoint;
            size_t consumed = utf8_decode(cursor + offset, fit - offset, &codepoint);
            glyph(pen, y, index, font, codepoint);
            if (lv_font_get_glyph_dsc(font, &dsc, codepoint, 0)) pen += dsc.adv_w + spacing;
            offset += consumed;
        }
        y += line_step;
        lines++;
        if (fit < length) cursor += fit;
        else cursor = end ? end + 1 : cursor + length;
    }
}

static void text(lv_layer_t *layer, int x, int y, int width, lv_color_t color,
                 const char *value, bool large, lv_text_align_t align)
{
    text_limited(layer, x, y, width, color, value, large, align, UINT32_MAX);
}

static void wrapped_text(lv_layer_t *layer, int x, int y, int width, lv_color_t color,
                         const char *value, unsigned max_lines)
{
    text_limited(layer, x, y, width, color, value, false, LV_TEXT_ALIGN_LEFT, max_lines);
}

static void box(lv_layer_t *layer, int x, int y, int w, int h, lv_color_t fill,
                lv_color_t border, int border_width, int radius)
{
    uint8_t fill_index = color_index(fill);
    uint8_t border_index = color_index(border);
    int px;
    int py;
    (void)layer;
    (void)radius;
    for (py = 0; py < h; ++py) {
        for (px = 0; px < w; ++px) {
            bool edge = px < border_width || py < border_width ||
                        px >= w - border_width || py >= h - border_width;
            pixel(x + px, y + py, edge ? border_index : fill_index);
        }
    }
}

static void rule(lv_layer_t *layer, int x, int y, int w, lv_color_t color)
{
    box(layer, x, y, w, 1, color, color, 0, 0);
}

static uint8_t art_state(buddy_character_t state)
{
    switch (state) {
    case BUDDY_CHARACTER_SLEEP: return 0;
    case BUDDY_CHARACTER_BUSY: return 2;
    case BUDDY_CHARACTER_ATTENTION:
    case BUDDY_CHARACTER_PAIRING:
    case BUDDY_CHARACTER_CONFIRMATION: return 3;
    case BUDDY_CHARACTER_CELEBRATE: return 4;
    case BUDDY_CHARACTER_DIZZY: return 5;
    case BUDDY_CHARACTER_HEART: return 6;
    default: return 1;
    }
}

static void draw_buddy(lv_layer_t *layer, const buddy_ui_snapshot_t *s, bool peek)
{
    buddy_i4_clip_t clip = {.x = 0, .y = BUDDY_UI_STAGE_Y,
                            .w = UI_W, .h = BUDDY_UI_STAGE_H};
    buddy_sprite_bounds_t bounds;
    int x = 88;
    int y = peek ? 72 : 65;
    (void)layer;
    if (buddy_sprite_bounds(s->species, art_state(s->character), s_tick, &bounds)) {
        x = (UI_W - bounds.w) / 2 - bounds.x;
    }
    buddy_sprite_render(&s_surface, &clip, s->species, art_state(s->character),
                        s_tick, x, y);
}

static void draw_circle(int cx, int cy, int r, lv_color_t color)
{
    uint8_t c_idx = color_index(color);
    int dy, dx;
    for (dy = -r; dy <= r; ++dy) {
        for (dx = -r; dx <= r; ++dx) {
            if (dx * dx + dy * dy <= r * r) {
                pixel(cx + dx, cy + dy, c_idx);
            }
        }
    }
}

static void draw_battery_widget(lv_layer_t *layer, int x, int y, uint8_t percent, bool available)
{
    int w = 18;
    int h = 9;
    uint8_t border_idx = color_index(COL_DIM);
    uint8_t bg_idx = color_index(lv_color_hex(0x151719));
    lv_color_t fill_col = percent <= 15U ? COL_RED : (percent <= 35U ? COL_YELLOW : COL_GREEN);
    uint8_t fill_idx = color_index(fill_col);
    int px, py;
    char p_buf[16];

    if (!available) percent = 100U;

    /* 电池外壳与正极凸点 */
    for (py = 0; py < h; ++py) {
        for (px = 0; px < w; ++px) {
            bool edge = (px == 0 || py == 0 || px == w - 1 || py == h - 1);
            pixel(x + px, y + py, edge ? border_idx : bg_idx);
        }
    }
    for (py = 2; py < h - 2; ++py) {
        pixel(x + w, y + py, border_idx);
        pixel(x + w + 1, y + py, border_idx);
    }
    /* 内部电量平滑填充 */
    int max_fill = w - 4;
    int fill_w = ((int)percent * max_fill) / 100;
    if (fill_w > max_fill) fill_w = max_fill;
    for (py = 2; py < h - 2; ++py) {
        for (px = 0; px < fill_w; ++px) {
            pixel(x + 2 + px, y + py, fill_idx);
        }
    }

    if (available) {
        snprintf(p_buf, sizeof(p_buf), "%u%%", (unsigned)percent);
    } else {
        snprintf(p_buf, sizeof(p_buf), "--");
    }
    text(layer, x - 42, y - 2, 38, fill_col, p_buf, false, LV_TEXT_ALIGN_RIGHT);
}

static void draw_card_frame(int x, int y, int w, int h, lv_color_t accent)
{
    box(NULL, x, y, w, h, lv_color_hex(0x151719), COL_LINE, 1, 0);
    box(NULL, x, y, 3, h, accent, accent, 0, 0);
}

static void draw_progress_track(int x, int y, int w, int h, unsigned percent, lv_color_t fill_col)
{
    int px, py;
    int fill_w;
    uint8_t bg_idx = color_index(lv_color_hex(0x242830));
    uint8_t fill_idx = color_index(fill_col);
    uint8_t mark_idx = color_index(lv_color_hex(0x151719));

    if (percent > 100U) percent = 100U;
    fill_w = ((int)percent * w) / 100;

    for (py = 0; py < h; ++py) {
        for (px = 0; px < w; ++px) {
            pixel(x + px, y + py, px < fill_w ? fill_idx : bg_idx);
        }
    }
    /* 刻度分割线 25%、50%、75% */
    for (py = 0; py < h; ++py) {
        pixel(x + w / 4, y + py, mark_idx);
        pixel(x + w / 2, y + py, mark_idx);
        pixel(x + (w * 3) / 4, y + py, mark_idx);
    }
}

static void draw_status_bar(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char center[32];
    uint64_t age_ms = s_elapsed_ms >= s->time_received_ms ? s_elapsed_ms - s->time_received_ms : 0;
    time_t epoch = (time_t)(s->epoch_seconds + s->timezone_offset_seconds + age_ms / 1000U);
    struct tm tm_value;

    /* 左侧：发光指示灯 + 连接状态 */
    draw_circle(14, 13, 3, s->ble_connected ? COL_GREEN : COL_DIM);
    text(layer, 21, 6, 50, s->ble_connected ? COL_GREEN : COL_DIM,
         s->ble_connected ? "已连接" : "未连接", false, LV_TEXT_ALIGN_LEFT);

    /* 中间：居中时钟 */
    if (s->epoch_seconds > 0 && gmtime_r(&epoch, &tm_value) != NULL) {
        snprintf(center, sizeof(center), "%02d:%02d", tm_value.tm_hour, tm_value.tm_min);
    } else {
        snprintf(center, sizeof(center), "%s", s->heartbeat_stale ? "休眠" : "在线");
    }
    text(layer, 75, 6, 90, COL_INK, center, false, LV_TEXT_ALIGN_CENTER);

    /* 右侧：高质感电池组件 */
    draw_battery_widget(layer, 212, 10, s->battery_percent, s->battery_available);

    /* 状态栏底部分隔线 */
    rule(layer, 8, 24, 224, COL_LINE);
}

static void draw_companion(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char caption[176];
    text(layer, 10, 35, 220, COL_ORANGE, buddy_sprite_name(s->species), false,
         LV_TEXT_ALIGN_CENTER);
    draw_buddy(layer, s, false);
    rule(layer, 18, BUDDY_UI_INFO_Y, 204, COL_LINE);
    snprintf(caption, sizeof(caption), "%s", s->message[0] ? s->message :
             (s->ble_connected ? "助手已就绪" : "请启动电脑端桥接程序进行配对"));
    wrapped_text(layer, 18, 174, 204, s->heartbeat_stale ? COL_DIM : COL_INK,
                 caption, 8);
    text(layer, 8, 300, 224, COL_DIM, BUDDY_ACTION_HOME, false, LV_TEXT_ALIGN_CENTER);
}

static void usage_reset_text(char *destination, size_t size, uint64_t resets_at,
                             const buddy_ui_snapshot_t *s)
{
    uint64_t now = s->epoch_seconds > 0
                       ? (uint64_t)s->epoch_seconds +
                             (s_elapsed_ms >= s->time_received_ms
                                  ? (s_elapsed_ms - s->time_received_ms) / 1000U
                                  : 0U)
                       : 0U;
    uint64_t remaining = resets_at > now ? resets_at - now : 0U;

    if (resets_at == 0U || now == 0U) {
        snprintf(destination, size, "重置时间：--");
    } else if (remaining >= 86400U) {
        snprintf(destination, size, "距重置 %llu天%llu小时",
                 (unsigned long long)(remaining / 86400U),
                 (unsigned long long)((remaining % 86400U) / 3600U));
    } else {
        snprintf(destination, size, "距重置 %llu小时%02llu分",
                 (unsigned long long)(remaining / 3600U),
                 (unsigned long long)((remaining % 3600U) / 60U));
    }
}


static void format_token_metric(char *dest, size_t size, uint64_t count)
{
    if (count == 0U) {
        snprintf(dest, size, "0");
    } else if (count < 1000U) {
        snprintf(dest, size, "%u", (unsigned)count);
    } else if (count < 1000000U) {
        unsigned k = (unsigned)(count / 1000U);
        unsigned rem = (unsigned)((count % 1000U) / 100U);
        if (k < 100U && rem > 0U) {
            snprintf(dest, size, "%u.%uk", k, rem);
        } else {
            snprintf(dest, size, "%uk", k);
        }
    } else {
        unsigned m = (unsigned)(count / 1000000U);
        unsigned rem = (unsigned)((count % 1000000U) / 100000U);
        if (m < 1000U && rem > 0U) {
            snprintf(dest, size, "%u.%uM", m, rem);
        } else {
            snprintf(dest, size, "%uM", m);
        }
    }
}

static void draw_home(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char quota_buf[48];
    char reset_buf[48];
    char status_buf[64];
    char token_buf[16];
    char label_buf[64];
    const buddy_codex_usage_t *u = &s->codex_usage;

    /* 顶部标题区 */
    text(layer, 8, 30, 224, COL_ORANGE, "AI PASSPORT", true, LV_TEXT_ALIGN_CENTER);

    /* 运行状态微徽章：待命时直接醒目展示今日 Token 消耗 */
    if (s->running > 0) {
        box(NULL, 60, 49, 120, 16, lv_color_hex(0x19281f), COL_GREEN, 1, 0);
        snprintf(status_buf, sizeof(status_buf), "工作中 · %u 任务", s->running);
        text(layer, 60, 50, 120, COL_GREEN, status_buf, false, LV_TEXT_ALIGN_CENTER);
    } else {
        format_token_metric(token_buf, sizeof(token_buf), s->token_monitor.tokens_today);
        box(NULL, 50, 49, 140, 16, lv_color_hex(0x151f18), COL_GREEN, 1, 0);
        snprintf(status_buf, sizeof(status_buf), "今日 Token: %s", token_buf);
        text(layer, 50, 50, 140, COL_GREEN, status_buf, false, LV_TEXT_ALIGN_CENTER);
    }

    /* 卡片 1: 主力配额 (Hero Quota Card) */
    if (!u->available) {
        draw_card_frame(8, 68, 224, 98, COL_DIM);
        wrapped_text(layer, 18, 98, 204, COL_INK,
                     "等待桌面端配额同步\n请保持桌面端桥接程序运行", 4);
    } else {
        unsigned rem = 100U - u->primary_used_percent;
        lv_color_t accent = rem <= 15U ? COL_RED : (rem <= 35U ? COL_YELLOW : COL_GREEN);

        draw_card_frame(8, 68, 224, 98, accent);

        /* 标题行 */
        text(layer, 18, 75, 110, COL_INK, "主力模型 (Gemini)", false, LV_TEXT_ALIGN_LEFT);
        snprintf(quota_buf, sizeof(quota_buf), "剩余 %u%%", rem);
        text(layer, 120, 74, 102, accent, quota_buf, true, LV_TEXT_ALIGN_RIGHT);

        /* 平滑进度槽 */
        draw_progress_track(18, 98, 204, 10, rem, accent);

        /* 细分割暗线 */
        rule(layer, 18, 118, 204, lv_color_hex(0x282D35));

        /* 底部倒计时与主力模型 Token 用量 */
        usage_reset_text(reset_buf, sizeof(reset_buf), u->primary_resets_at, s);
        text(layer, 18, 126, 124, COL_DIM, reset_buf, false, LV_TEXT_ALIGN_LEFT);

        uint64_t pm_tokens = (s->token_monitor.active_tools_count > 0)
                                 ? s->token_monitor.tools[0].tokens_today
                                 : s->token_monitor.tokens_today;
        format_token_metric(token_buf, sizeof(token_buf), pm_tokens);
        snprintf(label_buf, sizeof(label_buf), "用量 %s", token_buf);
        text(layer, 144, 126, 78, accent, label_buf, false, LV_TEXT_ALIGN_RIGHT);
    }

    /* 卡片 2: 辅助模型与活跃卡片 (Claude/GPT) */
    {
        unsigned sec_rem = u->available ? (100U - u->secondary_used_percent) : 100U;
        draw_card_frame(8, 176, 224, 98, COL_BLUE);

        /* 标题行 */
        text(layer, 18, 183, 110, COL_INK, "Claude/GPT", false, LV_TEXT_ALIGN_LEFT);
        snprintf(quota_buf, sizeof(quota_buf), "剩余 %u%%", sec_rem);
        text(layer, 120, 182, 102, COL_BLUE, quota_buf, true, LV_TEXT_ALIGN_RIGHT);

        /* 平滑进度槽 */
        draw_progress_track(18, 206, 204, 10, sec_rem, COL_BLUE);

        /* 细分割暗线 */
        rule(layer, 18, 226, 204, lv_color_hex(0x282D35));

        /* 今日总 Token 消耗与辅助模型实际消耗状态 */
        format_token_metric(token_buf, sizeof(token_buf), s->token_monitor.tokens_today);
        snprintf(status_buf, sizeof(status_buf), "今日总计 %s", token_buf);
        text(layer, 18, 234, 130, COL_INK, status_buf, false, LV_TEXT_ALIGN_LEFT);

        uint64_t sec_tokens = (s->token_monitor.active_tools_count > 1)
                                  ? s->token_monitor.tools[1].tokens_today
                                  : 0U;
        if (sec_tokens > 0U) {
            char sec_tok_buf[16];
            format_token_metric(sec_tok_buf, sizeof(sec_tok_buf), sec_tokens);
            snprintf(label_buf, sizeof(label_buf), "用量 %s", sec_tok_buf);
            text(layer, 144, 234, 78, COL_BLUE, label_buf, false, LV_TEXT_ALIGN_RIGHT);
        } else {
            text(layer, 158, 234, 64, COL_ORANGE, "未消耗", false, LV_TEXT_ALIGN_RIGHT);
        }
    }

    text(layer, 8, 298, 224, COL_DIM, "按 UP 键切换额度与伴侣", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_limits(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char value[64];
    char reset[48];
    char token_buf[16];
    char label_buf[64];
    const buddy_codex_usage_t *u = &s->codex_usage;

    text(layer, 8, 34, 224, COL_ORANGE, "模型配额中心", true, LV_TEXT_ALIGN_CENTER);

    if (!u->available) {
        wrapped_text(layer, 22, 120, 196, COL_INK,
                     "暂无配额数据\n请启动桌面端 Token Monitor 桥接程序。", 5);
    } else {
        unsigned primary_remaining = 100U - u->primary_used_percent;
        unsigned secondary_remaining = 100U - u->secondary_used_percent;
        lv_color_t acc1 = primary_remaining <= 15U ? COL_RED : (primary_remaining <= 35U ? COL_YELLOW : COL_GREEN);

        /* 主力模型卡片 (Gemini) */
        draw_card_frame(8, 68, 224, 102, acc1);
        text(layer, 18, 77, 110, COL_INK, "短期滚动 (Gemini)", false, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof(value), "剩余 %u%%", primary_remaining);
        text(layer, 120, 76, 102, acc1, value, true, LV_TEXT_ALIGN_RIGHT);
        draw_progress_track(18, 102, 204, 10, primary_remaining, acc1);
        rule(layer, 18, 124, 204, lv_color_hex(0x282D35));
        usage_reset_text(reset, sizeof(reset), u->primary_resets_at, s);
        text(layer, 18, 134, 124, COL_DIM, reset, false, LV_TEXT_ALIGN_LEFT);
        {
            uint64_t pm_tk = (s->token_monitor.active_tools_count > 0)
                                 ? s->token_monitor.tools[0].tokens_today
                                 : s->token_monitor.tokens_today;
            format_token_metric(token_buf, sizeof(token_buf), pm_tk);
            snprintf(label_buf, sizeof(label_buf), "用量 %s", token_buf);
            text(layer, 144, 134, 78, acc1, label_buf, false, LV_TEXT_ALIGN_RIGHT);
        }

        /* 辅助模型卡片 (Claude/GPT) */
        draw_card_frame(8, 180, 224, 102, COL_BLUE);
        text(layer, 18, 189, 110, COL_INK, "Claude/GPT 配额", false, LV_TEXT_ALIGN_LEFT);
        snprintf(value, sizeof(value), "剩余 %u%%", secondary_remaining);
        text(layer, 120, 188, 102, COL_BLUE, value, true, LV_TEXT_ALIGN_RIGHT);
        draw_progress_track(18, 214, 204, 10, secondary_remaining, COL_BLUE);
        rule(layer, 18, 236, 204, lv_color_hex(0x282D35));
        usage_reset_text(reset, sizeof(reset), u->secondary_resets_at, s);
        text(layer, 18, 246, 124, COL_DIM, reset, false, LV_TEXT_ALIGN_LEFT);
        {
            uint64_t sec_tk = (s->token_monitor.active_tools_count > 1)
                                  ? s->token_monitor.tools[1].tokens_today
                                  : 0U;
            if (sec_tk > 0U) {
                format_token_metric(token_buf, sizeof(token_buf), sec_tk);
                snprintf(label_buf, sizeof(label_buf), "用量 %s", token_buf);
            } else {
                snprintf(label_buf, sizeof(label_buf), "未消耗");
            }
            text(layer, 144, 246, 78, COL_BLUE, label_buf, false, LV_TEXT_ALIGN_RIGHT);
        }
    }
    text(layer, 8, 298, 224, COL_DIM, "UP: 工具明细 · DOWN: 滚动", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_tools_breakdown(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    unsigned count = s->token_monitor.active_tools_count;
    unsigned i;
    char token_buf[32];
    char cost_buf[32];

    text(layer, 8, 34, 224, COL_ORANGE, "AI 工具状态明细", true, LV_TEXT_ALIGN_CENTER);

    if (count == 0) {
        wrapped_text(layer, 22, 110, 196, COL_INK,
                     "暂无工具消耗分类。\n在电脑端使用 Claude/Codex 等工具后将在此自动汇总。", 6);
    } else {
        draw_card_frame(8, 66, 224, 218, COL_ORANGE);
        text(layer, 18, 74, 82, COL_DIM, "工具名称", false, LV_TEXT_ALIGN_LEFT);
        text(layer, 102, 74, 64, COL_DIM, "今日用量", false, LV_TEXT_ALIGN_CENTER);
        text(layer, 168, 74, 54, COL_DIM, "配额余量", false, LV_TEXT_ALIGN_RIGHT);
        rule(layer, 14, 94, 212, lv_color_hex(0x282D35));

        for (i = 0; i < count && i < 5; ++i) {
            int y = 104 + (int)i * 32;
            const buddy_tool_usage_entry_t *t = &s->token_monitor.tools[i];
            unsigned rem = 100U - t->used_percent;
            format_token_metric(token_buf, sizeof(token_buf), t->tokens_today);
            snprintf(cost_buf, sizeof(cost_buf), "%u%%", rem);

            text(layer, 18, y, 82, COL_INK, t->name[0] ? t->name : "AI Tool", false, LV_TEXT_ALIGN_LEFT);
            text(layer, 102, y, 64, COL_INK, token_buf, false, LV_TEXT_ALIGN_CENTER);
            text(layer, 168, y, 54, rem < 20U ? COL_RED : COL_GREEN, cost_buf, false, LV_TEXT_ALIGN_RIGHT);
            if (i < count - 1 && i < 4) {
                rule(layer, 18, y + 24, 204, lv_color_hex(0x22262B));
            }
        }
    }
    text(layer, 8, 298, 224, COL_DIM, "UP: 宠物伴侣 · DOWN: 滚动", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_info(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    static const char *const titles[] = {"关于", "按键说明", "用量状态", "设备信息", "蓝牙", "致谢"};
    char body[512];
    char page[16];
    unsigned p = s->info_page < 6 ? s->info_page : 0;
    text(layer, 14, 38, 180, COL_ORANGE, titles[p], true, LV_TEXT_ALIGN_LEFT);
    snprintf(page, sizeof(page), "%u / 6", p + 1);
    text(layer, 174, 43, 52, COL_DIM, page, false, LV_TEXT_ALIGN_RIGHT);
    rule(layer, 14, 66, 212, COL_LINE);
    switch (p) {
    case 0: snprintf(body, sizeof(body), "你的桌面助手。\n\n显示 5 小时与 7 天使用量，\n并在任务完成时提醒你。"); break;
    case 1: snprintf(body, sizeof(body), "上键：切换界面\n下键：翻页或拒绝\n确认键：允许或更改\n长按确认键：打开菜单"); break;
    case 2:
        if (s->codex_usage.available) {
            snprintf(body, sizeof(body),
                     "任务数：%u\n运行中：%u\n\n5 小时剩余：%u%%\n7 天剩余：%u%%",
                     s->total, s->running,
                     100U - s->codex_usage.primary_used_percent,
                     100U - s->codex_usage.secondary_used_percent);
        } else {
            snprintf(body, sizeof(body),
                     "任务数：%u\n运行中：%u\n\n5 小时剩余：--\n7 天剩余：--",
                     s->total, s->running);
        }
        break;
    case 3: snprintf(body, sizeof(body), "名称\n%s\n\n所有者\n%s\n\n屏幕：240 × 320", s->name[0] ? s->name : "Codex 助手", s->owner[0] ? s->owner : "-"); break;
    case 4: snprintf(body, sizeof(body), "%s\n\n%s\n%s\n\n请在电脑上运行\nCodex 桥接程序", s->name[0] ? s->name : "Codex 助手", s->ble_connected ? "已连接" : "正在广播", s->ble_encrypted ? "连接已加密" : "连接未加密"); break;
    default: snprintf(body, sizeof(body), "Codex 使用量助手\n\n适用于 FoloToy AI Passport\nESP32-C3 硬件\n\n基于公开的 Buddy 参考分支"); break;
    }
    wrapped_text(layer, 16, 82 - s_scroll, 208, COL_INK, body, 18);
    text(layer, 8, 300, 224, COL_DIM, BUDDY_ACTION_INFO, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_list(lv_layer_t *layer, const char *title, const char *const *items,
                      unsigned count, unsigned selected, const buddy_ui_snapshot_t *s)
{
    unsigned first = selected > 5 ? selected - 5 : 0;
    unsigned i;
    text(layer, 14, 34, 212, COL_ORANGE, title, true, LV_TEXT_ALIGN_LEFT);
    rule(layer, 14, 62, 212, COL_LINE);
    for (i = first; i < count && i < first + 7; ++i) {
        int y = 76 + (int)(i - first) * 29;
        bool active = i == selected;
        char row[64];
        const char *suffix = "";
        char value[12];
        if (!s->reset_open && i == BUDDY_SETTINGS_BRIGHTNESS) { snprintf(value, sizeof(value), "%u/4", s->brightness_level); suffix = value; }
        else if (!s->reset_open && i == BUDDY_SETTINGS_BLE) suffix = s->ble_enabled ? "开" : "关";
        else if (!s->reset_open && i == BUDDY_SETTINGS_TRANSCRIPT) suffix = s->transcript_enabled ? "开" : "关";
        else if (!s->reset_open && i == BUDDY_SETTINGS_ASCII_PET) suffix = buddy_sprite_name(s->species);
        snprintf(row, sizeof(row), "%s", items[i]);
        if (active) box(layer, 12, y - 7, 216, 24, COL_ORANGE, COL_ORANGE, 0, 3);
        text(layer, 20, y, 142, active ? COL_BG : COL_INK, row, false, LV_TEXT_ALIGN_LEFT);
        text(layer, 158, y, 62, active ? COL_BG : COL_DIM, suffix, false, LV_TEXT_ALIGN_RIGHT);
    }
    text(layer, 8, 300, 224, COL_DIM, BUDDY_ACTION_SETTINGS, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_settings(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    static const char *const settings[] = {"屏幕亮度", "声音", "蓝牙", "无线网络", "指示灯", "任务记录", "时钟旋转", "伙伴形象", "重置", "返回"};
    static const char *const reset[] = {"删除自定义角色", "恢复出厂设置", "解除蓝牙配对", "返回"};
    draw_list(layer, s->reset_open ? "重置" : "设置", s->reset_open ? reset : settings,
              s->reset_open ? BUDDY_RESET_COUNT : BUDDY_SETTINGS_COUNT,
              s->reset_open ? s->reset_selection : s->settings_selection, s);
}

static void panel(lv_layer_t *layer, int y, int h, lv_color_t accent, const char *title,
                  const char *body, const char *footer)
{
    box(layer, 10, y, 220, h, lv_color_hex(0x151719), accent, 2, 6);
    box(layer, 10, y, 220, 27, accent, accent, 0, 5);
    text(layer, 18, y + 8, 204, COL_BG, title, false, LV_TEXT_ALIGN_LEFT);
    wrapped_text(layer, 20, y + 42 - s_scroll, 200, COL_INK, body, 5);
    rule(layer, 20, y + h - 34, 200, COL_LINE);
    text(layer, 18, y + h - 23, 204, COL_DIM, footer, false, LV_TEXT_ALIGN_CENTER);
}

static void draw_overlay(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char body[448];
    int x;
    int y;
    buddy_overlay_kind_t overlay = buddy_overlay_select(s->confirmation_pending,
                                                        s->passkey_visible,
                                                        s->prompt_id[0] != '\0',
                                                        s->menu_open);
    if (overlay != BUDDY_OVERLAY_NONE) {
        for (y = BUDDY_UI_STATUS_H; y < BUDDY_UI_ACTION_Y; ++y) {
            for (x = (y & 1); x < UI_W; x += 2) {
                uint8_t current = buddy_i4_get_pixel(s_surface.pixels, UI_W,
                                                     (uint16_t)x, (uint16_t)y);
                if (current != 0) buddy_i4_set_pixel(s_surface.pixels, UI_W,
                                                          (uint16_t)x, (uint16_t)y,
                                                          15);
            }
        }
    }
    if (overlay == BUDDY_OVERLAY_CONFIRMATION) {
        panel(layer, 62, 196, COL_RED, "确认操作",
              s->confirmation == BUDDY_CONFIRM_FACTORY_RESET ? "确定恢复出厂设置吗？\n\n全部设置和统计数据将被清除。" : "确定解除 Codex 桥接配对吗？\n\n已保存的蓝牙配对信息将被清除。",
              BUDDY_ACTION_CONFIRM);
    } else if (overlay == BUDDY_OVERLAY_PAIRING) {
        snprintf(body, sizeof(body), "请在电脑上输入此配对码\n\n       %06lu", (unsigned long)s->passkey);
        panel(layer, 66, 188, COL_BLUE, "蓝牙配对", body, "请保持此界面开启");
    } else if (overlay == BUDDY_OVERLAY_APPROVAL) {
        snprintf(body, sizeof(body), "%s\n\n%s", s->prompt_tool, s->prompt_hint);
        panel(layer, 154, 158, s->approval_locked ? COL_DIM : COL_RED, "助手请求授权", body,
              s->approval_locked ? (s->permission_delivery == BUDDY_PERMISSION_DELIVERY_FAILED ? "发送失败" : "正在发送……") : BUDDY_ACTION_APPROVAL);
    } else if (overlay == BUDDY_OVERLAY_MENU) {
        static const char *const menu[] = {"设置", "关闭屏幕", "帮助", "关于", "演示", "关闭菜单"};
        unsigned i;
        box(layer, 38, 48, 164, 224, lv_color_hex(0x151719), COL_INK, 2, 5);
        text(layer, 52, 61, 136, COL_ORANGE, "菜单", true, LV_TEXT_ALIGN_CENTER);
        rule(layer, 52, 88, 136, COL_LINE);
        for (i = 0; i < BUDDY_MENU_COUNT; ++i) {
            int y = 103 + (int)i * 25;
            bool active = i == (unsigned)s->menu_selection;
            if (active) box(layer, 48, y - 7, 144, 21, COL_ORANGE, COL_ORANGE, 0, 2);
            text(layer, 56, y, 128, active ? COL_BG : COL_INK, menu[i], false, LV_TEXT_ALIGN_CENTER);
        }
    } else if (s->character == BUDDY_CHARACTER_CELEBRATE) {
        panel(layer, 194, 86, COL_GREEN, "任务已完成",
              "助手已完成当前任务。", "请在电脑上查看结果");
    }
}

static void redraw(void)
{
    lv_layer_t *layer = NULL;
    if (!s_canvas || !s_have_snapshot) return;
    memset(s_canvas_buffer + I4_PALETTE_BYTES, 0, sizeof(s_canvas_buffer) - I4_PALETTE_BYTES);
    draw_status_bar(layer, &s_snapshot);
    switch (s_snapshot.page) {
    case BUDDY_PAGE_LIMITS: draw_limits(layer, &s_snapshot); break;
    case BUDDY_PAGE_TOOLS: draw_tools_breakdown(layer, &s_snapshot); break;
    case BUDDY_PAGE_PET: draw_companion(layer, &s_snapshot); break;
    case BUDDY_PAGE_INFO: draw_info(layer, &s_snapshot); break;
    case BUDDY_PAGE_SETTINGS: draw_settings(layer, &s_snapshot); break;
    default: draw_home(layer, &s_snapshot); break;
    }
    draw_overlay(layer, &s_snapshot);
    lv_obj_invalidate(s_canvas);
}

void buddy_ui_init(void)
{
    unsigned i;
    if (s_screen) return;
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, UI_W, UI_H);
    lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_set_style_border_width(s_screen, 0, 0);
    lv_obj_set_style_bg_color(s_screen, COL_BG, 0);
    s_canvas = lv_canvas_create(s_screen);
    lv_canvas_set_buffer(s_canvas, s_canvas_buffer, UI_W, UI_H, LV_COLOR_FORMAT_I4);
    lv_obj_set_pos(s_canvas, 0, 0);
    buddy_i4_surface_init(&s_surface, s_canvas_buffer + I4_PALETTE_BYTES,
                          UI_W, UI_H, UI_W / 2);
    for (i = 0; i < sizeof(s_palette_rgb) / sizeof(s_palette_rgb[0]); ++i)
        lv_canvas_set_palette(s_canvas, i, lv_color_to_32(lv_color_hex(s_palette_rgb[i]), LV_OPA_COVER));
    lv_screen_load(s_screen);
}

void buddy_ui_render(const buddy_ui_snapshot_t *snapshot)
{
    if (!snapshot) return;
    buddy_ui_init();
    s_snapshot = *snapshot;
    s_have_snapshot = true;
    s_scroll = 0;
    redraw();
}

void buddy_ui_show_passkey(uint32_t passkey)
{
    buddy_ui_snapshot_t snapshot = {.passkey_visible = true, .passkey = passkey};
    buddy_ui_render(&snapshot);
}

void buddy_ui_tick(uint64_t elapsed_ms)
{
    uint32_t tick = (uint32_t)(elapsed_ms / 200U);
    s_elapsed_ms = elapsed_ms;
    if (tick != s_tick) {
        s_tick = tick;
        redraw();
    }
}

void buddy_ui_scroll(int delta)
{
    s_scroll += delta;
    if (s_scroll < 0) s_scroll = 0;
    if (s_scroll > 160) s_scroll = 160;
    redraw();
}

bool buddy_ui_write_screenshot(FILE *stream)
{
    uint8_t row[UI_W * 2U];
    unsigned x;
    unsigned y;

    if (stream == NULL || !s_have_snapshot) {
        return false;
    }
    if (fprintf(stream, "FAP_SCREENSHOT_V1 %u %u RGB565LE %u\n",
                UI_W, UI_H, UI_W * UI_H * 2U) < 0) {
        return false;
    }
    for (y = 0; y < UI_H; ++y) {
        for (x = 0; x < UI_W; ++x) {
            uint8_t palette_index = buddy_i4_get_pixel(
                s_canvas_buffer + I4_PALETTE_BYTES, UI_W,
                (uint16_t)x, (uint16_t)y);
            uint32_t rgb = s_palette_rgb[palette_index & 0x0fU];
            uint16_t rgb565 = (uint16_t)(((rgb >> 8) & 0xf800U) |
                                         ((rgb >> 5) & 0x07e0U) |
                                         ((rgb >> 3) & 0x001fU));

            row[x * 2U] = (uint8_t)(rgb565 & 0xffU);
            row[x * 2U + 1U] = (uint8_t)(rgb565 >> 8);
        }
        if (fwrite(row, 1, sizeof(row), stream) != sizeof(row)) {
            return false;
        }
    }
    return fflush(stream) == 0;
}
