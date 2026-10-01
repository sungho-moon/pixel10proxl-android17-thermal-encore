# Encore Pixel 10 Pro XL Android 17 — 0.2.3 generic adviser

This build keeps Encore as the only frequency-request writer and adds a
fas-rs-derived proportional frame-error adviser to its existing tracefs
observer. The original fas-rs process must stay disabled: its CPU cpufreq
writes and exit reset are incompatible with a single-owner policy. The
adviser's extra request is bounded to the existing three OPP levels, responds
from 30 recent frame intervals, and adds one OPP step on the first proportional
deficit or up to two on a severe deficit. It has no fixed 6-second hold or
20-second cooldown and remains subject to the raised 40/41/42°C FAS
temperature caps. The 43°C whole-controller protection remains active. Read `FAS_EXPERIMENT.md` for
controls, limits and rollback. The 0.1.6 behavior below remains the baseline.

适用设备：Pixel 10 Pro XL (`mustang`)，Android 17（SDK 37）各 QPR/测试版本。频率节点预检通过后启用基础 Encore 调度；只有已匹配的 `libgui.so` 才启用 FAS 帧探针。

这是 Encore 的源码分支，非官方 Pixel 版本。上游：<https://github.com/Rem01Gaming/encore>，版本 5.2.1，提交 `361ab0ef483f3a6a9d57b2aae0ac2284c2fe6d64`，Apache-2.0。

## 模块做什么

- 使用重新编译的 Encore 原生 Binder 应用、屏幕事件监听、游戏列表和配置监听框架。
- 修复原版忽略游戏退到后台、普通应用进入前台的逻辑；定期用有超时的 ActivityManager/PowerManager 快照核对，支持服务启动时游戏已经在前台。
- 针对本机四个 CPU 策略和 GPU、DSU、IRM GMC 写入独立的 `vote_manager/debug_min_freq` 最低频率请求。原生 Power HAL 的 `powerhint_*` 投票仍由系统管理。
- 启动时从每个 CPU/GPU 的实时 `available_frequencies` 表选择档位，不再依赖固定频率值；频率表暂时不可读时才回退到已验证的 CP41 档位。
- 复用旧版线程调度器的 `uclamp.min` 后端，只调整游戏主线程和渲染线程；Encore 仍是全局频率和 FAS 的唯一控制器，退出时统一恢复。
- 温度控制采用 40/41/42/43°C 分级限制，并使用 1°C 滞回逐级恢复，避免温度在阈值附近造成频率和帧率反复跳变。
- 游戏退出、切后台、息屏、省电模式、电量低于 15%、电池温度保护、服务停止或原生监听进程失效时释放自己的请求。
- 每次写入前保存同次开机的原值和请求值；退出时只恢复仍属于本模块的值。检测到其他程序修改相同投票时放弃该节点，避免覆盖对方的新设置。
- 识别到符合保护条件的前台游戏时，通过实机解析的 GameManager Binder 方法临时关闭 Android 默认游戏帧率策略。退出、息屏、省电、电量/温度保护、暂停或服务停止时恢复原值；这项系统开关作用于所有前台游戏，模块只在游戏模式中持有它。已有禁用状态由其原持有者管理，模块不接管。此功能允许高帧率，不保证游戏渲染达到 120 FPS。

## 默认频率请求

这些是请求的最低频率，实际频率还取决于系统、负载和温控上限；更高的系统投票仍可提高频率。

| 控制对象 | 正常游戏模式 | Lite 模式 |
|---|---:|---:|
| CPU 0–1 | 729 MHz | 533 MHz |
| CPU 2–4 | 1401 MHz | 1075 MHz |
| CPU 5–6 | 1401 MHz | 1075 MHz |
| CPU 7 | 1305 MHz | 1036 MHz |
| GPU | 512 MHz | 448 MHz |
| DSU | 691 MHz | 537 MHz |
| IRM GMC | 844 MHz | 672 MHz |

电池温度低于 40°C 时允许 FAS 3 档，40–41°C 限 2 档，41–42°C 限 1 档；达到 42°C 使用 Lite 请求并停止 FAS 额外提频。达到 43°C 释放整体请求，降至 41°C 以下重新允许启用。电池温度不能代表芯片或机身的全部温度，原有热管理继续运行。

## 0.1.2 安装与开机校验修复

安装校验排除 KernelSU 不解压的 META-INF；开机校验进一步排除安装完成后被清理的 customize.sh 和 README.md。两个阶段分别使用 INSTALL_SHA256SUMS 和 SHA256SUMS，运行文件继续进行 SHA256 校验。新增开机校验失败状态和 startup-check.log，避免静默退出。两个原生执行文件和性能参数与 0.1.0 相同。

## 0.1.3 游戏帧率策略适配

