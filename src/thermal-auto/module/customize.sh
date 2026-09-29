#!/system/bin/sh
SKIPUNZIP=0
ui_print "Pixel 10 Pro XL Game Thermal 1.6.0 Auto Config"
ui_print "Schema discovery will run after the complete module is extracted"
[ "$(getprop ro.product.device)" = mustang ] || abort "Unsupported device"
[ "$(getprop ro.build.version.sdk)" = 37 ] || abort "Unsupported Android SDK"
capacity=$(cat /sys/class/power_supply/battery/capacity 2>/dev/null || echo 0)
[ "$capacity" -ge 15 ] || abort "Charge battery to at least 15%"
mkdir -p "$MODPATH/state" "$MODPATH/profiles" "$MODPATH/system/vendor/etc"
rm -f "$MODPATH/disable" "$MODPATH/skip_mount" "$MODPATH/state/blocked-reason" "$MODPATH/state/config.env" "$MODPATH/state/runtime.env"
printf '%s\n' game > "$MODPATH/requested-mode"
printf '%s\n' pending > "$MODPATH/state/discovery"
# No static thermal overlay is shipped. post-fs-data creates the file after
# the binary and all module files are present.
rm -f "$MODPATH/system/vendor/etc/thermal_info_config"*.json 2>/dev/null || true
set_perm_recursive "$MODPATH" 0 0 0755 0644
for file in customize.sh post-fs-data.sh service.sh action.sh uninstall.sh tools/*.sh; do
  [ -f "$MODPATH/$file" ] && set_perm "$MODPATH/$file" 0 0 0755
done
ui_print "Automatic Thermal JSON discovery deferred to post-fs-data"
