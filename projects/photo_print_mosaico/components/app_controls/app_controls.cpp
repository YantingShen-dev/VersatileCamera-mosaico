#include "app_controls.h"
#include "bsp/esp_mosaico.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mosaico_module_interact.h"
#include <atomic>
#include <cmath>
#include <new>
struct app_controls {
    photo_button_callback_t callback = nullptr;
    void *context = nullptr;
    QueueHandle_t sounds = nullptr;
    std::atomic<bool> stop{false}, board_done{true}, sound_done{true}, board_ready{false},
        audio_ready{false};
    int16_t pcm[240] = {}; // 10 ms mono blocks owned only by the audio task.
    void buttons() {
        mosaico_interact_handle_t board = nullptr;
        bool raw[2] = {}, stable[2] = {};
        uint64_t changed[2] = {};
        bool first = true;
        while (!stop.load()) {
            if (!board) {
                mosaico_interact_config_t config = MOSAICO_INTERACT_DEFAULT_CONFIG();
                config.slot = MOSAICO_MODULE_MGR_SLOT_RIGHT;
                config.button_mode = MOSAICO_INTERACT_BUTTON_MODE_GPIO;
                config.led_brightness = 0;
                esp_err_t e = mosaico_interact_open(&config, &board);
                if (e != ESP_OK) {
                    if (board) {
                        mosaico_interact_close(board);
                        board = nullptr;
                    }
                    vTaskDelay(pdMS_TO_TICKS(250));
                    continue;
                }
                first = true;
                board_ready.store(true);
                ESP_LOGI("photo_buttons", "Right interaction board ready");
            }
            mosaico_interact_inputs_t inputs = {};
            if (mosaico_interact_read_inputs(board, &inputs) != ESP_OK) {
                board_ready.store(false);
                mosaico_interact_close(board);
                board = nullptr;
                continue;
            }
            // With the module on the right, its printed RIGHT pad is the upper key.
            bool values[2] = {inputs.right_pressed, inputs.left_pressed};
            uint64_t now = esp_timer_get_time() / 1000;
            for (unsigned i = 0; i < 2; i++) {
                if (first) {
                    raw[i] = stable[i] = values[i];
                    changed[i] = now;
                    continue;
                }
                if (values[i] != raw[i]) {
                    raw[i] = values[i];
                    changed[i] = now;
                }
                if (raw[i] != stable[i] && now - changed[i] >= 40) {
                    stable[i] = raw[i];
                    if (stable[i]) {
                        ESP_LOGI("photo_buttons", "Button %u", i + 1);
                        callback(context, i, now);
                    }
                }
            }
            first = false;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (board)
            mosaico_interact_close(board);
        board_ready.store(false);
        board_done.store(true);
        vTaskDelete(nullptr);
    }
    void audio() {
        i2s_std_config_t i2s = {};
        i2s.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(24000);
        i2s.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        i2s.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        i2s.gpio_cfg.mclk = BSP_AUDIO_I2S_MCLK;
        i2s.gpio_cfg.bclk = BSP_AUDIO_I2S_SCLK;
        i2s.gpio_cfg.ws = BSP_AUDIO_I2S_LRCLK;
        i2s.gpio_cfg.dout = BSP_AUDIO_I2S_SDOUT;
        i2s.gpio_cfg.din = GPIO_NUM_NC; // This app only plays sound; it never records.
        auto speaker = bsp_audio_init(&i2s) == ESP_OK ? bsp_audio_codec_speaker_init() : nullptr;
        esp_codec_dev_sample_info_t format = {};
        format.sample_rate = 24000;
        format.bits_per_sample = 16;
        format.channel = 1;
        bool opened = speaker && esp_codec_dev_open(speaker, &format) == ESP_CODEC_DEV_OK;
        if (opened && esp_codec_dev_set_out_vol(speaker, 65) == ESP_CODEC_DEV_OK)
            audio_ready.store(true);
        ESP_LOGI("photo_sound", "Speaker ready=%d", audio_ready.load());
        uint32_t noise = 0x21b3;
        while (!stop.load()) {
            bool shutter = false;
            if (xQueueReceive(sounds, &shutter, pdMS_TO_TICKS(50)) != pdTRUE)
                continue;
            if (!audio_ready.load())
                continue;
            unsigned length = shutter ? 3600 : 2040;
            for (unsigned start = 0; start < length && !stop.load(); start += 240) {
                for (unsigned i = 0; i < 240; i++) {
                    unsigned n = start + i;
                    float sample = 0;
                    if (n < length) {
                        if (shutter) {
                            noise ^= noise << 13;
                            noise ^= noise >> 17;
                            noise ^= noise << 5;
                            unsigned local = n < 1080 ? n : n - 1080;
                            float envelope = expf(-(float)local / (n < 1080 ? 150.0f : 325.0f));
                            sample = ((int)(noise & 65535) - 32768) * 0.32f * envelope;
                        } else {
                            float envelope = fminf(1.0f, fminf(n / 240.0f, (length - n) / 240.0f));
                            sample = 6000 * sinf(6.2831853f * 1800 * n / 24000.0f) * envelope;
                        }
                    }
                    pcm[i] = (int16_t)sample;
                }
                if (esp_codec_dev_write(speaker, pcm, sizeof(pcm)) != ESP_CODEC_DEV_OK) {
                    audio_ready.store(false);
                    break;
                }
            }
        }
        if (opened)
            esp_codec_dev_close(speaker);
        audio_ready.store(false);
        sound_done.store(true);
        vTaskDelete(nullptr);
    }
};
extern "C" esp_err_t app_controls_start(photo_button_callback_t cb, void *ctx,
                                        app_controls_handle_t *out) {
    if (!cb || !out)
        return ESP_ERR_INVALID_ARG;
    *out = nullptr;
    auto *c = new (std::nothrow) app_controls;
    if (!c)
        return ESP_ERR_NO_MEM;
    c->sounds = xQueueCreate(8, sizeof(bool));
    if (!c->sounds) {
        delete c;
        return ESP_ERR_NO_MEM;
    }
    c->callback = cb;
    c->context = ctx;
    *out = c;
    c->board_done.store(false);
    if (xTaskCreate([](void *p) { static_cast<app_controls *>(p)->buttons(); }, "photo_keys", 4096,
                    c, 4, nullptr) != pdPASS)
        c->board_done.store(true);
    c->sound_done.store(false);
    if (xTaskCreate([](void *p) { static_cast<app_controls *>(p)->audio(); }, "photo_sound", 4096,
                    c, 4, nullptr) != pdPASS)
        c->sound_done.store(true);
    return c->board_done.load() && c->sound_done.load() ? ESP_ERR_NO_MEM : ESP_OK;
}
extern "C" bool app_controls_board_ready(app_controls_handle_t c) {
    return c && c->board_ready.load();
}
extern "C" bool app_controls_audio_ready(app_controls_handle_t c) {
    return c && c->audio_ready.load();
}
extern "C" void app_controls_sound(app_controls_handle_t c, bool shutter) {
    if (c && !c->stop.load())
        xQueueSend(c->sounds, &shutter, 0);
}
extern "C" bool app_controls_stop(app_controls_handle_t c) {
    if (!c)
        return true;
    c->stop.store(true);
    int64_t end = esp_timer_get_time() + 2500000;
    while ((!c->board_done.load() || !c->sound_done.load()) && esp_timer_get_time() < end)
        vTaskDelay(pdMS_TO_TICKS(10));
    return c->board_done.load() && c->sound_done.load();
}
