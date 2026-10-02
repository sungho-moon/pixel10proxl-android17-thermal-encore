#!/system/bin/sh
# Reused thread-level uclamp backend from Pixel Global Game Uclamp.
# Encore remains the sole global-frequency/FAS writer; this backend owns only
# per-thread sched_attr.uclamp.min and restores every touched thread.
MODDIR=${0%/*}
STATE="$MODDIR/state"
TOYBOX=/system/bin/toybox
mkdir -p "$STATE"
stopped() { [ -e "$MODDIR/../disable" ] || [ -e "$MODDIR/../remove" ] || [ -e "$STATE/paused" ]; }
foreground_package() {
  dumpsys activity activities 2>/dev/null |
    "$TOYBOX" sed -n 's/.*topResumedActivity=.* u[0-9][0-9]* \([^/ ]*\)\/.*/\1/p' |
    "$TOYBOX" head -n 1
}
sample_dynamic_threads() {
  local pid=$1 out="$MODDIR/../../../.config/encore_pixel_cp41/dynamic-tids" a b rest utime stime tid t d
  mkdir -p "${out%/*}"
  : > "$STATE/dynamic-a"
  for t in /proc/$pid/task/*; do
    tid=${t##*/}; [ -r "$t/stat" ] || continue
    [ "$tid" = "$pid" ] && continue
    rest=$("$TOYBOX" sed 's/^[0-9][0-9]* ([^)]*) //' "$t/stat") || continue
    set -- $rest
    utime=${12:-0}; stime=${13:-0}
    case "$utime:$stime" in *[!0-9:]*|:) continue;; esac
    printf '%s %s\n' "$tid" "$((utime + stime))" >> "$STATE/dynamic-a"
  done
  "$TOYBOX" sleep .25
  : > "$STATE/dynamic-b"
  for t in /proc/$pid/task/*; do
    tid=${t##*/}; [ -r "$t/stat" ] || continue
    [ "$tid" = "$pid" ] && continue
    rest=$("$TOYBOX" sed 's/^[0-9][0-9]* ([^)]*) //' "$t/stat") || continue
    set -- $rest
    utime=${12:-0}; stime=${13:-0}
    case "$utime:$stime" in *[!0-9:]*|:) continue;; esac
    printf '%s %s\n' "$tid" "$((utime + stime))" >> "$STATE/dynamic-b"
  done
  : > "$STATE/dynamic-delta"
  while read -r tid b; do
    a=$("$TOYBOX" grep "^$tid " "$STATE/dynamic-a" | "$TOYBOX" cut -d' ' -f2)
    case "$a:$b" in *[!0-9:]*|:) continue;; esac
    d=$((b - a)); [ "$d" -gt 0 ] && printf '%s %s\n' "$d" "$tid" >> "$STATE/dynamic-delta"
  done < "$STATE/dynamic-b"
  "$TOYBOX" sort -nr "$STATE/dynamic-delta" | "$TOYBOX" head -n 4 | "$TOYBOX" cut -d' ' -f2 > "$out.new"
  mv -f "$out.new" "$out"
}
find_game() {
  local pkg pid uid
  pkg=$(foreground_package)
  case "$pkg" in ''|android|com.android.*|com.google.android.apps.nexuslauncher) return 1;; esac
  "$TOYBOX" grep -qF "\"$pkg\"" /data/adb/.config/encore_pixel_cp41/gamelist.json 2>/dev/null || return 1
  for pid in $(pidof "$pkg" 2>/dev/null); do
    uid=$("$TOYBOX" sed -n 's/^Uid:[[:space:]]*//p' /proc/$pid/status 2>/dev/null | "$TOYBOX" tr '\t' ' ' | "$TOYBOX" cut -d' ' -f1)
    [ "$uid" -ge 10000 ] 2>/dev/null && { echo "$pid"; return 0; }
  done
  return 1
}
for i in $("$TOYBOX" seq 1 180); do [ "$(getprop sys.boot_completed)" = 1 ] && break; "$TOYBOX" sleep 1; done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0
"$TOYBOX" sleep 15
while ! stopped; do
  pid=$(find_game)
  case "$pid" in ''|*[!0-9]*) "$TOYBOX" sleep 2; continue;; esac
  sample_dynamic_threads "$pid"
  pkg=$(foreground_package)
  fps=$("$MODDIR/../bin/pixel-control" target-fps "$pkg")
  case "$fps" in ''|*[!0-9]*) fps=0;; esac
  "$MODDIR/bin/guardian" "$pid" "$MODDIR/bin/sampler" "$MODDIR" 300 "$fps" > "$STATE/frames.log" 2>&1 &
  GUARDIAN_PID=$!
  (
    while kill -0 "$GUARDIAN_PID" 2>/dev/null; do
      "$TOYBOX" sleep 3
      kill -0 "$GUARDIAN_PID" 2>/dev/null || break
      sample_dynamic_threads "$pid"
    done
  ) &
  SCAN_PID=$!
  wait "$GUARDIAN_PID"
  result=$?
  kill "$SCAN_PID" 2>/dev/null
  wait "$SCAN_PID" 2>/dev/null
  "$MODDIR/bin/guardian" --restore "$STATE" >> "$STATE/recovery.log" 2>&1
  [ "$result" -eq 0 ] || echo "guardian_exit=$result" >> "$STATE/recovery.log"
  [ "$("$TOYBOX" wc -c < "$STATE/recovery.log" 2>/dev/null)" -gt 32768 ] && mv "$STATE/recovery.log" "$STATE/recovery.previous.log"
  "$TOYBOX" sleep 1
done
"$MODDIR/bin/guardian" --restore "$STATE" >> "$STATE/recovery.log" 2>&1
