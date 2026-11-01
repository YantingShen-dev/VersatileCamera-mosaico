#pragma once
#include "esp_err.h"
#include "fridge_settings.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_settings *app_settings_handle_t;
/* Application-lifetime station owner. Start once after NVS initialization. */
esp_err_t app_settings_start(app_settings_handle_t *out);
void app_settings_read(app_settings_handle_t self, fridge_settings_t *out);
bool app_settings_request(app_settings_handle_t self, const fridge_setting_request_t *request);
unsigned app_settings_volume(app_settings_handle_t self);
#ifdef __cplusplus
}
#endif
