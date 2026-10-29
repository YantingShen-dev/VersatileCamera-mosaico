# 人脸 Idea 验证记录

验证日期：2026-10-29。硬件为 ESP-Mosaico / ESP32-S31，摄像头在左侧，Vibe Mode 0.2.1。

## 已完成

- 固定 ESP-IDF `7b9cc1ac79f865983f59bb8ff3ff43eb74ff1dbe` 的构建和环境检查通过。
- GSP 原生模拟器验证录入、管理、取消删除、确认删除、无人脸和多人拒绝。
- 源代码格式和项目质量检查通过；AStyle 在本机禁用 Wasmtime 编译缓存后单独运行，其余 pre-commit 检查通过。
- 用户实机确认摄像头预览、人脸框、录入与再次识别、删除和重新录入正常。
- 保存为本地 Idea 后从列表重新启动，录入记录保留并可识别。原有 AURA 与 Codex Voice 保留。

完整包 `firmware/face_mosaico-1.0.0.irisfw` 包含应用、UI 资源和兼容的分区表，保留 Vibe Mode 固定区域、NVS 和已有 Idea。校验值在相邻 `release.json` 中。

## 录入重启问题的修复

首版能够预览并检测人脸，但点击录入触发重启。Core Dump 的失败任务为 `face_vision`，中断上下文停在 `pxPortGetCoprocArea`；另一核心正执行 Flash 阻塞操作。固定 SDK 的该函数原位于外部映射区域。录入写入 NVS 时，任务切换可能调用缓存不可用的上下文保存代码。

项目的 `components/app_vision/coproc_cache_safe.lf` 将 `pxPortGetCoprocArea`、`pxPortUpdateCoprocOwner` 及相关常量放入内部 RAM。修复后 ELF 符号位置经验证位于内部地址，用户确认重复录入、识别和删除不再重启。此修复通过应用链接片段实现，不修改固定 SDK 源码。

另外，摄像头 D2 使用 GPIO33，应用禁用该引脚对应的 USB Serial/JTAG 功能；电脑通信仍使用 HS USB OTG。BSP 的 SC101IOT 补丁在 Windows 的 CRLF 源文件下通过忽略空白差异幂等应用。

## 验证边界

当前最多五人、按人员编号存储，未进行五人满额、多人身份区分和不同光照条件的系统准确率测试。没有活体检测。特征不随 Git 仓库迁移，在另一块板子上需重新录入。

迁移仓库保留了实测版本的应用源码、资源和模型依赖锁定；新电脑仍需下载 SDK 与子模块。不会把旧电脑的虚拟环境或编译缓存作为可迁移开发环境。
