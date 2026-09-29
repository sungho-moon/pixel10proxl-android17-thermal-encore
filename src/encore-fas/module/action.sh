#!/system/bin/sh
MODPATH=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
if [ -f "$C/pause" ]; then
 rm -f "$C/pause"
 echo "Resuming Encore Pixel (25 second startup delay)."
 nohup sh "$MODPATH/service.sh" >/dev/null 2>&1 &
else
 touch "$C/pause"
 PID=$(cat "$C/controller.pid" 2>/dev/null)
 if [ -n "$PID" ] && [ "$(readlink /proc/$PID/exe)" = "$MODPATH/bin/pixel-control" ]; then
  kill -TERM "$PID"
 fi
 echo "Paused. Game frequency requests will be restored. Tap Action again to resume."
fi
cat "$C/status" 2>/dev/null
