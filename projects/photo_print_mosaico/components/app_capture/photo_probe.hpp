#pragma once
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "photo_control.h"
#include <atomic>
#include <cstring>

// Developer-only grayscale frame input. Iris serializes RPC callbacks; the
// camera worker exclusively consumes a sealed frame. No image reaches flash.
struct PhotoProbe {
    enum { IDLE, ALLOCATING, UPLOADING, QUEUED, RUNNING };
    std::atomic<unsigned> stage{IDLE};
    uint8_t *gray = nullptr;
    size_t received = 0;
    uint64_t expires = 0;
    static constexpr size_t bytes = PHOTO_SIDE * PHOTO_SIDE;

    static esp_err_t rpc(const esp_iris_rpc_request_t *r, uint8_t *out,
                         size_t capacity, size_t *size, void *ctx) {
        if (!r || !r->payload_size || !out || capacity < 1 || !size)
            return ESP_ERR_INVALID_ARG;
        auto *p = static_cast<PhotoProbe *>(ctx);
        const auto *data = static_cast<const uint8_t *>(r->payload);
        switch (data[0]) {
        case 0: { // Begin, fixed 384x384 grayscale image.
            if (r->payload_size != 1) return ESP_ERR_INVALID_ARG;
            unsigned expected = IDLE;
            if (!p->stage.compare_exchange_strong(expected, ALLOCATING))
                return ESP_ERR_INVALID_STATE;
            p->gray = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
            if (!p->gray) { p->stage.store(IDLE); return ESP_ERR_NO_MEM; }
            p->received = 0;
            p->stage.store(UPLOADING);
            break;
        }
        case 1: { // Append: opcode + little-endian offset + raw grayscale bytes.
            if (r->payload_size <= 5 || p->stage.load() != UPLOADING)
                return ESP_ERR_INVALID_STATE;
            uint32_t offset;
            memcpy(&offset, data + 1, sizeof(offset));
            size_t n = r->payload_size - 5;
            if (offset != p->received || n > bytes - p->received)
                return ESP_ERR_INVALID_SIZE;
            memcpy(p->gray + p->received, data + 5, n);
            p->received += n;
            break;
        }
        case 2: // Seal; the worker starts the ten-second input replacement.
            if (r->payload_size != 1 || p->stage.load() != UPLOADING || p->received != bytes)
                return ESP_ERR_INVALID_STATE;
            p->stage.store(QUEUED);
            break;
        case 3: // Abort an incomplete upload.
            if (r->payload_size != 1 || p->stage.load() != UPLOADING)
                return ESP_ERR_INVALID_STATE;
            free(p->gray); p->gray = nullptr; p->stage.store(IDLE);
            break;
        case 4: // Read state without reading mutable payload metadata.
            if (r->payload_size != 1) return ESP_ERR_INVALID_ARG;
            break;
        default: return ESP_ERR_NOT_SUPPORTED;
        }
        out[0] = static_cast<uint8_t>(p->stage.load());
        *size = 1;
        return ESP_OK;
    }
    bool begin(uint64_t now) {
        if (stage.load() != QUEUED) return false;
        expires = now + 10000;
        stage.store(RUNNING);
        return true;
    }
    void expire(uint64_t now) {
        if (stage.load() == RUNNING && now >= expires) {
            free(gray); gray = nullptr; stage.store(IDLE);
        }
    }
    bool fill(uint8_t *bgr) const {
        if (stage.load() != RUNNING) return false;
        for (size_t i = 0; i < bytes; ++i)
            bgr[3 * i] = bgr[3 * i + 1] = bgr[3 * i + 2] = gray[i];
        return true;
    }
};
