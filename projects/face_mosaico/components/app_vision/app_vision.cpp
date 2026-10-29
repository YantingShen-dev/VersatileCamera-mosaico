// SPDX-License-Identifier: Apache-2.0
// Camera/PPA setup follows the ESP-Mosaico BSP ai_model_gallery example.
#include "app_vision.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "human_face_detect.hpp"
#include "human_face_recognition.hpp"
#include "mosaico_module_camera.h"
#include "nvs.h"

namespace {
constexpr unsigned MODEL_SIDE = 320;
constexpr unsigned FEATURE_MAX = 512;
constexpr unsigned FRAME_ALIGNMENT = 128;
constexpr unsigned MIN_FACE_SIZE = 65;
constexpr float MATCH_THRESHOLD = 0.55f;
constexpr uint32_t RECORD_MAGIC = 0x46414331;
constexpr int64_t RESULT_HOLD_US = 2200000;
const char *TAG = "face_vision";
struct Command {
    face_command_t kind;
    uint8_t person;
};
struct Record {
    uint32_t magic;
    uint32_t dimension;
    float feature[FEATURE_MAX];
};
struct Preview {
    uint16_t *pixels = nullptr;
    std::atomic<bool> free{true};
};
void released(void *ctx)
{
    static_cast<Preview *>(ctx)->free.store(true, std::memory_order_release);
}
}

/* The worker exclusively owns the camera, model, records, NVS, and snapshot.
 * UI crosses this boundary only through length-one FreeRTOS queues. */
struct app_vision {
    QueueHandle_t commands = nullptr;
    QueueHandle_t snapshots = nullptr;
    TaskHandle_t task = nullptr;
    app_face_handle_t ui = nullptr;
    face_snapshot_t snapshot = {};
    mosaico_camera_handle_t camera = nullptr;
    ppa_client_handle_t ppa = nullptr;
    uint8_t *bgr = nullptr;
    HumanFaceDetect *detector = nullptr;
    HumanFaceFeat *features = nullptr;
    nvs_handle_t nvs = 0;
    Record records[FACE_MAX_PEOPLE] = {};
    unsigned dimension = 0;
    bool storage_ok = false;
    Preview previews[3];
    int64_t hold_until = 0;
    ppa_srm_oper_config_t conversion = {};
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> stopped{false};
    /* ESP-IDF's shutdown callback has no user context; this owns its one instance. */
    static inline app_vision *shutdown_owner = nullptr;

