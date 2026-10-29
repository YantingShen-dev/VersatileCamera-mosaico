// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_gsp.h"
#ifdef __cplusplus
extern "C" {
#endif
#define FACE_MAX_PEOPLE 5
#define FACE_PREVIEW_W 440
#define FACE_PREVIEW_H 264
typedef enum {
    FACE_LOADING, FACE_NO_FACE, FACE_UNKNOWN, FACE_KNOWN, FACE_MULTIPLE,
    FACE_TOO_SMALL, FACE_ENROLLED, FACE_DUPLICATE, FACE_FULL, FACE_DELETED,
    FACE_STORAGE_ERROR, FACE_CAMERA_ERROR, FACE_MODEL_ERROR, FACE_WORKING
} face_status_t;
typedef enum { FACE_CMD_ENROLL, FACE_CMD_DELETE } face_command_t;
typedef struct {
    face_status_t status;
    uint32_t response;
    uint32_t elapsed_ms;
    uint8_t slots;
    uint8_t person;
    bool ready;
} face_snapshot_t;
typedef struct {
    void *ctx;
    bool (*poll)(void *ctx, face_snapshot_t *out);
    bool (*command)(void *ctx, face_command_t command, uint8_t person);
} face_provider_t;
typedef struct app_face *app_face_handle_t;
/* Creation and destruction require a live GSP runtime; callbacks execute on
 * its render thread. Provider poll/command must be nonblocking. See README. */
esp_gsp_err_t app_face_create(esp_gsp_handle_t ui, const face_provider_t *provider,
                              app_face_handle_t *out);
esp_gsp_err_t app_face_destroy(app_face_handle_t *handle);
/* Any producer task may present. Accepted pixels remain borrowed until release. */
esp_gsp_err_t app_face_present(app_face_handle_t handle, const uint16_t *pixels,
                               esp_gsp_canvas_release_cb_t release, void *ctx);
#ifdef __cplusplus
}
#endif
