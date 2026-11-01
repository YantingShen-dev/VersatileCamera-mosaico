#pragma once
#include "app_fridge.h"
#include "esp_err.h"
#include "cJSON.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_cloud *app_cloud_handle_t;
typedef struct {
    void *ctx;
    bool (*active)(void *ctx, uint32_t session, uint32_t capture, unsigned page);
    bool (*deliver)(void *ctx, const fridge_command_t *command);
} app_cloud_callbacks_t;
esp_err_t app_cloud_start(app_cloud_handle_t *out, const app_cloud_callbacks_t *callbacks);
/* Copies the 480x480 RGB565 image before returning. Never performs network I/O here. */
bool app_cloud_capture(app_cloud_handle_t self, const uint16_t *pixels, uint32_t session, uint32_t capture, const char *names);
/* USB provisioning: validates and saves a key in app-owned NVS; never returns it. */
esp_err_t app_cloud_configure(app_cloud_handle_t self, const cJSON *input);
void app_cloud_voice_state(app_cloud_handle_t self, const fridge_snapshot_t *snapshot);
void app_cloud_feed_audio(app_cloud_handle_t self, const int16_t *pcm, size_t samples);
void app_cloud_diagnostics(app_cloud_handle_t self, cJSON *result);
#ifdef __cplusplus
}
#endif