三角洲正常大战场的短时实机对照中，原策略的有效覆盖在 60/120 Hz 之间切换；关闭默认游戏策略后，12 个有效呈现窗口约 109–120 FPS（窗口中位数 116.21 FPS），恢复后 12 个窗口均约 60 FPS。P95 帧间隔约 8.40 ms 对 16.74 ms。测量来自 SurfaceFlinger 保留的帧时间窗口，场景负载与温度变化，不能当作整局平均或所有游戏的保证。训练营与实战分开记录。

新增 frame-policy-journal，在 Binder 调用前保存原状态和事务号；恢复时重新解析方法，拒绝事务号不匹配的日志。控制器与 service.sh 的恢复路径都涵盖此策略。配置目录创建 disable-high-fps 文件可单独停用此功能，约一轮状态检查后恢复原策略；删除该文件重新允许启用。

## 0.1.4 WebUI

恢复并适配上游 Vue WebUI，入口是 ReSukiSU/KernelSU 模块列表中的 WebUI。首页显示真实控制器和监听进程状态、前台应用、电池温度、默认游戏帧率策略及 7 项最低请求/当前频率/系统上限。首页可滚动查看全部节点；页面离开后停止定时采样。

- 游戏：列出已安装应用，逐个启用/停用游戏识别、设置 Lite；开关保存后立即生效。
- 设置：允许高帧率、全局 Lite、停用游戏优化、语言和日志等级。允许高帧率默认开启，表示游戏前台且保护条件允许时临时关闭系统默认游戏限帧策略，不能保证实际 120 FPS。
- Lite 降低最低频率请求；全局 Lite 优先于单游戏设置。停用游戏优化会释放模块请求及帧率策略。
- 每个游戏可在 WebUI 选择 30/40/45/60/75/90/120/144/165/240 FPS；选择“自动判断”时不写入固定目标，由 FAS 根据实际帧节奏锁存目标。
- 移除不适用于此 Pixel 分支的 governor、设备缓解、勿扰配置入口；没有按帧时间自适应频率的 FAS。
- 配置使用 UTF-8/Base64 传递，严格校验 JSON 和支持字段，检查保存前的旧内容，互斥写入后原子替换。原生监听新增 IN_MOVED_TO，支持替换后的配置重新加载。保存失败显示错误并恢复页面开关。

暂停/恢复仍使用模块的动作按钮。

实机 WebUI 验证：ReSukiSU 4.2.0-rc3 的中文主页、游戏列表、设置及单游戏页面正常；允许高帧率开关关闭/恢复，全局 Lite 开启/恢复，游戏 Lite 开启/恢复均与配置回读一致。严格输入校验、过期写入拒绝、原子替换后的原生配置/游戏列表重新加载及测试配置哈希恢复通过。重启后服务、双进程与心跳正常，运行文件校验通过；非游戏前台请求释放，配置保留。

## 0.1.5 状态页心跳修复

状态脚本原先用包含休眠时间的 `/proc/uptime` 与原生服务的 `CLOCK_MONOTONIC` 心跳相减。手机息屏累计休眠后，即使控制器和监听进程仍每秒运行，也可能把状态误报为停止。0.1.5 改为比较心跳文件修改时间与 `date +%s`，两者使用同一时钟；仍要求进程路径匹配且心跳在 8 秒内更新。此修复仅涉及 WebUI 只读状态脚本，调度频率、帧率策略和温控保护代码不变。

## 0.1.6 游戏 UID 恢复修复

和平精英 29 分 46 秒实战监测发现，同一游戏 PID 在原生服务短暂切换 balance/performance 后，可能继续以 UID 0 提交游戏请求。Pixel 控制器会正确拒绝该请求并释放频率投票与帧率策略，但游戏持续前台时不会自动恢复。0.1.6 保留游戏身份的 UID，在同 PID 前台回调中刷新身份，并将 UID 纳入性能请求的重应用条件；UID 0 不再发送性能请求。频率、温度保护和帧率策略参数不变。原始监测见 `pubg-full-match-2026-09-28.md`。

实机短时回归：身份采样中游戏请求均带有正确 UID；游戏切至桌面、息屏后解锁返回时，七项请求和帧率策略均先释放后恢复。退出游戏后七项请求为 0、帧率策略恢复原值。随后真实重启，原生服务及控制器自动运行、运行文件校验通过。此测试验证了故障路径，不代表长时间游戏的帧率提升或完全覆盖竞态；整局复测仍可继续进行。

## 安装

1. 在 KernelSU 中停用旧 `Pixel Global Game Uclamp`（ID `pixel_pubg_uclamp_cp41`，0.3.2）以及原版 Encore（如已安装）。
2. 安装 `Encore-Pixel-10-Android17-v0.2.3-generic.zip`，确认新模块处于启用状态，重启。
3. 开机后等待约 25 秒。游戏列表初次建立时使用上游列表筛选已安装应用；本机识别到三角洲、和平精英、光遇。不同游戏共用默认模式，可逐游戏选择 Lite。
4. 进入三角洲 120 帧实际对局，核对 status、投票读回值和实际呈现帧时间；最低请求高于当前系统上限时该节点释放投票。退出和息屏后应看到请求及帧率策略恢复。

