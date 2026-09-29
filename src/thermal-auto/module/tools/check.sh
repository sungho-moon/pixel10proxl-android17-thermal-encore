#!/system/bin/sh
# Read-only check. Alternate roots are solely for standalone preflight fixtures.
MODDIR=${1:-${0%/*}/..}
VENDOR=${2:-/vendor/etc}
MODULES=${3:-/data/adb/modules}
UPDATES=${4:-/data/adb/modules_update}
. "$MODDIR/tools/lib.sh"
pgt_check "$MODDIR" "$VENDOR" "$MODULES" "$UPDATES"
