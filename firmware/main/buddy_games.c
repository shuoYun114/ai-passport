#include "buddy_games.h"

#include <stdlib.h>
#include <string.h>
#include "esp_system.h"

static buddy_game_snake_t s_snake;
static buddy_game_dino_t s_dino;
static uint32_t s_rng_state = 123456789;

static uint32_t fast_rand(void)
{
    s_rng_state ^= s_rng_state << 13;
    s_rng_state ^= s_rng_state >> 17;
    s_rng_state ^= s_rng_state << 5;
    return s_rng_state;
}

static void snake_spawn_food(void)
{
    int attempts;
    for (attempts = 0; attempts < 100; ++attempts) {
        int8_t fx = (int8_t)(fast_rand() % SNAKE_GRID_W);
        int8_t fy = (int8_t)(fast_rand() % SNAKE_GRID_H);
        bool collision = false;
        for (uint16_t i = 0; i < s_snake.length; ++i) {
            if (s_snake.body[i].x == fx && s_snake.body[i].y == fy) {
                collision = true;
                break;
            }
        }
        if (!collision) {
            s_snake.food.x = fx;
            s_snake.food.y = fy;
            return;
        }
    }
    s_snake.food.x = 2;
    s_snake.food.y = 2;
}

void buddy_snake_reset(void)
{
    s_rng_state ^= (uint32_t)esp_random();
    uint32_t prev_high = s_snake.high_score;
    memset(&s_snake, 0, sizeof(s_snake));
    s_snake.high_score = prev_high;

    s_snake.length = 3;
    s_snake.dir = SNAKE_DIR_RIGHT;
    s_snake.body[0].x = SNAKE_GRID_W / 2;
    s_snake.body[0].y = SNAKE_GRID_H / 2;
    s_snake.body[1].x = s_snake.body[0].x - 1;
    s_snake.body[1].y = s_snake.body[0].y;
    s_snake.body[2].x = s_snake.body[0].x - 2;
    s_snake.body[2].y = s_snake.body[0].y;

    snake_spawn_food();
}

void buddy_snake_key(buddy_key_t key)
{
    if (s_snake.game_over) {
        if (key == BUDDY_KEY_OK) {
            buddy_snake_reset();
        }
        return;
    }

    if (key == BUDDY_KEY_UP) {
        /* 顺时针旋转 90 度 */
        s_snake.dir = (snake_dir_t)((s_snake.dir + 1) % 4);
    } else if (key == BUDDY_KEY_DOWN) {
        /* 逆时针旋转 90 度 */
        s_snake.dir = (snake_dir_t)((s_snake.dir + 3) % 4);
    } else if (key == BUDDY_KEY_OK) {
        s_snake.paused = !s_snake.paused;
    }
}

void buddy_snake_tick(void)
{
    if (s_snake.game_over || s_snake.paused) {
        return;
    }

    /* 计算下一头部坐标 */
    snake_point_t next_head = s_snake.body[0];
    switch (s_snake.dir) {
    case SNAKE_DIR_UP:
        next_head.y -= 1;
        break;
    case SNAKE_DIR_RIGHT:
        next_head.x += 1;
        break;
    case SNAKE_DIR_DOWN:
        next_head.y += 1;
        break;
    case SNAKE_DIR_LEFT:
        next_head.x -= 1;
        break;
    }

    /* 撞墙判定 */
    if (next_head.x < 0 || next_head.x >= SNAKE_GRID_W ||
        next_head.y < 0 || next_head.y >= SNAKE_GRID_H) {
        s_snake.game_over = true;
        if (s_snake.score > s_snake.high_score) {
            s_snake.high_score = s_snake.score;
        }
        return;
    }

    /* 撞自身判定 */
    for (uint16_t i = 0; i < s_snake.length; ++i) {
        if (s_snake.body[i].x == next_head.x && s_snake.body[i].y == next_head.y) {
            s_snake.game_over = true;
            if (s_snake.score > s_snake.high_score) {
                s_snake.high_score = s_snake.score;
            }
            return;
        }
    }

    /* 吃到食物判定 */
    bool ate = (next_head.x == s_snake.food.x && next_head.y == s_snake.food.y);

    /* 身体平移 */
    uint16_t new_len = s_snake.length;
    if (ate && new_len < SNAKE_MAX_LEN) {
        new_len++;
    }
    for (int i = (int)new_len - 1; i > 0; --i) {
        s_snake.body[i] = s_snake.body[i - 1];
    }
    s_snake.body[0] = next_head;
    s_snake.length = new_len;

    if (ate) {
        s_snake.score += 10;
        if (s_snake.score > s_snake.high_score) {
            s_snake.high_score = s_snake.score;
        }
        snake_spawn_food();
    }
}