已有独立温控模块可以保留；本模块自身不修改温控配置。已知旧调度或原版 Encore 仍启用时，本模块拒绝进入实际控制。

动作按钮：暂停并恢复请求；再次点击恢复服务。暂停状态跨重启保留。

配置目录：`/data/adb/.config/encore_pixel_cp41`。

- `gamelist.json`：游戏包名与 `lite_mode`；新增游戏可以手动添加。不依赖单一游戏包名。
- `config.json`：`preferences.enforce_lite_mode=true` 全局使用 Lite；`disable_tweaks=true` 暂停频率请求。修改已有文件后原生监听读取配置。
- `status`：当前有效状态、前台包名、电池温度（0.1°C）、电量和游戏 PID。
- `controller.log` / `encore.log` / `service.log`：硬件请求、事件监听和服务诊断。
- `journal`：同次开机的恢复记录，不要在服务运行时删除。
- `frame-policy-journal`：同次开机的默认帧率策略恢复记录。
- `disable-high-fps`：创建后只关闭帧率策略适配，频率控制继续运行。

维护命令（在 `adb shell` 的 root shell 中运行）：

```sh
cat /data/adb/.config/encore_pixel_cp41/status
touch /data/adb/.config/encore_pixel_cp41/pause
# 控制器检查暂停文件后自行恢复并退出。
# 控制器退出后，可再次恢复遗留请求：
/data/adb/modules/encore_pixel_cp41/bin/pixel-control restore
```

卸载会停止控制并尝试恢复；配置及诊断保留供审查。更新到其他 Android 17 QPR/测试版本后，基础调度会按设备、SDK 和频率节点预检决定是否加载；`libgui.so` 哈希未登记时只停用 FAS 帧探针，不猜测 uprobe 偏移。温控模块同样按 Thermal JSON 结构和目标频率预检，匹配失败会自动隔离。

## 已验证和仍需验证

实机验证：ARM64 NDK r30 编译、三个已安装游戏识别、Android 17 Binder 事务号解析和事件注册、7 个独立投票同值写入预检、短时提高投票后全部恢复、测试前后温控上限一致。模拟节点验证了崩溃后日志恢复、外部写入时不覆盖以及损坏日志拒绝控制。

使用只观察模式临时把当前普通应用注册为游戏，可以测试原生性能模式进入/退出，而不写硬件；测试后恢复正式游戏列表。这不能替代真实游戏测试。

0.1.2 已实机验证三角洲 GAME/GAME_LITE 切换、前台频率请求和退出释放，以及帧率策略的短时关闭/恢复对照。0.1.3 已在本次开机加载，通过真实 Binder 临时关闭/恢复、模拟崩溃日志恢复/外部写入/事务号错误拒绝控制、安装阶段和 KernelSU 清理后的双重校验。新版真实游戏自动进入通过，20 个有效窗口约 103–120 FPS，窗口中位数 119.97 FPS，P95 帧间隔 8.37 ms；退出后 3 次采样均释放 7 项投票并恢复原帧率策略。0.1.4 已随后完成真实重启自动加载验证；尚未验证多游戏连续对局、息屏/省电/高温状态实机切换及功耗。这些帧率是短时采样窗口数据。0.1.4 WebUI 验证另见 webui-validation.txt 与 webui-backend-validation.txt；没有根据每帧时间自适应频率的 FAS 控制或新的内核调度算法。

## 源码与复现

`Encore-Pixel-10-Android17-v0.2.3-generic-source.zip` 包含修改后的上游源码、固定的 RapidJSON/spdlog 子模块源码、Pixel 控制器、WebUI 源码与依赖锁文件、模块脚本、编译命令、差异和实机验证日志。

上游 `binder_resolver.apk` 沿用 5.2.1 官方发布二进制，本次没有重编译 APK；Encore 源码仓库把它放在 `prebuilt/` 中，其源码另见 <https://github.com/Rem01Gaming/binder_resolver>。模块中的 `encored` 和 `pixel-control` 均由随附源码编译。APK 的哈希和来源记录在 `PROVENANCE.json` 中。

```sh
export ANDROID_NDK_HOME=/path/to/android-ndk-r30
bash rebuild.sh
```

固定依赖：RapidJSON `24b5e7a8b27f42fa16b96fc70aade9106cf7102f`；spdlog `1685e694c5cd8328280c48f8b94b1281c17c6fee`。源码压缩包已包含这些头文件和各自许可证。

WebUI 构建环境：Node.js 24.19.0，npm 11.17.0。在 fork/webui 中运行 npm ci --ignore-scripts --no-audit --no-fund 和 npm run build，或运行 build-webui.ps1。先构建原生程序和前端，再运行 package.py，最后运行 package-regression.py 核对安装与清理后运行文件。
