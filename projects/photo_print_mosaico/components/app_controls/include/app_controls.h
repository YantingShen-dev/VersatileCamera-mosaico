#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_controls *app_controls_handle_t;
typedef void (*photo_button_callback_t)(void *ctx, unsigned button, uint64_t timestamp_ms);
esp_err_t app_controls_start(photo_button_callback_t cb, void *ctx, app_controls_handle_t *out);
bool app_controls_board_ready(app_controls_handle_t c);
bool app_controls_audio_ready(app_controls_handle_t c);
void app_controls_sound(app_controls_handle_t c, bool shutter);
/* Single owner requests shutdown; no callbacks after true is returned. */
bool app_controls_stop(app_controls_handle_t c);
#ifdef __cplusplus
}
#endif
