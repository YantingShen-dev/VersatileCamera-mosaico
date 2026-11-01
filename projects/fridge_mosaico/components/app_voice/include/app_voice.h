#pragma once
#include "app_fridge.h"
#include "app_cloud.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_voice *app_voice_handle_t;
typedef struct {
    void *ctx;
    bool (*deliver)(void *, const fridge_command_t *);
    int (*volume)(void *);
} app_voice_callbacks_t;
/* Initializes both codecs before Wi-Fi; audio workers own all codec access. */
esp_err_t app_voice_start(app_voice_handle_t *out, const app_voice_callbacks_t *callbacks);
void app_voice_connect(app_voice_handle_t self, app_cloud_handle_t cloud);
/* Publishes camera/correction state; only entry prompts are spoken. No synthesis on UI thread. */
void app_voice_publish(app_voice_handle_t self, const fridge_snapshot_t *snapshot);
void app_voice_test(app_voice_handle_t self);
void app_voice_diagnostics(app_voice_handle_t self, cJSON *result);
#ifdef __cplusplus
}
#endif
