#!/system/bin/sh
MODDIR=${0%/*}
. "$MODDIR/tools/lib.sh"
# A quarantine is automatic only when auto-disabled is present. Keep a manual
# KernelSU disable intact, but retry an automatic quarantine after an OTA.
if [ -f "$MODDIR/state/auto-disabled" ]; then
  blocked_fingerprint=$(cat "$MODDIR/state/blocked-fingerprint" 2>/dev/null)
  current_fingerprint=$(getprop ro.build.fingerprint)
  if [ -n "$blocked_fingerprint" ] && [ "$blocked_fingerprint" != "$current_fingerprint" ]; then
    rm -f "$MODDIR/disable" "$MODDIR/skip_mount" "$MODDIR/state/auto-disabled" \
      "$MODDIR/state/blocked-fingerprint" "$MODDIR/state/blocked-reason"
    rm -f "$(pgt_marker_dir)/thermal-disabled"
  fi
fi
# Re-discover after an OTA before honoring a quarantine marker. Otherwise a
# stale fingerprint can permanently prevent this hook from reaching discovery.
if [ -r "$MODDIR/state/config.env" ]; then
  . "$MODDIR/state/config.env"
  if [ "${BUILD_FINGERPRINT:-}" != "$(getprop ro.build.fingerprint)" ]; then
    rm -f "$MODDIR/state/config.env" "$MODDIR/state/runtime.env" \
      "$MODDIR/state/applied-mode" "$MODDIR/state/blocked-reason" \
      "$MODDIR/system/vendor/etc"/thermal_info_config*.json
    if [ -f "$MODDIR/disable" ] && [ -f "$MODDIR/state/auto-disabled" ]; then
      rm -f "$MODDIR/disable" "$MODDIR/skip_mount" "$MODDIR/state/auto-disabled"
      rm -f "$(pgt_marker_dir)/thermal-disabled" "$MODDIR/state/blocked-fingerprint"
    fi
  fi
fi
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
[ ! -r "$MODDIR/state/config.env" ] && sh "$MODDIR/tools/discover.sh" "$MODDIR"
[ -r "$MODDIR/state/config.env" ] || exit 0
result=$(pgt_check "$MODDIR" /vendor/etc /data/adb/modules /data/adb/modules_update 2>&1)
if [ $? -ne 0 ]; then pgt_quarantine "$MODDIR" "post_fs:$result"; exit 0; fi
. "$MODDIR/state/config.env"
target="$MODDIR/system/vendor/etc/$CONFIG_NAME"
mode=$(cat "$MODDIR/requested-mode" 2>/dev/null)
case "$mode" in game) source="$MODDIR/profiles/game.json";; stock) source="$MODDIR/profiles/stock.json";; *) pgt_quarantine "$MODDIR" invalid_requested_mode; exit 0;; esac
cp -fp "$source" "$target.new" && chmod 0644 "$target.new" && mv "$target.new" "$target" || { pgt_quarantine "$MODDIR" materialization_failed; exit 0; }
printf '%s\n' "$mode" > "$MODDIR/state/applied-mode"
printf '%s\n' "$(cat /proc/sys/kernel/random/boot_id)" > "$MODDIR/state/pending-boot"
pgt_log "$MODDIR" "prepared=$mode config=$CONFIG_NAME"
