// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_face.h"
#include "gsp_sim_bridge.h"

/* Host-only camera/model substitute; the real UI/controller is shared. */
static struct {
    app_face_handle_t controller;
    face_snapshot_t state;
    unsigned hold;
    uint16_t *preview;
} backend;

static bool poll_state(void *ctx, face_snapshot_t *out)
{
    (void)ctx;
    if (backend.hold) {
        --backend.hold;
    } else {
        backend.state.status = backend.state.slots & 1U ? FACE_KNOWN : FACE_UNKNOWN;
        backend.state.person = backend.state.slots & 1U ? 1 : 0;
        const char *file = getenv("FACE_SIM_STATE");
        if (file) {
            FILE *input = fopen(file, "r");
            if (input) {
                char state[24] = {0};
                if (fgets(state, sizeof(state), input)) {
                    if (strncmp(state, "no_face", 7) == 0) {
                        backend.state.status = FACE_NO_FACE;
                    }
                    if (strncmp(state, "multiple", 8) == 0) {
                        backend.state.status = FACE_MULTIPLE;
                    }
                    if (strncmp(state, "small", 5) == 0) {
                        backend.state.status = FACE_TOO_SMALL;
                    }
                    if (strncmp(state, "unknown", 7) == 0) {
                        backend.state.status = FACE_UNKNOWN;
                        backend.state.person = 0;
                    }
                }
                fclose(input);
            }
        }
    }
    *out = backend.state;
    return true;
}

static bool command(void *ctx, face_command_t kind, uint8_t person)
{
    (void)ctx;
    ++backend.state.response;
    if (kind == FACE_CMD_DELETE) {
        if (person >= 1 && person <= FACE_MAX_PEOPLE) {
            backend.state.slots &= ~(1U << (person - 1));
            backend.state.status = FACE_DELETED;
            backend.state.person = 0;
        }
    } else if (backend.state.status == FACE_NO_FACE || backend.state.status == FACE_MULTIPLE ||
               backend.state.status == FACE_TOO_SMALL) {
        /* Reject invalid capture, same visible response as the device worker. */
    } else if (backend.state.person) {
        backend.state.status = FACE_DUPLICATE;
    } else if (backend.state.slots == 31) {
        backend.state.status = FACE_FULL;
    } else {
        unsigned i = 0;
        while (backend.state.slots & (1U << i)) {
            ++i;
        }
        backend.state.slots |= 1U << i;
        backend.state.person = i + 1;
        backend.state.status = FACE_ENROLLED;
    }
    backend.hold = 14;
    fprintf(stderr, "FACE_SIM command=%d person=%u slots=%u status=%d response=%u\n",
            kind, person, backend.state.slots, backend.state.status, backend.state.response);
    return true;
}

static void release_preview(void *ctx)
{
    (void)ctx;
}

esp_gsp_err_t gsp_bridge_app_init(esp_gsp_handle_t ui)
{
    backend.state.ready = true;
    backend.state.elapsed_ms = 180;
    backend.state.status = FACE_UNKNOWN;
    face_provider_t provider = {.poll = poll_state, .command = command};
    esp_gsp_err_t err = app_face_create(ui, &provider, &backend.controller);
    if (err != ESP_GSP_OK) {
        return err;
    }
    backend.preview = calloc(FACE_PREVIEW_W * FACE_PREVIEW_H, sizeof(uint16_t));
    if (!backend.preview) {
        return ESP_GSP_ERR_NO_MEM;
    }
    for (unsigned y = 0; y < FACE_PREVIEW_H; ++y) {
        for (unsigned x = 0; x < FACE_PREVIEW_W; ++x) {
            uint16_t color = (x % 32 == 0 || y % 32 == 0) ? 0x31E8 : 0x2127;
            int dx = (int)x - 220;
            int dy = (int)y - 100;
            if (dx * dx * 2 + dy * dy < 5000) {
                color = 0x52EA;
            }
            if (x >= 148 && x <= 292 && y >= 24 && y <= 180 &&
                    (x < 151 || x > 289 || y < 27 || y > 177)) {
                color = 0xFDA0;
            }
            backend.preview[y * FACE_PREVIEW_W + x] = color;
        }
    }
    return app_face_present(backend.controller, backend.preview, release_preview, NULL);
}

void gsp_bridge_app_deinit(esp_gsp_handle_t ui)
{
    (void)ui;
    fprintf(stderr, "FACE_SIM final slots=%u response=%u\n", backend.state.slots, backend.state.response);
    app_face_destroy(&backend.controller);
    free(backend.preview);
}
