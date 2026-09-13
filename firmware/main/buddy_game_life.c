#include "buddy_game_life.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_random.h"
#include "esp_log.h"

static buddy_game_life_t s_life_state;

static void add_log(uint16_t age, const char *text, int8_t dc, int8_t di, int8_t ds, int8_t dm, int8_t dj) {
    if (s_life_state.log_count < LIFE_MAX_LOGS) {
        buddy_life_log_item_t *item = &s_life_state.logs[s_life_state.log_count++];
        item->age = age;
        strncpy(item->text, text, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
        item->d_chr = dc;
        item->d_int = di;
        item->d_str = ds;
        item->d_mny = dm;
        item->d_joy = dj;
    } else {
        // 向上移动一条
        for (int i = 0; i < LIFE_MAX_LOGS - 1; i++) {
            s_life_state.logs[i] = s_life_state.logs[i + 1];
        }
        buddy_life_log_item_t *item = &s_life_state.logs[LIFE_MAX_LOGS - 1];
        item->age = age;
        strncpy(item->text, text, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
        item->d_chr = dc;
        item->d_int = di;
        item->d_str = ds;
        item->d_mny = dm;
        item->d_joy = dj;
    }
    s_life_state.scroll_ofs = 0; // 新增日志时重置滚动偏移到最新
}

static void roll_talents(void) {
    // 从24个天赋中随机挑选10个不重复的天赋
    bool used[LIFE_TALENT_COUNT] = {false};
    int count = 0;
    while (count < 10) {
        uint32_t r = esp_random() % LIFE_TALENT_COUNT;
        if (!used[r]) {
            used[r] = true;
            s_life_state.talent_pool[count] = (uint8_t)r;
            s_life_state.talent_checked[count] = false;
            count++;
        }
    }
    s_life_state.talent_cursor = 0;
    s_life_state.talent_selected_count = 0;
}

static void finalize_life_over(const char *cause) {
    s_life_state.is_dead = true;
    s_life_state.phase = LIFE_PHASE_OVER;
    if (cause) {
        strncpy(s_life_state.death_cause, cause, sizeof(s_life_state.death_cause) - 1);
    } else {
        strncpy(s_life_state.death_cause, "寿终正寝，安详离世", sizeof(s_life_state.death_cause) - 1);
    }
    
    // 计算综合得分
    int score = (s_life_state.chr + s_life_state.int_val + s_life_state.str + s_life_state.mny + s_life_state.joy);
    score += (s_life_state.age / 2);
    if (score < 0) score = 0;
    s_life_state.total_score = (uint16_t)score;
    
    // 匹配称号
    s_life_state.current_title_idx = LIFE_TITLE_COUNT - 1;
    for (int i = 0; i < LIFE_TITLE_COUNT; i++) {
        if (s_life_state.total_score >= c_life_titles[i].min_score) {
            s_life_state.current_title_idx = (uint8_t)i;
            break;
        }
    }
    
    // 更新最高分记录
    if (s_life_state.age > s_life_state.best_age) {
        s_life_state.best_age = s_life_state.age;
        s_life_state.best_title_idx = s_life_state.current_title_idx;
    }
}

static void trigger_year_event(void) {
    uint16_t age = s_life_state.age;
    
    // 1. 检查是否寿元终尽
    uint16_t max_age = 85 + (s_life_state.str > 10 ? (s_life_state.str - 10) * 2 : 0);
    if (s_life_state.is_cultivator) {
        max_age = 500;
    } else if (s_life_state.is_cyber) {
        max_age = 200;
    }
    if (age >= max_age) {
        finalize_life_over("历经沧桑，安详辞世");
        return;
    }
    
    // 2. 检查是否有抉择事件
    for (int i = 0; i < LIFE_CHOICE_COUNT; i++) {
        if (c_life_choices[i].age == age) {
            // 特殊条件校验
            if (c_life_choices[i].id == 4) { // 赛博邀请
                if (!s_life_state.is_cyber && s_life_state.int_val < 10) continue;
            }
            if (c_life_choices[i].id == 5) { // 筑基天劫
                if (!s_life_state.is_cultivator) continue;
            }
            s_life_state.cur_choice_idx = (uint8_t)i;
            s_life_state.choice_sel = 0;
            s_life_state.phase = LIFE_PHASE_CHOICE;
            return;
        }
    }
    
    // 3. 从事件库中筛选适龄事件
    uint8_t candidates[LIFE_EVENT_COUNT];
    uint8_t cand_count = 0;
    
    for (int i = 0; i < LIFE_EVENT_COUNT; i++) {
        const life_event_def_t *ev = &c_life_events[i];
        if (age >= ev->min_age && age <= ev->max_age) {
            bool ok = false;
            switch (ev->req_type) {
                case 0: ok = true; break;
                case 1: ok = (s_life_state.chr >= ev->req_val); break;
                case 2: ok = (s_life_state.int_val >= ev->req_val); break;
                case 3: ok = (s_life_state.str >= ev->req_val); break;
                case 4: ok = (s_life_state.mny >= ev->req_val); break;
                case 5: ok = (s_life_state.str <= ev->req_val); break;
                case 6: ok = (s_life_state.joy <= ev->req_val); break;
                case 7: ok = s_life_state.is_cultivator; break;
                case 8: ok = s_life_state.is_cyber; break;
                default: break;
            }
            if (ok) {
                candidates[cand_count++] = (uint8_t)i;
            }
        }
    }
    
    if (cand_count > 0) {
        uint8_t chosen = candidates[esp_random() % cand_count];
        const life_event_def_t *ev = &c_life_events[chosen];
        
        s_life_state.chr += ev->d_chr;
        s_life_state.int_val += ev->d_int;
        s_life_state.str += ev->d_str;
        s_life_state.mny += ev->d_mny;
        s_life_state.joy += ev->d_joy;
        
        add_log(age, ev->text, ev->d_chr, ev->d_int, ev->d_str, ev->d_mny, ev->d_joy);
        
        if (ev->is_die || s_life_state.str <= 0) {
            finalize_life_over(ev->text);
            return;
        }
    } else {
        // 无特定事件时的平淡年华
        add_log(age, "这一年平淡无奇，你在柴米油盐中感受岁月的流逝。", 0, 0, 0, 0, 0);
    }
    
    // 检查是否在100岁激活修仙（如果有神秘小盒子）
    if (age == 100) {
        bool has_box = false;
        for (int t = 0; t < 3; t++) {
            if (s_life_state.selected_talents[t] == 0) { // 0号为神秘小盒子
                has_box = true;
                break;
            }
        }
        if (has_box && s_life_state.str >= 5) {
            s_life_state.is_cultivator = true;
        }
    }
}

void buddy_life_init(void) {
    memset(&s_life_state, 0, sizeof(s_life_state));
    s_life_state.phase = LIFE_PHASE_TITLE;
    s_life_state.best_age = 0;
    s_life_state.best_title_idx = LIFE_TITLE_COUNT - 1;
}

void buddy_life_reset(void) {
    uint16_t keep_best_age = s_life_state.best_age;
    uint8_t keep_best_title = s_life_state.best_title_idx;
    memset(&s_life_state, 0, sizeof(s_life_state));
    s_life_state.best_age = keep_best_age;
    s_life_state.best_title_idx = keep_best_title;
    s_life_state.phase = LIFE_PHASE_TITLE;
}

static void start_talent_selection(void) {
    s_life_state.phase = LIFE_PHASE_TALENT;
    roll_talents();
}

static void start_attribute_allocation(void) {
    s_life_state.phase = LIFE_PHASE_ALLOC;
    s_life_state.alloc_cursor = 0;
    s_life_state.alloc_pts[0] = 5; // 默认分配各5点
    s_life_state.alloc_pts[1] = 5;
    s_life_state.alloc_pts[2] = 5;
    s_life_state.alloc_pts[3] = 5;
    s_life_state.remain_pts = 0;   // 20 - 20 = 0
}

static void start_gameplay(void) {
    s_life_state.phase = LIFE_PHASE_PLAY;
    s_life_state.age = 0;
    s_life_state.log_count = 0;
    s_life_state.scroll_ofs = 0;
    s_life_state.is_cultivator = false;
    s_life_state.is_cyber = false;
    s_life_state.is_dead = false;
    
    // 结算基础属性
    s_life_state.chr = s_life_state.alloc_pts[0];
    s_life_state.int_val = s_life_state.alloc_pts[1];
    s_life_state.str = s_life_state.alloc_pts[2];
    s_life_state.mny = s_life_state.alloc_pts[3];
    s_life_state.joy = 5; // 基础初始快乐度5
    
    // 应用3个天赋的属性加成
    for (int i = 0; i < 3; i++) {
        uint8_t tid = s_life_state.selected_talents[i];
        const life_talent_def_t *t = &c_life_talents[tid];
        s_life_state.chr += t->chr;
        s_life_state.int_val += t->int_val;
        s_life_state.str += t->str;
        s_life_state.mny += t->mny;
        s_life_state.joy += t->joy;
        if (tid == 1) { // 赛博意识流
            s_life_state.is_cyber = true;
        }
    }
    
    // 生成0岁事件
    trigger_year_event();
}

void buddy_life_key(buddy_key_t key) {
    switch (s_life_state.phase) {
        case LIFE_PHASE_TITLE: {
            if (key == BUDDY_KEY_OK) {
                start_talent_selection();
            }
            break;
        }
        case LIFE_PHASE_TALENT: {
            if (key == BUDDY_KEY_UP) {
                if (s_life_state.talent_cursor > 0) {
                    s_life_state.talent_cursor--;
                } else {
                    s_life_state.talent_cursor = 10; // 循环到底部[踏入轮回]
                }
            } else if (key == BUDDY_KEY_DOWN) {
                if (s_life_state.talent_cursor < 10) {
                    s_life_state.talent_cursor++;
                } else {
                    s_life_state.talent_cursor = 0;
                }
            } else if (key == BUDDY_KEY_OK) {
                if (s_life_state.talent_cursor < 10) {
                    uint8_t idx = s_life_state.talent_cursor;
                    if (s_life_state.talent_checked[idx]) {
                        // 取消选择
                        s_life_state.talent_checked[idx] = false;
                        s_life_state.talent_selected_count--;
                    } else {
                        // 选择
                        if (s_life_state.talent_selected_count < 3) {
                            s_life_state.talent_checked[idx] = true;
                            s_life_state.talent_selected_count++;
                            if (s_life_state.talent_selected_count == 3) {
                                s_life_state.talent_cursor = 10; // 选满直接跳转到[踏入轮回]
                            }
                        }
                    }
                } else {
                    // 点击[踏入轮回]
                    if (s_life_state.talent_selected_count == 3) {
                        int sel = 0;
                        for (int i = 0; i < 10; i++) {
                            if (s_life_state.talent_checked[i] && sel < 3) {
                                s_life_state.selected_talents[sel++] = s_life_state.talent_pool[i];
                            }
                        }
                        start_attribute_allocation();
                    }
                }
            }
            break;
        }
        case LIFE_PHASE_ALLOC: {
            if (key == BUDDY_KEY_UP) {
                if (s_life_state.alloc_cursor < 4) {
                    if (s_life_state.remain_pts > 0 && s_life_state.alloc_pts[s_life_state.alloc_cursor] < 10) {
                        s_life_state.alloc_pts[s_life_state.alloc_cursor]++;
                        s_life_state.remain_pts--;
                    }
                } else {
                    s_life_state.alloc_cursor = 3;
                }
            } else if (key == BUDDY_KEY_DOWN) {
                if (s_life_state.alloc_cursor < 4) {
                    if (s_life_state.alloc_pts[s_life_state.alloc_cursor] > 0) {
                        s_life_state.alloc_pts[s_life_state.alloc_cursor]--;
                        s_life_state.remain_pts++;
                    }
                } else {
                    s_life_state.alloc_cursor = 0;
                }
            } else if (key == BUDDY_KEY_OK) {
                if (s_life_state.alloc_cursor < 4) {
                    s_life_state.alloc_cursor++;
                } else {
                    // [开始人生]
                    start_gameplay();
                }
            }
            break;
        }
        case LIFE_PHASE_PLAY: {
            if (key == BUDDY_KEY_OK) {
                s_life_state.age++;
                trigger_year_event();
            } else if (key == BUDDY_KEY_UP) {
                // 向上翻看历史记录
                if (s_life_state.log_count > 4 && s_life_state.scroll_ofs < (s_life_state.log_count - 4)) {
                    s_life_state.scroll_ofs++;
                }
            } else if (key == BUDDY_KEY_DOWN) {
                // 向下翻看历史记录
                if (s_life_state.scroll_ofs > 0) {
                    s_life_state.scroll_ofs--;
                }
            }
            break;
        }
        case LIFE_PHASE_CHOICE: {
            if (key == BUDDY_KEY_UP || key == BUDDY_KEY_DOWN) {
                s_life_state.choice_sel = (s_life_state.choice_sel == 0) ? 1 : 0;
            } else if (key == BUDDY_KEY_OK) {
                const life_choice_def_t *c = &c_life_choices[s_life_state.cur_choice_idx];
                if (s_life_state.choice_sel == 0) {
                    s_life_state.int_val += c->da_int;
                    s_life_state.mny += c->da_mny;
                    s_life_state.joy += c->da_joy;
                    s_life_state.str += c->da_str;
                    if (c->da_cyber) s_life_state.is_cyber = true;
                    add_log(s_life_state.age, c->res_a, 0, c->da_int, c->da_str, c->da_mny, c->da_joy);
                } else {
                    s_life_state.int_val += c->db_int;
                    s_life_state.mny += c->db_mny;
                    s_life_state.joy += c->db_joy;
                    s_life_state.str += c->db_str;
                    add_log(s_life_state.age, c->res_b, 0, c->db_int, c->db_str, c->db_mny, c->db_joy);
                }
                s_life_state.phase = LIFE_PHASE_PLAY;
            }
            break;
        }
        case LIFE_PHASE_OVER: {
            if (key == BUDDY_KEY_OK) {
                start_talent_selection();
            }
            break;
        }
        default:
            break;
    }
}

void buddy_life_double_key(void) {
    // 双击OK键可以在PLAY阶段连进3年，体验飙车快感
    if (s_life_state.phase == LIFE_PHASE_PLAY && !s_life_state.is_dead) {
        for (int i = 0; i < 3; i++) {
            if (s_life_state.phase != LIFE_PHASE_PLAY || s_life_state.is_dead) break;
            s_life_state.age++;
            trigger_year_event();
        }
    }
}

void buddy_life_tick(void) {
    // 游戏为事件回合驱动，tick预留
}

const buddy_game_life_t *buddy_life_get_state(void) {
    return &s_life_state;
}
