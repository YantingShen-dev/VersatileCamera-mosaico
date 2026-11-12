#pragma once
#include "esp_gsp.h"
#include "photo_control.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
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
void photo_filter_apply(const uint8_t *bgr, uint8_t *bits, photo_filter_t filter);
void photo_bitmap_preview(const uint8_t *bits, uint16_t *rgb565);
#ifdef __cplusplus
}
#endif
