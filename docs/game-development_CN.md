# 游戏创建与仿真

[English](game-development.md) | [返回索引](README_CN.md)

游戏优先使用 **Raylib Lite Engine**（`submodule/raylib-lite-engine`）。完整参考游戏、
运行时、渲染、资源、行为测试和 Host 仿真由引擎维护；BSP 负责产品板级支持与外设示例。
原生引擎示例使用自己的 Board Manager 适配器，原生构建本身不添加 Iris 或保留 Vibe Mode 布局。

## 从空白游戏开始

使用 Python 3.10+、Git 2.28+，初始化工作区依赖：

```sh
git submodule update --init submodule/esp-mosaico-utils submodule/esp-mosaico-bsp submodule/raylib-lite-engine
python mosaico.py game create my_game
python mosaico.py game sim --project projects/my_game
python mosaico.py game sim --project projects/my_game --headless --frames 120
# 首次构建可能需要下文的 Board Manager 配置步骤。
python mosaico.py game build --project projects/my_game
python mosaico.py iris system-update --project projects/my_game
```

默认 `blank` 模板呈现黑色 480×480 RGB565 画布，包含 Host/设备入口、Iris 服务和
保留分区布局，没有示例玩法、GSP 占位画布、atlas、音频或游戏资源分区。固件使用
工作区固定 ESP-IDF；空白或未验证设备先按[更新方式](mosaico-cli_CN.md#选择更新方式)初始化。

| 路径 | 职责 |
| --- | --- |
| `components/app_game/` | 纯 C 模型和游戏状态 |
| `components/app_game_runtime/` | 引擎生命周期/输入适配、绘制与 JSON |
| `main/game_entry.c` | 选择游戏模块 |
| `game.sim.json` | Host 与设备共用的源文件 |

依赖方向为 `main -> app_game_runtime -> app_game`，模型不依赖绘制或硬件。
Host 状态包含 `phase`、`tick`、指针坐标/按下状态和 `state_hash`；添加玩法前即可
验证暂停、继续和重置。headless 命令加 `--state-output state.json` 可保存状态。

## 从引擎示例开始

`game create/new` 支持 `--template shooter`、`--template sky-hop` 和
`--template tower-defense`。引擎当前有七个参考游戏，工作区创建命令提供上述
三个完整模板及 `blank`。创建会复制原生游戏及其私有共享 launcher/Board。

```sh
python mosaico.py game create my_platformer --template sky-hop
python mosaico.py game sim --project projects/my_platformer
python mosaico.py game build --project projects/my_platformer --target iris
# 使用构建实际输出的完整更新包路径：
python mosaico.py iris system-update --project projects/my_platformer --bundle /path/to/my_platformer-system-update.irisfw
```

`game build` 默认为 `--target native`，按源工程原样构建。`--target iris` 在工作区
运行目录下生成独立工程，添加产品服务和兼容分区，再输出 System Update 包；也接受
引擎游戏名，如 `game build sky_hop --target iris`。构建不安装固件，原生与 Iris
产物分别存放。适配工程引用 `main/` 和可选的 `components/`，不采用源工程顶层
分区表或自定义顶层 CMake 逻辑；资源应在组件 CMake 中声明。详见[集成契约](https://github.com/esp-mosaico/esp-mosaico-utils/blob/main/mosaico-tools/docs/application-integration.md#user-owned-raylib-lite-games)。

所有创建入口支持 `--dry-run`、拒绝覆盖已有目标，并初始化独立 Git 仓库和质量
配置，见[工程创建](project-init_CN.md)。

## 生成 Board Manager 配置

当前引擎要求 `components/gen_bmgr_codes`，工作区游戏构建器尚不自动生成。
新工程首次构建会解析托管依赖，随后可能报 `gen_bmgr_codes: unknown name`。
激活固定版本的 ESP-IDF 环境，再显式加载 Board Manager 扩展：

```sh
# 在工作区根目录执行，先由首次构建完成依赖解析。
source /path/to/pinned-esp-idf/export.sh
mosaico_workspace="$PWD"
mosaico_game_project="$mosaico_workspace/projects/my_game"
IDF_EXTRA_ACTIONS_PATH="$mosaico_game_project/managed_components/espressif__esp_board_manager" \
  idf.py -C "$mosaico_game_project" bmgr \
  -c "$mosaico_workspace/submodule/raylib-lite-engine/examples/boards/esp-mosaico/bmgr" \
  -b esp_mosaico
```

然后重跑原来的 `mosaico.py game build`。以上命令只生成板级源文件和配置，
设备安装仍使用 `mosaico.py`。显式加载扩展可避开 `esp-bmgr-assist` 对空白模板
间接包含 `project.cmake` 的识别限制。

使用 `--target iris` 时，将 `mosaico_game_project` 改为构建日志中的生成工程
（默认位于 `.codex-runs/mosaico/raylib-iris/<name-hash>/project`），然后重跑
带 `--target iris` 的原构建命令。生成工程需要自己的 Board Manager 配置，
仅在原始示例中生成还不够。构建时保留普通控制台输出：当前版本的
`game build --json` 成功时不输出结果对象或包路径。

## 验证行为

Host 需要 C 编译器和 Pillow；使用引擎纹理/资源转换工具时还需 NumPy。
交互仿真使用设备共用的 C 模型和 RGB565 绘制；按实际玩法检查动画、指针反馈、
碰撞、计分、胜负和重开。录制输入后可 headless 回放，修复后复验受影响流程。
成功启动或固定帧数运行不能证明游戏效果正确。当前 Host 未覆盖所有引擎 Scene、
UI、存档和音频模块；物理按键、触摸、显示、音频和实际性能仍需真机验证，
Host 耗时不能作为设备 FPS。

- [引擎示例与支持的开发板](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/README.md)
- [Sky Hop](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/sky_hop/README.md)
- [Tower Defense](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/tower_defense/README.md)
- [Raylib Shooter](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/raylib_shooter/README.md)
- [游戏开发指南](https://github.com/espressif2022/raylib-lite-engine/blob/main/docs/game-development.EN.md)
- [ESP-Mosaico Board Manager 适配器](https://github.com/espressif2022/raylib-lite-engine/blob/main/examples/boards/esp-mosaico/README.md)

本工作区的安装与诊断统一经过 `mosaico.py`；独立示例中的 flash/monitor 命令
不替代产品工作流。
