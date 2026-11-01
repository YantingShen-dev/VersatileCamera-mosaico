# 在电脑上运行人脸 Idea

当前电脑已经配置 Python 3.13 虚拟环境、Visual Studio C++、GSP 1.5.1、GSPC 0.6.1 和对应模拟器。无需接板子或安装 ESP-IDF，即可编译并运行共享的 C 界面逻辑。

在项目根目录的 PowerShell 中运行：

```powershell
.\scripts\sim.ps1
```

浏览器会自动打开预览，通常是 <http://127.0.0.1:3222/>。鼠标点击对应设备触摸操作。保持启动终端运行，按 Ctrl+C 结束；修改代码后先结束旧进程，再重新启动。不要同时打开多个模拟器，实际预览地址以启动输出为准。

当前中文仓库路径已通过本机 MSVC 模拟器编译验证。设备固件的 SDK 安装与构建仍需遵循根 README 的短英文路径要求。

## 可以验证的内容

- 480×480 界面和共享 C 控制器。
- 模拟录入、五个人员槽位、管理列表、取消和确认删除。
- 无人脸、多人入镜和人脸过小等提示。

画面中的人脸是占位图。模拟器不读取电脑摄像头，不执行识别模型，不模拟板子的 NVS 重启持久化；耗时数值也是测试数据。真实采集、识别准确率、性能和断电保存需要板子验证。

启动时默认使用自动模拟状态，录入后可显示已识别的人员。运行中可切换输入：

```powershell
Set-Content evidence/face-sim-input.txt unknown -Encoding ascii
# 其他值：auto、no_face、multiple、small
```

原生适配器是 `projects/face_mosaico/components/app_face/host/face_pc.c`；共享界面与逻辑位于 `components/app_face`。模拟人员数据仅存在于当前进程内存。

## 本机验证

短时启动检查：

```powershell
.\scripts\sim.ps1 -Headless -Duration 3
```

启动交互模拟器后，在另一个终端运行已有的指针交互检查：

```powershell
.\.venv-sim\Scripts\python.exe projects/face_mosaico/tools/check_sim.py
Set-Content evidence/face-sim-input.txt auto -Encoding ascii
```

脚本使用本机 3222 端口并保存截图到 `evidence/`；需结合截图和原生后端日志判断结果。2026-10-30 已检查录入、管理、确认删除、取消保留、删除完成、无脸拒绝和多人拒绝。截图及日志均不提交 Git。

## 本机依赖布局

这些依赖是本机安装内容，未提交到仓库。若移动仓库或换电脑，需要重新建立虚拟环境并准备依赖：

- `.venv-sim/`：Python 3.13，`pillow==13.0.0`、`websockets==17.3`。
- `.tools/esp-gsp-1.5.1/esp-gsp/`：从[官方组件页](https://components.espressif.com/components/espressif/esp-gsp/versions/1.5.1/readme)下载并解压的组件。
- `.tools/gspc/gspc.exe`：GSPC 0.6.1。
- `.tools/sim/sim.exe`：模拟器 1.5.1。
- Visual Studio 的 Desktop development with C++ 工作负载，包含 MSVC、CMake、Ninja 和 Windows SDK。

GSPC 和模拟器由仓库已有工具下载；设置 `ESP_GSP_COMPONENT_DIR` 指向上述组件，再分别执行：

```powershell
.\.venv-sim\Scripts\python.exe submodule/esp-mosaico-utils/mosaico-tools/tools/gsp-sim/fetch_gspc.py --output-dir .tools/gspc
.\.venv-sim\Scripts\python.exe submodule/esp-mosaico-utils/mosaico-tools/tools/gsp-sim/fetch_gspc.py --sim --output-dir .tools/sim
```

启动脚本仅配置环境并调用官方 `mosaico.py project sim`，下载和预览实现仍由 utils 与 GSP 组件维护。
