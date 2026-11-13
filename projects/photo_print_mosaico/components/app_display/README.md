# app_display

Adapts the BSP panel and optional touch to GSP's display configuration. It owns
application display policy (RGB565, byte order, rotation, TE and render alignment),
not UI behavior.
The public dependency is ESP-GSP; BSP, display-present, LCD and logging are private
implementation dependencies. There is no dependency on `app_hello` or `main`.

`panel_memory.c` wraps the public LCD SPI constructor at link time for the BSP
LCD host only. It limits the transaction queue to two of the BSP's 8-line chunks
(about 15 KiB internal DMA memory instead of 77 KiB). The original constructor
owns allocation, callbacks and lifetime; other buses pass through unchanged.
Direct PSRAM DMA remains disabled as in the pinned BSP. This bound is required
when camera, offline speech and Classic Bluetooth run together. Verify actual
panel refresh and absence of SPI allocation errors, not just application boot.

```c
esp_gsp_esp_lcd_config_t display;
esp_err_t err = app_display_init(&display);
/* On ESP_OK, pass &display to esp_gsp_esp_lcd_start with the bundle config. */
```

The CO5300 QSPI panel requires both the refresh window's X origin and width to
be multiples of 4 pixels. `app_display_init()` explicitly sets GSP's
`render_alignment.x_pixels = 4` and `render_alignment.width_pixels = 4` on the
device. Keep both values: GSP defaults to no expansion, and the BSP's area rounder
only handles LVGL. Existing generated applications must apply the same change
to their device display configuration; updating the template does not patch them.

After changing display initialization, verify partial refresh on the physical
panel: repeated counter updates, the celebration animation, and scrolling in
applications that provide it. Check for shifted pixels, corruption and stale
regions. Host simulation and Iris screenshots cannot establish panel alignment;
Iris captures the pixel buffer before the panel transfer.

Call once from boot, before GSP starts; do not call from ISR or concurrently.
Configuration is copied to caller storage only on success. Hardware handles are
borrowed from the singleton BSP and remain alive until reboot. Optional touch
failure yields `ESP_OK` with `display.touch == NULL`; display failure propagates.
The caller owns configuration storage, not hardware handles; do not free those
handles or reinitialize the BSP while GSP uses them. Resolution, pixel clock (Hz),
rotation and TE wiring come from BSP settings. NULL output returns
`ESP_ERR_INVALID_ARG`; missing panel/io returns `ESP_ERR_INVALID_STATE`; other
display initialization errors propagate from the BSP.
Hardware initialization may block using driver timeouts; there is no additional
application deadline. Partial hardware initialization is not rolled back. Keep
Recovery running if UI startup cannot proceed.

Private helpers remain static in the source file. If private headers become
necessary, put them under `private/` and declare only `PRIV_INCLUDE_DIRS`.
