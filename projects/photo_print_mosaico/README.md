# 拍照打印 Idea

独立应用 `projects/photo_print_mosaico`，使用 ESP-Mosaico 左侧摄像头取景，点击“拍照”后显示黑白点阵预览，再点击“确认打印”向 RPP02N 发送照片。也可以“重拍”。照片仅在内存中，退出或断电即丢弃。

已保存到本机 Idea 库：`photo_print_mosaico`，版本 `1.0.0`，本地标识 `ddfc02`。进入 Vibe Mode 后在“我的 Ideas”或“管理”中选择运行；这个本地标识不是社区下载码。

## 使用

1. 摄像头在左侧，打印机开机、装纸，并断开手机 App 的蓝牙连接。
2. 右上角显示“RPP02N / 已连接”后可打印；未连接也可拍照和预览。
3. 点击“拍照”，检查黑白画面，点击“确认打印”或“重拍”。
4. 发送期间锁定按钮；完成显示“已发送 / 请检查出纸”，再打印必须主动点击。

“已发送”仅指蓝牙传输完成。本版不读取缺纸、温度或机械出纸完成状态。失败可能已有部分纸张输出，先检查实物再决定是否重试，不会自动重打。

## 硬件和协议

- ESP32-S31；固定 ESP-IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。
- T3-GZP 6009 / RPP02N，地址 `86:67:7A:63:30:A8`，PIN `0000`。
- Classic SPP 客户端，通过 SDP 查询服务通道。
- 320×320 中央方形裁切，有序抖动，40 字节/行，黑点为 1，MSB-first。
- ESC @ 初始化，GS v 0 整张 320 行仅发送一条指令，像素流每块 256 字节，等待 SPP 完成/拥塞解除并短暂让出 CPU。
- 屏幕黑白预览与发送点阵使用同一份冻结位图。

## 代码

| 目录 | 职责 |
| --- | --- |
| components/app_photo | 共享 GSP UI、状态和点阵转换 |
| components/app_capture | 摄像头、PPA、照片和缓冲区生命周期 |
| components/app_printer | 配对、连接、分块打印 |
| components/app_display | 官方模板显示和触摸适配 |
| main | 启动和组件组装 |
| tools | 字体/场景生成、模拟器验证 |

## 构建和安装

按仓库 README 激活固定 SDK，从仓库根目录运行：

```powershell
$env:MOSAICO_MANAGED_COMPONENTS_PATH = Join-Path $PWD 'projects/photo_print_mosaico/managed_components'
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/photo_print_mosaico doctor
python submodule/esp-mosaico-utils/mosaico-tools/skills/idf-low-noise-build/scripts/idf_low_noise_build.py --project projects/photo_print_mosaico build
python mosaico.py iris system-update --project projects/photo_print_mosaico
```

Windows 推荐短英文源码和构建路径。本机辅助入口在父目录 `photo-print-build.ps1`。安装保留 Vibe Mode；验收后在 Vibe Mode 的“开发 → 保存当前开发版”存入 Idea 库。

## 模拟器

激活 SDK 和 GSP 主机工具，从仓库根目录运行：

```powershell
$env:CMAKE_GENERATOR = 'Ninja'
$env:PHOTO_SIM_STATE = 'D:/photo-sim-state.txt'
python mosaico.py project sim --project projects/photo_print_mosaico --interactive
```

另一个终端使用相同的 PHOTO_SIM_STATE，安装 Pillow 和 websockets，再运行 `python projects/photo_print_mosaico/tools/check_sim.py`。状态文件使用短英文路径，避免原生 Windows fopen 对 Unicode 路径的限制。

验证通过触摸事件驱动真实 UI，检查拍照、重拍、离线阻止打印、连点去重、失败保留照片。摄像头和蓝牙使用替身，不能代替硬件验收。修改字体时用 PHOTO_FONT_SOURCE 指向 NotoSansSC-Regular.ttf，再运行 tools/make_scene.py。

## 验证记录

2026-11-12，Windows 已验证此打印机的 SPP 文本和 GS v 0 棋盘格位图出纸。本应用原生模拟器交互通过，截图位于仓库忽略提交的 evidence/photo-sim-*。实机已验证拍照、预览和蓝牙打印。初版短条分段打印出现横向白纹及走纸停顿，改为整张 GS v 0 指令后，用户确认横纹消失、走纸连贯。随后补充退出时关闭蓝牙协议栈和控制器，实测软件切回 Vibe Mode 的 reset_reason=3、crash_count=0。

## 安装包

本地 `firmware/photo_print_mosaico-1.0.0.irisfw` 是完整应用与 UI 资源包，校验信息见 `firmware/release.json`。二进制安装包不纳入 Git，换电脑时需单独复制或按源码重新构建。通过仓库官方 `mosaico.py iris system-update` 安装；具体包参数可查看该命令的 `--help`。
