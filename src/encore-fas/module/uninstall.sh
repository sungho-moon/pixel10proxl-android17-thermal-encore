#!/system/bin/sh
MODPATH=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
touch "$C/pause"
PID=$(cat "$C/controller.pid" 2>/dev/null)
if [ -n "$PID" ] && [ "$(readlink /proc/$PID/exe)" = "$MODPATH/bin/pixel-control" ]; then
 kill -TERM "$PID"
 for i in $(seq 1 80); do [ -d "/proc/$PID" ] || break; sleep 0.1; done
fi
"$MODPATH/bin/pixel-control" restore
# Keep configuration and diagnostics available for recovery and review.
