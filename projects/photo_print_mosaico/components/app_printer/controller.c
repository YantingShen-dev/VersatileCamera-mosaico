#include "esp_bt.h"
#include "esp_err.h"

/* Keep the pinned SDK's C-only controller declarations out of C++ sources.
 * The caller is the 8 KiB startup worker; this config is used only at startup. */
esp_err_t photo_controller_start(void)
{
    esp_bt_controller_config_t config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_err_t result = esp_bt_controller_init(&config);
    if (result != ESP_OK) {
        return result;
    }
    return esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
}

esp_err_t photo_controller_stop(void)
{
    esp_err_t result = ESP_OK;
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        result = esp_bt_controller_disable();
    }
    if (result == ESP_OK && esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        result = esp_bt_controller_deinit();
    }
    return result;
}
