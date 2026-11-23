#!/system/bin/sh
# UniGL Spoofer installer (Magisk / KernelSU / KernelSU-Next / APatch)
SKIPUNZIP=0

[ "$API" -ge 26 ] || abort "! Android 8.0+ required"
case "$ARCH" in
  arm64|x64) ;;
  *) abort "! Unsupported architecture: $ARCH (need arm64 or x64)" ;;
esac

CONF=/data/adb/unigl
ui_print "- Preparing config in $CONF"
mkdir -p "$CONF/profiles"
# never overwrite the user's own profiles / targets
for f in "$MODPATH"/defaults/profiles/*.conf; do
  b=$(basename "$f")
  [ -f "$CONF/profiles/$b" ] || cp "$f" "$CONF/profiles/$b"
done
[ -f "$CONF/targets.conf" ] || cp "$MODPATH/defaults/targets.conf" "$CONF/targets.conf"
chmod 0755 "$CONF" "$CONF/profiles"
chmod 0644 "$CONF"/targets.conf "$CONF"/profiles/*.conf

ui_print "- Requires a Zygisk provider (Magisk Zygisk, Zygisk Next, ReZygisk, BRZygisk...)"
ui_print "- Open the module's WebUI to pick the game + profile, then reboot once"
