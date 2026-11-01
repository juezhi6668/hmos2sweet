# How the port works

## Starting point

- **Donor**: Huawei Pura 70 (Kirin 9010), HarmonyOS 5.1 = OpenHarmony 5.0/5.1 + Huawei services.
- **Target**: Redmi Note 10 Pro *sweet* (Snapdragon 732G / sm6150, Adreno 618, WCN3990, Bolero codec +
  2× Awinic aw882xx, AMOLED 1080x2400 120 Hz panel).

The HarmonyOS userspace (system, sys_prod, chip_prod, cust, patch) runs almost untouched. Everything that talks
to Kirin hardware lives in `/vendor` and is replaced or neutralised by the patches.

## Boot

```
Xiaomi ABL ─► boot.img (header v2)
               ├─ LineageOS 4.14 kernel + OpenHarmony patches (src/kernel/patches)
               ├─ sdmmagpie dtb + sweet dtbo
               └─ HarmonyOS ramdisk from the OTA + /init = hmos-preinit
hmos-preinit (src/preinit)
  1. reads the LP metadata of the super partition and creates a dm-linear for every logical partition
  2. writes /etc/fstab.required (system, vendor, sys_prod, chip_prod, cust, patch → dm-N, erofs)
  3. mounts cache and starts a logger: kmsg, snapshots of hilog/processes/threads/services, screenshot
  4. androidboot.mode=charger (off-mode charging): stays in a minimal loop, see below
  5. exec /bin/init_early (HarmonyOS's own init)
HarmonyOS init
  ├─ /vendor/etc/fstab.sweet: data f2fs, cache as /log, dsp
  └─ /vendor/etc/init/hmos2sweet.cfg: commands and services of the selected patches
```

Kernel command line (`lib/builder.py`): `hardware=sweet` (selects `fstab.sweet`), `hmos.permissive=1`
(SELinux permissive even if init asks for enforcing), `hmos.noreboot=1` (a crashing critical service does not
reboot the phone), `firmware_class.path=/vendor/firmware`, `service_locator.enable=1`.

## Kernel (src/kernel/patches)

| Patch | Why |
|---|---|
| 0001 userspace bring-up | `/dev/access_token_id` driver, binder ioctls `BINDER_GET_SENDER_INFO`/`ACCESS_TOKEN`, `hmos.*` options |
| 0002 binderfs off, sysrq/noreboot, DRM | HarmonyOS init creates the binder nodes from uevents |
| 0003 DRM auth for the HALs | composer, allocator and Mesa share `/dev/dri/card0` without being DRM master |
| 0004/0005 emulated hmdfs/sharefs | storage_daemon mounts hmdfs/sharefs: emulated with tmpfs + bind |
| 0006/0007 fs-verity stub | installd enables code signing: accepted on a kernel without fs-verity |
| 0008 firmware paths | modem/ADSP firmware from `/vendor/firmware_mnt/image` and `/vendor/dsp` |
| 0009 binder one-way sender pid | OpenHarmony uses the sender pid for asynchronous transactions too |
| 0010 icnss log | WiFi debug messages in kmsg (bring-up) |
| 0011 WLAN BDF | the kernel sends bdwlan to the WLAN firmware (cnss-daemon does it on Android) |
| 0012 reboot panic | `hmos.noreboot=1` also refuses `panic:<reason>` reboots |
| 0013 SSR without panic | a slow ADSP/modem restart no longer panics the kernel |
| 0014 icnss BDF status | BDF download reported success as failure |
| 0015 memcg | accepts the HarmonyOS memmgr hyperhold/zswapd files |
| 0016 vibrator | the aw8624 also appears as `/sys/class/leds/vibrator` |
| 0017 /sys/nfc | attributes Huawei's NFC stack reads (chip type, device node, config names) |
| 0018 reboot log | logs who asks for power off/restart; `reboot("shutdown")` powers off |
| 0019 icnss cal report | the BDF completion is answered with an error; the cal report after it is what makes the firmware send FW_READY |

## Display and GPU

- **Composer** (`src/display/composer`, OpenHarmony VDI): atomic DRM/KMS on sweet's SDE driver.
  - Only the DSI connector is used. The "Virtual" writeback connector must be ignored, otherwise the screen
    stays black.
  - Handles hardware planes, vblank events (with a software vsync fallback), DPMS + sysfs backlight.
  - The panel has a single 120 Hz mode; the composer also exposes a virtual 60 Hz mode for the refresh rate
    manager (HGM).
  - Fence ownership: Huawei's composer service takes ownership of the release fences it gets (`Move()`), so the
    VDI hands out each fence exactly once and never keeps or duplicates them. A leak here made the whole
    system stall (most visibly during audio playback) and left the screen black after screen-on.
- **Allocator** (`src/display/buffer`): dma-buf buffers through DRM dumb buffers, YUV formats.
- **Mesa 26** (`src/mesa`): freedreno on kgsl, OHOS EGL platform (NativeWindow/NativeBuffer, NV12/NV21 import).
  It replaces `libEGL_impl.so`/`libGLESv*_impl.so` in `/vendor/lib64/chipsetsdk`.
