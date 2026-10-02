#!/system/bin/sh
MODDIR=${0%/*}
case "${1:-}" in
  *m*|*n*|*w*) "$MODDIR/refresh-games.sh" package-event ;;
esac
