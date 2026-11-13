# app_voice

Offline Chinese command recognition for exactly `准备拍照` (`zhun bei pai zhao`).
Owns the microphone handle, ESP-SR 2.4.7 AFE, Chinese MultiNet7, model list, PCM
buffer and feed/detect tasks. Audio is 16 kHz, signed 16-bit mono. AFE enables
WebRTC VAD/AGC without WakeNet, AEC or neural denoising. MultiNet listens directly
with a 0.85 detection threshold; no extra wake phrase or network is required.

Start after app_controls has initialized the shared BSP duplex I2S at 16 kHz.
The camera worker toggles listening only in live view. The recognizer consumes
and clears buffered results while disabled; a detected command enqueues one
monotonic timestamp callback. UI/countdown/capture remain owned by app_capture.
Only microphone level summaries and recognized commands are logged, never audio.

pack_voice_model.py uses the pinned upstream packer to embed mn7_cn plus the
single command table in app rodata. srmodel_load accesses it without another
partition, preserving the retained Vibe layout and existing Ideas. No user
voice or credentials are embedded. Stop recognition/feed before closing speaker
I2S; shutdown waits up to two seconds and frees models after both tasks exit.

Hardware acceptance requires microphone levels, exact phrase trigger, five
beeps, shutter, preview, duplicate suppression and normal button behavior.
