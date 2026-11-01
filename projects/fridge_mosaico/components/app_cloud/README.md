# Device Qwen client

The application worker owns JPEG encoding, verified HTTPS, strict food parsing
and spoken-correction interpretation. The controller is the sole inventory writer.
Vision accepts all human food and drinks, including packaged products recognized
from labels. Return category, visible brand, quantity, food/drink kind and evidence
with verbatim label text. Ignore flavor. The vision prompt receives no inventory.
The parser downgrades drinks to uncertain if labels do not support the category;
a brand/logo alone is insufficient. Count packages, never net contents. Send
OBSERVE to the controller; only its local matcher can choose inventory IDs.
The simulator uses the same policy in `tools/service.py:VISION_PROMPT`.

`app_cloud_capture` copies one RGB565 frame and queues a bounded job. Preview
continues independently. Responses carry session/capture IDs and are rejected if
another capture or a return home invalidated them. Feedback remains visual-only;
`app_voice` speaks only entry greetings and settings tests. No cloud TTS call is made.

`app_cloud_voice_state` copies the latest batch. `app_cloud_feed_audio` accepts
16 kHz signed mono PCM after AEC, into a one-second bounded ring. The ASR worker
opens Qwen3 ASR Flash Realtime while the camera is open and closes it on exit.
Server VAD records the batch identity and item ID at speech start. Audio sample
timestamps reject utterances that began before the current capture, even if the
server event arrives late. Final transcripts queue a
qwen-flash command job on the existing cloud worker. Allowed commands are a
validated quantity/date correction of an existing batch item, undo, resume and
finish. Audio backlog overflow reconnects rather than applying delayed speech.

Start once after Wi-Fi initialization. The objects live until reboot. UI calls
never perform networking; shared state uses mutexes and atomics. HTTPS requests
have a 45-second deadline, 10-second socket timeout and 16 KiB response limit.
WebSocket sends have a one-second deadline and reconnect backoff. Errors do not
change inventory. Unknown/failed corrections resume speech and display an error.

Configuration is supplied via application RPC method 4 and app-owned NVS. No key
is embedded, returned in diagnostics or logged. Only fixed DashScope Beijing
endpoints receive the key, with certificate verification. The cloud worker starts
SNTP as soon as Wi-Fi has an address, even before the first photograph, so ASR
can establish verified TLS on a fresh boot. SNTP supplies dates in UTC+8.
Status includes vision HTTP/request ID/latency and ASR readiness, sent
samples and transcript counts, without recordings or credentials.

Dependencies: app_fridge, cJSON, ESP HTTP/WebSocket/TLS, JPEG, SNTP, NVS, FreeRTOS.

Compute/audio workers use explicit PSRAM stacks to preserve internal LCD DMA
memory. These workers never perform NVS/flash writes; the controller and RPC
configuration paths retain internal stacks. Do not move flash writes into them.
