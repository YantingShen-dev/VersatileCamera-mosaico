# CCD 拍照打印 Idea

`photo_print_mosaico` 1.2.0：Mosaico 左侧摄像头、右侧 Interaction 双按钮板，直接通过 Classic Bluetooth SPP 连接 RPP02N / T3-GZP 热敏打印机。开机即取景，拍后先预览，确认才打印，不需要手机 App 或电脑中转。照片只存在内存里。

1.2.0 改用离线语音口令“准备拍照”，已移除手势模型、手势触发和临时图片测试入口。取景时说出口令，进入五秒倒计时；机械按钮和拍后确认打印流程保留。当前版本正在进行实机语音验收，尚未另存 Idea。完整固件与验证状态见 `firmware/candidate-1.2.0.json`。旧 Idea `d1331f` 是 1.1.0 手势版。

## 操作

| 状态 | 上方按钮 1 | 下方按钮 2 |
| --- | --- | --- |
| 取景 | 快门音后拍照 | 进入滤镜选择 |
| 滤镜选择 | 上一项 | 单击下一项；350ms 内双击确认退出 |
| 五秒倒计时 | 立即拍照 | 取消倒计时 |
| 拍后预览 / 已发送 / 失败 | 打印，必须蓝牙已连接 | 重拍 |
| 正在拍照 / 发送 | 忽略重复输入 | 忽略重复输入 |

屏幕底部两个触摸按钮与实体按键含义一致。右侧模块旋转安装时，上方键对应模块 `right_pressed`，下方键对应 `left_pressed`；使用 GPIO 模式读取用户板上的两个机械按键。按键防抖 40ms，按住不会重复触发。

只在取景画面监听“准备拍照”，无需额外唤醒词。识别后显示 5/4/3/2/1、播放五声短提示音，最后发出机械快门音并拍照。倒计时中重复说话不会延长计时；滤镜、预览、打印时忽略语音。照片需要按上键确认才会打印，下键可重拍。识别在本机完成，不存储或上传语音。

## 滤镜与纸面

- 正常黑白：4×4 有序抖动，保留灰度层次。
- 黑白反色：对正常点阵逐位反色。
- 黑白波点：8×8 网格圆形聚簇网点。
- 阈值版画：局部锐化后固定阈值，形成硬边黑白块。
- ASCII：6×8 字符单元，把局部亮度映射为不同密度的字符。

五种滤镜实时处理，拍照后冻结同一份点阵供预览和打印；取景框、状态和倒计时不进入打印图。相机画面中心裁切、旋转后输出 384×384：画面短边至少 768 时裁出 768×768 并缩小，否则裁出 384×384。输出覆盖打印头的整个 384 点有效宽度。58mm 纸的物理边缘仍会留不可打印区域。

打印采用 ESC @ 初始化、ESC a 居中、**一条完整 GS v 0 光栅指令**，48 字节/行，像素流按 256 字节分块，等待 SPP 完成/拥塞释放，间隔 10ms。禁止恢复成多条短条光栅指令，否则此打印机可能出现白色横纹和走纸停顿。当前宽度配置是 `photo_control.h` 中的 PHOTO_SIDE=384，实际打印位置需以实机验证为准。

“已发送”只表示蓝牙数据发送完成，不表示打印机的纸张/温度/机械状态已确认。失败后不自动重打，原照片保留，用户可检查纸张后决定再试。

## 模块

| 目录 | 职责 |
| --- | --- |
| components/app_photo | 共享 CCD UI、状态机、五种点阵滤镜 |
| components/app_capture | 摄像头/PPA、冻结照片、语音命令协调、工作队列 |
| components/app_voice | 板载麦克风、离线中文命令识别 |
| components/app_controls | 右侧实体键、独立扬声器音效任务 |
| components/app_printer | Classic SPP 配对、连接、整图传输 |
| components/app_display | 官方显示与触摸适配 |
| tools | 场景生成、模拟器交互测试、点阵与状态机测试 |

