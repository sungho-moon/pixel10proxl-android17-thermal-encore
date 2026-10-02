#!/system/bin/sh
# Discover a previously unlisted foreground game and register it for Encore.
# The detector is deliberately conservative: the app must be a third-party
# package and expose a known game-engine library or thread name.
MODDIR=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
LIST="$C/gamelist.json"
LOG="$C/auto-game.log"

log() { printf '%s %s\n' "$(date +%s)" "$*" >> "$LOG"; }

foreground_package() {
  dumpsys activity activities 2>/dev/null | awk '
    /topResumedActivity=|mResumedActivity=|mFocusedApp=/ {
      for (i=1; i<=NF; i++) if ($i ~ /\//) {
        split($i, a, "/");
        if ($0 ~ /topResumedActivity=|mResumedActivity=/) { print a[1]; found=1; exit }
        fallback=a[1]; break
      }
    }
    END { if (!found && fallback != "") print fallback }
  ' | head -n 1
}

excluded_package() {
  case "$1" in
    ''|android|com.android.*|com.google.android.apps.*|com.google.android.gms|com.google.android.gsf|com.android.chrome|com.google.android.youtube|com.google.android.apps.youtube.*|com.google.android.googlequicksearchbox|com.google.android.settings|com.google.android.launcher|com.tencent.mm|com.tencent.mobileqq|com.netease.mail|com.microsoft.*|org.telegram.*|org.chromium.*|com.facebook.*|com.instagram.*|com.twitter.*|com.xiaomi.*)
      return 0 ;;
  esac
  return 1
}

third_party() {
  pm list packages -3 2>/dev/null | grep -Fxq "package:$1"
}

game_library() {
  grep -Eiq '/lib(unity|il2cpp|ue4|unreal|cocos|godot|minecraft|gdx|renpy|nwjs|cryengine)[^/]*\.so' \
    "/proc/$1/maps" 2>/dev/null
}

game_thread() {
  local pid=$1 task name
  for task in /proc/$pid/task/*; do
    [ -r "$task/comm" ] || continue
    name=$(cat "$task/comm" 2>/dev/null)
    case "$name" in
      UnityMain|UnityGfxDeviceW*|UnityPreload*|RHIThread|GameThread|MainThread-UE4|GfxDeviceWorker|Cocos2dxGLThread|Godot*|TaskGraphNP*|JobSystem*|JobThread*|Worker\ Thread*|SDLThread)
        return 0 ;;
    esac
  done
  return 1
}

candidate() {
  local pkg=$1 pid
  excluded_package "$pkg" && return 1
  third_party "$pkg" || return 1
  pid=$(pidof "$pkg" 2>/dev/null | awk '{print $1}')
  [ -n "$pid" ] || return 1
  game_library "$pid" && return 0
  game_thread "$pid"
}

register_game() {
  local pkg=$1 lock tmp
  grep -qF "\"$pkg\"" "$LIST" 2>/dev/null && return 0
  lock="$C/ui-write-lock"
  mkdir "$lock" 2>/dev/null || return 0
  trap 'rmdir "$lock" 2>/dev/null || true' EXIT INT TERM
  grep -qF "\"$pkg\"" "$LIST" 2>/dev/null && { rmdir "$lock" 2>/dev/null || true; trap - EXIT INT TERM; return 0; }
  tmp=$(mktemp "$C/gamelist.auto.XXXXXX") || { rmdir "$lock" 2>/dev/null || true; trap - EXIT INT TERM; return 1; }
  "$MODDIR/bin/encored" validate_gamelist "$LIST" >/dev/null 2>&1 || {
    log "invalid existing gamelist; registration skipped"
    rm -f "$tmp"; rmdir "$lock" 2>/dev/null || true; trap - EXIT INT TERM; return 1
  }
  # Remove the final object delimiter, including for a compact empty object {}.
  awk '{ text=text $0 "\n" } END { sub(/}[[:space:]]*$/, "", text); printf "%s", text }' "$LIST" > "$tmp"
  if grep -q '"' "$LIST"; then printf ',\n' >> "$tmp"; fi
  printf '  "%s": {\n    "lite_mode": false,\n    "enable_dnd": false,\n    "target_fps": 0\n  }\n}\n' "$pkg" >> "$tmp"
  if "$MODDIR/bin/encored" validate_gamelist "$tmp" >/dev/null 2>&1 && chmod 0600 "$tmp" && mv -f "$tmp" "$LIST"; then
    log "registered package=$pkg target_fps=auto"
  else
    log "registration failed package=$pkg"
    rm -f "$tmp"
  fi
  rmdir "$lock" 2>/dev/null || true
  trap - EXIT INT TERM
}

for i in $(seq 1 120); do
  [ "$(getprop sys.boot_completed)" = 1 ] && break
  sleep 1
done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0
sleep 8
log 'auto discovery started'
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] && [ ! -e "$C/pause" ]; do
  if [ -e "$C/disable-auto-game" ]; then sleep 2; continue; fi
  pkg=$(foreground_package)
  [ -n "$pkg" ] && ! grep -qF "\"$pkg\"" "$LIST" 2>/dev/null && candidate "$pkg" && register_game "$pkg"
  sleep 2
done
