# Local speech

Owns the speaker, microphone, Xiaole Chinese TTS and full duplex acoustic echo
cancellation. `app_board` creates it before Wi-Fi and publishes controller
snapshots. Only a new IN/OUT operation greeting or the settings sound test is
spoken. Results, corrections, undo and errors remain visual-only.
The microphone sends 16 kHz signed mono PCM to `app_cloud` while the camera is
open. No camera, inventory or network operations execute on the audio worker.

The voice resource is mapped from the `voice_data` partition and included in
System Update. Synthesis streams the entry prompt at speed 4. A new capture
cancels any unfinished greeting. Spoken corrections still use the microphone
and cloud command parser; there is no result speech queue to interrupt or resume.
All cross-task state is mutex protected; generation counters cancel playback in
at most one 20 ms PCM write. Codec buffers and AEC frames have instance ownership.
AEC alignment and barge-in sensitivity require real acoustic testing on the board.

Compute/audio workers use explicit PSRAM stacks to preserve internal LCD DMA
memory. These workers never perform NVS/flash writes; the controller and RPC
configuration paths retain internal stacks. Do not move flash writes into them.
