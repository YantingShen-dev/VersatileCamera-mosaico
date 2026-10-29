# Face Mosaico / 离线人脸识别

独立的 ESP-Mosaico Idea，支持摄像头预览、人脸框、最多五人的本地录入与识别、逐人删除及重启保留记录。

## 使用

1. 摄像头安装在左侧，启动本 Idea。
2. 正对摄像头；只有一个足够大的人脸入镜时，点击“录入新面孔”。
3. 提示“人员 01 / 录入成功”后，移开再重新入镜，观察识别结果。
4. “管理人脸”可选择人员删除，取消不会删除；其他人员编号保持不变。
5. 在 Vibe Mode 的“开发 → 保存当前开发版”保存后，可通过“我的 Ideas”切换回来。

所有推理在 ESP32-S31 上完成，无需 Wi-Fi。只在专用 NVS 命名空间保存特征，不保存照片、不上传图像。相似度阈值为 0.55；光照、角度和人脸大小会影响结果，当前版本没有活体检测。

## 工程

- `main` 负责启动与依赖组合。
- `components/app_face` 是设备和模拟器共用的 GSP UI 控制器。
- `components/app_vision` 负责官方 MSR/MNP 检测、MFN 特征、摄像头、PPA 与存储。
- `components/app_display` 保留官方模板的屏幕与触摸适配。
- `tools/check_sim.py` 通过模拟器指针输入检查录入、管理和失败流程；模拟图像和耗时均为测试数据。

固定 ESP-IDF 提交为 `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe`。模型依赖在 `main/idf_component.yml` 固定版本。换电脑的步骤见仓库根目录 README；使用根目录 `build.ps1` 进行低噪声构建。Windows 请将仓库与 SDK 放在较短的纯英文路径，避免 Binutils 的非 ASCII 路径问题。

本项目保持官方模板分区表，不改写 Vibe Mode、其他 Idea、系统身份或 Wi-Fi 配置。BSP 的官方 DVP 稳定性补丁由组件自动应用；SC101IOT 时序补丁在项目 CMake 中以忽略 CRLF 空白差异的方式幂等应用。不要对依赖源文件直接手改。

安装使用官方 `mosaico.py iris system-update --project projects/face_mosaico --device-id <设备ID> --json`，由 CLI 构建包含界面资源的安装包；复用完整包时可以增加 `--skip-build`。

实机已确认预览、人脸检测、录入和再次识别。首版录入测试出现重启，保留的 Core Dump 指向 `face_vision` 中断上下文的 `pxPortGetCoprocArea`，另一核心正执行 Flash 阻塞操作。项目通过 `coproc_cache_safe.lf` 将相关上下文保存函数及常量放入内部 RAM，保持 SDK 提交不变；修复后用户确认录入、识别正常且不再重启。

2026-10-29 已保存为本地 Idea `dc2ab0`，显示名称 `face_mosaico`，从 Idea 库重新启动成功，录入数量仍为 1，固件校验一致且无新崩溃。原有 AURA 和 Codex Voice 均保留。用户已确认重启后识别、删除和重新录入全部正常。模拟器另外覆盖了取消删除、无脸和多脸拒绝；五人满额和多人身份区分尚未进行实机规模测试。
