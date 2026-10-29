# Game creation and simulation

[简体中文](game-development_CN.md) | [Documentation index](README.md)

Prefer **Raylib Lite Engine** (`submodule/raylib-lite-engine`) for games. It owns
complete reference games, runtime, rendering, assets, behavior tests and Host
simulation. BSP owns product board support and peripheral examples. Native engine
examples use their own Board Manager adapters; a native build alone does not add
Iris or the retained Vibe Mode layout.

## Start with a blank game

Use Python 3.10+ and Git 2.28+. Initialize the workspace dependencies:

```sh
git submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp submodule/raylib-lite-engine
python mosaico.py game create my_game
python mosaico.py game sim --project projects/my_game
python mosaico.py game sim --project projects/my_game --headless --frames 120
# First builds may need the Board Manager setup below.
python mosaico.py game build --project projects/my_game
python mosaico.py iris system-update --project projects/my_game
```

The default `blank` template starts with a black 480×480 RGB565 surface. It
includes Host/device entry points, Iris services and the retained layout, with
no example gameplay, GSP canvas placeholder, atlas, audio or game resource partition.
Firmware uses the workspace's pinned ESP-IDF; provision a blank or unverified
board first as described in [update selection](mosaico-cli.md#select-an-update-method).

| Path | Responsibility |
| --- | --- |
| `components/app_game/` | Pure C model and gameplay state |
| `components/app_game_runtime/` | Engine lifecycle/input adapter, drawing and JSON |
| `main/game_entry.c` | Select the game module |
| `game.sim.json` | Sources shared by Host and device |

Dependencies flow `main -> app_game_runtime -> app_game`. The model does not
depend on drawing or hardware. Host state includes `phase`, `tick`, pointer
coordinates/pressed state and `state_hash`. Pause, resume and reset work before
adding gameplay; use `--state-output state.json` to save a headless run's state.

## Start with an engine example

`game create/new` supports `--template shooter`, `--template sky-hop` and
`--template tower-defense`. The engine currently contains seven reference games;
the workspace creator exposes these three complete templates plus `blank`.
Creation copies the selected native game with its private shared launcher/Board.

```sh
python mosaico.py game create my_platformer --template sky-hop
python mosaico.py game sim --project projects/my_platformer
python mosaico.py game build --project projects/my_platformer --target iris
# Use the exact bundle path printed by the build:
python mosaico.py iris system-update --project projects/my_platformer --bundle /path/to/my_platformer-system-update.irisfw
```

`game build` defaults to `--target native`, which builds the source as supplied.
`--target iris` adds product services and compatible partitions in a generated
project under the workspace run directory, then emits a System Update bundle.
It also accepts a named engine game, such as `game build sky_hop --target iris`.
Build does not install firmware. Native and Iris outputs use separate directories.
The wrapper references `main/` and optional `components/`; it does not use the
source's top-level partition table or custom top-level CMake logic. Declare
resources in component CMake files. See the [integration contract](https://github.com/esp-mosaico/esp-mosaico-utils/blob/main/mosaico-tools/docs/application-integration.md#user-owned-raylib-lite-games).

All creators support `--dry-run`, refuse existing targets and initialize an
independent Git repository plus quality configuration. See [project creation](project-init.md).

## Generate Board Manager configuration

The current engine requires `components/gen_bmgr_codes`. The workspace game
builder does not generate it automatically. On a fresh project, the first build
resolves managed dependencies and can stop with `gen_bmgr_codes: unknown name`.
Activate the pinned ESP-IDF environment and run the Board Manager extension:

```sh
# Run from the workspace root, after the first build has resolved dependencies.
source /path/to/pinned-esp-idf/export.sh
mosaico_workspace="$PWD"
mosaico_game_project="$mosaico_workspace/projects/my_game"
IDF_EXTRA_ACTIONS_PATH="$mosaico_game_project/managed_components/espressif__esp_board_manager" \
  idf.py -C "$mosaico_game_project" bmgr \
  -c "$mosaico_workspace/submodule/raylib-lite-engine/examples/boards/esp-mosaico/bmgr" \
  -b esp_mosaico
```

Then repeat the original `mosaico.py game build` command. These commands only
generate board sources/configuration; device installation still uses `mosaico.py`.
Explicitly loading the extension avoids relying on `esp-bmgr-assist` project detection,
which does not recognize the blank template's indirect `project.cmake` include.

For `--target iris`, set `mosaico_game_project` to the generated project reported
in the build log (`.codex-runs/mosaico/raylib-iris/<name-hash>/project` by default),
then repeat the same build with `--target iris`. The generated project needs its
own Board Manager configuration; generating it only in the original example
is insufficient. Keep normal console output when building: this revision's
successful `game build --json` does not emit a result object or bundle path.

## Validate behavior

The Host needs a C compiler and Pillow; install NumPy when using the engine's
texture/asset conversion tools. Interactive simulation uses the same C model and
RGB565 drawing as the device. Check animation, pointer feedback, collisions,
scoring, win/loss and restart behavior as relevant. Replay recorded input
headlessly for repeatable checks; inspect visuals and re-run affected flows after
fixes. A successful launch or fixed frame count does not establish gameplay quality.
Scene, UI, save and audio engine modules are not all covered by the current Host.
Validate physical controls, touch, display, audio and performance on hardware;
Host timing is not device FPS.

- [Engine examples and supported boards](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/README.md)
- [Sky Hop](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/sky_hop/README.md)
- [Tower Defense](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/tower_defense/README.md)
- [Raylib Shooter](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/raylib_shooter/README.md)
- [Game development guide](https://github.com/espressif2022/raylib-lite-engine/blob/main/docs/game-development.EN.md)
- [ESP-Mosaico Board Manager adapter](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/boards/esp-mosaico/README.md)

In this workspace, install and diagnose devices through `mosaico.py`; standalone
example flash/monitor instructions do not replace the product workflow.
