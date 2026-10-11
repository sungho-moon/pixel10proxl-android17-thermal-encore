#!/system/bin/sh
# Verify the integrated thermal profile without taking down Encore on failure.
MODDIR=${0%/*}
export PGT_SELF_ID=encore_pixel_cp41
. "$MODDIR/tools/lib.sh"
C=$(pgt_marker_dir)
[ -e "$C/thermal-disabled" ] && exit 0

i=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$i" -lt 180 ]; do sleep 1; i=$((i + 1)); done
[ "$(getprop sys.boot_completed)" = 1 ] || { pgt_log "$MODDIR" thermal_boot_not_completed; exit 0; }
sleep 2
# Discovery is restricted to post-fs-data, before any thermal overlay mounts.
[ -r "$MODDIR/state/config.env" ] || { pgt_disable "$MODDIR" runtime_metadata_missing; exit 0; }
result=$(pgt_check "$MODDIR" /vendor/etc /data/adb/modules /data/adb/modules_update 2>&1)
if [ $? -ne 0 ]; then
  pgt_quarantine "$MODDIR" "runtime:$result"
  exit 0
fi
. "$MODDIR/state/config.env"
mode=$(cat "$MODDIR/state/applied-mode" 2>/dev/null)
case "$mode" in game) expected=$GAME_SHA;; stock) expected=$STOCK_SHA;; *) pgt_quarantine "$MODDIR" runtime_mode_missing; exit 0;; esac
actual=$(pgt_sha "/vendor/etc/$CONFIG_NAME")
hal=$(getprop init.svc.vendor.thermal-hal)
snapshot=$(dumpsys thermalservice 2>/dev/null)
ready=0; printf '%s\n' "$snapshot" | grep -q 'HAL Ready: true' && ready=1
override=unknown; printf '%s\n' "$snapshot" | grep -q 'IsStatusOverride: false' && override=false
if [ "$actual" != "$expected" ] || [ "$hal" != running ] || [ "$ready" != 1 ] || [ "$override" != false ]; then
  pgt_quarantine "$MODDIR" "thermal_verification_failed:$CONFIG_NAME:$hal:$ready:$override"
  exit 0
fi
{
  printf 'boot_id=%s\n' "$(cat /proc/sys/kernel/random/boot_id)"
  printf 'mode=%s\n' "$mode"
  printf 'config_name=%s\n' "$CONFIG_NAME"
  printf 'active_sha256=%s\n' "$actual"
  printf 'thermal_hal=%s\n' "$hal"
  printf '%s\n' 'hal_ready=true' 'status_override=false' 'integrated=true'
} > "$MODDIR/state/runtime.env"
chmod 0600 "$MODDIR/state/runtime.env"
rm -f "$MODDIR/state/pending-boot"
pgt_log "$MODDIR" "thermal_verified=$mode config=$CONFIG_NAME"
