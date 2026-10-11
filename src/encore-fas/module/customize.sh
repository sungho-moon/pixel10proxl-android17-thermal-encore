#!/system/bin/sh
ui_print "Encore Pixel Tensor Android 17 $(sed -n 's/^version=//p' "$MODPATH/module.prop")"
[ "$(getprop ro.build.version.sdk)" = 37 ] || abort "需要 Android SDK 37 / Android SDK 37 required"
[ "$ARCH" = arm64 ] || abort "需要 arm64 架构 / arm64 required"
C=/data/adb/.config/encore_pixel_cp41
mkdir -p "$C"
chmod 700 "$C"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/thread/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/tools" 0 0 0755 0755
set_perm "$MODPATH/thread/service.sh" 0 0 0755
for s in service.sh post-fs-data.sh thermal-service.sh action.sh uninstall.sh ui-state.sh ui-write.sh refresh-games.sh package-event.sh package-watch.sh; do set_perm "$MODPATH/$s" 0 0 0755; done
set_perm "$MODPATH/fps-presets.conf" 0 0 0644
rm -f "$MODPATH/auto-game.sh"
rm -f "$C/auto-game.log" "$C/auto-game-service.log"
(cd "$MODPATH" && sha256sum -c INSTALL_SHA256SUMS) || abort "安装文件校验失败 / Install payload checksum failed"
# Older Encore builds used skip_mount because they had no vendor overlay.
# Remove it on upgrade so the integrated Thermal JSON can be mounted.
rm -f "$MODPATH/skip_mount"
for f in config.json device_mitigation.json default_cpu_gov; do
 [ -f "$C/$f" ] || cp "$MODPATH/defaults/$f" "$C/$f"
done
# Target FPS is configured per game in WebUI gamelist.json. Keep a legacy
# file empty so older installations do not reintroduce a global fixed target.
[ -f "$C/fas-targets.conf" ] || : > "$C/fas-targets.conf"
chmod 600 "$C/fas-targets.conf"

# Ask once during installation whether the integrated thermal profile should
# use its relaxed game ceiling.  This is deliberately kept in the shared
# configuration directory so the thermal backend can consume it regardless of
# whether the module is installed by KernelSU or Magisk.  Volume Up is the
# recommended choice; an idle prompt expires to the same default.
select_thermal_mode() {
  choice_file="$C/.thermal-choice.$$"
  rm -f "$choice_file"
  ui_print ""
  ui_print "温控限制 / Thermal limits"
  ui_print "音量上键 / Volume Up: 放宽温控（推荐）/ Relax limits (recommended)"
  ui_print "音量下键 / Volume Down: 原厂温控 / Stock limits"
  ui_print "10 秒无操作默认放宽 / No input for 10 seconds defaults to relaxed"

  # Keep one reader alive: a single-event reader can stop at MSC_SCAN before
  # receiving the key event. Ignore releases and unrelated touch/power events.
  thermal_choice=relaxed
  getevent -ql > "$choice_file" 2>/dev/null &
  reader=$!
  ticks=0
  chosen=0
  while [ "$ticks" -lt 100 ]; do
    sleep 0.1
    while IFS= read -r event; do
      case "$event" in
        *KEY_VOLUMEUP*DOWN*) thermal_choice=relaxed; chosen=1; break ;;
        *KEY_VOLUMEDOWN*DOWN*) thermal_choice=stock; chosen=1; break ;;
      esac
    done < "$choice_file"
    [ "$chosen" = 1 ] && break
    ticks=$((ticks + 1))
  done
  kill "$reader" 2>/dev/null || true
  wait "$reader" 2>/dev/null || true
  rm -f "$choice_file"

  thermal_tmp="$C/thermal-relaxed.new.$$"
  if [ "$thermal_choice" = relaxed ]; then
    printf '1\n' > "$thermal_tmp"
    ui_print "已选择：放宽温控 / Selected: relaxed thermal limits"
  else
    printf '0\n' > "$thermal_tmp"
    ui_print "已选择：原厂温控 / Selected: stock thermal limits"
  fi
  chmod 600 "$thermal_tmp"
  mv -f "$thermal_tmp" "$C/thermal-relaxed"
  # A previous build may have quarantined only its thermal backend.  A new
  # install gets a fresh discovery attempt after the user makes this choice.
  rm -f "$C/thermal-disabled"
}
select_thermal_mode
[ -d "$MODPATH/state" ] || mkdir -p "$MODPATH/state"
[ -d "$MODPATH/profiles" ] || mkdir -p "$MODPATH/profiles"
[ -d "$MODPATH/system/vendor/etc" ] || mkdir -p "$MODPATH/system/vendor/etc"
# Re-discover the stock schema for this boot/build; never carry a stale
# fingerprint or generated overlay across a module update.
rm -f "$MODPATH/state/config.env" "$MODPATH/state/expected.env" "$MODPATH/state/runtime.env" \
  "$MODPATH/state/pending-boot" "$MODPATH/state/applied-mode" "$MODPATH/state/blocked-reason" \
  "$MODPATH/system/vendor/etc"/thermal_info_config*.json
[ -f "$C/gamelist.json" ] || printf '{}\n' > "$C/gamelist.json"
"$MODPATH/refresh-games.sh" install || abort "已安装游戏列表刷新失败 / Installed game list refresh failed"
"$MODPATH/bin/encored" check_gamelist || abort "游戏列表无效 / Invalid game list"
"$MODPATH/bin/pixel-control" probe || abort "Pixel 频率接口检查失败 / Pixel frequency interface check failed"
ui_print "请保持 Encore Pixel 模块启用，并停用独立 Uclamp/fas-rs 模块。"
ui_print "Keep Encore Pixel enabled; disable standalone Uclamp/fas-rs modules."
ui_print "温控与调度已整合 / Thermal control and scheduling are integrated."
ui_print "请停用独立温控模块并重启 / Disable standalone thermal modules, then reboot."
ui_print "全局/轻量模式会在退出、息屏或省电时恢复请求。"
ui_print "Full/lite modes restore requests on exit, screen-off or battery saver."
ui_print "WebUI 支持状态、游戏列表、Lite 和 30/60/90/120 FPS。"
ui_print "WebUI: status, game list, Lite mode and 30/60/90/120 FPS presets."
ui_print "fas-rs 比例反馈运行在 Encore 内，无第二个 CPU 频率写入器。"
ui_print "fas-rs proportional feedback runs inside Encore; no second CPU writer."
ui_print "详见 FAS_EXPERIMENT.md / See FAS_EXPERIMENT.md for targets and rollback."
