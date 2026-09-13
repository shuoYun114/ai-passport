#include "buddy_ui.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "buddy_i4.h"
#include "buddy_font_zh.h"
#include "buddy_sprite.h"
#include "buddy_text_layout.h"
#include "buddy_games.h"
#include "buddy_game_life.h"
#include "buddy_vokie.h"
#include "qrcode.h"
#include "lvgl.h"

#define UI_W 240
#define UI_H 320

/* 晨曦暖白主题调色板 (Warm Light Palette): 暖白底 + 浓墨黑字 + 晨曦暖黄 */
static const uint32_t s_palette_warm_light[16] = {
    0xFBF9F5, /* 0: 象牙暖白底色 (清屏背景) */
    0x1A1A1A, /* 1: 浓墨正文字色 (深黑) */
    0x635E55, /* 2: 雅致暖灰副标 (对比度提升) */
    0xDCD5C8, /* 3: 极细通透分割线 (1px 微边框) */
    0xDD9110, /* 4: 晨曦暖黄/琥珀金 (高饱和高光焦点、胶囊标) */
    0xEF4B38, /* 5: 柔朱红 (错误/报警) */
    0x3E9146, /* 6: 翠竹绿 (正常/成功) */
    0xF2B93B, /* 7: 浅金暖黄 (次级高亮) */
    0x3078B4, /* 8: 靛青蓝 (信息/连接) */
    0xFFFFFF, /* 9: 纯白高光面 */
    0xF2ECE0, /* 10: 浅米暖白卡片底板 */
    0xE2D8C7, /* 11: 通透卡片边框 */
    0xD89520, /* 12: 琥珀金边 */
    0x2A2826, /* 13: 强调大标 */
    0x969085, /* 14: 中性弱灰 */
    0xEDE6DA, /* 15: 半透明点阵遮罩色 */
};

/* 黑曜极简主题调色板 (Warm Dark Palette): 极简暖黑 + 象牙白字 + 晨曦暖黄 */
static const uint32_t s_palette_warm_dark[16] = {
    0x121315, /* 0: 极简黑曜暖黑底色 (清屏背景) */
    0xF7F3E9, /* 1: 温润象牙暖白正文 */
    0x8C8880, /* 2: 微光暖灰副标 */
    0x282A2E, /* 3: 通透深邃微对比线 */
    0xF4B942, /* 4: 晨曦暖黄高光焦点 */
    0xEF4B38, /* 5: 柔朱红 */
    0x64C987, /* 6: 翡翠绿 */
    0xF1C75B, /* 7: 温暖黄 */
    0x72A7D8, /* 8: 冰湖蓝 */
    0xFFFFFF, /* 9: 极亮纯白 */
    0x191B1F, /* 10: 深色通透卡片底板 */
    0x30333A, /* 11: 精致卡片边框 */
    0xECAE36, /* 12: 琥珀暖金边 */
    0xEDEAE1, /* 13: 极亮象牙字 */
    0x5E626B, /* 14: 辅助灰 */
    0x222428, /* 15: 半透明点阵遮罩色 */
};

static const uint32_t *s_active_palette = s_palette_warm_light;
static uint8_t s_active_theme = BUDDY_THEME_WARM_LIGHT;

#define COL_BG          lv_color_hex(s_active_palette[0])
#define COL_INK         lv_color_hex(s_active_palette[1])
#define COL_DIM         lv_color_hex(s_active_palette[2])
#define COL_LINE        lv_color_hex(s_active_palette[3])
#define COL_ORANGE      lv_color_hex(s_active_palette[4])
#define COL_RED         lv_color_hex(s_active_palette[5])
#define COL_GREEN       lv_color_hex(s_active_palette[6])
#define COL_YELLOW      lv_color_hex(s_active_palette[7])
#define COL_BLUE        lv_color_hex(s_active_palette[8])
#define COL_WHITE       lv_color_hex(s_active_palette[9])
#define COL_CARD_BG     lv_color_hex(s_active_palette[10])
#define COL_CARD_BORDER lv_color_hex(s_active_palette[11])

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

