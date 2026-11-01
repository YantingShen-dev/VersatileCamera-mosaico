#pragma once
#include <stdbool.h>
#include <stdint.h>

#define FRIDGE_WIFI_MAX 12
typedef struct {
    char ssid[33];
    bool secured;
    int8_t rssi;
} fridge_wifi_ap_t;
typedef struct {
    uint32_t revision;
    unsigned volume; /* 0..100; zero is mute */
    bool connected, busy;
    unsigned count;
    char status[128], ssid[33], ip[16];
    fridge_wifi_ap_t aps[FRIDGE_WIFI_MAX];
} fridge_settings_t;
typedef enum { FRIDGE_CFG_SCAN = 1, FRIDGE_CFG_CONNECT, FRIDGE_CFG_VOLUME, FRIDGE_CFG_TEST } fridge_setting_action_t;
typedef struct {
    fridge_setting_action_t action;
    unsigned volume;
    char ssid[33], password[65];
} fridge_setting_request_t;
