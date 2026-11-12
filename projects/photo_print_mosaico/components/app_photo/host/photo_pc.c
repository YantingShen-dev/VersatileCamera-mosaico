#include "app_photo.h"
#include "gsp_sim_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Simulation owns substitutes; app_photo and photo_bitmap are shared device code. */
static struct {
    app_photo_handle_t ui;
    photo_state_t state;
    uint8_t *bgr, *bits;
    uint16_t *color, *mono;
    unsigned sending_ticks, prints;
    bool fail;
} backend;
static void release(void *ctx) { (void)ctx; }
static bool poll(void *ctx, photo_state_t *out) {
    (void)ctx;
    const char *path = getenv("PHOTO_SIM_STATE");
    if (path) {
        FILE *file = fopen(path, "r");
        if (file) {
            char text[32] = {0};
            fgets(text, sizeof(text), file);
            fclose(file);
            backend.state.connected = strncmp(text, "offline", 7) != 0;
            backend.fail = strncmp(text, "fail", 4) == 0;
        }
    }
    if (backend.sending_ticks && !--backend.sending_ticks) {
        backend.state.mode = backend.fail ? PHOTO_FAILED : PHOTO_SENT;
        fprintf(stderr, "PHOTO_SIM completed mode=%d prints=%u\n", backend.state.mode, backend.prints);
    }
    if (path) {
        char output[512];
        snprintf(output, sizeof(output), "%s.result", path);
        FILE *file = fopen(output, "w");
        if (file) {
            fprintf(file, "%d %u %lu %d", backend.state.mode, backend.prints,
                    (unsigned long)backend.state.response, backend.state.connected);
            fclose(file);
        }
    }
    *out = backend.state;
    return true;
}
static bool command(void *ctx, photo_command_t cmd) {
    (void)ctx;
    ++backend.state.response;
    if (cmd == PHOTO_CAPTURE) {
        photo_dither(backend.bgr, backend.bits);
        photo_bitmap_preview(backend.bits, backend.mono);
        app_photo_present(backend.ui, backend.mono, release, NULL);
        backend.state.mode = PHOTO_REVIEW;
    }
    if (cmd == PHOTO_RETAKE) {
        app_photo_present(backend.ui, backend.color, release, NULL);
        backend.state.mode = PHOTO_LIVE;
    }
    if (cmd == PHOTO_PRINT) {
        backend.state.mode = PHOTO_SENDING;
        backend.sending_ticks = 20;
        ++backend.prints;
    }
    fprintf(stderr, "PHOTO_SIM command=%d mode=%d prints=%u response=%lu\n", cmd, backend.state.mode,
            backend.prints, (unsigned long)backend.state.response);
    return true;
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui) {
    backend.state.mode = PHOTO_LIVE;
    backend.state.connected = true;
    backend.bgr = calloc(PHOTO_SIDE * PHOTO_SIDE, 3);
    backend.bits = calloc(PHOTO_BYTES, 1);
    backend.color = calloc(PHOTO_SIDE * PHOTO_SIDE, 2);
    backend.mono = calloc(PHOTO_SIDE * PHOTO_SIDE, 2);
    if (!backend.bgr || !backend.bits || !backend.color || !backend.mono)
        return ESP_GSP_ERR_NO_MEM;
    for (unsigned y = 0; y < PHOTO_SIDE; ++y)
        for (unsigned x = 0; x < PHOTO_SIDE; ++x) {
            unsigned i = y * PHOTO_SIDE + x;
            uint8_t *p = backend.bgr + i * 3;
            p[0] = x * 255 / 319;
            p[1] = y * 255 / 319;
            p[2] = 180;
            backend.color[i] = ((p[2] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[0] >> 3);
        }
    photo_provider_t provider = {.poll = poll, .command = command};
    esp_gsp_err_t err = app_photo_create(ui, &provider, &backend.ui);
    if (err)
        return err;
    return app_photo_present(backend.ui, backend.color, release, NULL);
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui) {
    (void)ui;
    app_photo_destroy(&backend.ui);
    free(backend.bgr);
    free(backend.bits);
    free(backend.color);
    free(backend.mono);
}