static uint8_t color_index(lv_color_t color)
{
    uint32_t rgb = lv_color_to_int(color);
    uint32_t best_distance = UINT32_MAX;
    uint8_t best = 0;
    uint8_t i;
    for (i = 0; i < 16; ++i) {
        int dr = (int)((rgb >> 16) & 0xffU) - (int)((s_active_palette[i] >> 16) & 0xffU);
        int dg = (int)((rgb >> 8) & 0xffU) - (int)((s_active_palette[i] >> 8) & 0xffU);
        int db = (int)(rgb & 0xffU) - (int)(s_active_palette[i] & 0xffU);
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
    uint8_t bg_idx = color_index(COL_CARD_BG);
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
    box(NULL, x, y, w, h, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
    box(NULL, x, y, 3, h, accent, accent, 0, 0);
}

static void draw_progress_track(int x, int y, int w, int h, unsigned percent, lv_color_t fill_col)
{
    int px, py;
    int fill_w;
    uint8_t bg_idx = color_index(COL_CARD_BORDER);
    uint8_t fill_idx = color_index(fill_col);
    uint8_t mark_idx = color_index(COL_BG);

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

static void draw_launcher(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char sub_buf[64];
    char token_buf[16];
    int i;
    int total_items = BUDDY_LAUNCHER_ITEM_COUNT;
    int visible_items = 5;
    int sel = (int)s->launcher_selection;
    int top_idx = 0;

    if (sel >= visible_items) {
        top_idx = sel - visible_items + 1;
    }
    if (top_idx > total_items - visible_items) {
        top_idx = total_items - visible_items;
    }
    if (top_idx < 0) top_idx = 0;

    /* 顶部大标题 */
    text(layer, 8, 27, 224, COL_ORANGE, "AI PASSPORT", true, LV_TEXT_ALIGN_CENTER);
    rule(layer, 20, 47, 200, COL_LINE);

    /* 滚动视窗卡片 (同屏显示 5 个，自适应滑动，垂直布局舒展通透) */
    for (int row = 0; row < visible_items; ++row) {
        i = top_idx + row;
        if (i >= total_items) break;
        int y = 52 + row * 47;
        bool selected = (i == sel);
        lv_color_t accent = COL_ORANGE;
        const char *title = "";
        const char *subtitle = "";

        if (i == BUDDY_LAUNCHER_ITEM_AI_MONITOR) {
            accent = COL_ORANGE;
            title = "1. AI 监控看板";
            format_token_metric(token_buf, sizeof(token_buf), s->token_monitor.tokens_today);
            unsigned rem = s->codex_usage.available ? (100U - s->codex_usage.primary_used_percent) : 0U;
            snprintf(sub_buf, sizeof(sub_buf), "今日 %s · 额度 %u%%", token_buf, rem);
            subtitle = sub_buf;
        } else if (i == BUDDY_LAUNCHER_ITEM_PROFILE) {
            accent = COL_YELLOW;
            title = "2. 个人智能主页";
            subtitle = "电子工牌 · 伴侣名片";
        } else if (i == BUDDY_LAUNCHER_ITEM_VOKIE) {
            accent = COL_BLUE;
            title = "3. Vokie 语音助手";
            subtitle = "实时语音 · 极速转写";
        } else if (i == BUDDY_LAUNCHER_ITEM_GAME_LIFE) {
            accent = COL_GREEN;
            title = "4. 赛博人生重开";
            subtitle = "天赋抽选 · 逆天改命";
        } else if (i == BUDDY_LAUNCHER_ITEM_GAME_SNAKE) {
            accent = COL_GREEN;
            title = "5. 经典贪吃蛇";
            subtitle = "转向避障 · 挑战最高分";
        } else if (i == BUDDY_LAUNCHER_ITEM_GAME_DINO) {
            accent = COL_YELLOW;
            title = "6. 跳跳恐龙跑酷";
            subtitle = "越过仙人掌 · 刷新纪录";
        } else if (i == BUDDY_LAUNCHER_ITEM_SETTINGS) {
            accent = COL_BLUE;
            title = "7. 系统设置";
            subtitle = "界面主题 · 屏幕亮度";
        }

        /* 绘制卡片底框与边框 */
        if (selected) {
            box(layer, 8, y, 216, 43, COL_CARD_BG, accent, 2, 4);
            box(layer, 8, y, 4, 43, accent, accent, 0, 0);
            text(layer, 18, y + 4, 180, COL_INK, title, true, LV_TEXT_ALIGN_LEFT);
            text(layer, 18, y + 23, 180, COL_DIM, subtitle, false, LV_TEXT_ALIGN_LEFT);
            text(layer, 202, y + 12, 18, accent, ">", true, LV_TEXT_ALIGN_CENTER);
        } else {
            box(layer, 8, y, 216, 43, COL_CARD_BG, COL_CARD_BORDER, 1, 4);
            box(layer, 8, y, 2, 43, COL_LINE, COL_LINE, 0, 0);
            text(layer, 18, y + 4, 180, COL_INK, title, true, LV_TEXT_ALIGN_LEFT);
            text(layer, 18, y + 23, 180, COL_DIM, subtitle, false, LV_TEXT_ALIGN_LEFT);
        }
    }

    /* 右侧精致微型滚动条 */
    int bar_track_y = 52;
    int bar_track_h = 5 * 47 - 4;
    box(layer, 228, bar_track_y, 3, bar_track_h, COL_LINE, COL_LINE, 0, 1);
    int thumb_h = bar_track_h * visible_items / total_items;
    int thumb_y = bar_track_y + (bar_track_h - thumb_h) * sel / (total_items - 1);
    box(layer, 227, thumb_y, 5, thumb_h, COL_ORANGE, COL_ORANGE, 0, 2);

    /* 底部操作提示 */
    text(layer, 8, 298, 224, COL_DIM, "UP/DOWN:选择  OK:进入  长按:休眠", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_vokie(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    const buddy_vokie_state_t *v = buddy_vokie_get_state();
    char buf[64];
    (void)s;

    /* 1. 顶部标题栏 (Y: 26 ~ 50) */
    text(layer, 12, 28, 150, COL_BLUE, "VOKIE AI VOICE", true, LV_TEXT_ALIGN_LEFT);
    lv_color_t dot_color = v->connected ? (v->recording ? COL_ORANGE : COL_GREEN) : COL_DIM;
    box(layer, 168, 28, 60, 17, COL_CARD_BG, dot_color, 1, 3);
    text(layer, 168, 30, 60, dot_color, v->connected ? "已连接" : "离线待命", false, LV_TEXT_ALIGN_CENTER);
    rule(layer, 12, 48, 216, COL_LINE);

    /* 2. 中央科技麦克风与动态声波徽标 (X: 76, Y: 58, W: 88, H: 76) */
    box(layer, 76, 58, 88, 76, COL_CARD_BG, v->recording ? COL_ORANGE : COL_BLUE, 1, 4);
    int bar_heights[5] = {16, 28, 44, 28, 16};
    if (v->recording) {
        uint32_t t = (v->active_tick / 150) % 4;
        bar_heights[0] = 12 + ((t * 7) % 18);
        bar_heights[1] = 20 + (((t + 1) * 9) % 24);
        bar_heights[2] = 36 + (((t + 2) * 11) % 20);
        bar_heights[3] = 22 + (((t + 3) * 8) % 22);
        bar_heights[4] = 14 + ((t * 6) % 16);
    }
    lv_color_t wave_col = v->recording ? COL_ORANGE : (v->connected ? COL_BLUE : COL_DIM);
    for (int b = 0; b < 5; ++b) {
        int bx = 90 + b * 12;
        int bh = bar_heights[b];
        int by = 96 - bh / 2;
        box(layer, bx, by, 6, bh, wave_col, wave_col, 0, 1);
    }

    /* 3. 核心状态文字 (Y: 142) */
    const char *state_str = "OFFLINE";
    lv_color_t state_col = COL_DIM;
    switch (v->status) {
    case BUDDY_VOKIE_STATE_LISTENING:
        state_str = "LISTENING...";
        state_col = COL_ORANGE;
        break;
    case BUDDY_VOKIE_STATE_THINKING:
        state_str = "THINKING...";
        state_col = COL_YELLOW;
        break;
    case BUDDY_VOKIE_STATE_SENT:
        state_str = "SENT SUCCESS";
        state_col = COL_GREEN;
        break;
    case BUDDY_VOKIE_STATE_ERROR:
        state_str = "ERROR";
        state_col = COL_RED;
        break;
    case BUDDY_VOKIE_STATE_READY:
        state_str = "READY";
        state_col = COL_GREEN;
        break;
    default:
        state_str = v->connected ? "READY" : "WAITING FOR HOST";
        state_col = v->connected ? COL_BLUE : COL_DIM;
        break;
    }
    text(layer, 12, 142, 175, state_col, state_str, true, LV_TEXT_ALIGN_CENTER);

    /* 4. 实时信息卡片 (Y: 172 ~ 276, H: 104) */
    box(layer, 12, 172, 175, 104, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
    box(layer, 12, 172, 3, 104, state_col, state_col, 0, 0);

    text(layer, 20, 178, 160, COL_DIM, "当前活动状态", false, LV_TEXT_ALIGN_LEFT);
    rule(layer, 20, 196, 155, COL_LINE);

    snprintf(buf, sizeof(buf), "%s", v->message[0] ? v->message : "Vokie 待命中");
    text(layer, 20, 204, 160, COL_INK, buf, true, LV_TEXT_ALIGN_LEFT);

    text(layer, 20, 226, 160, COL_DIM, "采样: 16kHz 16-bit", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 20, 244, 160, COL_DIM, "压缩: IMA ADPCM 20ms", false, LV_TEXT_ALIGN_LEFT);

    /* 5. 右侧悬浮按键提示轨 (Button Hints Rail, X: 194, W: 36) */
    const int rail_x = 194;
    const int rail_w = 36;
    box(layer, rail_x, 58, rail_w, 66, COL_CARD_BG, COL_ORANGE, 1, 3);
    text(layer, rail_x, 66, rail_w, COL_ORANGE, "UP", true, LV_TEXT_ALIGN_CENTER);
    text(layer, rail_x, 86, rail_w, COL_INK, v->recording ? "停止" : "录音", false, LV_TEXT_ALIGN_CENTER);
    text(layer, rail_x, 104, rail_w, COL_DIM, "切换", false, LV_TEXT_ALIGN_CENTER);

    box(layer, rail_x, 134, rail_w, 66, COL_CARD_BG, COL_BLUE, 1, 3);
    text(layer, rail_x, 142, rail_w, COL_BLUE, "DOWN", true, LV_TEXT_ALIGN_CENTER);
    text(layer, rail_x, 162, rail_w, COL_INK, "进入", false, LV_TEXT_ALIGN_CENTER);
    text(layer, rail_x, 180, rail_w, COL_DIM, "回车", false, LV_TEXT_ALIGN_CENTER);

    box(layer, rail_x, 210, rail_w, 66, COL_CARD_BG, COL_GREEN, 1, 3);
    text(layer, rail_x, 218, rail_w, COL_GREEN, "OK", true, LV_TEXT_ALIGN_CENTER);
    text(layer, rail_x, 238, rail_w, COL_INK, "点按", false, LV_TEXT_ALIGN_CENTER);
    text(layer, rail_x, 256, rail_w, COL_DIM, "退/清", false, LV_TEXT_ALIGN_CENTER);

    /* 6. 底部系统提示 */
    text(layer, 8, 296, 224, COL_DIM, "点OK:删/取消  双击:清空  长按:菜单", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_profile_qr_overlay(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    (void)s;
    QRCode qrcode;
    uint8_t qrcodeBytes[256];
    qrcode_initText(&qrcode, qrcodeBytes, 3, ECC_LOW, "http://192.168.48.156:8765/profile");

    /* 半透明点阵遮罩效果 */
    for (int y = BUDDY_UI_STATUS_H; y < BUDDY_UI_ACTION_Y; ++y) {
        for (int x = (y & 1); x < UI_W; x += 2) {
            uint8_t current = buddy_i4_get_pixel(s_surface.pixels, UI_W, (uint16_t)x, (uint16_t)y);
            if (current != 0) {
                buddy_i4_set_pixel(s_surface.pixels, UI_W, (uint16_t)x, (uint16_t)y, 15);
            }
        }
    }

    /* 居中悬浮卡片 (X: 18, Y: 46, W: 204, H: 236) */
    box(layer, 18, 46, 204, 236, COL_CARD_BG, COL_ORANGE, 2, 4);
    text(layer, 18, 54, 204, COL_ORANGE, "扫码修改个人信息", true, LV_TEXT_ALIGN_CENTER);
    rule(layer, 28, 73, 184, COL_LINE);

    /* 二维码纯白背景底板 (29 * 4 = 116px，加每边 8px 静区 = 132px，居中 X: 54, Y: 80) */
    const int qr_box_x = 54;
    const int qr_box_y = 80;
    const int qr_box_size = 132;
    box(layer, qr_box_x, qr_box_y, qr_box_size, qr_box_size, COL_WHITE, COL_WHITE, 0, 0);

    /* 绘制二维码像素点 (深黑模块，确保双主题下扫码对比度最高) */
    const int start_x = qr_box_x + 8;
    const int start_y = qr_box_y + 8;
    uint8_t mod_idx = (s_active_theme == BUDDY_THEME_WARM_LIGHT) ? 1 : 0;
    for (uint8_t qy = 0; qy < qrcode.size; ++qy) {
        for (uint8_t qx = 0; qx < qrcode.size; ++qx) {
            if (qrcode_getModule(&qrcode, qx, qy)) {
                int px = start_x + (int)qx * 4;
                int py = start_y + (int)qy * 4;
                for (int dy = 0; dy < 4; ++dy) {
                    for (int dx = 0; dx < 4; ++dx) {
                        pixel(px + dx, py + dy, mod_idx);
                    }
                }
            }
        }
    }

    /* 底部操作说明 */
    text(layer, 18, 222, 204, COL_INK, "手机扫码即可直接修改", false, LV_TEXT_ALIGN_CENTER);
    text(layer, 18, 246, 204, COL_DIM, "短按 OK 键关闭弹窗", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_profile(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    char token_buf[16];
    char cost_buf[32];
    char name_buf[48];
    char owner_buf[64];

    /* 顶部标头栏 */
    text(layer, 12, 28, 120, COL_ORANGE, "个人智能主页", true, LV_TEXT_ALIGN_LEFT);
    box(layer, 146, 27, 82, 18, COL_CARD_BG, COL_ORANGE, 1, 3);
    text(layer, 146, 29, 82, COL_ORANGE, "PASSPORT", false, LV_TEXT_ALIGN_CENTER);
    rule(layer, 12, 48, 216, COL_LINE);

    /* 卡片 1: 电子工牌与伴侣立绘 (ID Badge & Companion, Y: 54 ~ 166, H: 112) */
    box(layer, 8, 54, 224, 112, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
    box(layer, 8, 54, 3, 112, COL_YELLOW, COL_YELLOW, 0, 0);

    /* 伴侣头像立绘框 (X: 16, Y: 60, W: 74, H: 100) */
    box(layer, 16, 60, 74, 100, COL_BG, COL_LINE, 1, 2);
    buddy_i4_clip_t clip = {.x = 16, .y = 60, .w = 74, .h = 80};
    buddy_sprite_bounds_t bounds;
    int sp_x = 24;
    int sp_y = 66;
    if (buddy_sprite_bounds(s->species, art_state(s->character), s_tick, &bounds)) {
        sp_x = 16 + (74 - bounds.w) / 2 - bounds.x;
        sp_y = 60 + (80 - bounds.h) / 2 - bounds.y;
    }
    buddy_sprite_render(&s_surface, &clip, s->species, art_state(s->character), s_tick, sp_x, sp_y);
    text(layer, 16, 142, 74, COL_GREEN, buddy_sprite_name(s->species), false, LV_TEXT_ALIGN_CENTER);

    /* 右侧身份信息 (X: 98, Y: 60 ~ 160) */
    snprintf(name_buf, sizeof(name_buf), "%s", s->name[0] ? s->name : "syhx114514");
    text(layer, 98, 62, 126, COL_INK, name_buf, true, LV_TEXT_ALIGN_LEFT);

    /* 认证标签 */
    box(layer, 98, 86, 84, 16, COL_CARD_BG, COL_GREEN, 1, 3);
    text(layer, 98, 87, 84, COL_GREEN, "Pro 极客认证", false, LV_TEXT_ALIGN_CENTER);

    text(layer, 98, 110, 126, COL_DIM, "编号: C3-32EAAA", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 98, 128, 126, COL_YELLOW, "战力: S 级极客", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 98, 146, 126, s->ble_connected ? COL_GREEN : COL_DIM,
         s->ble_connected ? "● 在线同步中" : "○ 蓝牙待命中", false, LV_TEXT_ALIGN_LEFT);

    /* 卡片 2: 通行证核心数据与资产 (Passport Data, Y: 172 ~ 284, H: 112) */
    box(layer, 8, 172, 224, 112, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
    box(layer, 8, 172, 3, 112, COL_ORANGE, COL_ORANGE, 0, 0);
    text(layer, 18, 178, 120, COL_ORANGE, "通行证数据", true, LV_TEXT_ALIGN_LEFT);
    rule(layer, 18, 198, 204, COL_LINE);

    /* 行 1: 绑定账号 */
    snprintf(owner_buf, sizeof(owner_buf), "%s", s->owner[0] ? s->owner : "syhx114514@gmail.com");
    text(layer, 18, 204, 52, COL_DIM, "账号", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 72, 204, 150, COL_INK, owner_buf, false, LV_TEXT_ALIGN_LEFT);

    /* 行 2: 今日 Token 消耗 */
    format_token_metric(token_buf, sizeof(token_buf), s->token_monitor.tokens_today);
    text(layer, 18, 224, 52, COL_DIM, "用量", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 72, 224, 150, COL_YELLOW, token_buf, false, LV_TEXT_ALIGN_LEFT);

    /* 行 3: 主力模型状态 */
    text(layer, 18, 244, 52, COL_DIM, "主力", false, LV_TEXT_ALIGN_LEFT);
    if (s->codex_usage.available) {
        unsigned p_rem = 100U - s->codex_usage.primary_used_percent;
        char m_buf[32];
        snprintf(m_buf, sizeof(m_buf), "Gemini (剩余 %u%%)", p_rem);
        text(layer, 72, 244, 150, COL_GREEN, m_buf, false, LV_TEXT_ALIGN_LEFT);
    } else {
        text(layer, 72, 244, 150, COL_GREEN, "Gemini 3.8 Flash", false, LV_TEXT_ALIGN_LEFT);
    }

    /* 行 4: 预估价值 */
    snprintf(cost_buf, sizeof(cost_buf), "¥%.2f · PRO套餐", (double)s->token_monitor.cost_today_cents / 100.0);
    text(layer, 18, 264, 52, COL_DIM, "资产", false, LV_TEXT_ALIGN_LEFT);
    text(layer, 72, 264, 150, COL_BLUE, cost_buf, false, LV_TEXT_ALIGN_LEFT);

    /* 底部操作提示 */
    text(layer, 8, 296, 224, COL_DIM, "双击OK扫码修改  长按返回菜单", false, LV_TEXT_ALIGN_CENTER);

    /* 若双击打开了二维码弹窗，则覆盖绘制二维码悬浮窗 */
    if (s->profile_qr_open) {
        draw_profile_qr_overlay(layer, s);
    }
}

static void draw_game_snake(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    const buddy_game_snake_t *snake = buddy_snake_get_state();
    char score_buf[32];
    char high_buf[32];
    uint16_t i;
    (void)s;

    /* 顶部得分栏 */
    text(layer, 12, 28, 80, COL_GREEN, "经典贪吃蛇", true, LV_TEXT_ALIGN_LEFT);
    snprintf(score_buf, sizeof(score_buf), "得分: %u", (unsigned)snake->score);
    text(layer, 96, 28, 64, COL_INK, score_buf, false, LV_TEXT_ALIGN_RIGHT);
    snprintf(high_buf, sizeof(high_buf), "最高: %u", (unsigned)snake->high_score);
    text(layer, 164, 28, 64, COL_ORANGE, high_buf, false, LV_TEXT_ALIGN_RIGHT);

    /* 网格区域外边框 (220 x 220 居中在 240 x 320 中) */
    const int origin_x = 10;
    const int origin_y = 52;
    const int cell_size = 10;
    box(layer, origin_x - 2, origin_y - 2, 224, 224, COL_CARD_BG, COL_CARD_BORDER, 1, 2);

    /* 绘制食物 (双色发光点) */
    int food_px = origin_x + snake->food.x * cell_size;
    int food_py = origin_y + snake->food.y * cell_size;
    box(layer, food_px + 1, food_py + 1, 8, 8, COL_YELLOW, COL_ORANGE, 1, 0);

    /* 绘制蛇身 (墨绿到青绿) */
    for (i = 1; i < snake->length; ++i) {
        int bx = origin_x + snake->body[i].x * cell_size;
        int by = origin_y + snake->body[i].y * cell_size;
        box(layer, bx + 1, by + 1, 8, 8, lv_color_hex(0x3a8055), lv_color_hex(0x275a3a), 1, 0);
    }

    /* 绘制蛇头 (亮绿 + 眼睛) */
    if (snake->length > 0) {
        int hx = origin_x + snake->body[0].x * cell_size;
        int hy = origin_y + snake->body[0].y * cell_size;
        box(layer, hx + 1, hy + 1, 8, 8, COL_GREEN, lv_color_hex(0x429661), 1, 0);
        pixel(hx + 3, hy + 3, color_index(COL_BG));
        pixel(hx + 6, hy + 3, color_index(COL_BG));
    }

    /* 游戏结束或暂停悬浮窗 */
    if (snake->game_over) {
        box(layer, 36, 115, 168, 88, COL_CARD_BG, COL_RED, 2, 4);
        text(layer, 36, 125, 168, COL_RED, "游戏结束", true, LV_TEXT_ALIGN_CENTER);
        text(layer, 36, 150, 168, COL_INK, "短按 OK 重新开始", false, LV_TEXT_ALIGN_CENTER);
        text(layer, 36, 170, 168, COL_DIM, "长按 OK 返回菜单", false, LV_TEXT_ALIGN_CENTER);
    } else if (snake->paused) {
        box(layer, 48, 125, 144, 60, COL_CARD_BG, COL_YELLOW, 2, 4);
        text(layer, 48, 138, 144, COL_YELLOW, "游戏暂停", true, LV_TEXT_ALIGN_CENTER);
        text(layer, 48, 160, 144, COL_INK, "短按 OK 继续", false, LV_TEXT_ALIGN_CENTER);
    }

    /* 底部操作栏 */
    text(layer, 8, 292, 224, COL_DIM, "UP/DOWN:转向  OK:暂停  长按:菜单", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_game_dino(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    const buddy_game_dino_t *dino = buddy_dino_get_state();
    char score_buf[32];
    char high_buf[32];
    int i;
    (void)s;

    /* 顶部状态栏 */
    text(layer, 12, 28, 80, COL_YELLOW, "跳跳恐龙", true, LV_TEXT_ALIGN_LEFT);
    snprintf(score_buf, sizeof(score_buf), "得分: %u", (unsigned)dino->score);
    text(layer, 96, 28, 64, COL_INK, score_buf, false, LV_TEXT_ALIGN_RIGHT);
    snprintf(high_buf, sizeof(high_buf), "最高: %u", (unsigned)dino->high_score);
    text(layer, 164, 28, 64, COL_ORANGE, high_buf, false, LV_TEXT_ALIGN_RIGHT);

    /* 跑酷主背景框 (224 x 224 像素，Y: 52 ~ 276) */
    box(layer, 8, 52, 224, 224, COL_CARD_BG, COL_CARD_BORDER, 1, 2);

    /* 天空浮云 (两朵缓缓移动的像素云) */
    int cloud1_x = (int)((dino->tick_count * 2) % 260) - 20;
    int cloud2_x = (int)((dino->tick_count * 3 + 130) % 260) - 20;
    box(layer, 230 - cloud1_x, 75, 24, 6, COL_CARD_BORDER, COL_CARD_BORDER, 0, 1);
    box(layer, 230 - cloud2_x, 105, 30, 6, COL_CARD_BORDER, COL_CARD_BORDER, 0, 1);

    /* 地面基准线 Y = 230 */
    const int ground_y = 230;
    rule(layer, 12, ground_y, 216, COL_DIM);

    /* 地表碎石点缀 (根据 tick 滚动) */
    for (i = 0; i < 6; ++i) {
        int dot_x = 12 + (int)((i * 38 + (dino->tick_count * 4)) % 210);
        pixel(dot_x, ground_y + 4, color_index(COL_LINE));
        pixel(dot_x + 1, ground_y + 4, color_index(COL_LINE));
    }

    /* 绘制恐龙形象 (X = 32, Y = ground_y - dino->y) */
    const int dino_x = 32;
    int dino_base_y = ground_y - dino->y;
    lv_color_t dino_color = dino->game_over ? COL_RED : COL_GREEN;

    /* 恐龙身体像素块 */
    box(layer, dino_x + 4, dino_base_y - 22, 12, 14, dino_color, dino_color, 0, 0);
    box(layer, dino_x + 10, dino_base_y - 28, 10, 10, dino_color, dino_color, 0, 0);
    box(layer, dino_x + 15, dino_base_y - 26, 2, 2, COL_BG, COL_BG, 0, 0);
    box(layer, dino_x + 16, dino_base_y - 18, 5, 3, dino_color, dino_color, 0, 0);
    box(layer, dino_x, dino_base_y - 18, 5, 5, dino_color, dino_color, 0, 0);

    /* 奔跑步伐动画 (双腿交替摆动) */
    if (dino->is_jumping || dino->y > 0) {
        box(layer, dino_x + 6, dino_base_y - 8, 3, 5, dino_color, dino_color, 0, 0);
        box(layer, dino_x + 12, dino_base_y - 6, 3, 3, dino_color, dino_color, 0, 0);
    } else {
        bool leg_alt = (dino->tick_count % 4) < 2;
        box(layer, dino_x + 6, dino_base_y - 8, 3, leg_alt ? 8 : 4, dino_color, dino_color, 0, 0);
        box(layer, dino_x + 12, dino_base_y - 8, 3, leg_alt ? 4 : 8, dino_color, dino_color, 0, 0);
    }

    /* 绘制仙人掌障碍物 */
    for (i = 0; i < DINO_OBSTACLE_MAX; ++i) {
        if (!dino->obstacles[i].active) continue;
        int ox = dino->obstacles[i].x;
        int ow = dino->obstacles[i].w;
        int oh = dino->obstacles[i].h;
        if (ox > -20 && ox < 240) {
            box(layer, ox + 3, ground_y - oh, ow - 6 > 4 ? ow - 6 : 4, oh, COL_ORANGE, lv_color_hex(0xbc633e), 1, 0);
            if (oh > 16) {
                box(layer, ox, ground_y - oh + 6, 4, 3, COL_ORANGE, COL_ORANGE, 0, 0);
                box(layer, ox, ground_y - oh + 4, 2, 5, COL_ORANGE, COL_ORANGE, 0, 0);
                box(layer, ox + ow - 4, ground_y - oh + 8, 4, 3, COL_ORANGE, COL_ORANGE, 0, 0);
                box(layer, ox + ow - 2, ground_y - oh + 6, 2, 5, COL_ORANGE, COL_ORANGE, 0, 0);
            }
        }
    }

    /* 游戏结束弹窗 */
    if (dino->game_over) {
        box(layer, 36, 115, 168, 88, COL_CARD_BG, COL_RED, 2, 4);
        text(layer, 36, 125, 168, COL_RED, "挑战结束", true, LV_TEXT_ALIGN_CENTER);
        text(layer, 36, 150, 168, COL_INK, "短按 OK 重新开始", false, LV_TEXT_ALIGN_CENTER);
        text(layer, 36, 170, 168, COL_DIM, "长按 OK 返回菜单", false, LV_TEXT_ALIGN_CENTER);
    }

    /* 底部操作提示 */
    text(layer, 8, 292, 224, COL_DIM, "UP/OK:跳跃  DOWN:俯冲  长按:菜单", false, LV_TEXT_ALIGN_CENTER);
}

static void draw_game_life(lv_layer_t *layer, const buddy_ui_snapshot_t *s)
{
    const buddy_game_life_t *l = buddy_life_get_state();
    char buf[128];
    (void)s;

    if (l->phase == LIFE_PHASE_TITLE) {
        /* 标题开屏界面 */
        text(layer, 8, 28, 224, COL_ORANGE, "AI PASSPORT", true, LV_TEXT_ALIGN_CENTER);
        text(layer, 8, 46, 224, COL_DIM, "赛博人生重开模拟器", false, LV_TEXT_ALIGN_CENTER);
        rule(layer, 20, 62, 200, COL_LINE);

        /* 主介绍卡片 (Y: 72 ~ 190) */
        box(layer, 14, 72, 212, 118, COL_CARD_BG, COL_GREEN, 2, 4);
        text(layer, 14, 82, 212, COL_GREEN, "人生重开 · 逆天改命", true, LV_TEXT_ALIGN_CENTER);
        rule(layer, 24, 102, 192, COL_LINE);

        text(layer, 22, 110, 196, COL_INK, "这辈子不满意？那便重开！\n十选三逆天天赋，分配四维属性，\n探索凡人、修仙、赛博无限人生！", false, LV_TEXT_ALIGN_LEFT);

        /* 历史最佳徽章 (Y: 200 ~ 276) */
        box(layer, 14, 200, 212, 76, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
        box(layer, 14, 200, 4, 76, COL_YELLOW, COL_YELLOW, 0, 0);
        text(layer, 24, 206, 192, COL_YELLOW, "【生涯最高记录】", true, LV_TEXT_ALIGN_LEFT);
        snprintf(buf, sizeof(buf), "最高寿元: %u 岁", (unsigned)l->best_age);
        text(layer, 24, 226, 192, COL_INK, buf, false, LV_TEXT_ALIGN_LEFT);
        snprintf(buf, sizeof(buf), "生平评价: %s", c_life_titles[l->best_title_idx].name);
        text(layer, 24, 246, 192, COL_DIM, buf, false, LV_TEXT_ALIGN_LEFT);

        /* 底部操作 */
        text(layer, 8, 292, 224, COL_ORANGE, "短按 [OK] 立即开启新人生", true, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if (l->phase == LIFE_PHASE_TALENT) {
        /* 天赋抽选阶段 (10选3) */
        snprintf(buf, sizeof(buf), "天赋抽选 (已选 %u/3)", (unsigned)l->talent_selected_count);
        text(layer, 12, 28, 216, COL_ORANGE, buf, true, LV_TEXT_ALIGN_LEFT);
        rule(layer, 12, 46, 216, COL_LINE);

        /* 滚动列表 (同屏显示 4 个天赋) */
        int sel = (int)l->talent_cursor;
        int top = 0;
        if (sel < 10) {
            if (sel >= 4) top = sel - 4 + 1;
            if (top > 6) top = 6;
        } else {
            top = 6; // 光标在[踏入轮回]时保持显示最后几项
        }

        for (int r = 0; r < 4; r++) {
            int t_idx = top + r;
            if (t_idx >= 10) break;
            uint8_t tid = l->talent_pool[t_idx];
            const life_talent_def_t *t = &c_life_talents[tid];
            int y = 52 + r * 46;
            bool is_cur = (sel == t_idx);
            bool is_chk = l->talent_checked[t_idx];

            lv_color_t q_col = COL_DIM;
            const char *q_tag = "[普]";
            if (t->quality == 3) { q_col = COL_ORANGE; q_tag = "[神]"; }
            else if (t->quality == 2) { q_col = COL_YELLOW; q_tag = "[史]"; }
            else if (t->quality == 1) { q_col = COL_BLUE; q_tag = "[稀]"; }

            if (is_cur) {
                box(layer, 10, y, 216, 42, COL_CARD_BG, is_chk ? COL_GREEN : COL_ORANGE, 2, 3);
            } else {
                box(layer, 10, y, 216, 42, COL_CARD_BG, is_chk ? COL_GREEN : COL_CARD_BORDER, 1, 3);
            }

            // 勾选复选框
            if (is_chk) {
                box(layer, 14, y + 6, 14, 14, COL_GREEN, COL_GREEN, 0, 1);
                text(layer, 14, y + 6, 14, COL_BG, "V", true, LV_TEXT_ALIGN_CENTER);
            } else {
                box(layer, 14, y + 6, 14, 14, COL_CARD_BG, COL_DIM, 1, 1);
            }

            // 品质与名称
            snprintf(buf, sizeof(buf), "%s %s", q_tag, t->name);
            text(layer, 32, y + 5, 188, q_col, buf, true, LV_TEXT_ALIGN_LEFT);
            // 描述
            text(layer, 32, y + 23, 188, COL_INK, t->desc, false, LV_TEXT_ALIGN_LEFT);
        }

        /* 底部踏入轮回确认条 (Y: 242) */
        bool on_btn = (sel == 10);
        lv_color_t btn_col = (l->talent_selected_count == 3) ? COL_GREEN : COL_DIM;
        if (on_btn) {
            box(layer, 12, 242, 216, 36, COL_CARD_BG, btn_col, 2, 3);
            text(layer, 12, 250, 216, btn_col, ">>> 踏入轮回 · 前往加点 <<<", true, LV_TEXT_ALIGN_CENTER);
        } else {
            box(layer, 12, 242, 216, 36, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
            text(layer, 12, 250, 216, (l->talent_selected_count == 3) ? COL_INK : COL_DIM,
                 (l->talent_selected_count == 3) ? "踏入轮回 · 前往加点" : "请先勾选满 3 个天赋", false, LV_TEXT_ALIGN_CENTER);
        }

        /* 底部按键提示 */
        text(layer, 8, 292, 224, COL_DIM, "UP/DOWN:切换  OK:勾选  长按:菜单", false, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if (l->phase == LIFE_PHASE_ALLOC) {
        /* 属性分配阶段 (颜值, 智力, 体质, 家境) */
        snprintf(buf, sizeof(buf), "属性分配 (剩余可用: %d 点)", (int)l->remain_pts);
        text(layer, 12, 28, 216, (l->remain_pts == 0) ? COL_GREEN : COL_ORANGE, buf, true, LV_TEXT_ALIGN_LEFT);
        rule(layer, 12, 46, 216, COL_LINE);

        static const char *attr_names[4] = {"1. 颜值 (CHR)", "2. 智力 (INT)", "3. 体质 (STR)", "4. 家境 (MNY)"};
        static const char *attr_tips[4] = {"影响社交恋爱与贵人相助", "影响高考求职与修仙感悟", "影响健康寿命与抵抗突发意外", "决定童年起点与创业资本"};
        lv_color_t attr_colors[4] = {COL_YELLOW, COL_BLUE, COL_GREEN, COL_ORANGE};

        for (int i = 0; i < 4; i++) {
            int y = 54 + i * 46;
            bool is_cur = (l->alloc_cursor == i);
            int pts = l->alloc_pts[i];

            if (is_cur) {
                box(layer, 10, y, 216, 42, COL_CARD_BG, attr_colors[i], 2, 3);
                box(layer, 10, y, 4, 42, attr_colors[i], attr_colors[i], 0, 0);
            } else {
                box(layer, 10, y, 216, 42, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
            }

            // 属性名与数值
            snprintf(buf, sizeof(buf), "%s: %d 点", attr_names[i], pts);
            text(layer, 18, y + 4, 140, is_cur ? attr_colors[i] : COL_INK, buf, true, LV_TEXT_ALIGN_LEFT);

            // 进度槽
            box(layer, 148, y + 8, 70, 8, COL_CARD_BORDER, COL_CARD_BORDER, 0, 1);
            if (pts > 0) {
                int bar_w = pts * 7;
                if (bar_w > 70) bar_w = 70;
                box(layer, 148, y + 8, bar_w, 8, attr_colors[i], attr_colors[i], 0, 1);
            }

            // 提示文
            text(layer, 18, y + 22, 196, COL_DIM, attr_tips[i], false, LV_TEXT_ALIGN_LEFT);
        }

        /* 确认开局按钮 */
        bool on_btn = (l->alloc_cursor == 4);
        if (on_btn) {
            box(layer, 12, 244, 216, 36, COL_CARD_BG, COL_GREEN, 2, 3);
            text(layer, 12, 252, 216, COL_GREEN, ">>> 【降生人世 · 开启人生】 <<<", true, LV_TEXT_ALIGN_CENTER);
        } else {
            box(layer, 12, 244, 216, 36, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
            text(layer, 12, 252, 216, COL_INK, "【降生人世 · 开启人生】", false, LV_TEXT_ALIGN_CENTER);
        }

        text(layer, 8, 292, 224, COL_DIM, "UP:+1点  DOWN:-1点  OK:下一项/确定", false, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if (l->phase == LIFE_PHASE_PLAY) {
        /* 人生进行时主界面 (年谱流) */
        // 顶部 HUD (Y: 26 ~ 50)
        box(layer, 8, 26, 68, 22, COL_CARD_BG, COL_GREEN, 1, 3);
        snprintf(buf, sizeof(buf), "%u 岁", (unsigned)l->age);
        text(layer, 8, 28, 68, COL_GREEN, buf, true, LV_TEXT_ALIGN_CENTER);

        // 特殊状态角标
        if (l->is_cultivator) {
            box(layer, 80, 26, 52, 22, COL_CARD_BG, COL_YELLOW, 1, 3);
            text(layer, 80, 28, 52, COL_YELLOW, "修仙中", true, LV_TEXT_ALIGN_CENTER);
        } else if (l->is_cyber) {
            box(layer, 80, 26, 52, 22, COL_CARD_BG, COL_GREEN, 1, 3);
            text(layer, 80, 28, 52, COL_GREEN, "赛博流", true, LV_TEXT_ALIGN_CENTER);
        }

        // 紧凑属性行 (Y: 30)
        snprintf(buf, sizeof(buf), "颜%d 智%d 体%d 财%d 乐%d",
                 (int)l->chr, (int)l->int_val, (int)l->str, (int)l->mny, (int)l->joy);
        text(layer, 134, 30, 102, COL_DIM, buf, false, LV_TEXT_ALIGN_RIGHT);
        rule(layer, 8, 52, 224, COL_LINE);

        /* 中间日志流区域 (Y: 56 ~ 276, 最多渲染当前可见的 3~4 条事件) */
        int total_logs = l->log_count;
        if (total_logs > 0) {
            int view_count = 3;
            int end_idx = total_logs - 1 - l->scroll_ofs;
            if (end_idx < 0) end_idx = 0;
            int start_idx = end_idx - view_count + 1;
            if (start_idx < 0) start_idx = 0;

            int cur_y = 56;
            for (int idx = start_idx; idx <= end_idx; idx++) {
                const buddy_life_log_item_t *item = &l->logs[idx];
                bool is_latest = (idx == total_logs - 1);

                int card_h = 68;
                if (is_latest) {
                    box(layer, 8, cur_y, 224, card_h, COL_CARD_BG, COL_ORANGE, 2, 3);
                    box(layer, 8, cur_y, 4, card_h, COL_ORANGE, COL_ORANGE, 0, 0);
                } else {
                    box(layer, 8, cur_y, 224, card_h, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
                    box(layer, 8, cur_y, 3, card_h, COL_DIM, COL_DIM, 0, 0);
                }

                // 年份与属性变更标签
                snprintf(buf, sizeof(buf), "【%u 岁】", (unsigned)item->age);
                text(layer, 16, cur_y + 4, 70, is_latest ? COL_ORANGE : COL_YELLOW, buf, true, LV_TEXT_ALIGN_LEFT);

                // 属性变化小标签
                char d_buf[64] = "";
                if (item->d_int > 0) snprintf(d_buf + strlen(d_buf), sizeof(d_buf) - strlen(d_buf), "智+%d ", item->d_int);
                if (item->d_str > 0) snprintf(d_buf + strlen(d_buf), sizeof(d_buf) - strlen(d_buf), "体+%d ", item->d_str);
                if (item->d_str < 0) snprintf(d_buf + strlen(d_buf), sizeof(d_buf) - strlen(d_buf), "体%d ", item->d_str);
                if (item->d_mny > 0) snprintf(d_buf + strlen(d_buf), sizeof(d_buf) - strlen(d_buf), "财+%d ", item->d_mny);
                if (item->d_joy > 0) snprintf(d_buf + strlen(d_buf), sizeof(d_buf) - strlen(d_buf), "乐+%d ", item->d_joy);
                if (item->d_joy < 0) snprintf(d_buf + strlen(d_buf), sizeof(d_buf) - strlen(d_buf), "乐%d ", item->d_joy);
                if (d_buf[0] != '\0') {
                    text(layer, 86, cur_y + 4, 140, is_latest ? COL_GREEN : COL_DIM, d_buf, false, LV_TEXT_ALIGN_RIGHT);
                }

                // 事件详情文本
                text_limited(layer, 16, cur_y + 24, 208, is_latest ? COL_INK : COL_DIM, item->text, false, LV_TEXT_ALIGN_LEFT, 2);

                cur_y += card_h + 6;
            }
        }

        /* 底部操作指引 */
        text(layer, 8, 292, 224, COL_ORANGE, "OK:下一年  双击:连进3年  UP/DOWN:翻看", false, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if (l->phase == LIFE_PHASE_CHOICE) {
        /* 重大人生抉择弹窗 */
        const life_choice_def_t *c = &c_life_choices[l->cur_choice_idx];

        // 弹窗背景
        box(layer, 10, 48, 220, 234, COL_CARD_BG, COL_ORANGE, 2, 4);
        snprintf(buf, sizeof(buf), "人生重大抉择 (%u岁)", (unsigned)c->age);
        text(layer, 10, 56, 220, COL_ORANGE, buf, true, LV_TEXT_ALIGN_CENTER);
        rule(layer, 20, 76, 200, COL_LINE);

        text(layer, 18, 86, 204, COL_INK, c->title, true, LV_TEXT_ALIGN_CENTER);

        // 选项 A
        bool sel_a = (l->choice_sel == 0);
        if (sel_a) {
            box(layer, 18, 120, 204, 52, COL_CARD_BG, COL_GREEN, 2, 3);
            text(layer, 22, 126, 196, COL_GREEN, "A. 选项 [当前选中]", true, LV_TEXT_ALIGN_LEFT);
            text(layer, 22, 144, 196, COL_INK, c->opt_a, false, LV_TEXT_ALIGN_LEFT);
        } else {
            box(layer, 18, 120, 204, 52, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
            text(layer, 22, 126, 196, COL_DIM, "A. 选项", false, LV_TEXT_ALIGN_LEFT);
            text(layer, 22, 144, 196, COL_DIM, c->opt_a, false, LV_TEXT_ALIGN_LEFT);
        }

        // 选项 B
        bool sel_b = (l->choice_sel == 1);
        if (sel_b) {
            box(layer, 18, 182, 204, 52, COL_CARD_BG, COL_GREEN, 2, 3);
            text(layer, 22, 188, 196, COL_GREEN, "B. 选项 [当前选中]", true, LV_TEXT_ALIGN_LEFT);
            text(layer, 22, 206, 196, COL_INK, c->opt_b, false, LV_TEXT_ALIGN_LEFT);
        } else {
            box(layer, 18, 182, 204, 52, COL_CARD_BG, COL_CARD_BORDER, 1, 3);
            text(layer, 22, 188, 196, COL_DIM, "B. 选项", false, LV_TEXT_ALIGN_LEFT);
            text(layer, 22, 206, 196, COL_DIM, c->opt_b, false, LV_TEXT_ALIGN_LEFT);
        }

        text(layer, 10, 252, 220, COL_YELLOW, "UP/DOWN:切换选项  OK:确定命运", false, LV_TEXT_ALIGN_CENTER);
        return;
    }

    if (l->phase == LIFE_PHASE_OVER) {
        /* 结算与赛博墓碑界面 */
        box(layer, 10, 28, 220, 254, COL_CARD_BG, COL_RED, 2, 4);
        text(layer, 10, 36, 220, COL_RED, "=== 人生终局 · 盖棺定论 ===", true, LV_TEXT_ALIGN_CENTER);
        rule(layer, 20, 56, 200, COL_LINE);

        snprintf(buf, sizeof(buf), "享年: %u 岁", (unsigned)l->age);
        text(layer, 20, 64, 200, COL_INK, buf, true, LV_TEXT_ALIGN_LEFT);

        const life_title_def_t *title = &c_life_titles[l->current_title_idx];
        snprintf(buf, sizeof(buf), "生平称号: 【%s】", title->name);
        text(layer, 20, 84, 200, COL_YELLOW, buf, true, LV_TEXT_ALIGN_LEFT);

        snprintf(buf, sizeof(buf), "综合评分: %u 分", (unsigned)l->total_score);
        text(layer, 20, 104, 200, COL_GREEN, buf, true, LV_TEXT_ALIGN_LEFT);

        rule(layer, 20, 126, 200, COL_LINE);

        // 最终死因与评价
        text(layer, 20, 134, 200, COL_DIM, "离世缘由:", false, LV_TEXT_ALIGN_LEFT);
        text_limited(layer, 20, 152, 200, COL_INK, l->death_cause, false, LV_TEXT_ALIGN_LEFT, 2);

        text(layer, 20, 192, 200, COL_DIM, "人生墓志铭:", false, LV_TEXT_ALIGN_LEFT);
        text_limited(layer, 20, 210, 200, COL_ORANGE, title->eval, false, LV_TEXT_ALIGN_LEFT, 2);

        // 底部重开按钮
        box(layer, 20, 240, 200, 32, COL_CARD_BG, COL_GREEN, 1, 3);
        text(layer, 20, 248, 200, COL_GREEN, ">>> 短按 [OK] 再次重开 <<<", true, LV_TEXT_ALIGN_CENTER);

        text(layer, 8, 292, 224, COL_DIM, "OK:重新投胎  长按OK:返回系统菜单", false, LV_TEXT_ALIGN_CENTER);
        return;
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
    text(layer, 8, 28, 224, COL_ORANGE, "AI PASSPORT", true, LV_TEXT_ALIGN_CENTER);

    /* 运行状态微徽章：待命时直接醒目展示今日 Token 消耗 */
    if (s->running > 0) {
        box(NULL, 60, 48, 120, 18, COL_CARD_BG, COL_GREEN, 1, 3);
        snprintf(status_buf, sizeof(status_buf), "工作中 · %u 任务", s->running);
        text(layer, 60, 50, 120, COL_GREEN, status_buf, false, LV_TEXT_ALIGN_CENTER);
    } else {
        format_token_metric(token_buf, sizeof(token_buf), s->token_monitor.tokens_today);
        box(NULL, 46, 48, 148, 18, COL_CARD_BG, COL_GREEN, 1, 3);
        snprintf(status_buf, sizeof(status_buf), "今日 Token: %s", token_buf);
        text(layer, 46, 50, 148, COL_GREEN, status_buf, false, LV_TEXT_ALIGN_CENTER);
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
        rule(layer, 18, 118, 204, COL_LINE);

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
        rule(layer, 18, 226, 204, COL_LINE);

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

    text(layer, 8, 298, 224, COL_DIM, "长按功能键返回主菜单", false, LV_TEXT_ALIGN_CENTER);
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
        rule(layer, 18, 124, 204, COL_LINE);
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
        rule(layer, 18, 236, 204, COL_LINE);
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
        rule(layer, 14, 94, 212, COL_LINE);

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
                rule(layer, 18, y + 24, 204, COL_LINE);
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
        if (!s->reset_open && i == BUDDY_SETTINGS_THEME) {
            suffix = (s->ui_theme == BUDDY_THEME_WARM_DARK) ? "黑色" : "白色";
        }
        else if (!s->reset_open && i == BUDDY_SETTINGS_BRIGHTNESS) { snprintf(value, sizeof(value), "%u/4", s->brightness_level); suffix = value; }
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
    static const char *const settings[] = {"界面主题", "屏幕亮度", "声音", "蓝牙", "无线网络", "指示灯", "任务记录", "时钟旋转", "伙伴形象", "重置", "返回"};
    static const char *const reset[] = {"删除自定义角色", "恢复出厂设置", "解除蓝牙配对", "返回"};
    draw_list(layer, s->reset_open ? "重置" : "设置", s->reset_open ? reset : settings,
              s->reset_open ? BUDDY_RESET_COUNT : BUDDY_SETTINGS_COUNT,
              s->reset_open ? s->reset_selection : s->settings_selection, s);
}

static void panel(lv_layer_t *layer, int y, int h, lv_color_t accent, const char *title,
                  const char *body, const char *footer)
{
    box(layer, 10, y, 220, h, COL_CARD_BG, accent, 2, 6);
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
        box(layer, 38, 48, 164, 224, COL_CARD_BG, COL_CARD_BORDER, 2, 6);
        text(layer, 52, 61, 136, COL_ORANGE, "菜单", true, LV_TEXT_ALIGN_CENTER);
        rule(layer, 52, 88, 136, COL_LINE);
        for (i = 0; i < BUDDY_MENU_COUNT; ++i) {
            int y = 103 + (int)i * 25;
            bool active = i == (unsigned)s->menu_selection;
            if (active) box(layer, 48, y - 7, 144, 21, COL_ORANGE, COL_ORANGE, 0, 3);
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

    /* 根据 snapshot 的主题动态应用 16 色硬件调色板 */
    uint8_t theme = s_snapshot.ui_theme >= BUDDY_THEME_COUNT ? BUDDY_THEME_WARM_LIGHT : s_snapshot.ui_theme;
    if (s_active_theme != theme || s_active_palette == NULL) {
        s_active_theme = theme;
        s_active_palette = (theme == BUDDY_THEME_WARM_DARK) ? s_palette_warm_dark : s_palette_warm_light;
        for (unsigned i = 0; i < 16; ++i) {
            lv_canvas_set_palette(s_canvas, i, lv_color_to_32(lv_color_hex(s_active_palette[i]), LV_OPA_COVER));
        }
    }

    memset(s_canvas_buffer + I4_PALETTE_BYTES, 0, sizeof(s_canvas_buffer) - I4_PALETTE_BYTES);
    draw_status_bar(layer, &s_snapshot);
    switch (s_snapshot.page) {
    case BUDDY_PAGE_LAUNCHER: draw_launcher(layer, &s_snapshot); break;
    case BUDDY_PAGE_HOME: draw_home(layer, &s_snapshot); break;
    case BUDDY_PAGE_PROFILE: draw_profile(layer, &s_snapshot); break;
    case BUDDY_PAGE_GAME_SNAKE: draw_game_snake(layer, &s_snapshot); break;
    case BUDDY_PAGE_GAME_DINO: draw_game_dino(layer, &s_snapshot); break;
    case BUDDY_PAGE_GAME_LIFE: draw_game_life(layer, &s_snapshot); break;
    case BUDDY_PAGE_VOKIE: draw_vokie(layer, &s_snapshot); break;
    case BUDDY_PAGE_LIMITS: draw_limits(layer, &s_snapshot); break;
    case BUDDY_PAGE_TOOLS: draw_tools_breakdown(layer, &s_snapshot); break;
    case BUDDY_PAGE_PET: draw_companion(layer, &s_snapshot); break;
    case BUDDY_PAGE_INFO: draw_info(layer, &s_snapshot); break;
    case BUDDY_PAGE_SETTINGS: draw_settings(layer, &s_snapshot); break;
    default: draw_launcher(layer, &s_snapshot); break;
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
    s_active_palette = (s_active_theme == BUDDY_THEME_WARM_DARK) ? s_palette_warm_dark : s_palette_warm_light;
    for (i = 0; i < 16; ++i)
        lv_canvas_set_palette(s_canvas, i, lv_color_to_32(lv_color_hex(s_active_palette[i]), LV_OPA_COVER));
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
            uint32_t rgb = s_active_palette[palette_index & 0x0fU];
            uint16_t rgb565 = (uint16_t)(((rgb >> 8) & 0xf800U) |
                                         ((rgb >> 5) & 0x07e0U) |
                                         ((rgb >> 3) & 0x001fU));

            row[x * 2U] = (uint8_t)(rgb565 & 0xffU);
            row[x * 2U + 1U] = (uint8_t)(rgb565 >> 8);
        }
        if (fwrite(row, 1, sizeof(row), stream) != sizeof(row)) {
            return false;
        }
        if ((y & 15) == 0) {
            fflush(stream);
        }
    }
    fflush(stream);
    return true;
}
