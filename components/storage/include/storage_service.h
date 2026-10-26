#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    bool sd_ready;
    uint64_t sd_total_bytes, sd_free_bytes;
    char sd_error[64];
} storage_status_t;
typedef struct { char name[128]; uint64_t size_bytes; } storage_file_info_t;
typedef esp_err_t (*storage_chunk_callback_t)(const uint8_t *, size_t, void *);
esp_err_t storage_service_init(void);
esp_err_t storage_service_check_sd(void);
void storage_service_get_status(storage_status_t *status);
bool storage_service_is_busy(void);
esp_err_t storage_service_save_photo(const uint8_t *data, size_t length, char *path, size_t path_size);
esp_err_t storage_service_list_files(storage_file_info_t *, size_t capacity, size_t *count, bool *truncated);
esp_err_t storage_service_delete_file(const char *name);
esp_err_t storage_service_stream_file(const char *name, storage_chunk_callback_t callback, void *context);
// A session reserves storage against downloads/checks while allowing synchronized photo writes.
esp_err_t storage_service_claim_session(void);
void storage_service_release_session(void);
// Low-level writers must hold this lock across mount, file IO, and unmount.
bool storage_service_lock(TickType_t wait);
void storage_service_unlock(void);
esp_err_t storage_service_mount_locked(void);
void storage_service_unmount_locked(void);
