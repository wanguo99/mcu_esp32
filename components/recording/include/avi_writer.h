#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_camera.h"

typedef struct {
    FILE *video;
    FILE *index;
    long riff_size_offset;
    long avih_offset;
    long strh_offset;
    long strf_offset;
    long movi_size_offset;
    long movi_data_offset;
    uint32_t frames;
    uint32_t max_frame_size;
    uint16_t width;
    uint16_t height;
    char final_path[96];
    char temporary_path[104];
    char index_path[108];
} avi_writer_t;

void avi_writer_reset(avi_writer_t *writer);
bool avi_writer_open(avi_writer_t *writer, const camera_fb_t *frame);
bool avi_writer_write_frame(avi_writer_t *writer, const camera_fb_t *frame);
bool avi_writer_would_exceed_limit(const avi_writer_t *writer, size_t frame_len);
bool avi_writer_finalize(avi_writer_t *writer);
bool avi_writer_repair_legacy_index(const char *path);
void avi_writer_abort(avi_writer_t *writer);
bool avi_writer_is_open(const avi_writer_t *writer);
const char *avi_writer_path(const avi_writer_t *writer);
uint32_t avi_writer_frame_count(const avi_writer_t *writer);
