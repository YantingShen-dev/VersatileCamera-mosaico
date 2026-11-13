# app_photo

Portable C UI, pure timed camera controller, and deterministic 384x384 filters.
Only GSP is required. The provider poll/command calls are nonblocking; the provider
owns the controller and all state. UI and physical inputs use the same two commands.

photo_control takes monotonically timestamped events from a single owner. Next-filter
single clicks are delayed 350ms so a double click confirms without moving selection.
It returns capture/print/beep actions; hardware completion must be reported explicitly.
A voice event is accepted only in live mode with voice_ready set. It starts a
five-second deadline and five beep actions; repeated events in countdown or
preview are ignored. Cancel clears a queued beep. Manual capture remains instant.

photo_filter_apply consumes a 384x384 BGR888 buffer and writes 18432 bytes (MSB black=1).
photo_bitmap_preview expands exactly those bits to RGB565. Neither function allocates.
Canvas buffers remain borrowed until the GSP release callback; three device buffers
are owned by app_capture. Host replaces camera/printer/model/audio only.
