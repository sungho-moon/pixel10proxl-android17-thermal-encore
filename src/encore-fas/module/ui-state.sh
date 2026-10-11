#!/system/bin/sh
M=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
STATUS=$(cat "$C/status" 2>/dev/null || echo 'UNKNOWN')
echo STATUS_BEGIN
printf '%s\n' "$STATUS"
echo STATUS_END

# Read the current controller snapshot, never an old log entry.
# -1 means unavailable (for example, an older native controller).
FAS_LEVEL=$(printf '%s\n' "$STATUS" | sed -n 's/.*[[:space:]]FAS_LEVEL=\([0-9][0-9]*\)\([[:space:]].*\)\{0,1\}$/\1/p' | head -n 1)
case "$FAS_LEVEL" in 0|1|2|3) ;; *) FAS_LEVEL=-1;; esac
FOREGROUND_PACKAGE=$(printf '%s\n' "$STATUS" | sed -n 's/.*[[:space:]]package=\([^[:space:]]*\).*/\1/p' | head -n 1)
GAME_ACTIVE=0
case "$STATUS" in GAME\ *|GAME_LITE\ *) GAME_ACTIVE=1;; esac
# Native heartbeat uses CLOCK_MONOTONIC, which excludes device suspend.
# /proc/uptime includes suspend on this device, so comparing those counters
# falsely marks a healthy service stopped after the screen has been off.
# The heartbeat file's wall-clock mtime and date share the same clock.
NOW=$(date +%s)
HEARTBEAT_TIME=$(stat -c %Y "$C/heartbeat" 2>/dev/null)
case "$NOW:$HEARTBEAT_TIME" in *[!0-9:]*|:*|*:) AGE=999;; *) AGE=$((NOW-HEARTBEAT_TIME));; esac
OK=1
for f in controller.pid brain.pid; do
 PID=$(cat "$C/$f" 2>/dev/null)
 case "$PID" in ''|*[!0-9]*) OK=0; continue;; esac
 case "$f" in controller.pid) WANT="$M/bin/pixel-control";; *) WANT="$M/bin/encored";; esac
 [ "$(readlink /proc/$PID/exe)" = "$WANT" ] || OK=0
 printf '%s=%s\n' "$f" "$PID"
done
[ "$AGE" -ge 0 ] && [ "$AGE" -le 8 ] || OK=0
printf 'process_ok=%s\nheartbeat_age=%s\n' "$OK" "$AGE"
[ "$OK" = 1 ] || { GAME_ACTIVE=0; FAS_LEVEL=-1; FOREGROUND_PACKAGE=; }
printf 'foreground_package=%s\ngame_active=%s\n' "$FOREGROUND_PACKAGE" "$GAME_ACTIVE"
printf 'FAS_LEVEL=%s\nfas_level=%s\n' "$FAS_LEVEL" "$FAS_LEVEL"
for flag in pause disable-high-fps; do
 if [ -f "$C/$flag" ]; then printf '%s=1\n' "$flag"; else printf '%s=0\n' "$flag"; fi
done
printf 'fas_disabled=%s\n' "$( [ -f "$C/disable-fas" ] && echo 1 || echo 0 )"
printf 'fas_advisor_disabled=%s\n' "$( [ -f "$C/disable-fas-rs-advisor" ] && echo 1 || echo 0 )"
printf 'default_fps_disabled=%s\n' "$(getprop debug.graphics.game_default_frame_rate.disabled)"
for id in 0 2 5 7; do
 D=/sys/devices/system/cpu/cpufreq/policy$id
 printf 'CPU_%s=%s,%s,%s\n' "$id" "$(cat "$D/vote_manager/debug_min_freq")" "$(cat "$D/scaling_cur_freq")" "$(cat "$D/scaling_max_freq")"
done
for id in 34f00000.gpu0 200c0780.dsufreq irm_gmc_freq; do
 D=/sys/class/devfreq/$id
 printf '%s=%s,%s,%s\n' "$id" "$(cat "$D/vote_manager/debug_min_freq")" "$(cat "$D/cur_freq")" "$(cat "$D/max_freq")"
done
