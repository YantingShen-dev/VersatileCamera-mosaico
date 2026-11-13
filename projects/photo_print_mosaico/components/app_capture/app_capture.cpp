#include "app_capture.h"
#include "app_controls.h"
#include "app_printer.h"
#include "photo_probe.hpp"
#include "bsp/power.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "hand_detect.hpp"
#include "hand_gesture_recognition.hpp"
#include "mosaico_module_camera.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <new>
namespace {
struct ButtonEvent {
    photo_command_t command;
    uint64_t timestamp;
};
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
    photo_control_t control = {};
    PhotoProbe probe;
    photo_state_t &state = control.state;
    app_controls_handle_t controls = nullptr;
    HandDetect *detector = nullptr;
    HandGestureCls *gesture = nullptr;
    uint64_t last_gesture = 0;
    uint64_t last_gesture_report = 0;
    mosaico_camera_handle_t camera = nullptr;
    ppa_client_handle_t ppa = nullptr;
    uint8_t *bgr = nullptr, *bitmap = nullptr;
    Preview previews[3];
    ppa_srm_oper_config_t conversion = {};
    std::atomic<bool> stop{false}, stopped{false};
    std::atomic<int64_t> poweroff_at{0};
    static esp_err_t poweroff_rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                                  size_t capacity, size_t *size, void *context) {
        if (!request || request->payload_size != 3 ||
            memcmp(request->payload, "OFF", 3) != 0 || !response || capacity < 1 || !size)
            return ESP_ERR_INVALID_ARG;
        auto *self = static_cast<app_capture *>(context);
        if (self->stop.load())
            return ESP_ERR_INVALID_STATE;
        // Give Iris time to acknowledge before the owner stops peripherals.
        self->poweroff_at.store(esp_timer_get_time() + 250000);
        app_printer_cancel();
        response[0] = 1;
        *size = 1;
        return ESP_OK;
    }
    static inline app_capture *shutdown_owner = nullptr;
    void publish() {
        state.connected = app_printer_ready();
        state.board_connected = app_controls_board_ready(controls);
        state.audio_ready = app_controls_audio_ready(controls);
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
        esp_err_t err =
            esp_cache_msync(const_cast<void *>(frame.data), frame.size,
                            ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        if (err != ESP_OK) {
            return err;
        }
        // 384/768 crops give an exact 0.5 PPA scale and fill the 384-dot print head.
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
        bgr = (uint8_t *)heap_caps_aligned_alloc(
            128, PHOTO_SIDE * PHOTO_SIDE * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
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
    void preview() {
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
        photo_bitmap_preview(bitmap, target->pixels);
        if (app_photo_present(ui, target->pixels, released, target) != ESP_GSP_OK)
            target->free.store(true);
    }
    void recognize(uint64_t now) {
        if (!detector || !gesture || now - last_gesture < 200)
            return;
        last_gesture = now;
        dl::image::img_t image = {};
        image.data = bgr;
        image.width = PHOTO_SIDE;
        image.height = PHOTO_SIDE;
        image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_BGR888;
        auto &hands = detector->run(image);
        bool palm = false;
        const char *category = "none";
        float score = 0;
        if (hands.size() == 1) {
            const auto &hand = hands.front();
            // HandDetect already filters at its model's default 0.25 threshold.
            // Real hands also score 0.27: classify them instead of discarding them
            // with a second gate. The five-class score and temporal hold validate intent.
            if (hand.box[2] - hand.box[0] >= 45 &&
                hand.box[3] - hand.box[1] >= 45) {
                std::vector<int> crop = {hand.box[0], hand.box[1], hand.box[2], hand.box[3]};
                auto results = gesture->run_crop(image, crop);
                if (!results.empty()) {
                    const auto &r = results.front();
                    category = r.cat_name ? r.cat_name : "?";
                    score = r.score;
                    palm = r.cat_name && strcmp(r.cat_name, "five") == 0 && r.score >= 0.80f;
                    ESP_LOGD(TAG, "Gesture %s %.2f", r.cat_name ? r.cat_name : "?", r.score);
                }
            }
        }
        uint64_t finished = esp_timer_get_time() / 1000;
        if (!hands.empty() || finished - last_gesture_report >= 2000) {
            last_gesture_report = finished;
            ESP_LOGI(TAG, "Vision hands=%u gesture=%s score=%.2f palm=%d elapsed=%llu ms",
                     (unsigned)hands.size(), category, score, palm,
                     (unsigned long long)(finished - now));
            if (!hands.empty()) {
                const auto &h = hands.front();
                ESP_LOGI(TAG, "Hand score=%.2f box=%d,%d,%d,%d", h.score,
                         h.box[0], h.box[1], h.box[2], h.box[3]);
            }
        }
        const auto previous_mode = state.mode;
        photo_control_palm(&control, palm, esp_timer_get_time() / 1000);
        if (previous_mode == PHOTO_LIVE && state.mode == PHOTO_COUNTDOWN)
            ESP_LOGI(TAG, "Palm confirmed: starting 5-second countdown");
    }
    void run() {
        photo_control_init(&control);
        // The Classic controller needs contiguous internal RAM during startup.
        // Reserve it before audio and model metadata fragment the internal heap.
        esp_err_t bt = app_printer_init();
        ESP_LOGI(TAG, "Bluetooth init: %s; internal=%u largest=%u", esp_err_to_name(bt),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        esp_err_t ctl = app_controls_start(
            [](void *ctx, unsigned button, uint64_t stamp) {
                auto *self = static_cast<app_capture *>(ctx);
                ButtonEvent e = {button == 0 ? PHOTO_BUTTON1 : PHOTO_BUTTON2, stamp};
                xQueueSend(self->commands, &e, 0);
            },
            this, &controls);
        ESP_LOGI(TAG, "Controls init: %s", esp_err_to_name(ctl));
        // Reserve smaller mono audio DMA buffers before loading the two models.
        for (unsigned i = 0; i < 200 &&
                             (!app_controls_audio_ready(controls) ||
                              !app_controls_board_ready(controls)); ++i)
            vTaskDelay(pdMS_TO_TICKS(10));
        // ESP-DL temporarily creates many small graph objects while loading.
        // Prefer PSRAM for ordinary malloc/new so those objects cannot exhaust
        // the internal-only FreeRTOS/driver heap. Explicit DMA allocations retain
        // their capability requirements; keep this policy for runtime inference.
        heap_caps_malloc_extmem_enable(0);
        detector = new (std::nothrow) HandDetect(HandDetect::ESPDET_PICO_224_224_HAND, false);
        gesture = new (std::nothrow) HandGestureCls(HandGestureCls::MOBILENETV2_0_5_S8_V1, false);
        state.gesture_ready = detector && gesture;
        ESP_LOGI(TAG, "Models ready; internal=%u largest=%u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        bool ready = setup();
        bool capture_pending = false;
        while (!stop.load()) {
            int64_t deadline = poweroff_at.load();
            if (deadline && esp_timer_get_time() >= deadline) {
                stop.store(true);
                break;
            }
            app_printer_poll();
            state.connected = app_printer_ready();
            uint64_t now = esp_timer_get_time() / 1000;
            probe.expire(now);
            if (probe.begin(now)) {
                // Start a fresh live hold; test data never requests printing.
                state.mode = PHOTO_LIVE;
                state.countdown = 0;
                control.actions = 0;
                control.palm_tracking = control.absent_tracking = false;
                control.gesture_armed = true;
                xQueueReset(commands);
                ESP_LOGI(TAG, "Image probe started: 384x384 grayscale, 10 seconds");
            }
            ButtonEvent event = {};
            while (xQueueReceive(commands, &event, 0) == pdTRUE)
                photo_control_button(&control, event.command, event.timestamp);
            photo_control_tick(&control, esp_timer_get_time() / 1000);
            unsigned action = photo_control_take_actions(&control);
            if (action & PHOTO_DO_BEEP)
                app_controls_sound(controls, false);
            if (action & PHOTO_DO_CAPTURE)
                capture_pending = true;
            if (action & PHOTO_DO_PRINT) {
                publish();
                esp_err_t e = app_printer_send(bitmap, PHOTO_SIDE, PHOTO_SIDE);
                photo_control_printed(&control, e == ESP_OK);
                publish();
                ESP_LOGI(TAG, "Print result %s", esp_err_to_name(e));
                // Discard physical/UI input queued while the printer was busy.
                xQueueReset(commands);
            }
            bool frozen = state.mode == PHOTO_REVIEW || state.mode == PHOTO_SENT ||
                          state.mode == PHOTO_FAILED;
            if (frozen) {
                publish();
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            if (!ready) {
                state.mode = PHOTO_CAMERA_ERROR;
                publish();
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
            if (!camera && open_camera() != ESP_OK) {
                state.mode = PHOTO_CAMERA_ERROR;
                publish();
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            mosaico_camera_frame_t frame = {};
            const bool probing = probe.fill(bgr);
            esp_err_t e = probing ? ESP_OK : mosaico_camera_get_frame(camera, &frame);
            if (e == ESP_OK && !probing) {
                e = convert(frame);
                esp_err_t r = mosaico_camera_return_frame(camera, &frame);
                if (e == ESP_OK)
                    e = r;
            }
            if (e != ESP_OK) {
                if (capture_pending) {
                    capture_pending = false;
                    photo_control_captured(&control, false);
                }
                state.mode = PHOTO_CAMERA_ERROR;
                publish();
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            if (state.mode == PHOTO_LOADING || state.mode == PHOTO_CAMERA_ERROR)
                state.mode = PHOTO_LIVE;
            if (capture_pending) {
                capture_pending = false;
                app_controls_sound(controls, true);
                photo_filter_apply(bgr, bitmap, state.filter);
                photo_control_captured(&control, true);
                ESP_LOGI(TAG, "Photo captured %dx%d filter=%d", PHOTO_SIDE, PHOTO_SIDE,
                         state.filter);
            } else {
                photo_filter_apply(bgr, bitmap, state.filter);
                if (state.mode == PHOTO_LIVE)
                    recognize(now);
            }
            preview();
            publish();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        close_camera();
        app_controls_stop(controls);
        ESP_LOGI(TAG, "Bluetooth stop: %s", esp_err_to_name(app_printer_stop()));
        stopped.store(true);
        if (poweroff_at.load()) {
            ESP_LOGI(TAG, "Host requested power off; peripherals stopped");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_platform_mark_planned_restart());
            ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_power_prepare_sleep());
            ESP_ERROR_CHECK_WITHOUT_ABORT(bsp_power_set_shutdown(true));
            vTaskDelay(pdMS_TO_TICKS(500));
            // If USB keeps the rail alive, leave the MCU and display asleep.
            bsp_power_enter_deep_sleep();
        }
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
    a->commands = xQueueCreate(8, sizeof(ButtonEvent));
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
        ButtonEvent event = {cmd, (uint64_t)(esp_timer_get_time() / 1000)};
        return xQueueSend(static_cast<app_capture *>(ctx)->commands, &event, 0) == pdTRUE;
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
    e = esp_iris_rpc_register(0x5048, 1, app_capture::poweroff_rpc, a);
    if (e) {
        esp_unregister_shutdown_handler(app_capture::shutdown);
        return e;
    }
    e = esp_iris_rpc_register(0x5048, 2, PhotoProbe::rpc, &a->probe);
    if (e) {
        esp_iris_rpc_unregister(0x5048, 1);
        esp_unregister_shutdown_handler(app_capture::shutdown);
        return e;
    }
    if (xTaskCreate([](void *ctx) { static_cast<app_capture *>(ctx)->run(); }, "photo_capture",
                    16384, a, 4, &a->task) != pdPASS) {
        esp_unregister_shutdown_handler(app_capture::shutdown);
        esp_iris_rpc_unregister(0x5048, 1);
        esp_iris_rpc_unregister(0x5048, 2);
        app_capture::shutdown_owner = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