const buddy_game_snake_t *buddy_snake_get_state(void)
{
    return &s_snake;
}

/* ================= 跳跳恐龙小游戏 ================= */

void buddy_dino_reset(void)
{
    s_rng_state ^= (uint32_t)esp_random();
    uint32_t prev_high = s_dino.high_score;
    memset(&s_dino, 0, sizeof(s_dino));
    s_dino.high_score = prev_high;

    s_dino.y = 0;
    s_dino.vy = 0;
    s_dino.is_jumping = false;

    s_dino.obstacles[0].x = 240;
    s_dino.obstacles[0].w = 12;
    s_dino.obstacles[0].h = 24;
    s_dino.obstacles[0].active = true;

    s_dino.obstacles[1].x = 380;
    s_dino.obstacles[1].w = 14;
    s_dino.obstacles[1].h = 20;
    s_dino.obstacles[1].active = true;

    s_dino.obstacles[2].active = false;
}

void buddy_dino_key(buddy_key_t key)
{
    if (s_dino.game_over) {
        if (key == BUDDY_KEY_OK) {
            buddy_dino_reset();
        }
        return;
    }

    if (key == BUDDY_KEY_UP || key == BUDDY_KEY_OK) {
        if (!s_dino.is_jumping && s_dino.y == 0) {
            s_dino.vy = 13;
            s_dino.is_jumping = true;
        }
    } else if (key == BUDDY_KEY_DOWN) {
        /* 加速下坠 */
        if (s_dino.is_jumping && s_dino.vy > 0) {
            s_dino.vy = -4;
        }
    }
}

void buddy_dino_tick(void)
{
    if (s_dino.game_over) {
        return;
    }

    s_dino.tick_count++;

    /* 物理重力更新 */
    if (s_dino.is_jumping || s_dino.y > 0) {
        s_dino.y += s_dino.vy;
        s_dino.vy -= 2; /* 重力加速度 */
        if (s_dino.y <= 0) {
            s_dino.y = 0;
            s_dino.vy = 0;
            s_dino.is_jumping = false;
        }
    }

    /* 障碍物平移 */
    int speed = 5 + (int)(s_dino.score / 50);
    if (speed > 10) speed = 10;

    for (int i = 0; i < DINO_OBSTACLE_MAX; ++i) {
        if (!s_dino.obstacles[i].active) continue;

        s_dino.obstacles[i].x -= speed;

        /* 碰撞检测: 恐龙在 X 轴 28~44 之间，高度 0~20 */
        const int dino_left = 28;
        const int dino_right = 44;
        const int dino_bottom = s_dino.y;
        const int dino_top = s_dino.y + 20;

        int obs_left = s_dino.obstacles[i].x;
        int obs_right = s_dino.obstacles[i].x + s_dino.obstacles[i].w;
        int obs_top = s_dino.obstacles[i].h;

        if (dino_right > obs_left && dino_left < obs_right) {
            if (dino_bottom < obs_top) {
                /* 发生碰撞，游戏结束 */
                s_dino.game_over = true;
                if (s_dino.score > s_dino.high_score) {
                    s_dino.high_score = s_dino.score;
                }
                return;
            }
        }

        /* 离开屏幕左侧，重新在右侧生成 */
        if (s_dino.obstacles[i].x < -20) {
            int max_x = 240;
            for (int j = 0; j < DINO_OBSTACLE_MAX; ++j) {
                if (s_dino.obstacles[j].active && s_dino.obstacles[j].x > max_x) {
                    max_x = s_dino.obstacles[j].x;
                }
            }
            int spacing = 120 + (int)(fast_rand() % 90);
            s_dino.obstacles[i].x = max_x + spacing;
            s_dino.obstacles[i].h = 18 + (int)(fast_rand() % 14);
            s_dino.score += 1;
            if (s_dino.score > s_dino.high_score) {
                s_dino.high_score = s_dino.score;
            }
        }
    }
}

const buddy_game_dino_t *buddy_dino_get_state(void)
{
    return &s_dino;
}

void buddy_games_init(void)
{
    buddy_snake_reset();
    buddy_dino_reset();
}
