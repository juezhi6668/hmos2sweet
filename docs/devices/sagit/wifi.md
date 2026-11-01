# sagit (Mi 6, msm8998) - Wi-Fi bring-up

WCN3990 sits behind MPSS on this SoC, so HarmonyOS needs the modem up, the
firmware served to it, and its board data handed over. None of that happens by
itself: there is no cnss-daemon here (this kernel builds no cnss netlink family,
and `# CONFIG_CNSS is not set` in both the port kernel and stock recovery), so
the host side has to do each job separately. Everything below was established by
differential runs against stock recovery on the same phone, which brings the
whole chain up and reaches `phy0` + `wlan0/wlan1/p2p0`.

## Order of operations (patches/70-sagit-wifi)

| Step | What it is | Why the stack dies without it |
|---|---|---|
| `rfssrv2 <rfs tree>` | RFS/TFTP server, ipc-router services `0x1000`/`0x1001` | MPSS fetches `wlanmdsp.mbn` (the WLAN firmware) over host RFS; without a server there is no firmware to run. Do not start vendor `tftp_server` as well - it races for `0x1000` and whoever loses never gets the image |
| `wlq irsc /vendor/etc/sec_config` | one `IPC_ROUTER_IOCTL_CONFIG_SEC_RULES` per rule, then `close()` | `msm_ipc_check_send_permissions()` blocks *every* client-port send until an `IRSC_PORT` is removed. Vendor `irsc_util` cannot load on the KitKat-era 32-bit liblog, so without this replay the modem's own QMI answers never leave the router and every client (cnss-daemon included) stalls forever |
| `pdserve` | host-side **SERVREG_LOC** QMI server, service `0x40` instance `BUILD_INSTANCE_ID(1,1) = 0x0101` | `drivers/soc/qcom/service-locator.c` waits for a `0x40:0101` server and never resolves the PD table without one, so `service-notifier` never registers `msm/modem/wlan_pd`, MPSS never powers the WLAN PD and **WLFW (`0x45`) is never published**. This was the single wall for a long time: `dump_servers` showing `0x00000040 |0x00000101 |node 1` is the pass/fail signal |
| `rmt_storage` (patched) | **RMT STORAGE** server, service `0x0e` instance `1` | MPSS initialises EFS through it; missing means a self-triggered SSR about 40 s after the modem comes up, which on the default `restart_level` panics the box (`subsys-restart: Resetting the SoC - modem crashed`). The A15 blob drops `setgroups`/`setgid`/`setuid` then `capset`s NET_BIND_SERVICE back, which HarmonyOS refuses, so the three drop-privilege branches are patched to `mov w0, #0` and it stays root |
| `echo 1 > /sys/kernel/boot_adsp/boot` | ADSP up | sensor/dsp side services; unrelated to WLFW but needed for a quiet modem |
| `exec 9</dev/subsys_modem` | keeps MPSS online | closing that fd takes the subsystem back OFFLINE. This is why the bring-up script must **park on a sleep and never return**: an `init` service that exits (even a `oneshot`) drops WLAN again about a minute after boot |
| kernel `0001` | BDF + CAL_REPORT | see the next section |

Set `restart_level` to `RELATED` on every `/sys/bus/msm_subsys/devices/*` first;
this 4.4 `subsystem_restart.c` only knows `SYSTEM` and `RELATED` (`echo user`
is rejected with `-EPERM`), and `SYSTEM` turns any modem crash into a hard
reboot with no logs at all.

## Board data and the calibration report

`patches/70-sagit-wifi` cannot fix the last part, the kernel has to send it:
`src/kernel/sagit/patches/0001-icnss-download-the-BDF-and-send-the-cal-report.patch`
(`drivers/soc/qcom/icnss.c`, `wlfw_bdf_dnld_send_sync_msgs()` +
`wlfw_cal_report_send_sync_msg()`, called right after `wlfw_cap_send_sync_msg()`).

The accepted wire form, captured off the vendor `cnss-daemon`'s own successful
transactions rather than guessed:

