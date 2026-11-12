#pragma once
#include "esp_gsp.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PHOTO_SIDE 320
#define PHOTO_BYTES (PHOTO_SIDE * PHOTO_SIDE / 8)
typedef enum {
    PHOTO_LOADING,
    PHOTO_LIVE,
    PHOTO_REVIEW,
    PHOTO_SENDING,
    PHOTO_SENT,
    PHOTO_FAILED,
    PHOTO_CAMERA_ERROR
} photo_mode_t;
typedef enum { PHOTO_CAPTURE, PHOTO_RETAKE, PHOTO_PRINT } photo_command_t;
typedef struct {
    photo_mode_t mode;
    bool connected;
    uint32_t response;
    unsigned progress;
} photo_state_t;
typedef struct {
    void *ctx;
    bool (*poll)(void *, photo_state_t *);
    bool (*command)(void *, photo_command_t);
} photo_provider_t;
typedef struct app_photo *app_photo_handle_t;
esp_gsp_err_t app_photo_create(esp_gsp_handle_t ui, const photo_provider_t *provider,
                               app_photo_handle_t *out);
esp_gsp_err_t app_photo_present(app_photo_handle_t app, const uint16_t *pixels,
                                esp_gsp_canvas_release_cb_t release, void *ctx);
esp_gsp_err_t app_photo_destroy(app_photo_handle_t *app);
/* BGR888 -> MSB-first black bits; fixed-size buffers, no allocation. */
void photo_dither(const uint8_t *bgr, uint8_t *bits);
void photo_bitmap_preview(const uint8_t *bits, uint16_t *rgb565);
#ifdef __cplusplus
}
#endif
