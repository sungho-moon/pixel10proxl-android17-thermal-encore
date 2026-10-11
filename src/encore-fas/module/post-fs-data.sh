#!/system/bin/sh
# Materialize the discovered Thermal JSON before the vendor HAL starts.
MODDIR=${0%/*}
export PGT_SELF_ID=encore_pixel_cp41
. "$MODDIR/tools/lib.sh"
C=$(pgt_marker_dir)

if [ -e "$MODDIR/disable" ] || [ -e "$MODDIR/remove" ]; then
  exit 0
fi
# An OTA must regenerate both profiles from the new, not-yet-overlaid vendor.
if [ -r "$MODDIR/state/config.env" ]; then
  . "$MODDIR/state/config.env"
  if [ "${BUILD_FINGERPRINT:-}" != "$(getprop ro.build.fingerprint)" ]; then
    rm -f "$MODDIR/state/config.env" "$MODDIR/state/runtime.env" \
      "$MODDIR/state/applied-mode" "$MODDIR/system/vendor/etc"/thermal_info_config*.json \
      "$C/thermal-disabled"
  fi
fi
[ -e "$C/thermal-disabled" ] && exit 0
[ -r "$MODDIR/state/config.env" ] || sh "$MODDIR/tools/discover.sh" "$MODDIR"
[ -r "$MODDIR/state/config.env" ] || exit 0

result=$(pgt_check "$MODDIR" /vendor/etc /data/adb/modules /data/adb/modules_update 2>&1)
if [ $? -ne 0 ]; then
  pgt_quarantine "$MODDIR" "post_fs:$result"
  exit 0
fi
. "$MODDIR/state/config.env"

relaxed=$(cat "$C/thermal-relaxed" 2>/dev/null)
case "$relaxed" in
  0) mode=stock ;;
  1) mode=game ;;
  *) mode=game; printf '%s\n' 1 > "$C/thermal-relaxed"; chmod 0600 "$C/thermal-relaxed" ;;
esac
printf '%s\n' "$mode" > "$MODDIR/requested-mode.new" && mv -f "$MODDIR/requested-mode.new" "$MODDIR/requested-mode"
case "$mode" in
  game) source="$MODDIR/profiles/game.json" ;;
  stock) source="$MODDIR/profiles/stock.json" ;;
esac
target="$MODDIR/system/vendor/etc/$CONFIG_NAME"
cp -fp "$source" "$target.new" && chmod 0644 "$target.new" && mv -f "$target.new" "$target" || {
  pgt_quarantine "$MODDIR" materialization_failed
  exit 0
}
printf '%s\n' "$mode" > "$MODDIR/state/applied-mode"
printf '%s\n' "$(cat /proc/sys/kernel/random/boot_id)" > "$MODDIR/state/pending-boot"
pgt_log "$MODDIR" "prepared=$mode config=$CONFIG_NAME"
