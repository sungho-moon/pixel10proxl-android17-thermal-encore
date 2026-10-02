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
  dumpsys activity activities 2>/dev/null |
    sed -n 's/.*topResumedActivity=.* u[0-9][0-9]* \([^/ ]*\)\/.*/\1/p' |
    head -n 1
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
      UnityMain|UnityGfxDeviceW*|UnityPreload*|RHIThread|GameThread|MainThread-UE4|GfxDeviceWorker|Cocos2dxGLThread|Godot*|TaskGraphNP*|JobSystem*|Worker\ Thread*|SDLThread)
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
  lock="$C/auto-game.lock"
  mkdir "$lock" 2>/dev/null || return 0
  trap 'rmdir "$lock" 2>/dev/null || true' EXIT INT TERM
  grep -qF "\"$pkg\"" "$LIST" 2>/dev/null && { rmdir "$lock" 2>/dev/null || true; trap - EXIT INT TERM; return 0; }
  tmp=$(mktemp "$C/gamelist.auto.XXXXXX") || { rmdir "$lock" 2>/dev/null || true; trap - EXIT INT TERM; return 1; }
  sed '$d' "$LIST" > "$tmp" || { rm -f "$tmp"; rmdir "$lock" 2>/dev/null || true; trap - EXIT INT TERM; return 1; }
  if grep -q '^[[:space:]]*"' "$LIST"; then printf ',\n' >> "$tmp"; fi
  printf '  "%s": {\n    "lite_mode": false,\n    "enable_dnd": false,\n    "target_fps": 0\n  }\n}\n' "$pkg" >> "$tmp"
  chmod 0600 "$tmp" && mv -f "$tmp" "$LIST"
  log "registered package=$pkg target_fps=auto"
  rmdir "$lock" 2>/dev/null || true
  trap - EXIT INT TERM
}

for i in $(seq 1 120); do
  [ "$(getprop sys.boot_completed)" = 1 ] && break
  sleep 1
done
[ "$(getprop sys.boot_completed)" = 1 ] || exit 0
sleep 8
while [ ! -e "$MODDIR/disable" ] && [ ! -e "$MODDIR/remove" ] && [ ! -e "$C/pause" ]; do
  pkg=$(foreground_package)
  [ -n "$pkg" ] && candidate "$pkg" && register_game "$pkg"
  sleep 2
done
