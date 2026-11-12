#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t app_printer_init(void);
/* Call from the single camera/print worker, not a Bluetooth callback. */
void app_printer_poll(void);
bool app_printer_ready(void);
/* Reboot path only: prevent additional chunks; any in-flight wait ends within 5s. */
void app_printer_cancel(void);
/* After sending stops, release Bluetooth from the owning worker before reboot. */
esp_err_t app_printer_stop(void);
/* Synchronous, bounded sending of one 320x320 MSB-first raster. */
esp_err_t app_printer_send(const uint8_t *bitmap);
#ifdef __cplusplus
}
#endif
