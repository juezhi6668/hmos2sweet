#!/system/bin/sh
# v49: production bring-up -- the same chain as v48 but it does NOT reboot.
#
# Two things had to change together. (1) The auto-reboot into recovery was only
# ever a log-retrieval trick for the probes; with it removed the system stays up.
# (2) The modem hold fd must outlive the script: closing /dev/subsys_modem takes
# MPSS back OFFLINE (that is how the earlier probes re-triggered a clean modem
# start), so if this script just returned, WLAN would die about a minute after
# boot. It therefore ends by parking on a long sleep and holding fd 9 open.
# The kmsg tap is also bounded now (head -c) so /data cannot fill up over time.
D=/data/bootprobe
W=/data/wifi
L=$D/wlan49.log
R=/data/rfs
I=$W/ipcrtr
B=/vendor/bionic
K=$B/syslib64:$B/lib64
KS=/sys/kernel/debug/msm_ipc_router
mkdir -p $D $W $I
mount -t debugfs none /sys/kernel/debug 2>&1
for d in /sys/bus/msm_subsys/devices/*; do
	echo RELATED >"$d/restart_level" 2>/dev/null
done
echo "===== boot $(cat /proc/uptime) (v49) =====" >>$D/kmsg_full.log
( head -c 8000000 /dev/kmsg >>$D/kmsg_full.log 2>&1 ) &
exec >>$L 2>&1
echo "== wlan-up v49 $(cat /proc/uptime)"

st() {
	for d in /sys/bus/msm_subsys/devices/*; do
		read n <"$d/name"
		[ "$n" = "$1" ] && { cat "$d/$2" 2>&1; return; }
	done
	echo NA
}

cmdpid() {
	for p in /proc/[0-9]*; do
		c=$(cat $p/cmdline 2>/dev/null)
		case $c in
		*$1*) echo -n "${p#/proc/}:$c ";;
		esac
	done
	echo
}

iface() {
	echo "  phy=[$(ls /sys/class/ieee80211 2>&1)] ifaces=$(ls /sys/class/net 2>/dev/null | wc -l) WLFW=$(grep -c '^0x00000045 ' $KS/dump_servers 2>&1)"
	echo "  state=[$(grep -m1 '^State:' /sys/kernel/debug/icnss/stats 2>&1)]"
	echo "  modem=$(st modem state) crash=$(st modem crash_count) adsp=$(st adsp state)"
	if [ -e /sys/class/net/wlan0 ]; then
		echo "  wlan0 operstate=$(cat /sys/class/net/wlan0/operstate 2>&1) flags=$(cat /sys/class/net/wlan0/flags 2>&1) addr=$(cat /sys/class/net/wlan0/address 2>&1)"
	else
		echo "  wlan0 absent"
	fi
}

for sub in msm/mpss msm/adsp; do
	mkdir -p "$R/$sub/readonly/firmware/image" "$R/$sub/readwrite" "$R/$sub/shared" 2>&1
done
PF=/dev/block/platform/soc/1da4000.ufshc
BN=$PF/by-name
mkdir -p $BN 2>&1
[ -d $BN ] || { rm -f $PF 2>&1; mkdir -p $BN 2>&1; }
rm -f /dev/block/bootdevice
ln -s $PF /dev/block/bootdevice 2>&1
for e in modemst1:sdf4 modemst2:sdf5 fsc:sdf1 fsg:sde34; do
	n=${e%%:*}
	d=${e##*:}
	[ -e $BN/$n ] || ln -s /dev/block/$d $BN/$n 2>&1
	chmod 0666 /dev/block/$d 2>&1
done

# Already up? (service re-started by hand, or a second trigger) Do not bring a
# second copy of anything down over the running one.
if [ "$(cmdpid rfssrv2)" != "" ]; then
	echo "chain already running, taking over the modem hold only"
else
	( /vendor/bin/rfssrv2 $R/msm/mpss >$W/rfssrv2-49.log 2>&1 & )
	sleep 2
	/vendor/bin/wlq irsc /vendor/etc/sec_config >$W/wlq-irsc-49.log 2>&1
	echo "irsc: $(head -1 $W/wlq-irsc-49.log)"
	LD_LIBRARY_PATH=$K nohup $B/bin/linker64 $B/bin/rmt_storage >$W/rmt-49.log 2>&1 &
	sleep 3
	echo "0x0e=$(grep -c '^0x0000000e |0x00000001 ' $KS/dump_servers 2>&1)"
	# static binary, run directly (v47 lost a whole run by launching it via
	# linker64 from a path that does not exist)
	( /vendor/bin/pdserve >$W/pdserve-49.log 2>&1 & )
	sleep 2
	echo "pdserve=$(cmdpid pdserve) 0x40=$(grep -c '^0x00000040 ' $KS/dump_servers 2>&1)"
	[ -e /sys/kernel/boot_adsp/boot ] && echo 1 >/sys/kernel/boot_adsp/boot 2>&1
	sleep 4
fi
if [ ! -e /dev/subsys_modem ] && [ -e /sys/class/subsys/subsys_modem/dev ]; then
	d=$(cat /sys/class/subsys/subsys_modem/dev)
	mknod /dev/subsys_modem c ${d%:*} ${d#*:} 2>&1
fi
exec 9</dev/subsys_modem
echo "modem held from $(cat /proc/uptime)"

i=0
while [ $i -lt 24 ]; do
	sleep 5
	i=$((i + 1))
	echo "--- t=$(cat /proc/uptime) iter=$i" >>$L
	iface >>$L
	sync
	[ -e /sys/class/net/wlan0 ] && break
done
echo "-- bring-up finished, holding --"
iface
cp /sys/kernel/debug/icnss/stats $W/stats-49.log 2>&1
sync

# Never return: fd 9 is what keeps MPSS online, and with it WLAN.
while :; do
	sleep 3600
done
