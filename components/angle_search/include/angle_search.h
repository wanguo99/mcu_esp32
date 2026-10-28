#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    float min_angle,max_angle,coarse_step,fine_step,min_score,improvement;
    uint32_t settle_ms,timeout_ms,samples;
} angle_search_config_t;
typedef enum { ANGLE_SEARCH_IDLE,ANGLE_SEARCH_MOVE,ANGLE_SEARCH_SETTLE,ANGLE_SEARCH_SAMPLE,ANGLE_SEARCH_RETURN,ANGLE_SEARCH_DONE,ANGLE_SEARCH_FAILED } angle_search_phase_t;
#define ANGLE_SEARCH_MAX_POINTS 256
typedef struct { float angle,score; } angle_search_point_t;
typedef struct {
    angle_search_config_t config;
    angle_search_phase_t phase;
    float angle,best_angle,best_score,range_min,range_max,step;
    double sum;
    bool fine,have_best,settling;
    unsigned count;
    int64_t deadline_us,last_sample_us;
    esp_err_t error;
    unsigned point_count;
    angle_search_point_t points[ANGLE_SEARCH_MAX_POINTS];
} angle_search_t;
esp_err_t angle_search_start(angle_search_t *s,const angle_search_config_t *c);
// No tasks, servo, radio or UI dependency. Caller serializes access and executes requested moves.
// Returns true only when a new angle must be commanded; measurements must have monotonic timestamps.
bool angle_search_tick(angle_search_t *s,int64_t now_us,bool moving,bool valid,float score,int64_t sample_us,float *target);
void angle_search_cancel(angle_search_t *s);
bool angle_search_active(const angle_search_t *s);
