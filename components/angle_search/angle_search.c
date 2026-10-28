#include "angle_search.h"
#include <math.h>
#include <string.h>
bool angle_search_active(const angle_search_t *s) {
    return s && s->phase>=ANGLE_SEARCH_MOVE && s->phase<=ANGLE_SEARCH_RETURN;
}
void angle_search_cancel(angle_search_t *s) { if(s) s->phase=ANGLE_SEARCH_IDLE; }
esp_err_t angle_search_start(angle_search_t *s,const angle_search_config_t *c) {
    if(!s || !c || !isfinite(c->min_angle) || !isfinite(c->max_angle) || c->min_angle>=c->max_angle ||
       !isfinite(c->coarse_step) || !isfinite(c->fine_step) || c->coarse_step<=0 || c->fine_step<=0 || c->fine_step>c->coarse_step ||
       (c->max_angle-c->min_angle)/c->coarse_step>120 || 2*c->coarse_step/c->fine_step>120 ||
       !isfinite(c->min_score) || !isfinite(c->improvement) || c->improvement<0 ||
       !c->samples || c->samples>64 || c->timeout_ms<1000 || c->settle_ms>=c->timeout_ms) return ESP_ERR_INVALID_ARG;
    memset(s,0,sizeof(*s)); s->config=*c; s->phase=ANGLE_SEARCH_MOVE;
    s->range_min=c->min_angle; s->range_max=c->max_angle; s->step=c->coarse_step; s->angle=c->min_angle;
    return ESP_OK;
}
bool angle_search_tick(angle_search_t *s,int64_t now,bool moving,bool valid,float score,int64_t sample,float *target) {
    if(!s || !target || !angle_search_active(s)) return false;
    if(s->phase==ANGLE_SEARCH_MOVE) {
        *target=s->angle; s->phase=ANGLE_SEARCH_SETTLE; s->settling=false;
        s->deadline_us=now+(int64_t)s->config.timeout_ms*1000; return true;
    }
    if(now>s->deadline_us) { s->phase=ANGLE_SEARCH_FAILED; s->error=ESP_ERR_TIMEOUT; return false; }
    if(s->phase==ANGLE_SEARCH_RETURN) { if(!moving) s->phase=ANGLE_SEARCH_DONE; return false; }
    if(s->phase==ANGLE_SEARCH_SETTLE) {
        if(moving) return false;
        if(!s->settling) { s->settling=true; s->last_sample_us=now+(int64_t)s->config.settle_ms*1000; }
        if(now<s->last_sample_us) return false;
        s->phase=ANGLE_SEARCH_SAMPLE; s->count=0; s->sum=0;
    }
    if(!valid || !isfinite(score) || sample<=s->last_sample_us || sample>now) return false;
    s->last_sample_us=sample; s->sum+=score;
    if(++s->count<s->config.samples) return false;
    float mean=s->sum/s->count;
    if(s->point_count<ANGLE_SEARCH_MAX_POINTS) s->points[s->point_count++]=(angle_search_point_t){s->angle,mean};
    if(!s->have_best || mean>s->best_score+s->config.improvement) {
        s->have_best=true; s->best_score=mean; s->best_angle=s->angle;
    }
    if(s->angle<s->range_max) { s->angle=fminf(s->range_max,s->angle+s->step); s->phase=ANGLE_SEARCH_MOVE; return false; }
    if(s->best_score<s->config.min_score) { s->phase=ANGLE_SEARCH_FAILED; s->error=ESP_ERR_NOT_FOUND; return false; }
    if(!s->fine) {
        s->fine=true; s->range_min=fmaxf(s->config.min_angle,s->best_angle-s->config.coarse_step);
        s->range_max=fminf(s->config.max_angle,s->best_angle+s->config.coarse_step);
        s->step=s->config.fine_step; s->angle=s->range_min; s->phase=ANGLE_SEARCH_MOVE; return false;
    }
    *target=s->best_angle; s->phase=ANGLE_SEARCH_RETURN; s->deadline_us=now+(int64_t)s->config.timeout_ms*1000;
    return true;
}
