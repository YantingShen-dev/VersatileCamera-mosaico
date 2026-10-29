# Face pipeline

The single `face_vision` task owns the official MSR/MNP detector and MFN feature
extractor, camera lifecycle, synchronous PPA conversion, NVS and recognition state.
`app_main` creates the provider, creates the UI, then starts the worker.

The left camera's UYVY frame is center-cropped, rotated 90 degrees and converted to
320x320 BGR888. Detection, landmarks, features and preview boxes use that same
orientation. PPA completes before returning the camera frame. Three reusable
RGB565 preview buffers remain borrowed by GSP until their release callbacks.
The shutdown handler asks the worker to stop capture before a software reboot.

UI requests cross a length-one queue; UI polls a length-one snapshot queue without
blocking. A response counter acknowledges commands even when enrollment is refused.
One face of at least 65x65 pixels is required. Features use the model's L2-normalized
float output and a dot-product threshold of 0.55. This threshold needs validation on
the actual camera and lighting; it is not a calibrated probability.

At most five records are stored under the dedicated `face_mfn_v1` NVS namespace.
Each record has a model/schema marker and dimension. NVS commits precede success
feedback. No captured images, Wi-Fi settings, system metadata or Idea files are
written. A bad record is reported, not erased automatically. Delete affects only
the selected slot, and IDs of other slots stay unchanged.

Dependencies: app_face (UI contract), mosaico_module_camera (BSP), ESP-DL face model
components, PPA, NVS and FreeRTOS. Hardware initialization failures remain visible
while the separate Iris recovery service stays reachable.