    static void shutdown()
    {
        auto *owner = shutdown_owner;
        if (!owner) {
            return;
        }
        owner->stop_requested.store(true);
        const int64_t deadline = esp_timer_get_time() + 3000000;
        while (!owner->stopped.load() && esp_timer_get_time() < deadline) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!owner->stopped.load()) {
            ESP_LOGE(TAG, "Camera shutdown did not complete before reboot");
        }
    }

    void close_capture()
    {
        if (camera) {
            esp_err_t err = mosaico_camera_stop_stream(camera);
            if (err == ESP_OK) {
                err = mosaico_camera_del(camera);
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Camera cleanup: %s", esp_err_to_name(err));
            }
            camera = nullptr;
        }
        if (ppa) {
            ppa_unregister_client(ppa);
            ppa = nullptr;
        }
    }

    void publish()
    {
        xQueueOverwrite(snapshots, &snapshot);
    }

    void status(face_status_t value, uint8_t person = 0, bool hold = false)
    {
        if (!hold && esp_timer_get_time() < hold_until) {
            return;
        }
        snapshot.status = value;
        snapshot.person = person;
        if (hold) {
            hold_until = esp_timer_get_time() + RESULT_HOLD_US;
        }
    }

    bool load_records()
    {
        if (nvs_open("face_mfn_v1", NVS_READWRITE, &nvs) != ESP_OK) {
            return false;
        }
        for (unsigned i = 0; i < FACE_MAX_PEOPLE; ++i) {
            char key[8];
            snprintf(key, sizeof(key), "p%u", i + 1);
            size_t size = sizeof(Record);
            esp_err_t err = nvs_get_blob(nvs, key, &records[i], &size);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                continue;
            }
            if (err != ESP_OK || size != sizeof(Record) || records[i].magic != RECORD_MAGIC ||
                    records[i].dimension != dimension) {
                ESP_LOGE(TAG, "Invalid stored record %u; preserving storage", i + 1);
                return false;
            }
            float norm = 0;
            for (unsigned j = 0; j < dimension; ++j) {
                norm += records[i].feature[j] * records[i].feature[j];
            }
            if (!std::isfinite(norm) || norm < 0.9f || norm > 1.1f) {
                return false;
            }
            snapshot.slots |= 1U << i;
        }
        return true;
    }

    uint8_t match(const float *feature)
    {
        float best = MATCH_THRESHOLD;
        uint8_t person = 0;
        for (unsigned i = 0; i < FACE_MAX_PEOPLE; ++i) {
            if (!(snapshot.slots & (1U << i))) {
                continue;
            }
            float score = 0;
            for (unsigned j = 0; j < dimension; ++j) {
                score += feature[j] * records[i].feature[j];
            }
            if (score > best) {
                best = score;
                person = i + 1;
            }
        }
        return person;
    }

    void enroll(const float *feature, uint8_t known)
    {
        if (!storage_ok) {
            status(FACE_STORAGE_ERROR, 0, true);
            return;
        }
        if (known) {
            status(FACE_DUPLICATE, known, true);
            return;
        }
        unsigned i = 0;
        while (i < FACE_MAX_PEOPLE && (snapshot.slots & (1U << i))) {
            ++i;
        }
        if (i == FACE_MAX_PEOPLE) {
            status(FACE_FULL, 0, true);
            return;
        }
        Record &record = records[i];
        record = {};
        record.magic = RECORD_MAGIC;
        record.dimension = dimension;
        memcpy(record.feature, feature, dimension * sizeof(float));
        char key[8];
        snprintf(key, sizeof(key), "p%u", i + 1);
        esp_err_t err = nvs_set_blob(nvs, key, &record, sizeof(record));
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        if (err != ESP_OK) {
            record = {};
            status(FACE_STORAGE_ERROR, 0, true);
            ESP_LOGE(TAG, "Enrollment save failed: %s", esp_err_to_name(err));
            return;
        }
        snapshot.slots |= 1U << i;
        status(FACE_ENROLLED, i + 1, true);
        ESP_LOGI(TAG, "Enrolled person %u (feature only)", i + 1);
    }

    void erase(uint8_t person)
    {
        if (!storage_ok || person < 1 || person > FACE_MAX_PEOPLE) {
            status(FACE_STORAGE_ERROR, 0, true);
            return;
        }
        char key[8];
        snprintf(key, sizeof(key), "p%u", person);
        esp_err_t err = nvs_erase_key(nvs, key);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            err = ESP_OK;
        }
        if (err == ESP_OK) {
            err = nvs_commit(nvs);
        }
        if (err != ESP_OK) {
            status(FACE_STORAGE_ERROR, 0, true);
            return;
        }
        records[person - 1] = {};
        snapshot.slots &= ~(1U << (person - 1));
        status(FACE_DELETED, 0, true);
        ESP_LOGI(TAG, "Deleted person %u", person);
    }

    esp_err_t open_camera()
    {
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

    esp_err_t convert(const mosaico_camera_frame_t &frame)
    {
        if (frame.pixel_format != V4L2_PIX_FMT_UYVY || frame.width < MODEL_SIDE || frame.height < MODEL_SIDE) {
            return ESP_ERR_NOT_SUPPORTED;
        }
        const unsigned stride = frame.bytes_per_line ? frame.bytes_per_line / 2 : frame.width;
        const unsigned crop = std::min(frame.width, frame.height) >= MODEL_SIDE * 2 ? MODEL_SIDE * 2 : MODEL_SIDE;
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
        op->out.buffer_size = MODEL_SIDE * MODEL_SIDE * 3;
        op->out.pic_w = MODEL_SIDE;
        op->out.pic_h = MODEL_SIDE;
        op->out.srm_cm = PPA_SRM_COLOR_MODE_RGB888;
        op->rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
        op->scale_x = static_cast<float>(MODEL_SIDE) / crop;
        op->scale_y = op->scale_x;
        op->mode = PPA_TRANS_MODE_BLOCKING;
        err = ppa_do_scale_rotate_mirror(ppa, op);
        if (err == ESP_OK) {
            err = esp_cache_msync(bgr, MODEL_SIDE * MODEL_SIDE * 3,
                                  ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        }
        return err;
    }

    void preview(const std::list<dl::detect::result_t> &detections, uint8_t known)
    {
        Preview *target = nullptr;
        for (auto &buffer : previews) {
            bool available = true;
            if (buffer.free.compare_exchange_strong(available, false, std::memory_order_acq_rel)) {
                target = &buffer;
                break;
            }
        }
        if (!target) {
            return;
        }
        constexpr unsigned left = (FACE_PREVIEW_W - FACE_PREVIEW_H) / 2;
        std::fill(target->pixels, target->pixels + FACE_PREVIEW_W * FACE_PREVIEW_H, 0x2127);
        for (unsigned y = 0; y < FACE_PREVIEW_H; ++y) {
            for (unsigned x = 0; x < FACE_PREVIEW_H; ++x) {
                const uint8_t *p = bgr + ((y * MODEL_SIDE / FACE_PREVIEW_H) * MODEL_SIDE + x * MODEL_SIDE / FACE_PREVIEW_H) * 3;
                target->pixels[y * FACE_PREVIEW_W + left + x] = ((p[2] & 0xf8) << 8) | ((p[1] & 0xfc) << 3) | (p[0] >> 3);
            }
        }
        const uint16_t color = known ? 0x87B6 : 0xFDA0;
        for (const auto &face : detections) {
            if (face.box.size() < 4) {
                continue;
            }
            int x1 = std::clamp(face.box[0] * FACE_PREVIEW_H / static_cast<int>(MODEL_SIDE), 0, FACE_PREVIEW_H - 1) + left;
            int x2 = std::clamp(face.box[2] * FACE_PREVIEW_H / static_cast<int>(MODEL_SIDE), 0, FACE_PREVIEW_H - 1) + left;
            int y1 = std::clamp(face.box[1] * FACE_PREVIEW_H / static_cast<int>(MODEL_SIDE), 0, FACE_PREVIEW_H - 1);
            int y2 = std::clamp(face.box[3] * FACE_PREVIEW_H / static_cast<int>(MODEL_SIDE), 0, FACE_PREVIEW_H - 1);
            for (int y = y1; y <= y2; ++y) {
                for (int x = x1; x <= x2; ++x) {
                    if (x - x1 < 2 || x2 - x < 2 || y - y1 < 2 || y2 - y < 2) {
                        target->pixels[y * FACE_PREVIEW_W + x] = color;
                    }
                }
            }
        }
        if (app_face_present(ui, target->pixels, released, target) != ESP_GSP_OK) {
            target->free.store(true, std::memory_order_release);
        }
    }

    bool setup()
    {
        // Models first: avoid fragmenting SIMD-capable PSRAM with camera buffers.
        detector = new (std::nothrow) HumanFaceDetect(HumanFaceDetect::MSRMNP_S8_V1, false);
        features = new (std::nothrow) HumanFaceFeat(HumanFaceFeat::MFN_S8_V1, false);
        if (!detector || !features) {
            return false;
        }
        dimension = features->get_feat_len();
        if (!dimension || dimension > FEATURE_MAX) {
            return false;
        }
        storage_ok = load_records();
        bgr = static_cast<uint8_t *>(heap_caps_aligned_alloc(FRAME_ALIGNMENT, MODEL_SIDE * MODEL_SIDE * 3,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        for (auto &buffer : previews) {
            buffer.pixels = static_cast<uint16_t *>(heap_caps_aligned_alloc(FRAME_ALIGNMENT,
                                                                            FACE_PREVIEW_W * FACE_PREVIEW_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!buffer.pixels) {
                return false;
            }
        }
        ppa_client_config_t config = {};
        config.oper_type = PPA_OPERATION_SRM;
        config.max_pending_trans_num = 1;
        if (!bgr || ppa_register_client(&config, &ppa) != ESP_OK) {
            return false;
        }
        ESP_LOGI(TAG, "Models ready: feature=%u, PSRAM free=%u", dimension,
                 static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
        return true;
    }

    void run()
    {
        if (!setup()) {
            status(FACE_MODEL_ERROR);
            publish();
            close_capture();
            delete detector;
            delete features;
            heap_caps_free(bgr);
            for (auto &buffer : previews) {
                heap_caps_free(buffer.pixels);
            }
            if (nvs) {
                nvs_close(nvs);
            }
            stopped.store(true);
            vTaskDelete(nullptr);
            return;
        }
        while (!stop_requested.load()) {
            if (!camera) {
                esp_err_t err = open_camera();
                if (err != ESP_OK) {
                    snapshot.ready = false;
                    status(FACE_CAMERA_ERROR);
                    ESP_LOGE(TAG, "Camera init: %s", esp_err_to_name(err));
                    publish();
                    vTaskDelay(pdMS_TO_TICKS(500));
                    continue;
                }
            }
            Command command = {};
            const bool requested = xQueueReceive(commands, &command, 0) == pdTRUE;
            if (requested && command.kind == FACE_CMD_DELETE) {
                erase(command.person);
                ++snapshot.response;
                publish();
            }
            mosaico_camera_frame_t frame = {};
            esp_err_t err = mosaico_camera_get_frame(camera, &frame);
            if (err != ESP_OK) {
                snapshot.ready = false;
                status(FACE_CAMERA_ERROR, 0, true);
                if (requested && command.kind == FACE_CMD_ENROLL) {
                    ++snapshot.response;
                }
                publish();
                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }
            const int64_t started = esp_timer_get_time();
            err = convert(frame);
            // Blocking PPA has completed before the borrowed camera frame returns.
            esp_err_t release_err = mosaico_camera_return_frame(camera, &frame);
            if (err != ESP_OK || release_err != ESP_OK) {
                snapshot.ready = false;
                status(FACE_CAMERA_ERROR, 0, true);
                if (requested && command.kind == FACE_CMD_ENROLL) {
                    ++snapshot.response;
                }
                publish();
                vTaskDelay(pdMS_TO_TICKS(150));
                continue;
            }
            dl::image::img_t image = {};
            image.data = bgr;
            image.width = MODEL_SIDE;
            image.height = MODEL_SIDE;
            image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_BGR888;
            auto &detections = detector->run(image);
            face_status_t result = FACE_NO_FACE;
            uint8_t person = 0;
            const float *feature = nullptr;
            if (detections.size() > 1) {
                result = FACE_MULTIPLE;
            } else if (detections.size() == 1) {
                const auto &face = detections.front();
                if (face.box.size() < 4 || face.keypoint.size() != 10 ||
                        face.box[2] - face.box[0] < MIN_FACE_SIZE || face.box[3] - face.box[1] < MIN_FACE_SIZE) {
                    result = FACE_TOO_SMALL;
                } else {
                    auto *tensor = features->run(image, face.keypoint);
                    if (tensor && tensor->dtype == dl::DATA_TYPE_FLOAT && static_cast<unsigned>(tensor->size) == dimension) {
                        feature = static_cast<const float *>(tensor->data);
                        person = match(feature);
                        result = person ? FACE_KNOWN : FACE_UNKNOWN;
                    } else {
                        result = FACE_MODEL_ERROR;
                    }
                }
            }
            snapshot.ready = true;
            if (requested && command.kind == FACE_CMD_ENROLL) {
                if (feature) {
                    enroll(feature, person);
                } else {
                    status(result, 0, true);
                }
                ++snapshot.response;
            } else {
                status(storage_ok ? result : FACE_STORAGE_ERROR, person);
            }
            snapshot.elapsed_ms = (esp_timer_get_time() - started) / 1000;
            preview(detections, person);
            publish();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        close_capture();
        stopped.store(true);
        vTaskDelete(nullptr);
    }
};

extern "C" esp_err_t app_vision_create(app_vision_handle_t *out, face_provider_t *provider)
{
    if (!out || !provider) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = nullptr;
    auto *vision = new (std::nothrow) app_vision;
    if (!vision) {
        return ESP_ERR_NO_MEM;
    }
    vision->commands = xQueueCreate(1, sizeof(Command));
    vision->snapshots = xQueueCreate(1, sizeof(face_snapshot_t));
    if (!vision->commands || !vision->snapshots) {
        if (vision->commands) {
            vQueueDelete(vision->commands);
        }
        if (vision->snapshots) {
            vQueueDelete(vision->snapshots);
        }
        delete vision;
        return ESP_ERR_NO_MEM;
    }
    provider->ctx = vision;
    provider->poll = [](void *ctx, face_snapshot_t *snapshot) {
        return xQueuePeek(static_cast<app_vision *>(ctx)->snapshots, snapshot, 0) == pdTRUE;
    };
    provider->command = [](void *ctx, face_command_t kind, uint8_t person) {
        Command command = {kind, person};
        return xQueueSend(static_cast<app_vision *>(ctx)->commands, &command, 0) == pdTRUE;
    };
    *out = vision;
    return ESP_OK;
}

extern "C" esp_err_t app_vision_start(app_vision_handle_t vision, app_face_handle_t ui)
{
    if (!vision || !ui || vision->task) {
        return ESP_ERR_INVALID_ARG;
    }
    vision->ui = ui;
    app_vision::shutdown_owner = vision;
    esp_err_t err = esp_register_shutdown_handler(app_vision::shutdown);
    if (err != ESP_OK) {
        app_vision::shutdown_owner = nullptr;
        return err;
    }
    if (xTaskCreate([](void *ctx) {
    static_cast<app_vision *>(ctx)->run();
    },
    "face_vision", 16384, vision, 4, &vision->task) != pdPASS) {
        esp_unregister_shutdown_handler(app_vision::shutdown);
        app_vision::shutdown_owner = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
