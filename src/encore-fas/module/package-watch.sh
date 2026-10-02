#!/system/bin/sh
MODDIR=${0%/*}
[ -d /data/app ] || exit 0
exec toybox inotifyd "$MODDIR/package-event.sh" /data/app:mn
