#!/system/bin/sh
MODDIR=${0%/*}
. "$MODDIR/tools/lib.sh"
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
i=0
while [ "$(getprop sys.boot_completed)" != 1 ] && [ "$i" -lt 180 ]; do sleep 1; i=$((i + 1)); done
[ "$(getprop sys.boot_completed)" = 1 ] || { pgt_log "$MODDIR" boot_not_completed; exit 0; }
sleep 20
result=$(pgt_check "$MODDIR" /vendor/etc /data/adb/modules /data/adb/modules_update 2>&1)
if [ $? -ne 0 ]; then pgt_quarantine "$MODDIR" "runtime:$result"; exit 0; fi
. "$MODDIR/state/config.env"
mode=$(cat "$MODDIR/state/applied-mode" 2>/dev/null)
case "$mode" in game) expected=$GAME_SHA;; stock) expected=$STOCK_SHA;; *) pgt_quarantine "$MODDIR" runtime_mode_missing; exit 0;; esac
actual=$(pgt_sha "/vendor/etc/$CONFIG_NAME")
hal=$(getprop init.svc.vendor.thermal-hal)
snapshot=$(dumpsys thermalservice 2>/dev/null)
ready=0
printf '%s\n' "$snapshot" | grep -q 'HAL Ready: true' && ready=1
override=unknown
printf '%s\n' "$snapshot" | grep -q 'IsStatusOverride: false' && override=false
if [ "$actual" != "$expected" ] || [ "$hal" != running ] || [ "$ready" != 1 ] || [ "$override" != false ]; then
  pgt_log "$MODDIR" "runtime_failed config=$CONFIG_NAME active_sha=$actual hal=$hal ready=$ready override=$override"
  pgt_quarantine "$MODDIR" overlay_or_hal_verification_failed
  exit 0
fi
{
  printf 'boot_id=%s\n' "$(cat /proc/sys/kernel/random/boot_id)"
  printf 'mode=%s\n' "$mode"
  printf 'config_name=%s\n' "$CONFIG_NAME"
  printf 'active_sha256=%s\n' "$actual"
  printf 'thermal_hal=%s\n' "$hal"
  printf '%s\n' 'hal_ready=true' 'status_override=false' 'auto_discovery=verified'
} > "$MODDIR/state/runtime.env"
chmod 0600 "$MODDIR/state/runtime.env"
rm -f "$MODDIR/state/pending-boot"
pgt_log "$MODDIR" "boot_verified=$mode config=$CONFIG_NAME"
