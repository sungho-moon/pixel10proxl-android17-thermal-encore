#!/system/bin/sh
MODDIR=${1:-${0%/*}/..}
export PGT_SELF_ID=encore_pixel_cp41
. "$MODDIR/tools/lib.sh"
BUILDER="$MODDIR/bin/thermal-profile-builder"
chmod 0755 "$BUILDER" 2>/dev/null || true
fail() { pgt_disable "$MODDIR" "$1"; exit 1; }
[ -x "$BUILDER" ] || fail builder_missing
mkdir -p "$MODDIR/profiles" "$MODDIR/state" "$MODDIR/system/vendor/etc"
source_file=''; source_real=''; config_name=''
# Run before module mounts and only cache the current system's config. A
# retired module's stock cache may belong to an older OTA with the same schema.
pgt_platform || fail unsupported_sdk
pgt_conflicts /data/adb/modules /data/adb/modules_update || fail conflicting_thermal_module
if [ -z "$source_file" ]; then
  for dir in /data/adb/magisk/mirror/vendor/etc /sbin/.magisk/mirror/vendor/etc /vendor/etc /system/vendor/etc; do
    [ -d "$dir" ] || continue
    for candidate in "$dir"/thermal_info_config*.json; do
      [ -r "$candidate" ] || continue
      if "$BUILDER" probe "$candidate" >/dev/null 2>&1; then
        candidate_real=$(readlink -f "$candidate" 2>/dev/null || printf '%s' "$candidate")
        if [ -n "$source_file" ]; then
          [ "$candidate_real" = "$source_real" ] || fail ambiguous_thermal_config
          continue
        fi
        source_file="$candidate"; source_real="$candidate_real"; config_name="${candidate##*/}"
      fi
    done
  done
fi
[ -n "$source_file" ] && [ -n "$config_name" ] || fail no_matching_thermal_config
stock_tmp="$MODDIR/profiles/stock.json.new"
rm -f "$stock_tmp"
copied=0
for attempt in 1 2 3 4 5; do
  if cp -fp "$source_file" "$stock_tmp" && [ -s "$stock_tmp" ]; then copied=1; break; fi
  rm -f "$stock_tmp"
  sleep 1
done
[ "$copied" = 1 ] || fail stock_cache_failed
mv -f "$stock_tmp" "$MODDIR/profiles/stock.json" || fail stock_cache_promote_failed
"$BUILDER" patch "$MODDIR/profiles/stock.json" "$MODDIR/profiles/game.json" >/dev/null 2>&1 || fail patch_rejected
stock_sha=$(pgt_sha "$MODDIR/profiles/stock.json")
game_sha=$(pgt_sha "$MODDIR/profiles/game.json")
fingerprint=$(getprop ro.build.fingerprint)
cat > "$MODDIR/state/config.env.new" <<EOF
CONFIG_NAME=$config_name
STOCK_SHA=$stock_sha
GAME_SHA=$game_sha
BUILD_FINGERPRINT=$fingerprint
DISCOVERY=thermal-profile-builder
PATCH_CHANGES=57
EOF
chmod 0600 "$MODDIR/state/config.env.new" &&
  mv -f "$MODDIR/state/config.env.new" "$MODDIR/state/config.env" || fail metadata_promote_failed
cp -fp "$MODDIR/state/config.env" "$MODDIR/expected.env"
printf '%s\n' "$config_name" > "$MODDIR/config-name"
printf '%s\n' discovered > "$MODDIR/state/discovery"
rm -f "$MODDIR/state/blocked-reason" "$(pgt_marker_dir)/thermal-disabled"
chmod 0600 "$MODDIR/state/config.env" "$MODDIR/expected.env"
pgt_log "$MODDIR" "discovered_config=$config_name changes=57 fingerprint=$fingerprint"
