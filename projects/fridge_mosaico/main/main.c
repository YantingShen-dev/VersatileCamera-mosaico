// SPDX-License-Identifier: Apache-2.0
#include "app_display.h"
#include "app_fridge.h"
#include "app_board.h"
#include "nvs.h"
#include <stdlib.h>
#include "esp_iris.h"
#include "esp_log.h"
#include "iris_ota_support.h"
#include "iris_screen_mirror.h"
#include "nvs_flash.h"
#include "ui_bundle.h"

static const char *TAG = "fridge_mosaico";
extern const uint8_t fridge_font_start[] asm("_binary_FridgeSans_ttf_start");
extern const uint8_t fridge_font_end[] asm("_binary_FridgeSans_ttf_end");

/* The composition root retains owned handles, never controller internals. */
static struct {
    esp_gsp_handle_t ui;
    app_fridge_handle_t controller;
    app_board_handle_t board;
} application;

static bool load_inventory(void *ctx, fridge_store_t *store)
{
    (void)ctx;
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("fridge", NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) { return true; }
    if (err != ESP_OK) { return false; }
    size_t size = 0;
    err = nvs_get_blob(nvs, "inventory", NULL, &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(nvs); return true; }
    void *data = err == ESP_OK && size <= sizeof(*store) ? malloc(size) : NULL;
    bool ok = data && nvs_get_blob(nvs, "inventory", data, &size) == ESP_OK && fridge_store_decode(store,data,size);
    free(data); nvs_close(nvs); return ok;
}

static bool save_inventory(void *ctx, const fridge_store_t *store)
{
    (void)ctx;
    nvs_handle_t nvs;
    if (nvs_open("fridge", NVS_READWRITE, &nvs) != ESP_OK) { return false; }
    esp_err_t err = nvs_set_blob(nvs, "inventory", store, sizeof(*store));
    if (err == ESP_OK) { err = nvs_commit(nvs); }
    nvs_close(nvs);
    return err == ESP_OK;
}

void app_main(void)
{
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_iris_boot_probe());
    ESP_ERROR_CHECK(nvs_flash_init());
    /* Recovery must remain reachable even when the UI bundle cannot be loaded. */
    iris_ota_support_start();

    esp_gsp_config_t config;
    const esp_err_t err = ui_bundle_open(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GSP UI unavailable (0x%x); Recovery RPC remains active", err);
        return;
    }
    config.ttf = fridge_font_start;
    config.ttf_size = fridge_font_end - fridge_font_start;
    /* FreeType's gray renderer needs a 16 KiB local pool in addition to the
     * GSP/Chinese-glyph call chain. The captured crash exceeded the old 24 KiB
     * stack by 2524 bytes. Reserve 40 KiB before camera/Wi-Fi fragment SRAM. */
    if (esp_gsp_config_set(&config, ESP_GSP_FIELD_RENDER_TASK_STACK_SIZE_FREETYPE,
                           40 * 1024) != ESP_GSP_CONFIG_SET_OK) {
        ESP_LOGE(TAG, "Cannot reserve the Chinese glyph renderer stack");
        return;
    }
    esp_gsp_esp_lcd_config_t display;
    const esp_err_t display_err = app_display_init(&display);
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "Display unavailable (0x%x); Recovery RPC remains active", display_err);
        return;
    }
    ESP_ERROR_CHECK(iris_screen_mirror_init());
    ESP_ERROR_CHECK(esp_gsp_esp_lcd_start(&config, &display, &application.ui));
    ESP_ERROR_CHECK(iris_screen_mirror_attach(application.ui));
    fridge_platform_t platform = {.load=load_inventory, .save=save_inventory};
    ESP_ERROR_CHECK(app_board_create(&application.board, &platform));
    ESP_ERROR_CHECK(app_fridge_create(application.ui, &platform, &application.controller));
    ESP_ERROR_CHECK(app_board_start(application.board, application.controller));
    /* GSP retains the callback context. Device UI/controller live until reboot;
     * teardown requires quiescing GSP before app_fridge_destroy(), then closing UI. */
    ESP_LOGI(TAG, "Fridge UI ready; camera and interaction board starting");
}
