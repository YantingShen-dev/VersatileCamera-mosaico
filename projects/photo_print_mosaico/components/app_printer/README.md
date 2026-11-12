# app_printer

Owns one Bluedroid Classic SPP client for RPP02N at 86:67:7A:63:30:A8, PIN 0000.
Depends on ESP-IDF Bluetooth and FreeRTOS. The API callbacks have no context argument,
so this component owns one static session, rather than exporting cross-component globals.

Initialize once from the camera worker; call poll while idle to discover/connect.
Readiness is atomic. `app_printer_send` is single-caller, synchronous and must not run
on a UI or Bluetooth callback task. It consumes a width*height/8-byte immutable bitmap, emits
ESC @, ESC a center alignment and ONE GS v 0 image (384x384 dots), then feeds three lines.
The pixel stream uses 256-byte chunks with 10 ms yields, without inserting
new raster commands or feed instructions between chunks. Each write waits for
the SPP completion event and congestion clearance, with 5-second waits and pacing.

Failure never automatically resubmits a job. A partial print may already exist;
the UI retains the photo and requires a new deliberate print action. Success means
bytes were accepted by the Bluetooth stack, not a paper-out/thermal-head status report.

The bitmap command was verified on this exact printer from Windows before integration.
Protocol reference: https://download4.epson.biz/sec_pubs/pos/reference_en/escpos/gs_lv_0.html

Reboot calls cancel, then the owning worker disables/deinitializes Bluedroid and
the controller with app_printer_stop before the SDK shutdown callbacks run.
This avoids the observed watchdog reset in the pinned S31 fast shutdown path.
