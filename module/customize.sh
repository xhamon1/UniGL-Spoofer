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

# Migrate from older versions: old single profiles became "<name>-minimal"
OLD="honor-magic9-pro-max redmagic-11-pro poco-f9-ultra iqoo-15 oneplus-15 xiaomi-17-ultra poco-f8-ultra oppo-find-x9-ultra galaxy-s26-ultra"
GONE="mali-g720 s24ultra build-only-s24u template"
if [ -f "$CONF/targets.conf" ]; then
  for n in $OLD; do sed -i "s/=$n\$/=$n-minimal/" "$CONF/targets.conf"; done
  for n in $GONE; do sed -i "/=$n\$/d" "$CONF/targets.conf"; done
fi
for n in $OLD $GONE; do rm -f "$CONF/profiles/$n.conf"; done

# Built-in device profiles are always refreshed; your custom profiles are never touched
cp -f "$MODPATH"/defaults/profiles/*.conf "$CONF/profiles/"
[ -f "$CONF/targets.conf" ] || cp "$MODPATH/defaults/targets.conf" "$CONF/targets.conf"
chmod 0755 "$CONF" "$CONF/profiles"
chmod 0644 "$CONF"/targets.conf "$CONF"/profiles/*.conf

ui_print "- Requires Zygisk to be enabled"
ui_print "- Reboot, then open the module's WebUI to add an app and pick a profile"
