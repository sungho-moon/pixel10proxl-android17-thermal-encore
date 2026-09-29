#!/system/bin/sh
# Auto-discovery and fail-closed checks for the CP41 thermal profile.
pgt_sha() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }
pgt_reason() { printf 'PGT_FAIL=%s\n' "$1"; }
pgt_platform() {
  [ "$(getprop ro.product.device)" = "mustang" ] || { pgt_reason device_mismatch; return 1; }
  [ "$(getprop ro.build.version.sdk)" = "37" ] || { pgt_reason sdk_mismatch; return 1; }
}
pgt_conflicts() {
  _active=$1; _updates=$2
  for _root in "$_active" "$_updates"; do
    for _dir in "$_root"/*; do
      [ -d "$_dir" ] || continue; [ -f "$_dir/module.prop" ] || continue
      [ -e "$_dir/disable" ] && continue; [ -e "$_dir/remove" ] && continue
      _id=$(sed -n 's/^id=//p' "$_dir/module.prop" | head -n 1)
      [ "$_id" = pixel_game_thermal_cp41 ] && continue
      case "$_id" in ptune|pixel-10-pro-xl-thermal-fix) pgt_reason "conflicting_module:$_id"; return 1;; esac
      for _file in "$_dir/system/vendor/etc"/thermal*.json "$_dir/vendor/etc"/thermal*.json "$_dir/system/etc"/thermal*.json; do
        [ -f "$_file" ] || continue; pgt_reason "thermal_overlay_conflict:$_id"; return 1
      done
    done
  done
}
pgt_config() {
  [ -r "$1/state/config.env" ] || { pgt_reason config_state_missing; return 1; }
  . "$1/state/config.env"
  [ -n "${CONFIG_NAME:-}" ] || { pgt_reason config_name_missing; return 1; }
  [ -r "$1/profiles/stock.json" ] && [ -r "$1/profiles/game.json" ] || { pgt_reason profiles_missing; return 1; }
  [ "$(pgt_sha "$1/profiles/stock.json")" = "${STOCK_SHA:-invalid}" ] || { pgt_reason stock_profile_hash; return 1; }
  [ "$(pgt_sha "$1/profiles/game.json")" = "${GAME_SHA:-invalid}" ] || { pgt_reason game_profile_hash; return 1; }
  "$1/bin/thermal-profile-builder" probe "$1/profiles/stock.json" >/dev/null 2>&1 || { pgt_reason stock_profile_schema; return 1; }
  "$1/bin/thermal-profile-builder" probe "$1/profiles/game.json" >/dev/null 2>&1 || { pgt_reason game_profile_schema; return 1; }
}
pgt_active_file() {
  _m=$1; . "$_m/state/config.env"
  [ -s "/vendor/etc/$CONFIG_NAME" ] || { pgt_reason active_config_missing; return 1; }
  _sha=$(pgt_sha "/vendor/etc/$CONFIG_NAME")
  [ "$_sha" = "$GAME_SHA" ] || [ "$_sha" = "$STOCK_SHA" ] || { pgt_reason active_config_unrecognized; return 1; }
}
pgt_hardware() {
  grep -qw 1881000 /sys/devices/system/cpu/cpufreq/policy0/scaling_available_frequencies || { pgt_reason unsupported_cpu0_target; return 1; }
  grep -qw 2534000 /sys/devices/system/cpu/cpufreq/policy2/scaling_available_frequencies || { pgt_reason unsupported_cpu2_target; return 1; }
  grep -qw 2937000 /sys/devices/system/cpu/cpufreq/policy7/scaling_available_frequencies || { pgt_reason unsupported_cpu7_target; return 1; }
  grep -qw 633000000 /sys/devices/platform/34f00000.gpu0/devfreq/34f00000.gpu0/available_frequencies || { pgt_reason unsupported_gpu_target; return 1; }
}
pgt_check() {
  _module=$1; _vendor=${2:-/vendor/etc}; _modules=${3:-/data/adb/modules}; _pending=${4:-/data/adb/modules_update}
  [ -r "$_module/state/config.env" ] || { pgt_reason metadata_missing; return 1; }
  pgt_platform || return 1; pgt_config "$_module" || return 1; pgt_active_file "$_module" || return 1
  pgt_hardware || return 1; pgt_conflicts "$_modules" "$_pending" || return 1
  printf '%s\n' PGT_CHECK=pass
}
pgt_log() { _m=$1; shift; mkdir -p "$_m/state"; printf '%s %s\n' "$(date +%s)" "$*" >> "$_m/state/boot.log"; chmod 0600 "$_m/state/boot.log" 2>/dev/null || true; }
pgt_quarantine() {
  _m=$1; _why=$2; . "$_m/state/config.env" 2>/dev/null || true
  [ -n "${CONFIG_NAME:-}" ] && rm -f "$_m/system/vendor/etc/$CONFIG_NAME" 2>/dev/null || true
  touch "$_m/skip_mount" "$_m/disable"; printf '%s\n' "$_why" > "$_m/state/blocked-reason"; pgt_log "$_m" "quarantined=$_why"
}
