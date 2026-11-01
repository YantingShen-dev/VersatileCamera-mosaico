// SPDX-License-Identifier: Apache-2.0
#include "app_board.h"
#include "app_settings.h"
#include "app_cloud.h"
#include "app_voice.h"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <new>
#include "bsp/esp_mosaico.h"
#include "cJSON.h"
#include "driver/ppa.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_iris.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mosaico_module_camera.h"
#include "mosaico_module_interact.h"

namespace {
constexpr size_t FRAME_BYTES = FRIDGE_PREVIEW_WIDTH * FRIDGE_PREVIEW_HEIGHT * 2;
constexpr uint16_t RPC_SERVICE = 0x4652;
const char *TAG = "fridge_board";
struct Frame {
    uint16_t *pixels = nullptr;
    std::atomic<bool> available{true};
    uint32_t session = 0, capture = 0;
};
void release_frame(void *ctx) { static_cast<Frame *>(ctx)->available.store(true); }
}

struct app_board {
    app_fridge_handle_t controller = nullptr;
    std::atomic<app_cloud_handle_t> cloud{nullptr};
    app_voice_handle_t voice = nullptr;
    uint16_t *best_capture=nullptr;
    uint64_t best_sharpness=0;
    uint32_t burst_token=0;
    unsigned burst_frames=0;
    int64_t burst_last=0;
    std::atomic<app_settings_handle_t> settings{nullptr};
    fridge_settings_t settings_snapshot = {};
    QueueHandle_t commands = nullptr, frames = nullptr;
    SemaphoreHandle_t lock = nullptr;
    fridge_snapshot_t snapshot = {};
    fridge_command_t command = {};
    Frame buffers[3];
    mosaico_camera_handle_t camera = nullptr;
    ppa_client_handle_t ppa = nullptr;
    ppa_srm_oper_config_t conversion = {};
    mosaico_interact_handle_t interact = nullptr;
    mosaico_module_mgr_info_t right_info = {};
    button_handle_t ai = nullptr;
    std::atomic<unsigned> page{0};
    std::atomic<bool> stop{false}, camera_stopped{true};
    std::atomic<bool> camera_ready{false}, buttons_ready{false};
    std::atomic<uint32_t> frame_count{0};
    std::atomic<int> camera_error{ESP_OK};
    std::atomic<bool> preview_frozen{false};
    uint32_t captured_token = 0;
    static inline app_board *shutdown_owner = nullptr;

