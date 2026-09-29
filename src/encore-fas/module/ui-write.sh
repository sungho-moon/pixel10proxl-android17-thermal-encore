#!/system/bin/sh
# Fixed destinations, validated staged JSON, compare before replacing, one writer.
set -eu
M=${0%/*}
C=/data/adb/.config/encore_pixel_cp41
case "${1:-}" in
 config) DEST="$C/config.json"; VALIDATOR=validate_config;;
 games) DEST="$C/gamelist.json"; VALIDATOR=validate_gamelist;;
 *) echo 'Unsupported configuration destination' >&2; exit 2;;
esac
DATA=${2:-}; EXPECTED=${3:-}
case "$DATA$EXPECTED" in ''|*[!A-Za-z0-9+/=]*) echo 'Invalid encoded input' >&2; exit 2;; esac
[ "${#DATA}" -le 49152 ] && [ "${#EXPECTED}" -le 49152 ] || exit 2
LOCK="$C/ui-write-lock"
mkdir "$LOCK" 2>/dev/null || { echo 'Another configuration write is in progress' >&2; exit 3; }
T=; E=
trap '[ -z "$T" ] || rm -f "$T"; [ -z "$E" ] || rm -f "$E"; rmdir "$LOCK"' EXIT
trap 'exit 130' INT TERM HUP
T=$(mktemp "$C/ui-write.XXXXXX")
E=$(mktemp "$C/ui-expected.XXXXXX")
printf '%s' "$DATA" | base64 -d > "$T"
printf '%s' "$EXPECTED" | base64 -d > "$E"
[ "$(cat "$DEST")" = "$(cat "$E")" ] || { echo 'Configuration changed; reload this page' >&2; exit 4; }
"$M/bin/encored" "$VALIDATOR" "$T" >&2 || { echo 'Configuration validation failed' >&2; exit 5; }
chmod 600 "$T"
mv "$T" "$DEST"
echo saved
