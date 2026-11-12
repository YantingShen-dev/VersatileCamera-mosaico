# app_capture

Owns camera, PPA, the frozen bitmap, three reusable PSRAM preview buffers, and a
single worker. Depends on app_photo, app_printer and the official camera BSP.

`app_capture_create` allocates length-one command/snapshot queues; pass the returned
provider to `app_photo_create`, then call `app_capture_start` with that UI handle.
Only the worker accesses camera and bitmap memory. UI callbacks use nonblocking queues.

Camera UYVY frames are cropped to a central square, rotated 90 degrees, and converted
by PPA to 320x320 BGR888. Capture freezes the dithered image. Review does not update
the image; printing uses the same frozen bits. Retake resumes capture. No photo is
saved to NVS or uploaded. Shutdown cancels further printer chunks, then the worker stops camera/PPA and
Bluetooth before reboot. The shutdown handler waits up to 8 seconds for that worker.
