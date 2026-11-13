# app_controls

Owns the right Interaction board and speaker output in separate tasks. Upper key maps
from the module RIGHT pad, lower from LEFT. GPIO mode reads the two physical
key-to-GND switches with pull-ups; this matches the user's keyboard-style keys.
40 ms debounce emits one timestamped callback per press, with no hold-repeat.

Start with a callback that queues events; never manipulate camera state from it.
Readiness is atomic. Sound requests queue a short PCM beep or a two-impact mechanical
shutter sound. Only the audio task opens/writes/closes the speaker codec.
Playback uses 16 kHz, 16-bit mono in reusable 10 ms blocks; microphone DIN is connected for the separate app_voice owner. Start after Classic Bluetooth and before the speech model to reserve
audio DMA memory without starving the Bluetooth controller startup.
Stop is reboot-only, bounded at 2.5 seconds; owned task memory remains alive until reset.
Depends on the official Interaction BSP, core board audio codec, FreeRTOS and esp_timer.
