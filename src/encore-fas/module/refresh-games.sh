#!/system/bin/sh
# Build the fixed Encore list from the upstream package list and installed apps.
# This script is intentionally event-driven: it runs at install time and only
# when package-manager filesystem events are delivered by package-watch.sh.
MODDIR=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
LIST="$C/gamelist.json"
SOURCE="$MODDIR/gamelist.txt"
PRESETS="$MODDIR/fps-presets.conf"
LOG="$C/games-refresh.log"

log() { printf '%s %s\n' "$(date +%s)" "$*" >> "$LOG"; }

valid_list() {
  "$MODDIR/bin/encored" validate_gamelist "$1" >/dev/null 2>&1
}

preset_target() {
  pkg=$1; target=60
  while read -r rate name; do
    case "$rate" in ''|\#*) continue;; esac
    [ -n "$name" ] || continue
    [ "$name" = "$pkg" ] && target=$rate
  done < "$PRESETS"
  printf '%s\n' "$target"
}

migrate_targets() {
  tmp=$(mktemp "$C/gamelist.preset.XXXXXX") || return 1
  cp "$LIST" "$tmp" || { rm -f "$tmp"; return 1; }
  while read -r rate pkg; do
    case "$rate" in ''|\#*) continue;; esac
    [ -n "$pkg" ] || continue
    [ "$rate" = 120 ] || continue
    sed -i "/\"$pkg\": {/,/\"target_fps\"/s/\"target_fps\": 0/\"target_fps\": $rate/" "$tmp"
  done < "$PRESETS"
  sed -i 's/"target_fps": 0/"target_fps": 60/g' "$tmp"
  if valid_list "$tmp" && chmod 0600 "$tmp"; then mv -f "$tmp" "$LIST"; else rm -f "$tmp"; return 1; fi
}

add_game() {
  pkg=$1
  grep -qF "\"$pkg\"" "$LIST" 2>/dev/null && return 0
  lock="$C/ui-write-lock"
  mkdir "$lock" 2>/dev/null || return 0
  trap 'rmdir "$lock" 2>/dev/null || true' EXIT INT TERM
  grep -qF "\"$pkg\"" "$LIST" 2>/dev/null && {
    rmdir "$lock" 2>/dev/null || true
    trap - EXIT INT TERM
    return 0
  }
  valid_list "$LIST" || {
    log "invalid existing list; skipped package=$pkg"
    rmdir "$lock" 2>/dev/null || true
    trap - EXIT INT TERM
    return 1
  }
  tmp=$(mktemp "$C/gamelist.refresh.XXXXXX") || {
    rmdir "$lock" 2>/dev/null || true
    trap - EXIT INT TERM
    return 1
  }
  awk '{ text=text $0 "\n" } END { sub(/}[[:space:]]*$/, "", text); printf "%s", text }' "$LIST" > "$tmp"
  if grep -q '"' "$LIST"; then printf ',\n' >> "$tmp"; fi
  target=$(preset_target "$pkg")
  printf '  "%s": {\n    "lite_mode": false,\n    "enable_dnd": false,\n    "target_fps": %s\n  }\n}\n' "$pkg" "$target" >> "$tmp"
  if valid_list "$tmp" && chmod 0600 "$tmp"; then
    mv -f "$tmp" "$LIST"
    log "registered package=$pkg source=upstream"
  else
    rm -f "$tmp"
    log "registration failed package=$pkg"
  fi
  rmdir "$lock" 2>/dev/null || true
  trap - EXIT INT TERM
}

mkdir -p "$C"
[ -f "$LIST" ] || printf '{}\n' > "$LIST"
valid_list "$LIST" || { log 'invalid list; refresh aborted'; exit 1; }
migrate_targets || { log 'preset migration failed; refresh aborted'; exit 1; }

installed=$(mktemp "$C/installed-packages.XXXXXX") || exit 1
trap 'rm -f "$installed"' EXIT INT TERM
pm list packages -3 2>/dev/null | sed 's/^package://' > "$installed"
added=0
while IFS= read -r pkg; do
  case "$pkg" in ''|\#*) continue;; esac
  if grep -Fxq "$pkg" "$installed" && ! grep -qF "\"$pkg\"" "$LIST" 2>/dev/null; then
    add_game "$pkg"
    added=$((added + 1))
  fi
done < "$SOURCE"
log "refresh reason=${1:-event} candidates_added=$added"
