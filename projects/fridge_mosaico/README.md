# 日常有鲜 / Fridge Mosaico

Native 480x480 GSP fridge inventory prototype. From the workspace root:

Current 1.2.0 behavior: recognition/correction/undo/error feedback is visual-only.
New observations use brand + category, without flavor. Internal product IDs allow
unbranded OUT observations to resolve to a unique stocked category. Ambiguous
products and drinks without readable category evidence require a small on-camera
choice; unresolved batches do not change stock. Three frames are sampled per
shutter press and the sharpest is uploaded. Legacy v1 inventory migrates to v2
without losing quantities/dates. Use 1.2.0+ after migration; older apps cannot read v2.
Only IN/OUT entry greetings and the settings sound test are spoken. Microphone
corrections remain available. Snack names retain visible brand/category;
generic old inventory names cannot override clearer image evidence. Unreadable
details are not invented. Earlier version notes below are historical.

```powershell
./scripts/fridge-sim.ps1
```

Open http://127.0.0.1:3223/ for the native screen, camera/photo input, simulated
upper/lower buttons and voice controls. The native renderer also runs at 3222.
See [使用指南](../../docs/fridge-simulator.md) for setup and validation boundaries.

- components/app_fridge: C inventory, shared UI/controller and PC adapter.
- tools/service.py: loopback-only Qwen vision, ASR and correction service.
- tools/web: framebuffer viewer plus PC camera/microphone/TTS providers.
- tools/make_scene.py: native scene/font generation; authored scene is committed.
- tests/store_test.c and tools/test_service.py: offline regression checks.
- tools/check_qwen.py: opt-in live model checks, using API quota.
- tools/check_flow.py: empty-inventory fixture check. Close the companion page
  before running, then undo the test transaction afterwards.

Credentials, photos, speech fixtures and inventory belong only in the ignored
workspace .mosaico-state/fridge directory. No key enters the browser bundle.

Device preview was installed through System Update on 2026-10-30. It preserves
the retained Vibe Mode layout and uses NVS inventory persistence. The camera,
right interaction board and speaker initialize successfully. The first IN/OUT
press opens the camera and plays a local Chinese prompt; the second captures a
photo while live preview continues. The home screen also has IN/OUT touch buttons, with shutter and back
buttons over the full-screen camera. Top AI returns home. The user reported that
the physical interaction keys do not respond; their cause is still unconfirmed.
The device Qwen client uploads the captured frame directly over Wi-Fi, validates
food/drink results, and saves inventory through the native controller. Version
1.1.0 keeps the camera open and displays upward-moving, fading quantity badges.
Each shutter press is a separate transaction; there is no confirmation screen.
Local ESP-SR Xiaole TTS reads each item. AEC/VAD and Qwen3 ASR Flash Realtime
support spoken interruption, with qwen-flash interpreting corrections to the
latest batch. Saying a quantity replaces that batch's quantity, not total stock.

Build with `./scripts/fridge-build.ps1 build`. This stages sources under the ASCII
environment path to work around Windows GNU objdump's Unicode-path limitation.
Dynamic TTF rendering keeps the UI image within its existing 1 MiB partition.


The home Settings button opens Wi-Fi selection/password entry and volume controls.
Successful Wi-Fi credentials and volume are saved independently of inventory in
NVS. The PC adapter uses the host network. The device uses its own Wi-Fi connection.
See `components/app_settings/README.md` for lifecycle and network support limits.


Device cloud configuration is provisioned once from the ignored local `.env`:

```powershell
python projects/fridge_mosaico/tools/provision_device_cloud.py
```

Run it from the repository root with the selected device connected through the
project Gateway. It invokes `mosaico.py` in-process and sends the payload through
stdin, keeping the key out of shell history, subprocess command lines, firmware
and status logs. The device persists it in its app-owned NVS namespace.
See `components/app_cloud/README.md` for ownership, timeouts and API boundaries.
The native parser and real Qwen WAV format are covered by `tests/cloud_test.c`.

