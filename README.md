# Pixel Tensor Android 17 游戏调度与自动温控

面向 Pixel Tensor 系列 Android 17 SDK 37 的 KernelSU 模块项目。

## 项目内容

- **Encore 调度器**：CPU、GPU、DSU、IRM 频率请求、游戏前台识别、退出恢复和高帧率策略。
- **FAS-rs 风格帧反馈**：从当前 `libgui.so` 的 ELF 符号表解析帧探针偏移；解析失败时保留基础调度。
- **自动温控配置**：自动发现 Thermal JSON，生成游戏配置；结构或目标频率不匹配时自动隔离。
- **WebUI**：支持 ReSukiSU/KernelSU 的游戏列表、Lite 模式、高帧率开关、状态和频率投票查看。
- **固定游戏列表**：使用 Encore 上游公开游戏列表，安装模块时只登记设备已安装的匹配游戏。
- **安装事件刷新**：通过 Android `/data/app` 的 inotify 安装事件刷新已安装匹配项，不常驻扫描前台应用。
- **原生线程采样**：原生 C 工具直接读取 procfs，取代遍历线程时的 Shell fork/exec。
- **线程范围收敛**：只对固定列表中的前台游戏动态采样 CPU 周期最高的 4 个线程，并每 3 秒刷新候选；线程不再依赖名称识别，也不叠加整组同名工作线程。
- **游戏帧率预设**：已知竞技 FPS/MOBA 游戏预设 120 FPS，其余游戏预设 60 FPS，WebUI 支持 30/60/90/120 四档。
- **平滑帧反馈**：高档试探收益不足时退至轻量 level 1，不再直接清零；严重掉帧可提前退出冷却，全局频率降档增加连续窗口迟滞。

## 最新更新

当前构建包含：

- 通用兼容性构建 `0.8.1-generic`：温控和调度合并为一个模块；安装提示中英双语。
- 安装时音量上键放宽温控（推荐），音量下键保留原厂温控；10 秒无操作默认放宽。安装前停用独立温控模块。
- Encore 目标帧率按游戏配置：WebUI 可选 30、60、90、120 FPS；新游戏按预设表登记。
- 新安装的游戏如果出现在上游列表中，会在 APK 安装事件后自动加入；竞技 FPS/MOBA 预设 120，其余预设 60。
- 调度起始频率改为按需模式：GPU 约 512 MHz 起步，FAS 根据持续帧时间不足逐档提升，稳定后逐档回落。
- 动态线程后端继续运行，线程级 uclamp 与 Encore 共用同一游戏目标。
- FAS 回落改为逐级、带迟滞的平滑控制，减少高档与完全释放之间的周期性帧率波动。
- 自动温控 `v1.7.2-soc-smooth`：CPU SOC Ceiling 在 45/46.5°C 使用 1632/2188 与 1363/1670 MHz 平滑降级，GPU 保持 748 → 633 → 512 MHz，52°C 最高保护档保持原厂。

源码请查看仓库中的 `src/` 目录；发布页只提供可安装模块 ZIP，不放源码压缩包或 SHA256 文件。

固定游戏包名列表来自 [Rem01Gaming/encore 的 gamelist.txt](https://github.com/Rem01Gaming/encore/blob/main/gamelist.txt)。模块安装时和新 APK 安装事件后，会将其中已安装的包合并到本机列表；列表不依赖运行时联网。

安装包下载：

- [模块 Releases](https://github.com/sungho-moon/pixel10proxl-android17-thermal-encore/releases)

## 兼容范围

本模块没有 Pixel 10 Pro XL 专属白名单，也不读取 `ro.product.model`、`ro.product.device`、设备代号或固定系统指纹来拒绝其他 Pixel 10 变体。`encore_pixel_cp41` 只是现有配置目录和模块 ID，为了升级兼容性保留，不代表只支持 CP41/Pro XL。

基础调度检查以下条件：

1. Android SDK 必须是 `37`；
2. 当前设备为 arm64，且 CPU、GPU 或其他频率域必须通过运行时能力检查；
3. Thermal JSON 必须通过本机 schema 检查。

集成温控使用 v1.7.2-soc-smooth 的参数，按完整系统指纹重新建立 stock 缓存，自动发现唯一的 `thermal_info_config*.json`，并校验虚拟皮肤传感器和冷却设备。SOC 第 4/5 档采用有效离散频率平滑降级，SOC-EXTREME 的阈值与 Ceiling 保持原厂。未知结构会隔离温控 overlay，调度继续运行；若开机后才发现校验失败，温控恢复原厂需要下次重启。

## 目录结构

- `src/encore-fas/`：调度器源码、模块目录和打包脚本。
- `src/thermal-auto/`：RapidJSON 温控配置生成器、模块目录和构建脚本。
- [GitHub Releases](https://github.com/sungho-moon/pixel10proxl-android17-thermal-encore/releases)：集成调度与温控的可安装模块 ZIP 发布页面。
- `docs/`：兼容性、安装和回滚说明。

## 安装前须知

请先停用其他会写入相同 `debug_min_freq`、uclamp 或温控 JSON 的模块。集成包同时负责 Encore 调度和 Thermal JSON 温控，避免与独立温控或调度模块叠加。

当前实机验证设备为 Pixel 10 Pro XL、Android 17 SDK 37。Pixel 10 全系可按“运行时能力校验”方式尝试安装；其他变体仍需分别确认 CPU policy、GPU/DSU/IRM devfreq 节点和 Thermal JSON 冷却设备是否存在。`libgui.so` 符号解析失败只停用 FAS 帧探针，不影响基础调度；频率或温控能力不匹配时会安全停用对应后端，不写入未经校验的节点。集成包仍需安装后的开机验证。

详细步骤见 [兼容性与安装说明](docs/兼容性与安装.md)。

## 许可证

Encore 上游代码遵循其原许可证；FAS-rs 相关说明和许可证随源码保留。Pixel 适配部分为本项目的本地修改。

