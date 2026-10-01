# Pixel 10 Pro XL Android 17 游戏调度与自动温控

面向 Pixel 10 Pro XL（设备代号 `mustang`）Android 17 SDK 37 的 KernelSU 模块项目。

## 项目内容

- **Encore 调度器**：CPU、GPU、DSU、IRM 频率请求、游戏前台识别、退出恢复和高帧率策略。
- **FAS-rs 风格帧反馈**：仅对已验证的 `libgui.so` 启用帧探针；未知版本保留基础调度，不猜测 uprobe 偏移。
- **自动温控配置**：自动发现 Thermal JSON，生成游戏配置；结构或目标频率不匹配时自动隔离。
- **WebUI**：支持 ReSukiSU/KernelSU 的游戏列表、Lite 模式、高帧率开关、状态和频率投票查看。

## 最新更新

当前构建包含：

- Encore 目标帧率按游戏配置：WebUI 可选自动判断、30、60、120 FPS。
- WebUI 游戏页使用纵向展开菜单；主页路径已适配 `encore_pixel_cp41`，显示版本、当前配置和 `Tensor G5 (laguna)`。
- 调度起始频率改为按需模式：GPU 约 512 MHz 起步，FAS 根据持续帧时间不足逐档提升，稳定后逐档回落。
- 动态线程后端继续运行，线程级 uclamp 与 Encore 共用同一游戏目标。
- 自动温控 `v1.6.4-thermal-expanded`：高温前段延后 GPU 降档，748 → 633 → 512 MHz 平滑保护。

源码请查看仓库中的 `src/` 目录；发布页只提供可安装模块 ZIP，不放源码压缩包或 SHA256 文件。

## 兼容范围

基础调度检查以下条件：

1. 设备必须是 `mustang`；
2. Android SDK 必须是 `37`；
3. CPU、GPU、DSU、IRM 频率节点必须通过预检。

温控模块 v1.6.4-thermal-expanded 会按完整系统指纹重新建立 stock 缓存，自动发现唯一的 `thermal_info_config*.json`，并校验虚拟皮肤传感器、冷却设备和目标频率。不同 Android 17 QPR/测试版如果改变频率表或 Thermal JSON 结构，模块会安全停止，不覆盖系统原配置。

## 目录结构

- `src/encore-fas/`：调度器源码、模块目录和打包脚本。
- `src/thermal-auto/`：RapidJSON 温控配置生成器、模块目录和构建脚本。
- `release/`：两个可安装模块 ZIP 的版本化发布目录。
- `docs/`：兼容性、安装和回滚说明。

## 安装前须知

请先停用其他会写入相同 `debug_min_freq`、uclamp 或温控 JSON 的模块。Encore 作为唯一频率投票写入者运行；自动温控模块可以与 Encore 同时启用。

当前验证设备为 Pixel 10 Pro XL `mustang`、Android 17 SDK 37、构建 `CP41.260831.007.A3`。升级到其他 Android 17 QPR 后，基础调度按设备、SDK 和频率节点预检；温控模块按完整指纹重建缓存；未知 `libgui.so` 版本只停用 FAS 帧探针。

详细步骤见 [兼容性与安装说明](docs/兼容性与安装.md)。

## 许可证

Encore 上游代码遵循其原许可证；FAS-rs 相关说明和许可证随源码保留。Pixel 适配部分为本项目的本地修改。