The 2026-10-31 black-screen core dump identified a FreeType gray-renderer stack
overflow while drawing Chinese inventory names. Its 16 KiB local raster pool plus
the GSP call chain exceeded the previous 24 KiB task stack by 2524 bytes. `main.c`
now reserves a 40 KiB FreeType render task through the public per-instance config,
before camera and Wi-Fi initialization. This hardware-only failure is not covered
by desktop simulator runs. Keep existing NVS inventory when reproducing it: the
saved Chinese names also exercise the failure immediately after a restart.

Installing a development build does not automatically add it to My Ideas. In
Vibe Mode, use Develop → Save Current Build; it saves the installed application
and UI resources locally without packaging inventory or credentials.

Version 1.0.1 expands vision from fresh ingredients to all human food and drinks,
including labeled packaged products. A visible bottle/carton/bag counts as one;
label net weight/volume is not a quantity and no unit field is added. The native
and simulator prompts were checked for equality and Qwen still returned an empty
list for the current nonfood camera scene. A live bottle acceptance check remains
pending until a drink is placed in view; this is not claimed as verified packaging
recognition on the device.
The 1.0.1 build was installed through official App Update, saved in My Ideas
(local code `d1e7af`), and launched from that saved copy. Final normal boot
`6449859799153065252` reported version 1.0.1 with zero crashes; the existing
apple quantity, Wi-Fi connection and cloud configuration were retained.

The 1.1.0 native simulator acceptance test covers uninterrupted preview during
recognition, commit badges, quantity correction, stale-result rejection, undo,
IN/OUT, storage failure rollback, and a new shutter canceling an interruption.
Run `tests/continuous_flow.py` only with its isolated, empty fixture inventory;
it exercises native GSP pointer input and rendered frames.
Real-device logs recorded two positive vision responses in 2059/2562 ms and
accepted voice command parsing in 1475/1398 ms. These counters alone do not
prove a particular spoken quantity or acoustic interruption worked correctly.
Local TTS produced first audio in 24–60 ms. A later audio backlog caused an ASR
reconnection; noisy-room and sustained full-duplex speech need further testing.
The right interaction board still times out; use the screen buttons.

Version 1.1.0 is saved in My Ideas as `d3de31`; the older `d1e7af` copy remains
as a backup. Its package includes the local voice resource as well as the UI.
First-camera-entry validation before any photograph confirmed local TTS first
audio at 23 ms and Qwen ASR streaming; returning home closed the ASR session.
Evidence is retained locally in `evidence/fridge-final-smoke.json`.
One earlier Vibe Mode save attempt rebooted unexpectedly; its Vibe firmware
core dump was archived, but a matching ELF was unavailable for diagnosis.
A subsequent save completed and the 1.1.0 detail page confirmed `d3de31`.
Launching that saved Idea produced boot `10255697322078587731`, ELF SHA-256
`c4c8188f65af49a90f9cd601cbbf6b471442501e052a1ee3ba91106590364672`,
with zero reported application crashes. Application-RPC acceptance confirmed
89 preview frames, local first audio at 28 ms, 67200 ASR samples sent, and all
three inventory records retained. Returning home stopped ASR. This was not a
physical-touch test: the app does not expose Vibe Mode's pointer RPC service.
Full local evidence: `evidence/fridge-saved-idea-acceptance.json`.

1.1.1 validation: browser speech-policy checks confirm only one greeting per
operation, with no speech after results/corrections/repeated captures; all six
companion regression checks pass. Device and simulator vision prompts match,
and distinct snack names remain separate through the parser. On the device,
Qwen returned HTTP 200 in 2237 ms with an empty item list for the current frame;
audio output stayed at the greeting's 49342 bytes while preview continued.
This verifies the silent empty-result path, not real snack-label accuracy.
Evidence: `evidence/fridge-silent-acceptance.json`. Application firmware ELF:
`9d7ec1dd544c3b7c39ec4cb0a3b982f6cf69c245ba7d3ee3a560cf9cb8bc4f37`.
Saved in My Ideas as version 1.1.1, local code `d0bba2`; previous copies retained.
