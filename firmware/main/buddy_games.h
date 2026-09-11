#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "buddy_types.h"

#define SNAKE_GRID_W 24
#define SNAKE_GRID_H 24
#define SNAKE_MAX_LEN 128

typedef enum {
    SNAKE_DIR_UP = 0,
    SNAKE_DIR_RIGHT,
    SNAKE_DIR_DOWN,
    SNAKE_DIR_LEFT,
} snake_dir_t;

typedef struct {
    int8_t x;
    int8_t y;
} snake_point_t;

typedef struct {
    snake_point_t body[SNAKE_MAX_LEN];
    uint16_t length;
    snake_dir_t dir;
    snake_point_t food;
    uint32_t score;
    uint32_t high_score;
    bool game_over;
    bool paused;
} buddy_game_snake_t;

#define DINO_OBSTACLE_MAX 3

typedef struct {
    int16_t x;
    int16_t w;
    int16_t h;
    bool active;
} dino_obstacle_t;

typedef struct {
    int16_t y;          /* 恐龙相对地面高度 (0=地面) */
    int16_t vy;         /* Y 轴速度 */
    bool is_jumping;
    dino_obstacle_t obstacles[DINO_OBSTACLE_MAX];
    uint32_t score;
    uint32_t high_score;
    bool game_over;
    uint32_t tick_count;
} buddy_game_dino_t;

void buddy_games_init(void);

/* 贪吃蛇 API */
void buddy_snake_reset(void);
void buddy_snake_key(buddy_key_t key);
void buddy_snake_tick(void);
const buddy_game_snake_t *buddy_snake_get_state(void);

/* 跳跳恐龙 API */
void buddy_dino_reset(void);
void buddy_dino_key(buddy_key_t key);
void buddy_dino_tick(void);
const buddy_game_dino_t *buddy_dino_get_state(void);
