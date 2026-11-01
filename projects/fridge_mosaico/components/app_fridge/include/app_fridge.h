// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "esp_gsp.h"
#include "fridge_store.h"
#include "fridge_settings.h"
#include "fridge_match.h"

#define FRIDGE_PREVIEW_WIDTH 480
#define FRIDGE_PREVIEW_HEIGHT 480

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FRIDGE_IN = 1, FRIDGE_OUT, FRIDGE_CAPTURE, FRIDGE_APPLY, FRIDGE_CORRECT,
    FRIDGE_FINISH, FRIDGE_UNDO, FRIDGE_ERROR, FRIDGE_INTERRUPT, FRIDGE_SAY,
    FRIDGE_RESUME, FRIDGE_SETTINGS, FRIDGE_OBSERVE, FRIDGE_SELECT, FRIDGE_MORE
} fridge_action_t;

typedef struct {
    uint32_t id, session, capture;
    fridge_action_t action;
    int32_t value;
    char text[192];
    char date[11];
    fridge_item_t items[FRIDGE_BATCH_MAX];
    uint8_t evidence[FRIDGE_BATCH_MAX];
} fridge_command_t;

typedef struct {
    fridge_store_t store;
    uint32_t ack, capture;
    uint32_t operation, feedback_id;
    unsigned feedback; /* 1 committed, 2 corrected, 3 undone; only after save */
    unsigned page; /* home, camera, busy, review, settings, networks, password */
    bool interrupted;
    char message[192];
    bool pending;
    unsigned choice_count, choice_offset;
    uint32_t choice_ids[3];
    char choice_names[3][FRIDGE_NAME_BYTES];
} fridge_snapshot_t;

typedef struct {
    void *ctx;
    bool (*load)(void *ctx, fridge_store_t *store);
    bool (*save)(void *ctx, const fridge_store_t *store);
    bool (*poll)(void *ctx, fridge_command_t *command);
    void (*publish)(void *ctx, const fridge_snapshot_t *snapshot);
    void (*settings_read)(void *ctx, fridge_settings_t *out);
    bool (*settings_request)(void *ctx, const fridge_setting_request_t *request);
} fridge_platform_t;

typedef struct app_fridge *app_fridge_handle_t;
esp_gsp_err_t app_fridge_create(esp_gsp_handle_t ui, const fridge_platform_t *platform,
                               app_fridge_handle_t *out);
/* Call from the same UI owner thread. Board button events must be queued. */
void app_fridge_dispatch(app_fridge_handle_t app, const fridge_command_t *command);
esp_gsp_err_t app_fridge_present(app_fridge_handle_t app, const uint16_t *pixels,
                                esp_gsp_canvas_release_cb_t release, void *ctx);
esp_gsp_err_t app_fridge_destroy(app_fridge_handle_t *app);
#ifdef __cplusplus
}
#endif
