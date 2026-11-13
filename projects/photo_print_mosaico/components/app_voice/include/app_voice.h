#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_voice *app_voice_handle_t;
typedef void (*photo_voice_callback_t)(void *context, uint64_t timestamp_ms);
/* Call after the shared BSP audio path is initialized at 16kHz mono. */
esp_err_t app_voice_start(photo_voice_callback_t callback, void *context, app_voice_handle_t *out);
bool app_voice_ready(app_voice_handle_t voice);
void app_voice_listen(app_voice_handle_t voice, bool enabled);
/* Stops microphone/recognizer before the shared speaker path is closed. */
bool app_voice_stop(app_voice_handle_t voice);
#ifdef __cplusplus
}
#endif
