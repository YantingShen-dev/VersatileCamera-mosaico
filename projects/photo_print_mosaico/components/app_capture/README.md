# app_capture

Owns camera, PPA, reusable PSRAM preview buffers, frozen image, timed controller
and one 16KiB worker. Dependencies: app_photo, app_printer, app_controls, app_voice
and the official camera/BSP/Iris components. No hand model or image-probe RPC.

Startup reserves Classic Bluetooth and duplex 16kHz audio/keys first. Ordinary
malloc/new then prefers PSRAM before speech model initialization and camera
buffers; explicit internal/DMA allocations retain their capability requirements.

UI/key/voice callbacks enqueue timestamped events. The worker is the sole owner
of the controller and camera. Queue length 8 preserves button double clicks;
voice is accepted only in live mode and starts a five-second countdown. Speech
listening is disabled in filter/countdown/capture/preview/printing/error modes.
Commands queued while printing are discarded after transfer.

PPA crops a central 768 square, rotates and scales to 384x384 BGR888. Filters write
another bitmap; camera buffers and photos stay in memory. Shutdown stops camera,
voice, audio/keys and Bluetooth, with an eight-second shutdown-handler bound.

Maintenance poweroff: Iris RPC service 0x5048 method 1, exact ASCII OFF (hex
4f4646). Acknowledges 01, cancels printing, stops peripherals, signals official
BSP shutdown and falls back to deep sleep if USB retains power. Wake requires
the Power button/power cycle. It never modifies saved Ideas.
