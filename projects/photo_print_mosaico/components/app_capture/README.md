# app_capture

Startup reserves Classic Bluetooth, mono audio and the right-board driver before
loading the models. It then sets the application-wide ordinary malloc/new policy
to prefer PSRAM (`heap_caps_malloc_extmem_enable(0)`). ESP-DL's transient graph
objects must not consume internal RAM needed by FreeRTOS and peripheral drivers;
explicit internal/DMA-capability allocations are unaffected.

Owns the camera, PPA, two hand models, reusable PSRAM buffers, frozen image, timed
controller and one 16KiB worker. Depends on app_photo/app_printer/app_controls and
Espressif camera, ESP-DL and hand model components.

UI/physical button callbacks enqueue timestamped events only. The worker owns every
controller/model/camera call. Snapshot queue length 1 exposes the most recent state;
input queue length 8 preserves double clicks during inference. Commands received
while printing are discarded after transfer, preventing an unintended queued print.

PPA crops 768 square pixels and rotates/scales to 384x384 BGR888. Gesture analysis is
attempted at most every 200ms while live. A single detected hand must score >=0.65,
span >=45 pixels in both dimensions, and classify as five with score >=0.80. Pure
controller logic requires temporal persistence and rearming after hand absence.

Models initialize before camera buffers to reduce PSRAM fragmentation. Audio/keys
run in independent owned tasks. Captured point data and three RGB565 buffers remain
in PSRAM and are never stored or uploaded. Shutdown cancels sends, stops camera/PPA,
keys/audio and Bluetooth before resetting; shutdown callback waits up to 8 seconds.
