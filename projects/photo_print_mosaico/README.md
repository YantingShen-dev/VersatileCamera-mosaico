# CCD 拍照打印 Idea

`photo_print_mosaico` 1.1.0：Mosaico 左侧摄像头、右侧 Interaction 双按钮板，直接通过 Classic Bluetooth SPP 连接 RPP02N / T3-GZP 热敏打印机。开机即取景，拍后先预览，确认才打印，不需要手机 App 或电脑中转。照片只存在内存里。

设备 Idea 库中的 `d1331f` 是上一版（不是社区下载码）。当前候选修复采用检测模型自带门槛，并容忍短暂漏检；实机五指倒计时仍在验收，尚未另存到 Idea 库。机械按键、快门声和 384 点居中连续打印已验证。完整安装包及确切验证状态见 `firmware/candidate-1.1.0.json`。

## 操作

| 状态 | 上方按钮 1 | 下方按钮 2 |
| --- | --- | --- |
| 取景 | 快门音后拍照 | 进入滤镜选择 |
| 滤镜选择 | 上一项 | 单击下一项；350ms 内双击确认退出 |
| 五秒倒计时 | 立即拍照 | 取消倒计时 |
| 拍后预览 / 已发送 / 失败 | 打印，必须蓝牙已连接 | 重拍 |
| 正在拍照 / 发送 | 忽略重复输入 | 忽略重复输入 |

屏幕底部两个触摸按钮与实体按键含义一致。右侧模块旋转安装时，上方键对应模块 `right_pressed`，下方键对应 `left_pressed`；使用 GPIO 模式读取用户板上的两个机械按键。按键防抖 40ms，按住不会重复触发。

五指张开的手掌须至少三次识别成功、跨度不少于 700ms，允许中间短暂漏检（距上次成功不超过 500ms），才启动 5 秒倒计时，依次显示 5/4/3/2/1 并请求五次短提示音，然后发出机械快门音、拍照。持续举手不会重复触发；手掌离开至少 800ms 后重新允许触发。滤镜选择、预览和打印中不启动手势拍照。模型读取滤镜前的彩色图像，黑白预览不改变识别输入。

## 滤镜与纸面

- 正常黑白：4×4 有序抖动，保留灰度层次。
- 黑白反色：对正常点阵逐位反色。
- 黑白波点：8×8 网格圆形聚簇网点。
- 阈值版画：局部锐化后固定阈值，形成硬边黑白块。
- ASCII：6×8 字符单元，把局部亮度映射为不同密度的字符。

五种滤镜实时处理，拍照后冻结同一份点阵供预览和打印；取景框、状态和倒计时不进入打印图。相机从 1024×768 中央裁出 768×768、旋转、缩至 384×384，输出打印头的整个 384 点有效宽度。58mm 纸的物理边缘仍会留不可打印区域。

打印采用 ESC @ 初始化、ESC a 居中、**一条完整 GS v 0 光栅指令**，48 字节/行，像素流按 256 字节分块，等待 SPP 完成/拥塞释放，间隔 10ms。禁止恢复成多条短条光栅指令，否则此打印机可能出现白色横纹和走纸停顿。当前宽度配置是 `photo_control.h` 中的 PHOTO_SIDE=384，实际打印位置需以实机验证为准。

“已发送”只表示蓝牙数据发送完成，不表示打印机的纸张/温度/机械状态已确认。失败后不自动重打，原照片保留，用户可检查纸张后决定再试。

## 模块

| 目录 | 职责 |
| --- | --- |
| components/app_photo | 共享 CCD UI、状态机、五种点阵滤镜 |
| components/app_capture | 摄像头/PPA、冻结照片、离线手势识别、工作队列 |
| components/app_controls | 右侧实体键、独立扬声器音效任务 |
| components/app_printer | Classic SPP 配对、连接、整图传输 |
| components/app_display | 官方显示与触摸适配 |
| tools | 场景生成、模拟器交互测试、点阵与状态机测试 |

手势模型固定使用 espressif/hand_detect 0.2.1、hand_gesture_recognition 0.2.1、esp-dl 3.3.14。先检测一只足够大的手，再识别 `five` 类别（置信度至少 0.80）；仅凭 PIR 动作传感器不会触发拍照。模型在设备本地执行，不上传摄像头图像。

手部检测门槛为 0.35，最小框 45×45 像素；此前 0.65 会过滤掉实机记录中 0.62/0.38 的真实手掌。下次验证应先按“重拍”回到 `CCD SNAP` 实时取景，再让完整手掌入镜；`PHOTO CHECK` 预览或滤镜选择中暂停手势检测。

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

浏览器打开输出的 localhost 地址。底部两键可操作真实共享 C 状态机。原生模拟器的相机、打印机、手势分类结果及扬声器为替身，不代表这些硬件已经验收。

若原生模拟器位于 `http://127.0.0.1:3222/`，可在另一终端启动手势/连接状态控制面板：

```powershell
python projects/photo_print_mosaico/tools/sim_controls.py --state D:/photo-sim-state.txt
```

打开 `http://127.0.0.1:3223/`；其中嵌入同一个原生模拟器，可模拟举手、移手、断线和发送失败。`--state` 必须与原生模拟器的 `PHOTO_SIM_STATE` 指向同一个文件。控制面板只监听本机，不会连接真实打印机。

使用相同 PHOTO_SIM_STATE 运行 `tools/check_sim.py` 可自动通过触摸事件测试五滤镜、双击确认、离线保护、发送去重、失败保留照片、五秒倒计时及移手重触发。开发依赖见 `tools/requirements-dev.txt`。手动将状态文件写成 `online palm` 可模拟已识别五指手掌，写成 `online` 模拟移开；`offline` 与 `fail` 分别模拟断线与打印失败。

`tools/photo_control_test.c` 验证倒计时、五次提示音事件、取消、双击和去重；`tools/photo_bitmap_test.c` 验证灰度、反色、位序和预览/打印一致。模型真实识别、按键方向、声音和纸面宽度需要实物验证。

## 已有版本

1.0.0 已保存于这台设备的 Idea 库，本地标识 `ddfc02`（不是社区下载码），原版本实测打印无横纹、走纸连贯。源码提交 `a27420d` 已推送 `feat/photo-print-idea`。1.1.0 的当前完整安装包与验证记录见 `firmware/candidate-1.1.0.json`；待手势实物测试通过再提升为正式 `firmware/release.json`。`.irisfw` 安装包保留在本地，不纳入 Git。

需要从电脑关机时，运行中的本应用支持以下维护命令。它会停止外设、发出官方 BSP 关机信号；USB 保持供电时进入熄屏深睡眠。

```powershell
python mosaico.py iris rpc 0x5048 1 --payload-hex 4f4646 --project projects/photo_print_mosaico
```
