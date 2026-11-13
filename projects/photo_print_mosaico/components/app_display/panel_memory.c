#include "bsp/esp_mosaico.h"
#include "esp_lcd_io_spi.h"
#include "esp_log.h"

/* The pinned BSP queues ten 8-line internal DMA bounce buffers (~77 KiB).
 * Speech + Classic BT leave less internal RAM than that. Bound this app's
 * panel queue using the public LCD constructor, without changing shared BSP
 * sources or enabling its intentionally disabled direct PSRAM DMA path. */
esp_err_t __real_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out);

esp_err_t __wrap_esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t bus,
    const esp_lcd_panel_io_spi_config_t *config, esp_lcd_panel_io_handle_t *out)
{
    if (!config || bus != (esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST) {
        return __real_esp_lcd_new_panel_io_spi(bus, config, out);
    }
    esp_lcd_panel_io_spi_config_t bounded = *config;
    bounded.trans_queue_depth = 2;
    ESP_LOGI("photo_display", "LCD DMA queue limited to %u chunks", (unsigned)bounded.trans_queue_depth);
    return __real_esp_lcd_new_panel_io_spi(bus, &bounded, out);
}
