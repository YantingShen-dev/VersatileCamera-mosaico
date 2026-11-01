# app_board

Device-only camera and interaction-board adapter. The native fridge controller
remains the sole inventory owner. Worker tasks queue commands; `poll` dispatches
them on the UI timer. Camera frames are centrally cropped, rotated 90 degrees,
and converted to RGB565 with PPA. Three buffers stay borrowed until GSP releases
them. Worker shutdown closes the camera before the retained firmware reboots.

Right-slot interaction key L maps to IN and key R to OUT; physical orientation
must be verified on the attached board. The top AI button returns home. No module
EEPROM is changed and unidentified boards are not forced into a pin mode.

Application RPC service 0x4652: method 1 is a read-only JSON status snapshot;
method 2 accepts a JSON action and queues it for the controller. The adapter is
owned by main until reboot. It does not own inventory persistence or credentials.

Every shutter press samples three frames at least 120 ms apart, scores green-channel
edge gradients and copies the sharpest frame to `app_cloud`. The reusable 460800-byte
PSRAM buffer is board-owned until reboot; this is not multi-image inference. All preview frames
continue to GSP while recognition runs. Parsed results return through the
controller queue as OBSERVE, then resolve automatically or await a camera-overlay choice.
Read-only RPC method 5 returns pending choice IDs and current session/capture;
method 2 SELECT/MORE requires matching tokens. `app_voice` owns both codecs, local
Chinese TTS, microphone AEC and the audio stream to Qwen ASR. Snapshot publication
occurs outside the board mutex. The voice component is initialized before Wi-Fi
to reserve DMA memory. Runtime flags confirm initialization, not acoustic quality.
RPC method 4 provisions app_cloud's NVS configuration; status returns no key.

Settings are composed through `app_settings`; only the audio task updates codec
volume and mute. RPC method 3 accepts `{"setting":1}` (scan),
`{"setting":3,"volume":45}` (0–100), or `{"setting":4}` (test prompt).
It never accepts or returns Wi-Fi credentials. Method 2 action 12 opens settings.
Status reports desired/applied volume, Wi-Fi state and scan count, without SSIDs
or passwords. These controls aid device acceptance; simulator pointer tests
validate UI event routing separately from hardware service checks.
