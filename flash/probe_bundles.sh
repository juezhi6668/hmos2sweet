#!/bin/sh
# Read-only probe: why does BMS report every prebuilt HAP as "bundle not exist"?
# Run while the phone is in recovery (adb). Nothing is flashed, nothing rebooted.
DEST="$(dirname "$0")/probe_$(date +%Y%m%d_%H%M)"
mkdir -p "$DEST"
adb wait-for-recovery
adb shell "mkdir -p /tmp/d; (mount -t f2fs /dev/block/by-name/userdata /tmp/d 2>/dev/null || mount -t f2fs /dev/block/bootdevice/by-name/userdata /tmp/d) && echo MOUNTED || echo MOUNT_FAILED"

echo "--- staging dirs the first-boot preinstall is supposed to fill ---"
adb shell "for p in app/el1/bundle/public app/el1/bundle/0 service/el1/public/database/bundle_manager_service service/el1/public/bms; do echo \"[==] /tmp/d/\$p\"; ls -l /tmp/d/\$p 2>&1 | head -15; done" > "$DEST/staging.txt" 2>&1
adb shell "ls /tmp/d/app/el1/bundle/public 2>/dev/null | wc -l" > "$DEST/staging_count.txt" 2>&1

echo "--- is SettingsData / SceneBoard / launcher registered anywhere in /data? ---"
adb shell "find /tmp/d -maxdepth 7 \( -iname '*settingsdata*' -o -iname '*sceneboard*' -o -iname '*launcher*' \) 2>/dev/null | head -30" > "$DEST/bundles.txt" 2>&1

echo "--- BMS / installd leftovers in /data/log ---"
adb shell "ls -l /tmp/d/log 2>&1 | head -10; ls -l /tmp/d/log/bms 2>&1 | head -10; ls -l /tmp/d/log/appinstall 2>&1 | head -10" > "$DEST/logs.txt" 2>&1

echo "--- free space and ownership of /data (a failed mkdir/chown here breaks the preinstall) ---"
adb shell "df -h /tmp/d 2>&1 | tail -2; ls -ld /tmp/d /tmp/d/app /tmp/d/app/el1 /tmp/d/app/el1/bundle /tmp/d/app/el1/bundle/public 2>&1" > "$DEST/space.txt" 2>&1

adb shell "umount /tmp/d 2>/dev/null"
echo "probe saved in $DEST"
for f in "$DEST"/*.txt; do echo; echo "########## $(basename $f)"; cat "$f"; done
