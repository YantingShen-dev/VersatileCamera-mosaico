#include "app_voice.h"
#include "bsp/esp_mosaico.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "model_path.h"
#include <atomic>
#include <cstdlib>
#include <new>

extern const uint8_t photo_srmodels[] asm("_binary_photo_srmodels_bin_start");
static const char *TAG = "photo_voice";
struct app_voice {
    photo_voice_callback_t callback = nullptr;
    void *context = nullptr;
    std::atomic<bool> stop{false}, ready{false}, listening{false};
    std::atomic<bool> feed_done{true}, detect_done{true};
    srmodel_list_t *models = nullptr;
    const esp_mn_iface_t *mn = nullptr;
    model_iface_data_t *mn_data = nullptr;
    const esp_afe_sr_iface_t *afe = nullptr;
    esp_afe_sr_data_t *afe_data = nullptr;
    esp_codec_dev_handle_t mic = nullptr;
    bool mic_open = false, commands_allocated = false;
    int16_t *pcm = nullptr;
    int samples = 0;

    bool init() {
        models = srmodel_load(photo_srmodels);
        char *name = models ? esp_srmodel_filter(models, ESP_MN_PREFIX, ESP_MN_CHINESE) : nullptr;
        mn = name ? esp_mn_handle_from_name(name) : nullptr;
        if (!mn || !(mn_data = mn->create(name, 6000))) return false;
        if (esp_mn_commands_alloc(mn, mn_data) != ESP_OK) return false;
        commands_allocated = true;
        if (esp_mn_commands_add(1, "zhun bei pai zhao") != ESP_OK || esp_mn_commands_update())
            return false;
        mn->set_det_threshold(mn_data, 0.85f);
        afe_config_t *cfg = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
        if (!cfg) return false;
        cfg->aec_init = false;
        cfg->se_init = false;
        cfg->ns_init = false;
        cfg->wakenet_init = false;
        cfg->vad_init = true;
        cfg->vad_model_name = nullptr;
        cfg->vad_mode = VAD_MODE_3;
        cfg->agc_init = true;
        cfg->agc_mode = AFE_AGC_MODE_WEBRTC;
        cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
        afe = esp_afe_handle_from_config(cfg);
        if (afe) afe_data = afe->create_from_config(cfg);
        afe_config_free(cfg);
        if (!afe_data || afe->get_fetch_chunksize(afe_data) != mn->get_samp_chunksize(mn_data))
            return false;
        samples = afe->get_feed_chunksize(afe_data) * afe->get_feed_channel_num(afe_data);
        pcm = static_cast<int16_t *>(heap_caps_malloc(samples * sizeof(int16_t), MALLOC_CAP_SPIRAM));
        if (!pcm) return false;
        mic = bsp_audio_codec_microphone_init();
        esp_codec_dev_sample_info_t format = {};
        format.sample_rate = 16000;
        format.bits_per_sample = 16;
        format.channel = 1;
        if (!mic || esp_codec_dev_open(mic, &format) != ESP_CODEC_DEV_OK) return false;
        mic_open = true;
        if (esp_codec_dev_set_in_gain(mic, 30.0f) != ESP_CODEC_DEV_OK) return false;
        ESP_LOGI(TAG, "Offline command ready: zhun bei pai zhao; model=%s, feed=%d", name, samples);
        return true;
    }
    void release() {
        if (mic_open) esp_codec_dev_close(mic);
        if (afe_data) afe->destroy(afe_data);
        if (commands_allocated) esp_mn_commands_free();
        if (mn_data) mn->destroy(mn_data);
        if (models) srmodel_host_deinit(models);
        free(pcm);
        mic_open = false; afe_data = nullptr; commands_allocated = false;
        mn_data = nullptr; models = nullptr; pcm = nullptr;
    }
    void feed() {
        uint64_t last_report = 0;
        while (!stop.load()) {
            int error = esp_codec_dev_read(mic, pcm, samples * sizeof(int16_t));
            if (error != ESP_CODEC_DEV_OK) {
                ESP_LOGE(TAG, "Microphone read failed: %d", error);
                ready.store(false);
                stop.store(true);
                break;
            }
            uint64_t now = esp_timer_get_time() / 1000;
            if (now - last_report >= 2000) {
                unsigned peak = 0, total = 0;
                for (int i = 0; i < samples; ++i) {
                    unsigned level = std::abs(static_cast<int>(pcm[i]));
                    if (level > peak) peak = level;
                    total += level;
                }
                ESP_LOGI(TAG, "Mic peak=%u mean=%u listening=%d", peak, total / samples,
                         listening.load());
                last_report = now;
            }
            afe->feed(afe_data, pcm);
        }
        feed_done.store(true);
        vTaskDelete(nullptr);
    }
    void detect() {
        bool previously_listening = false;
        while (!stop.load()) {
            auto *result = afe->fetch_with_delay(afe_data, pdMS_TO_TICKS(100));
            if (!result || result->ret_value != ESP_OK) continue;
            bool enabled = listening.load();
            if (!enabled || !previously_listening) mn->clean(mn_data);
            previously_listening = enabled;
            if (!enabled) continue;
            auto status = mn->detect(mn_data, result->data);
            if (status == ESP_MN_STATE_DETECTED) {
                auto *r = mn->get_results(mn_data);
                if (r && r->num > 0) {
                    ESP_LOGI(TAG, "Command id=%d probability=%.3f text=%s", r->command_id[0],
                             r->prob[0], r->string);
                    if (r->command_id[0] == 1 && r->prob[0] >= 0.85f && listening.exchange(false))
                        callback(context, esp_timer_get_time() / 1000);
                }
                mn->clean(mn_data);
            } else if (status == ESP_MN_STATE_TIMEOUT) {
                mn->clean(mn_data);
            }
        }
        detect_done.store(true);
        vTaskDelete(nullptr);
    }
};
extern "C" esp_err_t app_voice_start(photo_voice_callback_t cb, void *ctx, app_voice_handle_t *out) {
    if (!cb || !out) return ESP_ERR_INVALID_ARG;
    *out = nullptr;
    auto *v = new (std::nothrow) app_voice;
    if (!v) return ESP_ERR_NO_MEM;
    v->callback = cb; v->context = ctx;
    if (!v->init()) {
        ESP_LOGE(TAG, "Voice initialization failed");
        v->release(); delete v; return ESP_FAIL;
    }
    *out = v;
    v->feed_done.store(false);
    if (xTaskCreate([](void *p) { static_cast<app_voice *>(p)->feed(); }, "photo_mic", 4096,
                    v, 5, nullptr) != pdPASS) v->feed_done.store(true);
    v->detect_done.store(false);
    if (xTaskCreate([](void *p) { static_cast<app_voice *>(p)->detect(); }, "photo_asr", 8192,
                    v, 4, nullptr) != pdPASS) v->detect_done.store(true);
    bool ok = !v->feed_done.load() && !v->detect_done.load();
    v->ready.store(ok);
    if (!ok) { v->stop.store(true); return ESP_ERR_NO_MEM; }
    return ESP_OK;
}
extern "C" bool app_voice_ready(app_voice_handle_t v) { return v && v->ready.load(); }
extern "C" void app_voice_listen(app_voice_handle_t v, bool enabled) {
    if (v) v->listening.store(enabled && v->ready.load() && !v->stop.load());
}
extern "C" bool app_voice_stop(app_voice_handle_t v) {
    if (!v) return true;
    v->listening.store(false); v->ready.store(false); v->stop.store(true);
    int64_t end = esp_timer_get_time() + 2000000;
    while ((!v->feed_done.load() || !v->detect_done.load()) && esp_timer_get_time() < end)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (!v->feed_done.load() || !v->detect_done.load()) return false;
    v->release();
    return true;
}
