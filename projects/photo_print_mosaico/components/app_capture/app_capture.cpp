#include "app_capture.h"
#include "app_printer.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mosaico_module_camera.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
namespace {
struct Preview {
    uint16_t *pixels = nullptr;
    std::atomic<bool> free{true};
};
void released(void *p) { static_cast<Preview *>(p)->free.store(true); }
const char *TAG = "photo_capture";
} // namespace
struct app_capture {
    QueueHandle_t commands = nullptr, snapshots = nullptr;
    TaskHandle_t task = nullptr;
    app_photo_handle_t ui = nullptr;
    photo_state_t state = {};
    mosaico_camera_handle_t camera = nullptr;
    ppa_client_handle_t ppa = nullptr;
    uint8_t *bgr = nullptr, *bitmap = nullptr;
    Preview previews[3];
    ppa_srm_oper_config_t conversion = {};
    std::atomic<bool> stop{false}, stopped{false};
    static inline app_capture *shutdown_owner = nullptr;
    void publish() {
        state.connected = app_printer_ready();
        xQueueOverwrite(snapshots, &state);
    }
    static void shutdown() {
        auto *s = shutdown_owner;
        if (!s)
            return;
        s->stop.store(true);
        app_printer_cancel();
        int64_t end = esp_timer_get_time() + 8000000;
        while (!s->stopped.load() && esp_timer_get_time() < end)
            vTaskDelay(pdMS_TO_TICKS(10));
        if (!s->stopped.load())
            ESP_LOGE(TAG, "Peripheral shutdown timed out");
    }
    void close_camera() {
        if (camera) {
            mosaico_camera_stop_stream(camera);
            mosaico_camera_del(camera);
            camera = nullptr;
        }
        if (ppa) {
            ppa_unregister_client(ppa);
            ppa = nullptr;
        }
    }
    esp_err_t open_camera() {
        mosaico_camera_config_t config = MOSAICO_CAMERA_DEFAULT_CONFIG();
        config.buffer_count = 2;
        esp_err_t err = mosaico_camera_new(&config, &camera);
        if (err == ESP_OK) {
            err = mosaico_camera_open(camera);
        }
        if (err == ESP_OK) {
            err = mosaico_camera_start_stream(camera);
        }
        if (err != ESP_OK && camera) {
            mosaico_camera_del(camera);
            camera = nullptr;
        }
        return err;
    }

