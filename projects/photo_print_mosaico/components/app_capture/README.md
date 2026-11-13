# app_capture

Startup reserves Classic Bluetooth, mono audio and the right-board driver before
loading the models. It then sets the application-wide ordinary malloc/new policy
to prefer PSRAM (`heap_caps_malloc_extmem_enable(0)`). ESP-DL's transient graph
objects must not consume internal RAM needed by FreeRTOS and peripheral drivers;
explicit internal/DMA-capability allocations are unaffected.

Maintenance power-off uses Iris RPC service `0x5048`, method `1`, exact payload
`OFF` (`4f4646`). It acknowledges one byte, cancels pending printing, then the
capture owner stops camera/audio/buttons/Bluetooth and calls the official BSP
shutdown signal. If USB keeps the board powered, BSP deep sleep is the fallback.
Wake requires the board Power button/power cycle. This RPC is only available
while this application is running; it never edits other Ideas.

Developer image probe uses Iris RPC service `0x5048`, method `2`. It accepts one
384x384 grayscale frame into PSRAM, then substitutes that input for ten seconds
through the same detector, classifier, live controller and sound path. The worker
starts a fresh live hold and frees the uploaded frame after ten seconds. A captured
preview stays until retake; the probe never requests printing. There is no flash
write. RPC opcodes: 0 begin, 1 append (uint32 LE offset + <=1000 raw bytes), 2 run,
3 abort incomplete upload, 4 read state. States are 0 idle, 1 allocating, 2 uploading,
3 queued, 4 running. `tools/probe_image.py` uses only official mosaico.py RPC calls.
The uploader preserves aspect ratio with white padding and converts to grayscale;
this diagnoses that supplied image, not the quality of live camera capture.

Owns the camera, PPA, two hand models, reusable PSRAM buffers, frozen image, timed
controller and one 16KiB worker. Depends on app_photo/app_printer/app_controls and
Espressif camera, ESP-DL and hand model components.

UI/physical button callbacks enqueue timestamped events only. The worker owns every
controller/model/camera call. Snapshot queue length 1 exposes the most recent state;
input queue length 8 preserves double clicks during inference. Commands received
while printing are discarded after transfer, preventing an unintended queued print.

PPA crops 768 square pixels and rotates/scales to 384x384 BGR888. Gesture analysis is
attempted at most every 200ms while live. HandDetect applies its default 0.25 score
threshold; there is no second detection-score gate. A single hand must span >=45
pixels in both dimensions and classify as five with score >=0.80. Pure
controller logic requires temporal persistence and rearming after hand absence.

Models initialize before camera buffers to reduce PSRAM fragmentation. Audio/keys
run in independent owned tasks. Captured point data and three RGB565 buffers remain
in PSRAM and are never stored or uploaded. Shutdown cancels sends, stops camera/PPA,
keys/audio and Bluetooth before resetting; shutdown callback waits up to 8 seconds.
