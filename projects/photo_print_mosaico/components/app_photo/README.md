# app_photo

Owns the portable GSP UI and fixed-size image conversion. Depends on ESP-GSP;
the device and native simulator share the same controller and ordered dithering.

The provider's `command` callback must return immediately and report queue acceptance.
`poll` returns a copied snapshot. Commands are locked until a changed response counter
arrives; printing also blocks capture, retake and duplicate submission.

The preview is 320x320 RGB565. `app_photo_present` borrows a buffer until its release
callback; the caller must not mutate or free it earlier. The bitmap uses 40 bytes per
row, MSB-first, with set bits meaning black. The reviewed bitmap is the printed bitmap.

Create after GSP startup, then call `app_photo_present` from the capture producer.
At shutdown stop the producer before destroying the UI. Device instances live until reboot.
