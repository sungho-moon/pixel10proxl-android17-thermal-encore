#!/system/bin/sh
MODPATH=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
mkdir -p "$C"
# Post-boot only; never participate in early boot or restart the vendor HAL.
for i in $(seq 1 120); do
 [ "$(getprop sys.boot_completed)" = 1 ] && break
 sleep 1
done
[ "$(getprop sys.boot_completed)" = 1 ] || { echo 'STARTUP_FAILED boot timeout' > "$C/status"; exit 1; }
sleep 25
[ -f "$MODPATH/disable" ] && exit 0
[ -f "$MODPATH/remove" ] && exit 0
[ -f "$MODPATH/update" ] && exit 0
[ -f "$C/pause" ] && exit 0
[ "$(getprop ro.product.device)" = mustang ] || { echo 'STARTUP_FAILED device mismatch' > "$C/status"; exit 1; }
[ "$(getprop ro.build.version.sdk)" = 37 ] || { echo 'STARTUP_FAILED Android SDK mismatch' > "$C/status"; exit 1; }
# KernelSU removes customize.sh and README.md after installation. SHA256SUMS
# covers only persistent runtime files; INSTALL_SHA256SUMS is used by customize.sh.
if ! (cd "$MODPATH" && sha256sum -c SHA256SUMS) > "$C/startup-check.log" 2>&1; then
 echo 'STARTUP_FAILED runtime checksum; see startup-check.log' > "$C/status"
 exit 1
fi
# A separate shell waits for the controller and recovers its journal if it crashes.
"$MODPATH/bin/pixel-control" run >> "$C/service.log" 2>&1 &
PID=$!
trap 'kill -TERM "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; "$MODPATH/bin/pixel-control" restore >> "$C/service.log" 2>&1' EXIT INT TERM
wait "$PID"
RESULT=$?
trap - EXIT INT TERM
"$MODPATH/bin/pixel-control" restore >> "$C/service.log" 2>&1
echo "controller_exit=$RESULT" >> "$C/service.log"
# No restart loop: a failing backend stays off until manually resumed/rebooted.
