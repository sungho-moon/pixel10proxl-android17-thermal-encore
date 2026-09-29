#!/system/bin/sh
MODDIR=${1:-${0%/*}/..}
COMMAND=${2:-status}
. "$MODDIR/tools/lib.sh"
status() {
  . "$MODDIR/state/config.env" 2>/dev/null || { echo 'Auto config not initialized'; return 1; }
  requested=$(cat "$MODDIR/requested-mode" 2>/dev/null)
  actual=$(pgt_sha "/vendor/etc/$CONFIG_NAME")
  active=unknown
  [ "$actual" = "$STOCK_SHA" ] && active=stock
  [ "$actual" = "$GAME_SHA" ] && active=game
  echo "Detected config: $CONFIG_NAME"
  echo "Requested for next reboot: $requested"
  echo "Current vendor file profile: $active"
  echo "Thermal HAL: $(getprop init.svc.vendor.thermal-hal)"
  [ ! -r "$MODDIR/state/runtime.env" ] || cat "$MODDIR/state/runtime.env"
  [ ! -r "$MODDIR/state/blocked-reason" ] || cat "$MODDIR/state/blocked-reason"
}
select_mode() {
  case "$1" in game|stock) ;; *) echo 'Invalid mode'; return 2;; esac
  pgt_check "$MODDIR" /vendor/etc /data/adb/modules /data/adb/modules_update || return 1
  printf '%s\n' "$1" > "$MODDIR/requested-mode.new" && mv "$MODDIR/requested-mode.new" "$MODDIR/requested-mode"
  chmod 0600 "$MODDIR/requested-mode"
  echo "Selected $1. Reboot to apply."
}
case "$COMMAND" in status) status;; game|stock) select_mode "$COMMAND";; *) echo 'Usage: control.sh MODULE_DIR status|game|stock'; exit 2;; esac
