#!/system/bin/sh
MODDIR=${1:-${0%/*}/..}
. "$MODDIR/tools/lib.sh"
BUILDER="$MODDIR/bin/thermal-profile-builder"
chmod 0755 "$BUILDER" 2>/dev/null || true
fail() { printf '%s\n' "$1" > "$MODDIR/state/blocked-reason"; touch "$MODDIR/skip_mount" "$MODDIR/disable"; pgt_log "$MODDIR" "discovery_failed=$1"; exit 1; }
[ -x "$BUILDER" ] || fail builder_missing
mkdir -p "$MODDIR/profiles" "$MODDIR/state" "$MODDIR/system/vendor/etc"
source_file=''; config_name=''
old=/data/adb/modules/pixel_game_thermal_cp41
if [ "$old" != "$MODDIR" ] && [ -r "$old/profiles/stock.json" ]; then
  [ -r "$old/state/config.env" ] && . "$old/state/config.env" || true
  [ -n "${CONFIG_NAME:-}" ] || CONFIG_NAME=$(find "$old/system/vendor/etc" -maxdepth 1 -type f -name 'thermal_info_config*.json' 2>/dev/null | head -n 1 | sed 's#.*/##')
  if [ -n "${CONFIG_NAME:-}" ] && "$BUILDER" probe "$old/profiles/stock.json" >/dev/null 2>&1; then
    source_file="$old/profiles/stock.json"; config_name="$CONFIG_NAME"
  fi
fi
if [ -z "$source_file" ]; then
  for dir in /data/adb/magisk/mirror/vendor/etc /sbin/.magisk/mirror/vendor/etc /vendor/etc /system/vendor/etc; do
    [ -z "$source_file" ] || continue
    [ -d "$dir" ] || continue
    for candidate in "$dir"/thermal_info_config*.json; do
      [ -r "$candidate" ] || continue
      if "$BUILDER" probe "$candidate" >/dev/null 2>&1; then
        [ -z "$source_file" ] || fail ambiguous_thermal_config
        source_file="$candidate"; config_name="${candidate##*/}"
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
game_tmp="$MODDIR/system/vendor/etc/$config_name.new"
rm -f "$game_tmp"
cp -fp "$MODDIR/profiles/game.json" "$game_tmp" && chmod 0644 "$game_tmp" && mv -f "$game_tmp" "$MODDIR/system/vendor/etc/$config_name" || fail overlay_prepare_failed
stock_sha=$(pgt_sha "$MODDIR/profiles/stock.json")
game_sha=$(pgt_sha "$MODDIR/profiles/game.json")
fingerprint=$(getprop ro.build.fingerprint)
cat > "$MODDIR/state/config.env" <<EOF
CONFIG_NAME=$config_name
STOCK_SHA=$stock_sha
GAME_SHA=$game_sha
BUILD_FINGERPRINT=$fingerprint
DISCOVERY=thermal-profile-builder
PATCH_CHANGES=57
EOF
cp -fp "$MODDIR/state/config.env" "$MODDIR/expected.env"
printf '%s\n' "$config_name" > "$MODDIR/config-name"
printf '%s\n' discovered > "$MODDIR/state/discovery"
rm -f "$MODDIR/state/blocked-reason" "$MODDIR/disable" "$MODDIR/skip_mount"
chmod 0600 "$MODDIR/state/config.env" "$MODDIR/expected.env"
pgt_log "$MODDIR" "discovered_config=$config_name changes=57 fingerprint=$fingerprint"
