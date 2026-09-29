# Pixel 10 Pro XL Android 17 游戏调度与自动温控

这是面向 Pixel 10 Pro XL（设备代号 `mustang`）Android 17 SDK 37 的 KernelSU 模块项目，包含：

- Encore 单写入者游戏调度器：CPU、GPU、DSU、IRM 频率请求、游戏前台识别、退出恢复和高帧率策略。
- FAS-rs 风格帧反馈：已验证的 `libgui.so` 才启用帧探针；未知版本只保留基础调度，不猜测 uprobe 偏移。
- 自动温控配置：扫描 Thermal JSON 结构，生成游戏配置；匹配失败时自动隔离并保留系统原配置。
- ReSukiSU/KernelSU WebUI：游戏列表、Lite 模式、高帧率开关、状态和频率投票查看。

## 兼容范围

基础调度不再锁定某一个 CP41 构建号，而是检查：

1. 设备必须是 `mustang`；
2. Android SDK 必须是 `37`；
3. CPU/GPU 等频率节点必须通过预检。

温控模块会自动发现唯一的 `thermal_info_config*.json`，并校验虚拟皮肤传感器、冷却设备和目标频率。不同 Android 17 QPR/测试版如果改变了频率表或 Thermal JSON 结构，模块会安全停止，需要重新核对对应接口。

## 发布包

| 文件 | 用途 |
| --- | --- |
| `release/Encore-Pixel-10-Android17-v0.2.3-generic.zip` | Encore 调度模块 |
| `release/pixel10proxl-game-thermal-CP41-v1.6.0-auto-config.zip` | 自动温控模块 |
| `release/pixel10proxl-game-thermal-source-v1.6.0-auto-config.zip` | 温控模块源码包 |

安装前请停用其他会写入相同 `debug_min_freq`、uclamp 或温控 JSON 的模块。两个模块可以同时启用，但必须保持单一频率投票写入者。

## 当前设备验证

- 设备：Pixel 10 Pro XL `mustang`
- Android SDK：37
- 当前构建：`CP41.260831.007`
- Encore 基础调度：7 项频率节点预检通过
- `encored` 与 `pixel-control`：重启后正常运行
- FAS：当前 `libgui.so` 已完成哈希和偏移验证
- 温控：自动发现、40 项 JSON 修改和启动后校验通过

## 源码目录

- `src/encore-fas/`：调度器源码、模块目录和打包脚本。
- `src/thermal-auto/`：RapidJSON 温控配置生成器、模块目录和构建脚本。
- `docs/`：适配、验证和故障排查说明。

## 安全策略

未知 Android 17 版本不会套用旧的 `libgui` 偏移，也不会在 Thermal 配置不明确时覆盖 vendor 文件。升级系统后，如果 FAS 被停用，先收集新版本的 `libgui.so` 哈希和 `Surface::hook_queueBuffer` 偏移，再加入验证记录。

## 许可证

Encore 上游代码遵循其原许可证；FAS-rs 相关说明和许可证随模块源码保留。Pixel 适配部分为本项目的本地修改，使用者应自行确认设备、内核和系统版本的风险。