    void queue_action(fridge_action_t action) {
        // Small physical events do not allocate the large item batch on a task stack.
        xSemaphoreTake(lock, portMAX_DELAY);
        memset(&command, 0, sizeof(command)); command.action = action;
        if (xQueueSend(commands, &command, 0) != pdTRUE) ESP_LOGW(TAG, "Input queue full");
        xSemaphoreGive(lock);
    }
    static void ai_clicked(void *, void *ctx) { static_cast<app_board *>(ctx)->queue_action(FRIDGE_FINISH); }
    static void settings_read(void *ctx, fridge_settings_t *out) {
        app_settings_read(static_cast<app_board *>(ctx)->settings, out);
    }
    static bool settings_request(void *ctx, const fridge_setting_request_t *request) {
        auto *self = static_cast<app_board *>(ctx);
        if (request->action == FRIDGE_CFG_TEST) {
            app_voice_test(self->voice); return self->voice != nullptr;
        }
        return app_settings_request(self->settings, request);
    }
    static bool poll(void *ctx, fridge_command_t *out) {
        auto *self = static_cast<app_board *>(ctx);
        Frame *frame = nullptr;
        if (xQueueReceive(self->frames, &frame, 0) == pdTRUE) {
            const uint32_t session = frame->session, capture = frame->capture;
            if (capture && !self->preview_frozen.load()) self->capture_feedback(true, session, capture, frame->pixels);
            if ((self->page.load() != 1 && self->page.load() != 2) ||
                app_fridge_present(self->controller, frame->pixels, release_frame, frame) != ESP_GSP_OK)
                frame->available.store(true);

        }
        return xQueueReceive(self->commands, out, 0) == pdTRUE;
    }
    static void publish(void *ctx, const fridge_snapshot_t *state) {
        auto *self = static_cast<app_board *>(ctx);
        xSemaphoreTake(self->lock, portMAX_DELAY);
        if (state->page == 2 && self->snapshot.page != 2) self->preview_frozen.store(false);
        self->snapshot = *state;
        self->page.store(state->page);
        xSemaphoreGive(self->lock);
        app_voice_publish(self->voice, state);
    }
    static bool cloud_active(void *ctx, uint32_t session, uint32_t capture, unsigned page) {
        auto *self = static_cast<app_board *>(ctx);
        xSemaphoreTake(self->lock, portMAX_DELAY);
        bool active = !self->stop.load() && self->snapshot.store.session == session && self->snapshot.capture == capture &&
            self->snapshot.page == page;
        xSemaphoreGive(self->lock);
        return active;
    }
    static bool cloud_deliver(void *ctx, const fridge_command_t *command) {
        return xQueueSend(static_cast<app_board *>(ctx)->commands, command, pdMS_TO_TICKS(500)) == pdTRUE;
    }
    static int voice_volume(void *ctx) { return app_settings_volume(static_cast<app_board *>(ctx)->settings); }
    void capture_feedback(bool success, uint32_t session = 0, uint32_t capture = 0, const uint16_t *pixels = nullptr) {
        xSemaphoreTake(lock, portMAX_DELAY);
        if (snapshot.page == 2 && snapshot.capture != captured_token &&
            (!success || (snapshot.store.session == session && snapshot.capture == capture))) {
            bool submitted = false;
            if (success && pixels) {
                const int64_t now=esp_timer_get_time();
                if(burst_token!=capture) {burst_token=capture;burst_frames=0;best_sharpness=0;burst_last=0;}
                if(burst_last && now-burst_last<120000) {xSemaphoreGive(lock);return;}
                uint64_t sharpness=0;
                for(unsigned y=4;y<476;y+=4)for(unsigned x=4;x<476;x+=4) {
                    unsigned i=y*480+x;
                    int green=(pixels[i]>>5)&63;
                    int dx=green-((pixels[i-4]>>5)&63),dy=green-((pixels[i-1920]>>5)&63);
                    sharpness+=(uint64_t)(dx*dx+dy*dy);
                }
                if(!burst_frames || sharpness>best_sharpness) {memcpy(best_capture,pixels,FRAME_BYTES);best_sharpness=sharpness;}
                ++burst_frames;burst_last=now;
                if(burst_frames<3) {xSemaphoreGive(lock);return;}
                submitted = app_cloud_capture(cloud, best_capture, session, capture, "");
            }
            captured_token = snapshot.capture;
            preview_frozen.store(submitted);
            if (!submitted) {
                memset(&command, 0, sizeof(command)); command.action = FRIDGE_ERROR;
                command.session = snapshot.store.session; command.capture = snapshot.capture;
                snprintf(command.text, sizeof(command.text), "%s", success ? "识别服务忙或内存不足，请稍后重拍" : "相机尚未就绪，请稍后重试");
                if (xQueueSend(commands, &command, 0) != pdTRUE) captured_token = 0;
            }
        }
        xSemaphoreGive(lock);
    }
    static void shutdown() {
        auto *self = shutdown_owner;
        if (!self) return;
        self->stop.store(true);
        const int64_t deadline = esp_timer_get_time() + 3000000;
        while (!self->camera_stopped.load() && esp_timer_get_time() < deadline)
            vTaskDelay(pdMS_TO_TICKS(10));
    }
    esp_err_t open_camera() {
        mosaico_camera_config_t config = MOSAICO_CAMERA_DEFAULT_CONFIG();
        config.slot = MOSAICO_MODULE_MGR_SLOT_LEFT;
        esp_err_t err = mosaico_camera_new(&config, &camera);
        if (err == ESP_OK) err = mosaico_camera_open(camera);
        if (err == ESP_OK) err = mosaico_camera_start_stream(camera);
        if (err != ESP_OK && camera) { mosaico_camera_del(camera); camera = nullptr; }
        return err;
    }
    esp_err_t convert(const mosaico_camera_frame_t &source, Frame *target) {
        if (source.pixel_format != V4L2_PIX_FMT_UYVY || source.width < 480 || source.height < 480)
            return ESP_ERR_NOT_SUPPORTED;
        // 640 -> 480 and 480 -> 480 are exact PPA scales.
        const unsigned crop = std::min(source.width, source.height) >= 640 ? 640 : 480;
        esp_err_t err = esp_cache_msync(const_cast<void *>(source.data), source.size,
            ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        if (err != ESP_OK) return err;
        conversion = {};
        auto *op = &conversion;
        op->in.buffer = source.data;
        op->in.pic_w = source.bytes_per_line ? source.bytes_per_line / 2 : source.width;
        op->in.pic_h = source.height;
        op->in.block_w = op->in.block_h = crop;
        op->in.block_offset_x = ((source.width - crop) / 2) & ~1U;
        op->in.block_offset_y = ((source.height - crop) / 2) & ~1U;
        op->in.srm_cm = PPA_SRM_COLOR_MODE_YUV422_UYVY;
        op->in.yuv_range = PPA_COLOR_RANGE_LIMIT;
        op->in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
        op->out.buffer = target->pixels;
        op->out.buffer_size = FRAME_BYTES;
        op->out.pic_w = FRIDGE_PREVIEW_WIDTH;
        op->out.pic_h = FRIDGE_PREVIEW_HEIGHT;
        op->out.srm_cm = PPA_SRM_COLOR_MODE_RGB565;
        op->rotation_angle = PPA_SRM_ROTATION_ANGLE_90;
        op->scale_x = op->scale_y = 480.0f / crop;
        op->mode = PPA_TRANS_MODE_BLOCKING;
        err = ppa_do_scale_rotate_mirror(ppa, op);
        if (err == ESP_OK) err = esp_cache_msync(target->pixels, FRAME_BYTES,
            ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
        return err;
    }
    void camera_run() {
        camera_stopped.store(false);
        while (!stop.load()) {
            if (!camera) {
                const esp_err_t err = open_camera();
                if (err != ESP_OK) { capture_feedback(false); ESP_LOGW(TAG, "Camera: %s", esp_err_to_name(err)); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
                camera_ready.store(true); ESP_LOGI(TAG, "Camera ready");
            }
            mosaico_camera_frame_t source = {};
            const esp_err_t err = mosaico_camera_get_frame(camera, &source);
            if (err != ESP_OK) { capture_feedback(false); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
            const bool capture = page.load() == 2 && !preview_frozen.load();
            if (page.load() == 1 || page.load() == 2) {
                Frame *target = nullptr;
                for (auto &candidate : buffers) {
                    bool available = true;
                    if (candidate.available.compare_exchange_strong(available, false)) { target = &candidate; break; }
                }
                if (target) {
                    xSemaphoreTake(lock, portMAX_DELAY);
                    target->session = snapshot.store.session;
                    target->capture = snapshot.page == 2 ? snapshot.capture : 0;
                    xSemaphoreGive(lock);
                    const esp_err_t converted = convert(source, target);
                    if (camera_error.exchange(converted) != converted && converted != ESP_OK)
                        ESP_LOGE(TAG, "Camera conversion: %s", esp_err_to_name(converted));
                    if (converted != ESP_OK || xQueueSend(frames, &target, 0) != pdTRUE) target->available.store(true);
                    else frame_count.fetch_add(1);
                    if (converted != ESP_OK && capture) capture_feedback(false);
                }
            }
            mosaico_camera_return_frame(camera, &source);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (camera) { mosaico_camera_stop_stream(camera); mosaico_camera_del(camera); camera = nullptr; }
        camera_stopped.store(true); vTaskDelete(nullptr);
    }
    void buttons_run() {
        mosaico_interact_inputs_t previous = {};
        bool initialized = false;
        int64_t last_press = 0;
        while (!stop.load()) {
            if (!interact) {
                mosaico_interact_config_t config = MOSAICO_INTERACT_DEFAULT_CONFIG();
                config.slot = MOSAICO_MODULE_MGR_SLOT_RIGHT;
                config.button_mode = MOSAICO_INTERACT_BUTTON_MODE_GPIO;
                config.led_brightness = 0;
                const esp_err_t err = mosaico_interact_open(&config, &interact);
                if (err != ESP_OK) { ESP_LOGW(TAG, "Right interaction board: %s", esp_err_to_name(err)); vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
                buttons_ready.store(true); ESP_LOGI(TAG, "Interaction board ready: key L=IN, key R=OUT");
            }
            mosaico_interact_inputs_t current = {};
            if (mosaico_interact_read_inputs(interact, &current) == ESP_OK) {
                const int64_t now = esp_timer_get_time();
                if (initialized && now - last_press > 250000) {
                    if (current.left_pressed && !previous.left_pressed) { queue_action(FRIDGE_IN); last_press = now; }
                    else if (current.right_pressed && !previous.right_pressed) { queue_action(FRIDGE_OUT); last_press = now; }
                }
                previous = current; initialized = true;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (interact) mosaico_interact_close(interact);
        vTaskDelete(nullptr);
    }
    static esp_err_t rpc(const esp_iris_rpc_request_t *request, uint8_t *response,
                        size_t capacity, size_t *size, void *ctx) {
        auto *self = static_cast<app_board *>(ctx);
        cJSON *result = cJSON_CreateObject();
        if (!result) return ESP_ERR_NO_MEM;
        esp_err_t err = ESP_OK;
        xSemaphoreTake(self->lock, portMAX_DELAY);
        if (request->method_id == 2) {
            cJSON *input = cJSON_ParseWithLength(reinterpret_cast<const char *>(request->payload), request->payload_size);
            const cJSON *action = input ? cJSON_GetObjectItemCaseSensitive(input, "action") : nullptr;
            memset(&self->command, 0, sizeof(self->command));
            if (!cJSON_IsNumber(action) || action->valuedouble != action->valueint || action->valueint < FRIDGE_IN || action->valueint > FRIDGE_MORE)
                err = ESP_ERR_INVALID_ARG;
            else {
                // Test/input RPC accepts only physical-equivalent controls for now.
                const int a = action->valueint;
                if (a != FRIDGE_IN && a != FRIDGE_OUT && a != FRIDGE_FINISH && a != FRIDGE_UNDO && a != FRIDGE_SETTINGS && a != FRIDGE_SELECT && a != FRIDGE_MORE) err = ESP_ERR_NOT_SUPPORTED;
                else {
                    self->command.action=static_cast<fridge_action_t>(a);
                    if(a==FRIDGE_SELECT || a==FRIDGE_MORE) {
                        const cJSON *sid=cJSON_GetObjectItemCaseSensitive(input,"session"),*cap=cJSON_GetObjectItemCaseSensitive(input,"capture"),*value=cJSON_GetObjectItemCaseSensitive(input,"value");
                        if(!cJSON_IsNumber(sid) || !cJSON_IsNumber(cap) || sid->valuedouble!=self->snapshot.store.session || cap->valuedouble!=self->snapshot.capture ||
                           (a==FRIDGE_SELECT && (!cJSON_IsNumber(value) || value->valuedouble!=value->valueint || value->valueint<0))) err=ESP_ERR_INVALID_ARG;
                        else {self->command.session=self->snapshot.store.session;self->command.capture=self->snapshot.capture;self->command.value=value?value->valueint:0;}
                    }
                    if(err==ESP_OK && xQueueSend(self->commands,&self->command,0)!=pdTRUE)err=ESP_ERR_TIMEOUT;
                }
            }
            cJSON_Delete(input);
        } else if (request->method_id == 4) {
            cJSON *input = cJSON_ParseWithLength(reinterpret_cast<const char *>(request->payload), request->payload_size);
            err = app_cloud_configure(self->cloud, input);
            cJSON *key = cJSON_GetObjectItemCaseSensitive(input, "api_key");
            if (cJSON_IsString(key)) memset(key->valuestring, 0, strlen(key->valuestring));
            cJSON_Delete(input);
        } else if (request->method_id == 3) {
            // Diagnostic equivalent of scan/volume/test controls; never accepts credentials.
            cJSON *input = cJSON_ParseWithLength(reinterpret_cast<const char *>(request->payload), request->payload_size);
            const cJSON *action = input ? cJSON_GetObjectItemCaseSensitive(input, "setting") : nullptr;
            const cJSON *volume = input ? cJSON_GetObjectItemCaseSensitive(input, "volume") : nullptr;
            fridge_setting_request_t setting = {};
            if (!cJSON_IsNumber(action) || action->valuedouble != action->valueint ||
                (action->valueint != FRIDGE_CFG_SCAN && action->valueint != FRIDGE_CFG_VOLUME && action->valueint != FRIDGE_CFG_TEST)) err = ESP_ERR_INVALID_ARG;
            else {
                setting.action = static_cast<fridge_setting_action_t>(action->valueint);
                if (setting.action == FRIDGE_CFG_VOLUME && (!cJSON_IsNumber(volume) || volume->valuedouble != volume->valueint || volume->valueint < 0 || volume->valueint > 100)) err = ESP_ERR_INVALID_ARG;
                else {
                    setting.volume = volume ? volume->valueint : 0;
                    if (!settings_request(self, &setting)) err = ESP_ERR_INVALID_STATE;
                }
            }
            cJSON_Delete(input);
        }
        if(request->method_id==5) {
            cJSON_AddBoolToObject(result,"pending",self->snapshot.pending);
            cJSON_AddNumberToObject(result,"session",self->snapshot.store.session);
            cJSON_AddNumberToObject(result,"capture",self->snapshot.capture);
            cJSON_AddNumberToObject(result,"total",self->snapshot.choice_count);
            cJSON *choices=cJSON_AddArrayToObject(result,"choices");
            for(unsigned i=0;i<3 && self->snapshot.choice_offset+i<self->snapshot.choice_count;++i) {
                cJSON *choice=cJSON_CreateObject();cJSON_AddItemToArray(choices,choice);
                cJSON_AddNumberToObject(choice,"id",self->snapshot.choice_ids[i]);cJSON_AddStringToObject(choice,"name",self->snapshot.choice_names[i]);
            }
            xSemaphoreGive(self->lock);
            bool ok=cJSON_PrintPreallocated(result,reinterpret_cast<char *>(response),capacity,false);
            if(ok) *size=strlen(reinterpret_cast<char *>(response));
            cJSON_Delete(result);
            return ok?ESP_OK:ESP_ERR_NO_MEM;
        }
        cJSON_AddBoolToObject(result,"pending",self->snapshot.pending);
        cJSON_AddNumberToObject(result, "page", self->snapshot.page);
        cJSON_AddNumberToObject(result, "settings_api", 2);
        cJSON_AddNumberToObject(result, "session", self->snapshot.store.session);
        cJSON_AddNumberToObject(result, "capture", self->snapshot.capture);
        cJSON_AddNumberToObject(result, "direction", self->snapshot.store.direction);
        cJSON_AddNumberToObject(result, "count", self->snapshot.store.count);
        cJSON_AddStringToObject(result, "message", self->snapshot.message);
        cJSON_AddBoolToObject(result, "camera_ready", self->camera_ready.load());
        cJSON_AddNumberToObject(result, "camera_error", self->camera_error.load());
        cJSON_AddBoolToObject(result, "buttons_ready", self->buttons_ready.load());
        app_voice_diagnostics(self->voice, result);
        app_settings_read(self->settings, &self->settings_snapshot);
        cJSON_AddNumberToObject(result, "volume", self->settings_snapshot.volume);
        cJSON_AddBoolToObject(result, "wifi_connected", self->settings_snapshot.connected);
        cJSON_AddBoolToObject(result, "wifi_busy", self->settings_snapshot.busy);
        cJSON_AddStringToObject(result, "wifi_status", self->settings_snapshot.status);
        cJSON_AddNumberToObject(result, "wifi_network_count", self->settings_snapshot.count);
        cJSON_AddStringToObject(result, "edition", "hardware-qwen");
        app_cloud_diagnostics(self->cloud, result);
        cJSON_AddNumberToObject(result, "preview_frames", self->frame_count.load());
        cJSON_AddBoolToObject(result, "photo_frozen", false);
        cJSON_AddBoolToObject(result, "capture_submitted", self->preview_frozen.load());
        cJSON_AddNumberToObject(result, "operation", self->snapshot.operation);
        cJSON_AddNumberToObject(result, "feedback_id", self->snapshot.feedback_id);
        if (mosaico_module_mgr_get_info(MOSAICO_MODULE_MGR_SLOT_RIGHT, &self->right_info) == ESP_OK) {
            cJSON_AddNumberToObject(result, "right_module_type", self->right_info.eeprom.board_type);
            cJSON_AddNumberToObject(result, "right_module_presence", self->right_info.presence);
        }
        xSemaphoreGive(self->lock);
        if (err == ESP_OK && !cJSON_PrintPreallocated(result, reinterpret_cast<char *>(response), capacity, false)) err = ESP_ERR_NO_MEM;
        if (err == ESP_OK) *size = strlen(reinterpret_cast<char *>(response));
        cJSON_Delete(result); return err;
    }
};

extern "C" esp_err_t app_board_create(app_board_handle_t *out, fridge_platform_t *platform) {
    if (!out || !platform) return ESP_ERR_INVALID_ARG;
    *out = nullptr;
    auto *self = new (std::nothrow) app_board();
    if (!self) return ESP_ERR_NO_MEM;
    self->lock = xSemaphoreCreateMutex();
    self->commands = xQueueCreate(4, sizeof(fridge_command_t));
    self->frames = xQueueCreate(1, sizeof(Frame *));
    bool allocated = self->lock && self->commands && self->frames;
    for (auto &buffer : self->buffers) {
        buffer.pixels = static_cast<uint16_t *>(heap_caps_aligned_alloc(128, FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        allocated = allocated && buffer.pixels;
    }
    self->best_capture=static_cast<uint16_t *>(heap_caps_aligned_alloc(128,FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    allocated=allocated && self->best_capture;
    ppa_client_config_t ppa = {};
    ppa.oper_type = PPA_OPERATION_SRM; ppa.max_pending_trans_num = 1;
    esp_err_t err = allocated ? ppa_register_client(&ppa, &self->ppa) : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        for (auto &buffer : self->buffers) heap_caps_free(buffer.pixels);
        if (self->lock) vSemaphoreDelete(self->lock);
        if (self->commands) vQueueDelete(self->commands);
        if (self->frames) vQueueDelete(self->frames);
        heap_caps_free(self->best_capture);delete self; return err;
    }
    platform->ctx = self; platform->poll = app_board::poll; platform->publish = app_board::publish;
    platform->settings_read = app_board::settings_read; platform->settings_request = app_board::settings_request;
    *out = self; return ESP_OK;
}
extern "C" esp_err_t app_board_start(app_board_handle_t self, app_fridge_handle_t controller) {
    if (!self || !controller) return ESP_ERR_INVALID_ARG;
    self->controller = controller;
    app_board::shutdown_owner = self;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_register_shutdown_handler(app_board::shutdown));
    esp_err_t err = esp_iris_rpc_register(RPC_SERVICE, 1, app_board::rpc, self);
    if (err == ESP_OK) err = esp_iris_rpc_register(RPC_SERVICE, 2, app_board::rpc, self);
    if (err == ESP_OK) err = esp_iris_rpc_register(RPC_SERVICE, 3, app_board::rpc, self);
    if (err == ESP_OK) err = esp_iris_rpc_register(RPC_SERVICE, 5, app_board::rpc, self);
    if (err == ESP_OK) err = esp_iris_rpc_register(RPC_SERVICE, 4, app_board::rpc, self);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Settings RPC registered (methods 1, 2, 3)");
    int count = 0;
    if (bsp_iot_button_create(&self->ai, &count, 1) == ESP_OK && count == 1)
        ESP_ERROR_CHECK_WITHOUT_ABORT(iot_button_register_cb(self->ai, BUTTON_SINGLE_CLICK, nullptr, app_board::ai_clicked, self));
    app_voice_callbacks_t voice_callbacks{self, app_board::cloud_deliver, app_board::voice_volume};
    esp_err_t voice_err = app_voice_start(&self->voice, &voice_callbacks);
    if (voice_err != ESP_OK) ESP_LOGE(TAG, "Local voice unavailable: %s", esp_err_to_name(voice_err));
    if (xTaskCreate([](void *ctx){static_cast<app_board *>(ctx)->camera_run();}, "fridge_camera", 6144, self, 4, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    if (xTaskCreate([](void *ctx){static_cast<app_board *>(ctx)->buttons_run();}, "fridge_buttons", 4096, self, 4, nullptr) != pdPASS) return ESP_ERR_NO_MEM;
    app_settings_handle_t settings = nullptr;
    esp_err_t settings_err = app_settings_start(&settings);
    if (settings_err != ESP_OK) return settings_err;
    self->settings.store(settings);
    app_cloud_callbacks_t callbacks = {self, app_board::cloud_active, app_board::cloud_deliver};
    app_cloud_handle_t cloud = nullptr;
    esp_err_t cloud_err = app_cloud_start(&cloud, &callbacks);
    if (cloud_err == ESP_OK) { self->cloud.store(cloud); app_voice_connect(self->voice, cloud); }
    return cloud_err;
}
