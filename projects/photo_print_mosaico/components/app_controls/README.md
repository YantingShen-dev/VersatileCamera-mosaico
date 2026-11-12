# app_controls

Owns the right Interaction board and speaker output in separate tasks. Upper key maps
from the module RIGHT pad, lower from LEFT. AUTO mode prefers touch input, which
detects fingers and mechanical key-to-GND presses; GPIO is the BSP fallback.
40 ms debounce emits one timestamped callback per press, with no hold-repeat.

Start with a callback that queues events; never manipulate camera state from it.
Readiness is atomic. Sound requests queue a short PCM beep or a two-impact mechanical
shutter sound. Only the audio task opens/writes/closes the speaker codec.
Playback uses 24 kHz, 16-bit mono in reusable 10 ms blocks; microphone DIN is
unassigned. Start after Classic Bluetooth and before the vision models to reserve
audio DMA memory without starving the Bluetooth controller startup.
Stop is reboot-only, bounded at 2.5 seconds; owned task memory remains alive until reset.
Depends on the official Interaction BSP, core board audio codec, FreeRTOS and esp_timer.
