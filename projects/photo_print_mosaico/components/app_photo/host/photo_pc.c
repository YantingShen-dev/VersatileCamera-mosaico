#include "app_photo.h"
#include "gsp_sim_bridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
static uint64_t now_ms(void) { return GetTickCount64(); }
#else
#include <time.h>
static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
#endif
static struct {
    app_photo_handle_t ui;
    photo_control_t control;
    uint8_t *bgr, *bits;
    uint16_t *mono;
    unsigned prints, beeps, shutters;
    uint64_t sent_at;
    bool fail, rendered, voice_latched;
    photo_filter_t rendered_filter;
} backend;
static void release(void *ctx) { (void)ctx; }
static void render(void) {
    photo_filter_apply(backend.bgr, backend.bits, backend.control.state.filter);
    photo_bitmap_preview(backend.bits, backend.mono);
    app_photo_present(backend.ui, backend.mono, release, NULL);
    backend.rendered = true;
    backend.rendered_filter = backend.control.state.filter;
}
static bool poll(void *ctx, photo_state_t *out) {
    (void)ctx;
    uint64_t now = now_ms();
    const char *path = getenv("PHOTO_SIM_STATE");
    bool voice = false;
    if (path) {
        FILE *file = fopen(path, "r");
        if (file) {
            char text[64] = {0};
            fgets(text, sizeof(text), file);
            fclose(file);
            backend.control.state.connected = strstr(text, "offline") == NULL;
            backend.fail = strstr(text, "fail") != NULL;
            voice = strstr(text, "voice") != NULL;
        }
    }
    if (voice && !backend.voice_latched)
        photo_control_voice(&backend.control, now);
    backend.voice_latched = voice;
    photo_control_tick(&backend.control, now);
    unsigned a = photo_control_take_actions(&backend.control);
    if (a & PHOTO_DO_BEEP)
        ++backend.beeps;
    if (a & PHOTO_DO_CAPTURE) {
        render();
        ++backend.shutters;
        photo_control_captured(&backend.control, true);
    }
    if (a & PHOTO_DO_PRINT) {
        ++backend.prints;
        backend.sent_at = now;
    }
    if (backend.control.state.mode == PHOTO_SENDING && now - backend.sent_at >= 1800)
        photo_control_printed(&backend.control, !backend.fail);
    if (!backend.rendered || backend.rendered_filter != backend.control.state.filter)
        render();
    if (path) {
        char output[512];
        snprintf(output, sizeof(output), "%s.result", path);
        FILE *f = fopen(output, "w");
        if (f) {
            fprintf(f, "%d %u %lu %d %d %u %u %u", backend.control.state.mode, backend.prints,
                    (unsigned long)backend.control.state.response, backend.control.state.connected,
                    backend.control.state.filter, backend.control.state.countdown, backend.beeps,
                    backend.shutters);
            fclose(f);
        }
    }
    *out = backend.control.state;
    return true;
}
static bool command(void *ctx, photo_command_t cmd) {
    (void)ctx;
    photo_control_button(&backend.control, cmd, now_ms());
    return true;
}
esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui) {
    photo_control_init(&backend.control);
    backend.control.state.mode = PHOTO_LIVE;
    backend.control.state.connected = true;
    backend.control.state.voice_ready = true;
    backend.control.state.audio_ready = true;
    backend.bgr = calloc(PHOTO_SIDE * PHOTO_SIDE, 3);
    backend.bits = calloc(PHOTO_BYTES, 1);
    backend.mono = calloc(PHOTO_SIDE * PHOTO_SIDE, 2);
    if (!backend.bgr || !backend.bits || !backend.mono)
        return ESP_GSP_ERR_NO_MEM;
    for (unsigned y = 0; y < PHOTO_SIDE; y++)
        for (unsigned x = 0; x < PHOTO_SIDE; x++) {
            unsigned v = 225 - y * 80 / PHOTO_SIDE;
            if (y > PHOTO_SIDE * 2 / 3)
                v = 50 + (x + y) % 70;
            int dx = (int)x - 290, dy = (int)y - 80;
            if (dx * dx + dy * dy < 27 * 27)
                v = 255;
            if (y > 230 - abs((int)x - 110) && y < 260)
                v = 75 + x / 5;
            if (x > 200 && x < 310 && y > 180 && y < 300)
                v = 120;
            if (x > 215 && x < 245 && y > 210 && y < 245)
                v = 230;
            if (x > 257 && x < 290 && y > 245 && y < 300)
                v = 25;
            uint8_t *p = backend.bgr + (y * PHOTO_SIDE + x) * 3;
            p[0] = p[1] = p[2] = (uint8_t)v;
        }
    const char *input = getenv("PHOTO_SIM_BGR");
    if (input) {
        FILE *f = fopen(input, "rb");
        if (f) {
            size_t n = fread(backend.bgr, 1, PHOTO_SIDE * PHOTO_SIDE * 3, f);
            fclose(f);
            if (n != PHOTO_SIDE * PHOTO_SIDE * 3)
                return ESP_GSP_ERR_INVALID_ARG;
        }
    }
    photo_provider_t provider = {.poll = poll, .command = command};
    return app_photo_create(ui, &provider, &backend.ui);
}
void gsp_bridge_app_deinit(esp_gsp_handle_t ui) {
    (void)ui;
    app_photo_destroy(&backend.ui);
    free(backend.bgr);
    free(backend.bits);
    free(backend.mono);
}
