#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct mock_timer *mcpwm_timer_handle_t;
typedef struct mock_operator *mcpwm_oper_handle_t;
typedef struct mock_comparator *mcpwm_cmpr_handle_t;
typedef struct mock_generator *mcpwm_gen_handle_t;
#define MCPWM_TIMER_CLK_SRC_DEFAULT 0
#define MCPWM_TIMER_COUNT_MODE_UP 0
#define MCPWM_TIMER_DIRECTION_UP 0
#define MCPWM_TIMER_EVENT_EMPTY 0
#define MCPWM_GEN_ACTION_HIGH 1
#define MCPWM_GEN_ACTION_LOW 0
#define MCPWM_TIMER_START_NO_STOP 1
#define MCPWM_TIMER_STOP_EMPTY 0
typedef struct { int group_id,clk_src; uint32_t resolution_hz,period_ticks; int count_mode; } mcpwm_timer_config_t;
typedef struct { int group_id; } mcpwm_operator_config_t;
typedef struct { struct { unsigned update_cmp_on_tez:1; } flags; } mcpwm_comparator_config_t;
typedef struct { int gen_gpio_num; } mcpwm_generator_config_t;
typedef struct { int direction,event,action; } mcpwm_gen_timer_event_action_t;
typedef struct { int direction; mcpwm_cmpr_handle_t comparator; int action; } mcpwm_gen_compare_event_action_t;
#define MCPWM_GEN_TIMER_EVENT_ACTION(d,e,a) ((mcpwm_gen_timer_event_action_t){d,e,a})
#define MCPWM_GEN_COMPARE_EVENT_ACTION(d,c,a) ((mcpwm_gen_compare_event_action_t){d,c,a})
esp_err_t mcpwm_new_timer(const mcpwm_timer_config_t *,mcpwm_timer_handle_t *);
esp_err_t mcpwm_del_timer(mcpwm_timer_handle_t);
esp_err_t mcpwm_new_operator(const mcpwm_operator_config_t *,mcpwm_oper_handle_t *);
esp_err_t mcpwm_del_operator(mcpwm_oper_handle_t);
esp_err_t mcpwm_operator_connect_timer(mcpwm_oper_handle_t,mcpwm_timer_handle_t);
esp_err_t mcpwm_new_comparator(mcpwm_oper_handle_t,const mcpwm_comparator_config_t *,mcpwm_cmpr_handle_t *);
esp_err_t mcpwm_del_comparator(mcpwm_cmpr_handle_t);
esp_err_t mcpwm_new_generator(mcpwm_oper_handle_t,const mcpwm_generator_config_t *,mcpwm_gen_handle_t *);
esp_err_t mcpwm_del_generator(mcpwm_gen_handle_t);
esp_err_t mcpwm_generator_set_force_level(mcpwm_gen_handle_t,int,bool);
esp_err_t mcpwm_comparator_set_compare_value(mcpwm_cmpr_handle_t,uint32_t);
esp_err_t mcpwm_generator_set_action_on_timer_event(mcpwm_gen_handle_t,mcpwm_gen_timer_event_action_t);
esp_err_t mcpwm_generator_set_action_on_compare_event(mcpwm_gen_handle_t,mcpwm_gen_compare_event_action_t);
esp_err_t mcpwm_timer_enable(mcpwm_timer_handle_t);
esp_err_t mcpwm_timer_disable(mcpwm_timer_handle_t);
esp_err_t mcpwm_timer_start_stop(mcpwm_timer_handle_t,int);
