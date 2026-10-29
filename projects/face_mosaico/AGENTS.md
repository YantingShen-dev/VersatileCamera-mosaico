# Face Idea

- User approved the 480x480 dark/green design on 2026-10-29: offline enrollment,
  recognition, five stable numbered slots, delete confirmation and persistent data.
- Use the workspace's pinned ESP-IDF and official `mosaico.py` device workflow.
- UI source and shared controller are in `components/app_face`; hardware/model
  ownership is in `components/app_vision`; `main` only wires them together.
- The simulator provider is a test fixture, never real face recognition evidence.
- Source font is an OFL Noto Sans SC subset. Rebuild scene/assets using
  `tools/make_scene.py`, with `FACE_FONT_SOURCE` pointing to the original font.
- Preserve the existing partition table and other Ideas. Do not erase NVS.
- Read the repository README for new-machine setup. Build through `../../build.ps1`
  from this project, using the SDK revision in the root `versions.json`.
- Use short ASCII paths on Windows. Never commit local SDKs, build directories,
  Gateway state, credentials, captured camera images or enrolled face records.
- Device acceptance requires actual preview, enrollment, subsequent matching,
  deletion, reboot persistence and saving the app to the Vibe Mode Idea library.
