#!/system/bin/sh
# Reused thread-level uclamp backend from Pixel Global Game Uclamp.
# Encore remains the sole global-frequency/FAS writer; this backend owns only
# per-thread sched_attr.uclamp.min and restores every touched thread.
MODDIR=${0%/*}
STATE="$MODDIR/state"
mkdir -p "$STATE"
stopped() { [ -e "$MODDIR/../disable" ] || [ -e "$MODDIR/../remove" ] || [ -e "$STATE/paused" ]; }
foreground_package() {
  dumpsys activity activities 2>/dev/null |
    sed -n 's/.*topResumedActivity=.* u[0-9][0-9]* \([^/ ]*\)\/.*/\1/p' | head -n 1
}
has_game_threads() {
  local pid=$1 task tid name
  for task in /proc/$pid/task/*; do
    tid=${task##*/}; [ -r "$task/comm" ] || continue
    name=$(cat "$task/comm")
    case "$name" in RenderThread|RenderThread\ *|RHIThread|GameThread|MainThread-UE4|UnityMain|UnityPreload*|UnityGfxDeviceW*|UnityMultiRende*|UnityChoreograp*|GfxDeviceWorker|Cocos2dxGLThread|GLThread|NativeThread|SDLThread|TaskGraphNP\ *|Worker\ Thread*|CoreThread*|Job.Worker\ *|JobSystem\ *) return 0;; esac
  done
  return 1
}
find_game() {
  local pkg pid uid
  pkg=$(foreground_package)
  case "$pkg" in ''|android|com.android.*|com.google.android.apps.nexuslauncher) return 1;; esac
  for pid in $(pidof "$pkg" 2>/dev/null); do
    uid=$(sed -n 's/^Uid:[[:space:]]*//p' /proc/$pid/status 2>/dev/null | awk '{print $1}')
    [ "$uid" -ge 10000 ] 2>/dev/null && has_game_threads "$pid" && { echo "$pid"; return 0; }
  done
  return 1
}
for i in $(seq 1 180); do [ "$(getprop sys.boot_completed)" = 1 ] && break; sleep 1; done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0
sleep 15
while ! stopped; do
  pid=$(find_game)
  case "$pid" in ''|*[!0-9]*) sleep 2; continue;; esac
  pkg=$(foreground_package)
  fps=$(awk -v p="$pkg" '$1=="default"{d=$2} $1==p{v=$2} END{print v?v:d}' "$MODDIR/targets.conf")
  case "$fps" in 30|60|90|120|144) ;; *) fps=120;; esac
  "$MODDIR/bin/guardian" "$pid" "$MODDIR/bin/sampler" "$MODDIR" 300 "$fps" > "$STATE/frames.log" 2>&1
  result=$?
  "$MODDIR/bin/guardian" --restore "$STATE" >> "$STATE/recovery.log" 2>&1
  [ "$result" -eq 0 ] || echo "guardian_exit=$result" >> "$STATE/recovery.log"
  [ "$(wc -c < "$STATE/recovery.log" 2>/dev/null)" -gt 32768 ] && mv "$STATE/recovery.log" "$STATE/recovery.previous.log"
  sleep 1
done
"$MODDIR/bin/guardian" --restore "$STATE" >> "$STATE/recovery.log" 2>&1
