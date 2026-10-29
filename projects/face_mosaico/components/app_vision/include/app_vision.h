// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "app_face.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_vision *app_vision_handle_t;
/* Create queues; no camera/model access until start. Lifetime is application boot. */
esp_err_t app_vision_create(app_vision_handle_t *out, face_provider_t *provider);
esp_err_t app_vision_start(app_vision_handle_t handle, app_face_handle_t ui);
#ifdef __cplusplus
}
#endif
