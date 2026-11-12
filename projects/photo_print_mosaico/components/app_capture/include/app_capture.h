#pragma once
#include "app_photo.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_capture *app_capture_handle_t;
esp_err_t app_capture_create(app_capture_handle_t *out, photo_provider_t *provider);
esp_err_t app_capture_start(app_capture_handle_t capture, app_photo_handle_t ui);
#ifdef __cplusplus
}
#endif
