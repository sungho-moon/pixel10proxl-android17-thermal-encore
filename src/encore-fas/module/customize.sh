#!/system/bin/sh
ui_print "Encore Pixel Tensor Android 17 generic 0.3.0 fas-rs advisor"
[ "$(getprop ro.build.version.sdk)" = 37 ] || abort "This package requires Android SDK 37"
[ "$ARCH" = arm64 ] || abort "arm64 required"
C=/data/adb/.config/encore_pixel_cp41
mkdir -p "$C"
chmod 700 "$C"
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755
set_perm_recursive "$MODPATH/thread/bin" 0 0 0755 0755
set_perm "$MODPATH/thread/service.sh" 0 0 0755
for s in service.sh action.sh uninstall.sh ui-state.sh ui-write.sh refresh-games.sh package-event.sh package-watch.sh; do set_perm "$MODPATH/$s" 0 0 0755; done
rm -f "$MODPATH/auto-game.sh"
rm -f "$C/auto-game.log" "$C/auto-game-service.log"
(cd "$MODPATH" && sha256sum -c INSTALL_SHA256SUMS) || abort "Install payload checksum failed"
for f in config.json device_mitigation.json default_cpu_gov; do
 [ -f "$C/$f" ] || cp "$MODPATH/defaults/$f" "$C/$f"
done
# Target FPS is configured per game in WebUI gamelist.json. Keep a legacy
# file empty so older installations do not reintroduce a global fixed target.
[ -f "$C/fas-targets.conf" ] || : > "$C/fas-targets.conf"
chmod 600 "$C/fas-targets.conf"
[ -f "$C/gamelist.json" ] || printf '{}\n' > "$C/gamelist.json"
"$MODPATH/refresh-games.sh" install || abort "Installed game list refresh failed"
"$MODPATH/bin/encored" check_gamelist || abort "Invalid game list"
"$MODPATH/bin/pixel-control" probe || abort "Pixel frequency interface check failed"
ui_print "Keep this Encore Pixel module enabled; disable standalone Uclamp and fas-rs modules."
ui_print "Keep your separate thermal module if desired."
ui_print "Full/lite game modes; restores requests on exit, screen off or battery saver."
ui_print "Foreground game: disables Android default 60 FPS policy; restores on exit."
ui_print "WebUI: status, game list, Lite mode and high-frame-rate preferences."
ui_print "Uses the upstream fixed game list and refreshes installed matches at install/package events."
ui_print "fas-rs proportional feedback runs inside Encore; no second CPU frequency writer."
ui_print "See FAS_EXPERIMENT.md for targets and opt-out."
