@echo off
REM Pull HarmonyOS bring-up logs while the phone is in recovery (adb enabled).
set DEST=%~dp0logs_%DATE:~-4%%DATE:~3,2%%DATE:~0,2%_%TIME:~0,2%%TIME:~3,2%
set DEST=%DEST: =0%
mkdir "%DEST%"
adb wait-for-recovery
REM 1) kernel log copied by hmos-preinit to the cache partition
adb shell "mkdir -p /tmp/c && (mount -t ext4 /dev/block/by-name/cache /tmp/c 2>/dev/null || mount /dev/block/bootdevice/by-name/cache /tmp/c)"
adb pull /tmp/c/hmos "%DEST%"
adb shell "rm -f /tmp/c/hmos/boot_inflight /tmp/c/hmos/PREVIOUS_BOOT_CRASHED"
adb shell "umount /tmp/c"
REM 2) pstore (last kernel console before the reboot); 4.4 does not auto-mount it
adb shell "mkdir -p /sys/fs/pstore && mount -t pstore pstore /sys/fs/pstore 2>/dev/null; mkdir -p /tmp/pstore; cp -a /sys/fs/pstore/. /tmp/pstore/ 2>/dev/null; ls -l /tmp/pstore"
adb pull /tmp/pstore "%DEST%\pstore"
adb shell "umount /sys/fs/pstore 2>/dev/null"
REM 3) HarmonyOS hilog / faultlog from /data (f2fs, not encrypted)
adb shell "mkdir -p /tmp/d && (mount -t f2fs /dev/block/by-name/userdata /tmp/d 2>/dev/null || mount -t f2fs /dev/block/bootdevice/by-name/userdata /tmp/d)"
adb shell "cd /tmp/d && tar czf /tmp/hmos_data_log.tgz log 2>/dev/null; ls -la /tmp/hmos_data_log.tgz"
adb pull /tmp/hmos_data_log.tgz "%DEST%\data_log.tgz"
adb shell "umount /tmp/d"
echo Logs saved in %DEST%
