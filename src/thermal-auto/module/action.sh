#!/system/bin/sh
MODDIR=${0%/*}
exec sh "$MODDIR/tools/control.sh" "$MODDIR" menu