语音固定使用 `espressif/esp-sr` 2.4.7 的中文 MultiNet7，唯一口令为 `zhun bei pai zhao`（准备拍照），检测门槛 0.85。板载 ES8311 麦克风采集 16 kHz、16 位单声道，经过 AFE 的 VAD/AGC 后交给识别器。中文模型内嵌在应用中，不更改 Vibe 固件或分区布局。


## 构建与安装

ESP-IDF 必须为 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`，目标 esp32s31。按仓库 README 配置环境，从仓库根目录运行：

```powershell
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/photo_print_mosaico doctor
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/photo_print_mosaico build
python mosaico.py iris system-update --project projects/photo_print_mosaico
```

本机辅助入口位于父目录 `photo-print-build.ps1`。Windows 推荐短英文构建路径与 managed_components 实际路径；本机已配置目录联接。UI 资源或模型变化需 system-update，保留既有 Vibe Mode，不整片擦除。退出前停止摄像头、音效/按键和蓝牙控制器，以正常软件重启切换应用。

## 模拟器和验证

```powershell
$env:CMAKE_GENERATOR = 'Ninja'
$env:PHOTO_SIM_STATE = 'D:/photo-sim-state.txt'
python mosaico.py project sim --project projects/photo_print_mosaico --interactive
```

浏览器打开输出的 localhost 地址。底部两键可操作真实共享 C 状态机。原生模拟器的相机、打印机、语音命令结果及扬声器为替身，不代表这些硬件已经验收。

若原生模拟器位于 `http://127.0.0.1:3222/`，可在另一终端启动语音/连接状态控制面板：

```powershell
python projects/photo_print_mosaico/tools/sim_controls.py --state D:/photo-sim-state.txt
```

打开 `http://127.0.0.1:3223/`；其中嵌入同一个原生模拟器，可模拟口令、结束口令、断线和发送失败。`--state` 必须与原生模拟器的 `PHOTO_SIM_STATE` 指向同一个文件。控制面板只监听本机，不会连接真实打印机。

使用相同 PHOTO_SIM_STATE 运行 `tools/check_sim.py` 可自动通过触摸事件测试五滤镜、双击确认、离线保护、发送去重、失败保留照片、五秒倒计时及新口令触发。开发依赖见 `tools/requirements-dev.txt`。手动将状态文件写成 `online voice` 可模拟已识别语音口令，写成 `online` 模拟结束口令；`offline` 与 `fail` 分别模拟断线与打印失败。

`tools/photo_control_test.c` 验证倒计时、五次提示音事件、取消、双击和去重；`tools/photo_bitmap_test.c` 验证灰度、反色、位序和预览/打印一致。语音实际识别、按键方向、声音和纸面宽度需要实物验证。

## 已有版本

当前 **1.2.0 语音版**已保存于这台设备的 Idea 库，本地标识 **`d7fab0`**（不是社区下载码）。2026-11-13 实机确认实时取景、拍照、重拍，以及“准备拍照”触发五秒倒计时、五声提示音和快门拍照；并已从 Idea 库重新打开核验。修复了语音与相机同时运行时显示 DMA 内存不足导致的白屏。构建哈希与验证记录见 `firmware/candidate-1.2.0.json`。

1.0.0 的本地标识为 `ddfc02`，原版本实测打印无横纹、走纸连贯，源码提交 `a27420d` 已推送 `feat/photo-print-idea`；`firmware/release.json` 保留该初版记录。1.1.0 手势候选版记录保留在 `firmware/candidate-1.1.0.json`。`.irisfw` 安装包保留在本地，不纳入 Git。

需要从电脑关机时，运行中的本应用支持以下维护命令。它会停止外设、发出官方 BSP 关机信号；USB 保持供电时进入熄屏深睡眠。

```powershell
python mosaico.py iris rpc 0x5048 1 --payload-hex 4f4646 --project projects/photo_print_mosaico
```
