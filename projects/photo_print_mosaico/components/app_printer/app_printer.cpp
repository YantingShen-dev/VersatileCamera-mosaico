#include "app_printer.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_log.h"
#include "esp_spp_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>
extern "C" esp_err_t photo_controller_start(void);
extern "C" esp_err_t photo_controller_stop(void);
namespace {
constexpr EventBits_t ACK = 1, FREE = 2;
/* Bluedroid callbacks do not supply user context; this component owns one session. */
struct Printer {
    esp_bd_addr_t address = {0x86, 0x67, 0x7a, 0x63, 0x30, 0xa8};
    EventGroupHandle_t events = nullptr;
    std::atomic<uint32_t> handle{0};
    std::atomic<bool> initialized{false}, connecting{false}, write_ok{false};
    std::atomic<bool> cancelled{false};
    int64_t attempt = 0; // accessed only by the camera/print worker
    uint8_t packet[256] = {};
} printer;
const char *TAG = "photo_printer";
void spp(esp_spp_cb_event_t event, esp_spp_cb_param_t *p) {
    switch (event) {
    case ESP_SPP_INIT_EVT:
        printer.initialized.store(p->init.status == ESP_SPP_SUCCESS);
        break;
    case ESP_SPP_DISCOVERY_COMP_EVT:
        if (printer.cancelled.load())
            break;
        if (p->disc_comp.status == ESP_SPP_SUCCESS && p->disc_comp.scn_num > 0) {
            esp_err_t e = esp_spp_connect(ESP_SPP_SEC_AUTHENTICATE, ESP_SPP_ROLE_MASTER,
                                          p->disc_comp.scn[0], printer.address);
            if (e != ESP_OK)
                printer.connecting.store(false);
        } else
            printer.connecting.store(false);
        break;
    case ESP_SPP_OPEN_EVT:
        printer.connecting.store(false);
        if (p->open.status == ESP_SPP_SUCCESS) {
            printer.handle.store(p->open.handle);
            xEventGroupSetBits(printer.events, FREE);
            ESP_LOGI(TAG, "RPP02N connected handle=%lu", (unsigned long)p->open.handle);
        } else
            ESP_LOGW(TAG, "SPP open failed %d", p->open.status);
        break;
    case ESP_SPP_CLOSE_EVT:
        printer.handle.store(0);
        printer.connecting.store(false);
        printer.write_ok.store(false);
        xEventGroupSetBits(printer.events, ACK | FREE);
        ESP_LOGW(TAG, "Printer disconnected");
        break;
    case ESP_SPP_WRITE_EVT:
        printer.write_ok.store(p->write.status == ESP_SPP_SUCCESS);
        if (p->write.cong)
            xEventGroupClearBits(printer.events, FREE);
        else
            xEventGroupSetBits(printer.events, FREE);
        xEventGroupSetBits(printer.events, ACK);
        break;
    case ESP_SPP_CONG_EVT:
        if (p->cong.cong)
            xEventGroupClearBits(printer.events, FREE);
        else
            xEventGroupSetBits(printer.events, FREE);
        break;
    default:
        break;
    }
}
void gap(esp_bt_gap_cb_event_t e, esp_bt_gap_cb_param_t *p) {
    if (e == ESP_BT_GAP_PIN_REQ_EVT && !memcmp(p->pin_req.bda, printer.address, 6)) {
        esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
        esp_bt_gap_pin_reply(p->pin_req.bda, true, 4, pin);
    }
    if (e == ESP_BT_GAP_AUTH_CMPL_EVT)
        ESP_LOGI(TAG, "Pairing result %d", p->auth_cmpl.stat);
}
esp_err_t write_packet(uint8_t *data, unsigned length) {
    if (printer.cancelled.load())
        return ESP_ERR_INVALID_STATE;
    if (!(xEventGroupWaitBits(printer.events, FREE, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000)) & FREE))
        return ESP_ERR_TIMEOUT;
    uint32_t handle = printer.handle.load();
    if (!handle)
        return ESP_ERR_INVALID_STATE;
    if (printer.cancelled.load())
        return ESP_ERR_INVALID_STATE;
    xEventGroupClearBits(printer.events, ACK);
    printer.write_ok.store(false);
    esp_err_t e = esp_spp_write(handle, length, data);
    if (e != ESP_OK)
        return e;
    if (!(xEventGroupWaitBits(printer.events, ACK, pdTRUE, pdTRUE, pdMS_TO_TICKS(5000)) & ACK)) {
        esp_spp_disconnect(handle);
        return ESP_ERR_TIMEOUT;
    }
    if (!printer.handle.load() || !printer.write_ok.load())
        return ESP_FAIL;
    // SPP completion/congestion events pace the link. A short yield avoids
    // flooding the printer without pausing the thermal head between strips.
    vTaskDelay(pdMS_TO_TICKS(10));
    return ESP_OK;
}
} // namespace
extern "C" esp_err_t app_printer_init(void) {
    printer.events = xEventGroupCreate();
    if (!printer.events)
        return ESP_ERR_NO_MEM;
    esp_err_t e = photo_controller_start();
    if (e)
        return e;
    esp_bluedroid_config_t host = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    host.ssp_en = false;
    e = esp_bluedroid_init_with_cfg(&host);
    if (e)
        return e;
    e = esp_bluedroid_enable();
    if (e)
        return e;
    e = esp_bt_gap_register_callback(gap);
    if (e)
        return e;
    esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
    e = esp_bt_gap_set_pin(ESP_BT_PIN_TYPE_FIXED, 4, pin);
    if (e)
        return e;
    e = esp_spp_register_callback(spp);
    if (e)
        return e;
    esp_spp_cfg_t spp_config = {};
    spp_config.mode = ESP_SPP_MODE_CB;
    return esp_spp_enhanced_init(&spp_config);
}
extern "C" void app_printer_poll(void) {
    if (!printer.initialized.load() || printer.handle.load())
        return;
    int64_t now = esp_timer_get_time();
    if (printer.connecting.load()) {
        if (now - printer.attempt < 25000000)
            return;
        printer.connecting.store(false);
    }
    if (now - printer.attempt < 5000000)
        return;
    printer.attempt = now;
    printer.connecting.store(true);
    esp_err_t e = esp_spp_start_discovery(printer.address);
    if (e)
        printer.connecting.store(false);
}
extern "C" bool app_printer_ready(void) { return printer.handle.load() != 0; }
extern "C" void app_printer_cancel(void) { printer.cancelled.store(true); }
extern "C" esp_err_t app_printer_stop(void) {
    printer.cancelled.store(true);
    printer.initialized.store(false);
    esp_err_t e = ESP_OK;
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED)
        e = esp_bluedroid_disable();
    if (e == ESP_OK && esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED)
        e = esp_bluedroid_deinit();
    if (e == ESP_OK)
        e = photo_controller_stop();
    printer.handle.store(0);
    return e;
}
extern "C" esp_err_t app_printer_send(const uint8_t *bitmap, unsigned width, unsigned height) {
    if (!bitmap || !width || width > 384 || width % 8 || !height || height > 2048)
        return ESP_ERR_INVALID_ARG;
    if (!app_printer_ready())
        return ESP_ERR_INVALID_STATE;
    // Reset then center within the printer's printable line (not the paper edges).
    uint8_t init[] = {0x1b, 0x40, 0x1b, 0x61, 0x01};
    esp_err_t e = write_packet(init, sizeof(init));
    if (e)
        return e;
    // ONE raster command for the complete image. Repeated short GS v 0
    // commands can insert feed/rounding gaps in this printer's firmware.
    uint8_t header[] = {0x1d,
                        0x76,
                        0x30,
                        0,
                        (uint8_t)(width / 8),
                        0,
                        (uint8_t)(height & 255),
                        (uint8_t)(height >> 8)};
    e = write_packet(header, sizeof(header));
    if (e)
        return e;
    const unsigned total = width * height / 8;
    for (unsigned pos = 0; pos < total;) {
        unsigned count = total - pos;
        if (count > sizeof(printer.packet))
            count = sizeof(printer.packet);
        memcpy(printer.packet, bitmap + pos, count);
        e = write_packet(printer.packet, count);
        if (e)
            return e;
        pos += count;
    }
    uint8_t feed[] = {0x0a, 0x0a, 0x0a};
    e = write_packet(feed, sizeof(feed));
    ESP_LOGI(TAG, "Raster transfer finished status=%s", esp_err_to_name(e));
    return e;
}
