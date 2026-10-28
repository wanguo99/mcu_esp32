#pragma once
static inline void native_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
#define ESP_LOGI(...) native_log(__VA_ARGS__)
#define ESP_LOGE(...) native_log(__VA_ARGS__)
#define ESP_LOGW(...) native_log(__VA_ARGS__)