- dpi/cutout and the HGM policy are rewritten from the OTA files and bind-mounted over `/sys_prod` at boot.

## No TEE

sweet has no iTrustee. Huawei's "CA" libraries would wait forever for the TEE:

- `libuserauth_daemon.z.so` → stub (patch 20): no credentials, swipe-only lock screen;
- user_auth / pin_auth HDI services (patch 46) → OpenHarmony's REE implementation (drivers_peripheral
  user_auth 3.0 / pin_auth 2.1), PIN data salted and hashed in `/data/service/el1/public/{userauth,pinauth}`;
- `libhuks_ca_hdi.z.so` → OpenHarmony's HUKS core on OpenSSL with a software root key (patch 21).

## Qualcomm coprocessors

On Android the vendor daemons do this. Here the free linux-msm versions are used, patched for the downstream
kernel:

- `qrtr-ns`: QRTR name service, not in the 4.14 kernel;
- `rmtfs`: shared memory through UIO;
- `tqftpserv`: files read from the firmware partition;
- `pd-mapper`: reads the `.jsn` files.

On top of them `sweet-rproc`:

1. mounts the firmware partition (vfat, not supported by the HarmonyOS fstab);
2. starts `pd-mapper` and `tqftpserv`;
3. boots the ADSP if `/vendor/etc/sweet/feature_audio` exists;
4. loads the IPA firmware, boots the modem and asks for `wlan0` if `feature_wifi` exists;
5. opens the ALSA nodes to the audio host;
6. sets up `rmnet_ipa0` and the `rmnet_data*` interfaces for mobile data (in a child process).

## Audio

`src/audio/sweet_audio_vdi.cpp` implements the VDI that the HarmonyOS audio host loads from
`/vendor/lib64/libaudio_primary_impl.z.so`. It is based on tinyalsa:

- PCM 0 (deep buffer) and 13 (low latency) for playback, PCM 0 for capture;
- mixer paths read from sweet's original `mixer_paths_*.xml`: `speaker`, `headphones`, `handset`, `handset-mic`;
- without a sound card the frames are consumed in real time, so the audio server never blocks.

## Setup wizard

The wizard (`HwStartupGuide`) needs WiFi. It is kept by default; patch 50 (off by default):

- removes it from `system.img` and from the preinstall list;
- starts `sweet-oobedone`, which writes `device_provisioned`, `user_setup_complete` and `basic_statement_agreed`
  into the settingsdata database.

Without these keys SceneBoard would try to start the wizard on every boot.

## Reboots

On a port a missing driver is enough to make a service crash, and HarmonyOS reacts by rebooting:

- AMS reboots the phone if SceneBoard crashes 4 times in 4 minutes (`supportSCBCrashReboot`);
- when a "critical" service (render_service, foundation, samgr, ...) crashes too often, init runs the `reboot`
  job (stops all services, unmounts /data) and then panics the kernel.

Patch 15 turns off the first and clears the critical flag: directly in the vendor cfgs, and for the system ones
by declaring the service again in `/vendor/etc/init/hmos2sweet_nocritical.cfg` (init, musl build, updates an
already defined service). The kernel (0013) no longer panics if a coprocessor takes more than 20 s to restart.

## Power off and off-mode charging

With a charger plugged in, "power off" makes the PMIC start the phone again and the bootloader passes
`androidboot.mode=charger`. Android would show its charging screen; booting HarmonyOS there looks like a reboot.
`hmos-preinit` instead stays in a small loop: the screen is off, charging goes on in hardware, unplugging the
cable powers the phone off, holding the power key for 1.5 s restarts into HarmonyOS. `hmos.charger=boot` on the
kernel command line disables this.

## Sensors

sm6150's sensors are in the Snapdragon Sensor Core, on the ADSP. Two pieces:

- `hexagonrpcd` (patch 34) serves the ADSP sensors domain its configuration files and registry;
- `libhdi_sensor_impl.z.so` (patch 39, `src/sensors`) is the VDI the HarmonyOS sensor host loads. It talks to
  the QMI "SNS client" service (400) over QRTR like the Android HAL: it looks up the SUID of `accel`, `gyro`,
  `mag`, `ambient_light`, `gravity`, `rotv`, `game_rv`, sends the configuration requests in protobuf and
  forwards the events. If the SSC has no `gravity` it computes it from the accelerometer (needed for rotation).

## Thermal, USB, Kirin daemons

- Patch 36: the thermal HDI reads the real thermal zones (CPU, GPU, battery, back, USB connector) and the
  thermal service uses a policy for sweet. CPU/GPU throttling stays in the kernel.
- Patch 38: `/system/etc/init.usb.cfg` imports `/vendor/etc/init.${ohos.boot.hardware}.usb.cfg`; the patch
  writes `init.sweet.usb.cfg` from the OTA's Kirin file with the `a600000.dwc3` controller.
- Patch 37: Huawei daemons that only talk to Kirin hardware (THP, sensorhub, Balong, NPU, TEE, ...) are not
  started.