    esp_err_t convert(const mosaico_camera_frame_t &frame) {
        if (frame.pixel_format != V4L2_PIX_FMT_UYVY || frame.width < PHOTO_SIDE ||
            frame.height < PHOTO_SIDE) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        const unsigned stride = frame.bytes_per_line ? frame.bytes_per_line / 2 : frame.width;
        const unsigned crop =
            std::min(frame.width, frame.height) >= PHOTO_SIDE * 2 ? PHOTO_SIDE * 2 : PHOTO_SIDE;
        esp_err_t err = esp_cache_msync(const_cast<void *>(frame.data), frame.size,
                                        ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        if (err != ESP_OK) {
            return err;
        }
        // 320/640 crops make the PPA scaling factor exactly representable.
        conversion = {};
        auto *op = &conversion;
        op->in.buffer = frame.data;
        op->in.pic_w = stride;
        op->in.pic_h = frame.height;
        op->in.block_w = crop;
        op->in.block_h = crop;
        op->in.block_offset_x = ((frame.width - crop) / 2) & ~1U;
        op->in.block_offset_y = ((frame.height - crop) / 2) & ~1U;
        op->in.srm_cm = PPA_SRM_COLOR_MODE_YUV422_UYVY;
        op->in.yuv_range = PPA_COLOR_RANGE_LIMIT;
        op->in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
        op->out.buffer = bgr;
        op->out.buffer_size = PHOTO_SIDE * PHOTO_SIDE * 3;
        op->out.pic_w = PHOTO_SIDE;
        op->out.pic_h = PHOTO_SIDE;
        op->out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
        op->rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
        op->scale_x = static_cast<float>(PHOTO_SIDE) / crop;
        op->scale_y = op->scale_x;
        op->mode = PPA_TRANS_MODE_BLOCKING;
        err = ppa_do_scale_rotate_mirror(ppa, op);
        if (err == ESP_OK) {
            err = esp_cache_msync(bgr, PHOTO_SIDE * PHOTO_SIDE * 3,
                                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        }
        return err;
    }

    bool setup() {
        bgr = (uint8_t *)heap_caps_aligned_alloc(128, PHOTO_SIDE * PHOTO_SIDE * 3,
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
        bitmap = (uint8_t *)heap_caps_malloc(PHOTO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        for (auto &p : previews) {
            p.pixels = (uint16_t *)heap_caps_aligned_alloc(128, PHOTO_SIDE * PHOTO_SIDE * 2,
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!p.pixels)
                return false;
        }
        ppa_client_config_t config = {};
        config.oper_type = PPA_OPERATION_SRM;
        config.max_pending_trans_num = 1;
        return bgr && bitmap && ppa_register_client(&config, &ppa) == ESP_OK;
    }
    void preview(bool frozen) {
        Preview *target = nullptr;
        for (auto &p : previews) {
            bool available = true;
            if (p.free.compare_exchange_strong(available, false)) {
                target = &p;
                break;
            }
        }
        if (!target)
            return;
        if (frozen)
            photo_bitmap_preview(bitmap, target->pixels);
        else
            for (unsigned i = 0; i < PHOTO_SIDE * PHOTO_SIDE; ++i) {
                const uint8_t *p = bgr + i * 3;
                target->pixels[i] = ((p[2] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[0] >> 3);
            }
        if (app_photo_present(ui, target->pixels, released, target) != ESP_GSP_OK)
            target->free.store(true);
    }
    void run() {
        // Worker stack is 8 KiB, including controller configuration startup.
        esp_err_t bt = app_printer_init();
        ESP_LOGI(TAG, "Bluetooth init: %s", esp_err_to_name(bt));
        if (!setup()) {
            state.mode = PHOTO_CAMERA_ERROR;
            publish();
            close_camera();
            app_printer_stop();
            stopped.store(true);
            vTaskDelete(nullptr);
            return;
        }
        bool frozen = false;
        while (!stop.load()) {
            app_printer_poll();
            photo_command_t command = PHOTO_RETAKE;
            bool request = xQueueReceive(commands, &command, 0) == pdTRUE;
            if (request) {
                ++state.response;
                if (command == PHOTO_RETAKE) {
                    frozen = false;
                    state.mode = PHOTO_LOADING;
                } else if (command == PHOTO_PRINT && frozen) {
                    state.mode = PHOTO_SENDING;
                    publish();
                    esp_err_t e = app_printer_send(bitmap);
                    state.mode = e == ESP_OK ? PHOTO_SENT : PHOTO_FAILED;
                    publish();
                    ESP_LOGI(TAG, "Print result %s", esp_err_to_name(e));
                }
            }
            if (frozen) {
                preview(true);
                publish();
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            if (!camera) {
                esp_err_t e = open_camera();
                if (e) {
                    state.mode = PHOTO_CAMERA_ERROR;
                    publish();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    continue;
                }
            }
            mosaico_camera_frame_t frame = {};
            esp_err_t e = mosaico_camera_get_frame(camera, &frame);
            if (e == ESP_OK) {
                e = convert(frame);
                esp_err_t r = mosaico_camera_return_frame(camera, &frame);
                if (e == ESP_OK)
                    e = r;
            }
            if (e) {
                state.mode = PHOTO_CAMERA_ERROR;
                publish();
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            state.mode = PHOTO_LIVE;
            if (request && command == PHOTO_CAPTURE) {
                photo_dither(bgr, bitmap);
                frozen = true;
                state.mode = PHOTO_REVIEW;
                ESP_LOGI(TAG, "Photo captured 320x320");
            }
            preview(frozen);
            publish();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        close_camera();
        ESP_LOGI(TAG, "Bluetooth stop: %s", esp_err_to_name(app_printer_stop()));
        stopped.store(true);
        vTaskDelete(nullptr);
    }
};
extern "C" esp_err_t app_capture_create(app_capture_handle_t *out, photo_provider_t *provider) {
    if (!out || !provider)
        return ESP_ERR_INVALID_ARG;
    *out = nullptr;
    auto *a = new (std::nothrow) app_capture;
    if (!a)
        return ESP_ERR_NO_MEM;
    a->commands = xQueueCreate(1, sizeof(photo_command_t));
    a->snapshots = xQueueCreate(1, sizeof(photo_state_t));
    if (!a->commands || !a->snapshots) {
        if (a->commands)
            vQueueDelete(a->commands);
        if (a->snapshots)
            vQueueDelete(a->snapshots);
        delete a;
        return ESP_ERR_NO_MEM;
    }
    provider->ctx = a;
    provider->poll = [](void *ctx, photo_state_t *out) {
        return xQueuePeek(static_cast<app_capture *>(ctx)->snapshots, out, 0) == pdTRUE;
    };
    provider->command = [](void *ctx, photo_command_t cmd) {
        return xQueueSend(static_cast<app_capture *>(ctx)->commands, &cmd, 0) == pdTRUE;
    };
    *out = a;
    return ESP_OK;
}
extern "C" esp_err_t app_capture_start(app_capture_handle_t a, app_photo_handle_t ui) {
    if (!a || !ui || a->task)
        return ESP_ERR_INVALID_ARG;
    a->ui = ui;
    app_capture::shutdown_owner = a;
    esp_err_t e = esp_register_shutdown_handler(app_capture::shutdown);
    if (e)
        return e;
    if (xTaskCreate([](void *ctx) { static_cast<app_capture *>(ctx)->run(); }, "photo_capture", 8192, a, 4,
                    &a->task) != pdPASS) {
        esp_unregister_shutdown_handler(app_capture::shutdown);
        app_capture::shutdown_owner = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
