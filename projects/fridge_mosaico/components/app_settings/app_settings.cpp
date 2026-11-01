#include "app_settings.h"
#include <atomic>
#include <cstring>
#include <cstdio>
#include <new>
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace {
constexpr EventBits_t GOT_IP = BIT0, LOST = BIT1;
constexpr int64_t CONNECT_TIMEOUT_US = 25000000;
struct Credentials { char ssid[33]; char password[65]; };
}
struct app_settings {
    SemaphoreHandle_t lock = nullptr;
    QueueHandle_t requests = nullptr;
    EventGroupHandle_t events = nullptr;
    esp_event_handler_instance_t wifi_handler = nullptr, ip_handler = nullptr;
    esp_netif_t *netif = nullptr;
    fridge_settings_t state = {};
    fridge_setting_request_t request = {};
    Credentials saved = {}, candidate = {};
    wifi_config_t config = {};
    wifi_ap_record_t records[FRIDGE_WIFI_MAX] = {};
    wifi_ap_record_t associated = {};
    std::atomic<unsigned> volume{45};
    std::atomic<bool> ready{false};
    unsigned persisted_volume = 45;
    bool connecting = false, candidate_active = false;
    int64_t deadline = 0, retry_at = 0;
    unsigned retry = 0;
    void status(const char *message, bool busy = false) {
        xSemaphoreTake(lock, portMAX_DELAY);
        snprintf(state.status, sizeof(state.status), "%s", message); state.busy = busy; ++state.revision;
        xSemaphoreGive(lock);
    }
    static void event(void *ctx, esp_event_base_t base, int32_t id, void *data) {
        auto *self = static_cast<app_settings *>(ctx);
        if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
            auto *ip = static_cast<ip_event_got_ip_t *>(data);
            xSemaphoreTake(self->lock, portMAX_DELAY);
            snprintf(self->state.ip, sizeof(self->state.ip), IPSTR, IP2STR(&ip->ip_info.ip));
            xSemaphoreGive(self->lock);
            xEventGroupSetBits(self->events, GOT_IP);
        } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
            xSemaphoreTake(self->lock, portMAX_DELAY);
            self->state.connected = false; self->state.ip[0] = 0; ++self->state.revision;
            xSemaphoreGive(self->lock);
            xEventGroupSetBits(self->events, LOST);
        }
    }
    void load() {
        nvs_handle_t nvs;
        esp_err_t err = nvs_open("fridge_cfg", NVS_READONLY, &nvs);
        if (err == ESP_ERR_NVS_NOT_FOUND) return;
        if (err != ESP_OK) { status("设置读取失败"); return; }
        uint8_t value = 45;
        if (nvs_get_u8(nvs, "volume", &value) == ESP_OK && value <= 100) volume.store(value);
        persisted_volume = volume.load();
        size_t size = sizeof(saved);
        if (nvs_get_blob(nvs, "wifi", &saved, &size) != ESP_OK || size != sizeof(saved) ||
            !memchr(saved.ssid, 0, sizeof(saved.ssid)) || !memchr(saved.password, 0, sizeof(saved.password))) saved = {};
        nvs_close(nvs);
    }
    bool persist(bool credentials) {
        nvs_handle_t nvs;
        esp_err_t err = nvs_open("fridge_cfg", NVS_READWRITE, &nvs);
        if (err != ESP_OK) return false;
        const unsigned value = volume.load();
        err = nvs_set_u8(nvs, "volume", value);
        if (err == ESP_OK && credentials) err = nvs_set_blob(nvs, "wifi", &candidate, sizeof(candidate));
        if (err == ESP_OK) err = nvs_commit(nvs);
        nvs_close(nvs);
        if (err == ESP_OK) persisted_volume = value;
        return err == ESP_OK;
    }
    esp_err_t init_wifi() {
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK) return err;
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
        netif = esp_netif_create_default_wifi_sta();
        if (!netif) return ESP_ERR_NO_MEM;
        // The SDK's init structure is >128 bytes; allocate it off the task stack.
        auto *init = new (std::nothrow) wifi_init_config_t(WIFI_INIT_CONFIG_DEFAULT());
        if (!init) return ESP_ERR_NO_MEM;
        // This device shares DMA RAM with a display, camera and full-duplex codec.
        // Provision a bounded station pool rather than throughput-benchmark defaults.
        init->static_rx_buf_num = 4;
        init->static_tx_buf_num = 6;
        init->cache_tx_buf_num = 8;
        init->mgmt_sbuf_num = 8;
        err = esp_wifi_init(init); delete init;
        if (err == ESP_OK) err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (err == ESP_OK) err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, event, this, &wifi_handler);
        if (err == ESP_OK) err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, this, &ip_handler);
        if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err == ESP_OK) err = esp_wifi_start();
        return err;
    }
    void connect(const Credentials &credentials, bool candidate_request) {
        connecting = false;
        // Ignore the intentional disconnect before starting the next association.
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(100));
        xEventGroupClearBits(events, GOT_IP | LOST);
        config = {};
        memcpy(config.sta.ssid, credentials.ssid, strlen(credentials.ssid));
        memcpy(config.sta.password, credentials.password, strlen(credentials.password));
        config.sta.pmf_cfg.capable = true;
        config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
        esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
        if (err == ESP_OK) err = esp_wifi_connect();
        memset(config.sta.password, 0, sizeof(config.sta.password));
        candidate_active = candidate_request;
        xSemaphoreTake(lock, portMAX_DELAY);
        state.connected = false; state.ip[0] = 0;
        snprintf(state.ssid, sizeof(state.ssid), "%s", credentials.ssid);
        xSemaphoreGive(lock);
        if (err != ESP_OK) { failed(); return; }
        connecting = true; deadline = esp_timer_get_time() + CONNECT_TIMEOUT_US;
        status(candidate_request ? "正在连接…" : "正在连接已保存的网络…", true);
    }
    void failed() {
        connecting = false;
        esp_wifi_disconnect();
        xSemaphoreTake(lock, portMAX_DELAY);
        state.connected = false; state.ip[0] = 0;
        xSemaphoreGive(lock);
        status("连接失败，请检查密码或网络");
        candidate = {}; candidate_active = false;
        retry_at = saved.ssid[0] ? esp_timer_get_time() + 30000000 : 0;
    }
    void scan() {
        status("正在扫描附近网络…", true);
        wifi_scan_config_t scan = {};
        esp_err_t err = esp_wifi_scan_start(&scan, true);
        uint16_t count = FRIDGE_WIFI_MAX;
        if (err == ESP_OK) err = esp_wifi_scan_get_ap_records(&count, records);
        if (err != ESP_OK) { esp_wifi_clear_ap_list(); status("扫描失败，请点刷新重试"); return; }
        xSemaphoreTake(lock, portMAX_DELAY);
        state.count = 0;
        for (unsigned i = 0; i < count; ++i) {
            if (!records[i].ssid[0]) continue;
            bool duplicate = false;
            for (unsigned j = 0; j < state.count; ++j)
                if (!strcmp(state.aps[j].ssid, reinterpret_cast<char *>(records[i].ssid))) duplicate = true;
            if (duplicate) continue;
            auto &ap = state.aps[state.count++];
            snprintf(ap.ssid, sizeof(ap.ssid), "%s", reinterpret_cast<char *>(records[i].ssid));
            ap.secured = records[i].authmode != WIFI_AUTH_OPEN; ap.rssi = records[i].rssi;
        }
        snprintf(state.status, sizeof(state.status), "%s", state.count ? "选择网络，* 表示需要密码" : "未找到网络，请靠近路由器后刷新");
        state.busy = false; ++state.revision;
        xSemaphoreGive(lock);
    }
    void run() {
        load();
        esp_err_t err = init_wifi();
        if (err != ESP_OK) {
            ESP_LOGE("fridge_settings", "Wi-Fi initialization failed: %s", esp_err_to_name(err));
            if (wifi_handler) esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_handler);
            if (ip_handler) esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_handler);
            esp_wifi_stop(); esp_wifi_deinit();
            if (netif) esp_netif_destroy_default_wifi(netif);
            status("Wi-Fi 启动失败，请重启");
            // Volume remains usable even when Wi-Fi initialization fails.
        } else {
            ready.store(true); status("未连接 Wi-Fi");
            if (saved.ssid[0]) connect(saved, false);
        }
        for (;;) {
            if (xQueueReceive(requests, &request, pdMS_TO_TICKS(200)) == pdTRUE) {
                if (request.action == FRIDGE_CFG_SCAN) scan();
                else if (request.action == FRIDGE_CFG_CONNECT) {
                    snprintf(candidate.ssid, sizeof(candidate.ssid), "%s", request.ssid);
                    snprintf(candidate.password, sizeof(candidate.password), "%s", request.password);
                    connect(candidate, true);
                }
                memset(&request, 0, sizeof(request));
            }
            EventBits_t bits = xEventGroupWaitBits(events, GOT_IP | LOST, pdTRUE, pdFALSE, 0);
            if ((bits & GOT_IP) && connecting && esp_wifi_sta_get_ap_info(&associated) == ESP_OK) {
                const char *expected = candidate_active ? candidate.ssid : saved.ssid;
                if (!strcmp(expected, reinterpret_cast<char *>(associated.ssid))) {
                    bool stored = !candidate_active || persist(true);
                    if (candidate_active && stored) saved = candidate;
                    candidate = {}; candidate_active = false; connecting = false; retry_at = 0; retry = 0;
                    xSemaphoreTake(lock, portMAX_DELAY); state.connected = true; xSemaphoreGive(lock);
                    status(stored ? "Wi-Fi 已连接" : "已连接，但设置保存失败");
                }
            } else if (bits & LOST) {
                if (connecting && esp_timer_get_time() < deadline && retry++ < 3) {
                    if (esp_wifi_connect() != ESP_OK) failed();
                } else if (connecting) { retry = 0; failed(); }
                else if (!retry_at && saved.ssid[0]) {
                    retry_at = esp_timer_get_time() + 5000000; status("网络断开，正在等待重连");
                }
            }
            if (connecting && esp_timer_get_time() >= deadline) { retry = 0; failed(); }
            if (!connecting && retry_at && esp_timer_get_time() >= retry_at && ready.load()) {
                retry_at = 0; retry = 0; connect(saved, false);
            }
            if (volume.load() != persisted_volume && !persist(false)) {
                status("音量已调整，但保存失败"); vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
    }
};
extern "C" esp_err_t app_settings_start(app_settings_handle_t *out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = nullptr;
    auto *self = new (std::nothrow) app_settings;
    if (!self) return ESP_ERR_NO_MEM;
    self->lock = xSemaphoreCreateMutex(); self->requests = xQueueCreate(2, sizeof(fridge_setting_request_t));
    self->events = xEventGroupCreate();
    if (!self->lock || !self->requests || !self->events) {
        if (self->lock) vSemaphoreDelete(self->lock);
        if (self->requests) vQueueDelete(self->requests);
        if (self->events) vEventGroupDelete(self->events);
        delete self; return ESP_ERR_NO_MEM;
    }
    self->status("正在启动 Wi-Fi…", true);
    if (xTaskCreate([](void *ctx){static_cast<app_settings *>(ctx)->run();}, "fridge_settings", 6144, self, 3, nullptr) != pdPASS) {
        vSemaphoreDelete(self->lock); vQueueDelete(self->requests); vEventGroupDelete(self->events); delete self; return ESP_ERR_NO_MEM;
    }
    *out = self; return ESP_OK;
}
extern "C" void app_settings_read(app_settings_handle_t self, fridge_settings_t *out) {
    if (!self || !out) return;
    xSemaphoreTake(self->lock, portMAX_DELAY); *out = self->state; out->volume = self->volume.load(); xSemaphoreGive(self->lock);
}
extern "C" unsigned app_settings_volume(app_settings_handle_t self) { return self ? self->volume.load() : 45; }
extern "C" bool app_settings_request(app_settings_handle_t self, const fridge_setting_request_t *request) {
    if (!self || !request) return false;
    if (request->action == FRIDGE_CFG_VOLUME) {
        if (request->volume > 100) return false;
        self->volume.store(request->volume);
        xSemaphoreTake(self->lock, portMAX_DELAY); ++self->state.revision; xSemaphoreGive(self->lock); return true;
    }
    if (!self->ready.load() || (request->action != FRIDGE_CFG_SCAN && request->action != FRIDGE_CFG_CONNECT)) return false;
    if (request->action == FRIDGE_CFG_CONNECT && (!request->ssid[0] || !memchr(request->ssid, 0, sizeof(request->ssid)) ||
        !memchr(request->password, 0, sizeof(request->password)))) return false;
    xSemaphoreTake(self->lock, portMAX_DELAY);
    bool ok = !self->state.busy && xQueueSend(self->requests, request, 0) == pdTRUE;
    if (ok) { self->state.busy = true; ++self->state.revision; }
    xSemaphoreGive(self->lock); return ok;
}
