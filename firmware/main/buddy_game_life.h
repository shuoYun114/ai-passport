#ifndef BUDDY_GAME_LIFE_H
#define BUDDY_GAME_LIFE_H

#include <stdint.h>
#include <stdbool.h>
#include "buddy_types.h"
#include "buddy_game_life_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LIFE_PHASE_TITLE = 0,   // 封面标题界面
    LIFE_PHASE_TALENT,      // 天赋10选3
    LIFE_PHASE_ALLOC,       // 属性加点分配
    LIFE_PHASE_PLAY,        // 人生历程进行中
    LIFE_PHASE_CHOICE,      // 重大人生抉择弹窗
    LIFE_PHASE_OVER,        // 人生结算与墓碑
} buddy_life_phase_t;

#define LIFE_MAX_LOGS 40

typedef struct {
    uint16_t age;
    char text[128];
    int8_t d_chr;
    int8_t d_int;
    int8_t d_str;
    int8_t d_mny;
    int8_t d_joy;
} buddy_life_log_item_t;

typedef struct {
    buddy_life_phase_t phase;
    uint16_t age;
    
    // 五维属性
    int16_t chr;
    int16_t int_val;
    int16_t str;
    int16_t mny;
    int16_t joy;
    
    // 特殊隐藏路线标记
    bool is_cultivator;
    bool is_cyber;
    bool is_dead;
    
    // 天赋抽选状态
    uint8_t talent_pool[10];     // 10个随机抽选的天赋ID
    bool talent_checked[10];     // 是否勾选
    uint8_t talent_cursor;       // 0~9为天赋，10为[踏入轮回]
    uint8_t talent_selected_count;
    uint8_t selected_talents[3]; // 最终选中的3个天赋
    
    // 属性加点状态
    uint8_t alloc_cursor;        // 0:颜值 1:智力 2:体质 3:家境 4:[开启人生]
    int8_t alloc_pts[4];         // 玩家分配的点数
    int8_t remain_pts;           // 剩余点数 (初始20点)
    
    // 抉择状态
    uint8_t cur_choice_idx;      // 当前抉择索引
    uint8_t choice_sel;          // 0: 选项A, 1: 选项B
    
    // 事件日志与滚动
    buddy_life_log_item_t logs[LIFE_MAX_LOGS];
    uint8_t log_count;
    int8_t scroll_ofs;           // 翻页查看偏移 (0表示最新一条位于底部)
    
    // 结算与记录
    uint16_t best_age;
    uint8_t best_title_idx;
    uint8_t current_title_idx;
    char death_cause[64];
    uint16_t total_score;
} buddy_game_life_t;

void buddy_life_init(void);
void buddy_life_reset(void);
void buddy_life_key(buddy_key_t key);
void buddy_life_double_key(void);
void buddy_life_tick(void);
const buddy_game_life_t *buddy_life_get_state(void);

#ifdef __cplusplus
}
#endif

#endif // BUDDY_GAME_LIFE_H
