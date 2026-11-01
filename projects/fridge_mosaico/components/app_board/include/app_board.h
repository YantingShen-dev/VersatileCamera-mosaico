// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "app_fridge.h"
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct app_board *app_board_handle_t;
/* Application-lifetime adapter. Create before the UI, then start after its
 * controller exists. Call poll/publish only on the GSP controller thread. */
esp_err_t app_board_create(app_board_handle_t *out, fridge_platform_t *platform);
esp_err_t app_board_start(app_board_handle_t board, app_fridge_handle_t controller);
#ifdef __cplusplus
}
#endif
