#!/system/bin/sh
MODDIR=${0%/*}
. "$MODDIR/tools/lib.sh"
[ -e "$MODDIR/disable" ] && exit 0
[ -e "$MODDIR/remove" ] && exit 0
[ -e "$MODDIR/state/pending-boot" ] && { pgt_quarantine "$MODDIR" previous_boot_unverified; exit 0; }
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