## Mounts in the pre-init job

init creates the service sandboxes (`mksandbox system`, `mksandbox chipset` in the `init` job) with `rec` +
`private` binds: a mount made later is not visible to sandboxed services (foundation/DMS, render_service, HDF
hosts such as Bluetooth). That is why all the patch mounts (modem, bluetooth and persist partitions,
configuration files replaced in /sys_prod and /system/etc) are in the `pre-init` job, before the sandboxes.

## Telephony, GPS, infrared

- **RIL** (`src/ril`, patch `42-ril`): replaces `libril_msgtransfer.z.so` (Huawei's Balong RIL) with a library
  implementing `RilInitOps` of `telephony_ril_adapter` (OpenHarmony 5.1, same function tables as Huawei's
  `libhril.z.so`) that talks to the modem's QMI services over QRTR: DMS (radio, IMEI, baseband), UIM (SIM state,
  PIN/PUK, file read/write, IMSI, ICCID, APDU), NAS (registration, signal, operator, network selection, network
  type, network time), VOICE (CS calls), WMS (3GPP SMS) and WDS/WDA (data). A `ServiceWatcher` follows the QRTR
  name service and reconnects the clients when the modem restarts.
- **Supplementary services** (`ril_ss.cpp`): call waiting, call forwarding, call barring and barring password,
  CLIP/CLIR, USSD (send, answer, network-initiated reports, UCS-2 decoding) through QMI VOICE.
- **IMS / VoLTE** (`ril_ims.cpp`): voice domain preference (`persist.sweet.volte_only`, default PS only),
  IMS settings enabled on the modem (IMS settings service 0x12), IMSA registration indications, and when LTE
  stays unregistered for IMS the "ims" APN PDN is brought up through IMSDCM. Untested.
- **Mobile data**: `sweet-rproc` configures `rmnet_ipa0` (QMAP in/out) and creates `rmnet_data0..3` (mux id
  1..4) through rtnetlink; the RIL sets the data format on the modem (WDA, IPA "embedded" endpoint), binds a WDS
  client per data call to the mux and returns addresses/DNS to the telephony stack, which configures the
  interface.
- **In-call audio**: the `AUDIO_IN_CALL` scene on the primary render starts the `VoiceMMode1` front end
  (hostless PCM) with the stock `voicemmode1-call <device>` mixer paths.
- **GPS** (`src/gnss`, patch `44-gnss`): `vendorGnssAdapter.so` for OpenHarmony's GNSS HDI service, on QMI LOC
  (positions, satellites, NMEA).
- **Infrared** (`src/ir`, patch `43-consumer-ir`): `libconsumerir.so`, loaded by Huawei's IR service, transmits
  through `/dev/lirc0`.
- **Vibration**: kernel patch 0016, the aw8624 also appears as `/sys/class/leds/vibrator`.

## NFC

sweet's NXP PN553 is driven by the Qualcomm `nq-nci` driver (`/dev/nq-nci`). Huawei's NFC stack (NXP-based
`libnfc_hal_impl_t001` + `libnfc_nci_t001`) reads its configuration from `/sys/nfc/*`: kernel patch 0017 provides
those attributes (chip type `sn110u` selects the NXP libraries). Patch 45 replaces the SN110 configuration with
sweet's PN553 one and hides the SN110 firmware library so that the HAL never tries to flash it.

## Camera (work in progress)

The camera sensors, ISP and JPEG encoder of sweet are only usable through Qualcomm's Android camera HAL
(CamX, `camera.qcom.so`, camera3). The plan, being implemented in `src/camera`:

- **sweetcam-launch** (static): creates a private mount namespace that looks like a minimal Android
  (`/system`, `/vendor`, `/apex/com.android.runtime` from `/vendor/android`), writes an Android property area
  (vendor build.prop + `ro.boot.*` from the kernel command line) and runs cambridge, restarting it if it dies.
- **cambridge** (bionic): loads `camera.qcom.so` and the Qualcomm gralloc, describes every camera, configures
  streams, runs the repeating preview request and single captures (JPEG), and hands the gralloc buffers
  (dma-buf) to the host once over a unix socket (`sweetcam_proto.h`).
- **sweet camera host** (musl, OpenHarmony camera HDI v1.0): registers `camera_service` with the HDF service
  manager like Huawei's CameraDaemon did, converts the camera description into OpenHarmony abilities, maps the
  controls (zoom, focus, flash, exposure, JPEG orientation/quality, GPS) and copies every frame into the
  BufferProducer of the stream (NV21/NV12/JPEG), so every app using the camera API works.

## fetchfw

`fetchfw.py` (`lib/fetchfw.py`) finds Pura 70 full packages: catalog, user list, optional query to Huawei's
update service. Each candidate is verified remotely with HTTP range requests (zip64 central directory,
`packageinfo.mbn`, `VERSION.mbn`, `update.tag`) and accepted only if it is a full BASE package of an `ADY-*`
device. The download is resumable and checked against the sha256 in `filelist.xml`.