* `file_id` is **0**, not the board id; `total_size` is the **whole image size,
  repeated in every segment** (not a remaining count); `seg_id` starts at 0.
* the data TLV is `0x13`, length `dlen + 2`, and its value carries a **u16 LE
  inner length before the bytes** - chunks of 6144, so a 19152-byte `bdwlan.bin`
  goes out as 6144/6144/6144/720.
* the `end` TLV (`0x14`, len 1) is present on **every** segment (0,0,0,1) and
  there is **no `bdf_type` TLV** (`0x15`) at all.
* optional fields are one value TLV: `lib/qmi_encdec.c` `_qmi_kernel_encode()`
  only *reads* the `*_valid` byte for `QMI_OPT_FLAG` and advances, so writing a
  presence-flag TLV in front of the value - the old userspace libqmi habit -
  produces duplicate TLV types and the target answers `QMI_ERR_MALFORMED_MSG`.
* `ind_register` matters: the daemon registers `10=1 11=1 12=1 13=1 15="NOMD"
  1c=1 1d=1`. With the stock registration the completion segment answers
  `error=19` (NOT_POSSIBLE); with this one it answers `error=1`.

**The completion segment is supposed to fail.** `end=1` returns
`result=1 error=1` even for the vendor daemon, whose request bytes are otherwise
identical. What releases the firmware is the **`CAL_REPORT` (`0x0026`)** that the
daemon sends next - `TLV 0x01`, length 1, value 0, a 4-byte body - and
`READY_IND` (`cf=04`, `id=0x0021`) follows within about 100 ms. So the driver
must not treat the completion response as fatal, and must send the calibration
report regardless; that is exactly the same correction sweet needs (kernel
patch `0019`). Because the generated `wlfw_cal_report_req_msg_v01_ei` encodes
`0x01` as a data length plus a u32 array and cannot emit that 4-byte body, the
patch installs its own one-element table.

Verified on the phone (kernel `split12`, `/data/bootprobe/wlan49.log`):

```
icnss: BDF completion answered result:1 error:1
icnss: BDF download done, reporting cal
icnss: Cal reported, waiting for FW ready indication
icnss: WLAN FW is ready: 0xd87
  state=[State: 0xd8f(QMI CONN | POWER ON | FW READY | DRIVER PROBED | ...)]
  phy=[phy0] ifaces=11  wlan0 addr=50:8f:4c:e0:7e:64   modem=ONLINE crash=0
```

HarmonyOS attaches on its own: `wifi_host` and `wifi_manager_service` are both
running by the time `wlan0` appears (~45 s after boot), and qcacld logs
`Created vdev 1` from a `WpaMai` thread, so the app layer is the next thing to
chase rather than a blocker behind it.

## Screen geometry is a separate file

Not Wi-Fi, but it lives in the fused ext4 and is edited the same way:
`/sys_prod/etc/window/resources/display_manager_config.xml` carries `<dpi>`,
`<cutoutArea>`, `<defaultDisplayCutoutPath>`, `<curvedScreenBoundary>` and
`<isWaterfallDisplay>`. The OTA ships sweet's values (dpi 540, a 273x72 punch
hole at x=494); sagit wants dpi 480 with no cutout and waterfall off. There is no
resolution key in it - the panel size comes from the composer.

## How the QMI transcript was taken

Worth keeping because it is the only way to see what a vendor blob actually
sends, and two details burned time:

* `LD_PRELOAD` works on these blobs (`file` reports
  `dynamic (/system/bin/linker64)`), and the transport is `sendto()` on the
  ipc-router socket - interposing only `write()` captures nothing.
* the daemon **re-points fd 0/1/2 to `/dev/null` when it detaches**, so a shim
  that logs to stderr looks like a shim that never ran. `outfd` has to be a file
  the shim opens itself (`/data/recwl/qmi4.log`), and `/proc/<pid>/maps` is how
  you confirm the preload actually loaded.
* sources: `tmp/v27/wifi/dumpqmi4.c` (two-way, filtered to `0x0020..0x0047` and
  the `0x0120..` indications), transcript in `tmp/v27/wifi/qmi-vendor.log`.
