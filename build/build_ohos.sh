#!/bin/bash
# HarmonyOS userspace components, built with ohos-clang against the sysroot:
#   display composer + buffer VDI, audio VDI (tinyalsa), user_auth CA stub, software HUKS.
# Needs: make_sysroot.sh and fetch_oh_sources.sh done first; clang/lld 15+, llvm-strip.
set -e
. "$(dirname "$0")/env.sh"
S=$REPO/src; P=$REPO/patches; B=$WORK/ohos; mkdir -p "$B"
O=$OH_SRC
CXX="ohos-clang++ -std=c++17 -O2 -fPIC -Wno-everything"
CC="ohos-clang -O2 -fPIC -Wno-everything"
BCF="$O/third_party_bounds_checking_function/include"

# ---- display: buffer allocator + composer (DRM/KMS, or fbdev MDP atomic commit
# when the kernel has no DRM). Needs the 5.0.3 interface headers that
# fetch_oh_sources.sh pins: Huawei's composer ABI is the OpenHarmony 5.0 line.
HAL_INC="-I$S/display/common -I$S/display/hdi-gen -I$S/display/hdi-gen/display -I$S/display/hdi-gen/display/buffer \
 -I$S/display/hdi-gen/display/composer -I$S/display/hdi-gen/display/graphic/common \
 -I$O/drivers_interface/display/composer/hdifd_parcelable -I$O/drivers_interface/display/composer \
 -I$O/drivers_interface/display -I$O/hiviewdfx_hitrace/interfaces/native/innerkits/include/hitrace_meter \
 -I$O/drivers_interface/display/composer/common/include -I$O/drivers_peripheral/display/buffer/hdi_service/include \
 -I$O/drivers_peripheral/display/composer/hdi_service/include -I$O/drivers_peripheral/display/utils/include \
 -I$O/graphic_graphic_surface/interfaces/inner_api/utils -I$O/hdf_core/interfaces/inner_api/utils \
 -I$O/hdf_core/interfaces/inner_api/osal/shared -I$O/hdf_core/interfaces/inner_api/osal/uhdf \
 -I$O/hdf_core/interfaces/inner_api/hdi -I$O/hdf_core/interfaces/inner_api/hdi/base -I$O/hdf_core/interfaces/inner_api/ipc \
 -I$O/hdf_core/interfaces/inner_api/core -I$O/communication_ipc/interfaces/innerkits/ipc_core/include \
 -I$O/commonlibrary_c_utils/base/include -I$O/hiviewdfx_hilog/interfaces/native/innerkits/include \
 -I$O/third_party_libdrm -I$O/third_party_libdrm/include/drm -I$BCF"
D=$B/display; mkdir -p "$D/obj"
$CXX $HAL_INC -shared -o "$D/libdisplay_buffer_vdi_impl.z.so" "$S/display/buffer/sweet_buffer_vdi.cpp" \
  -Wl,-soname,libdisplay_buffer_vdi_impl.z.so -ldrm -lhilog -lc++ -lc
for f in "$S"/display/composer/src/*.cpp; do
  $CXX -I"$S/display/composer/include" $HAL_INC -c "$f" -o "$D/obj/$(basename "${f%.cpp}").o"
done
ohos-clang++ -shared -o "$D/libdisplay_composer_vdi_impl.z.so" "$D"/obj/*.o -Wl,-soname,libdisplay_composer_vdi_impl.z.so \
  -L"$D" -ldisplay_buffer_vdi_impl.z -ldrm -lhilog -lhdf_utils.z -lsec_shared.z -lhitrace_meter -lutils.z -lc++ -lc
llvm-strip "$D"/*.so
cp "$D"/libdisplay_buffer_vdi_impl.z.so "$D"/libdisplay_composer_vdi_impl.z.so "$P/10-display/files/lib64/"

# ---- audio VDI on tinyalsa (BSD-3-Clause)
A=$B/audio; mkdir -p "$A/obj"
[ -d "$A/tinyalsa" ] || git clone -q https://github.com/tinyalsa/tinyalsa "$A/tinyalsa"
for f in mixer mixer_hw pcm pcm_hw limits snd_card_plugin; do
  $CC -fvisibility=hidden -I"$A/tinyalsa/include" -c "$A/tinyalsa/src/$f.c" -o "$A/obj/ta_$f.o"
done
$CXX -fvisibility=hidden -I"$S/audio/vdi" -I"$A/tinyalsa/include" -c "$S/audio/sweet_audio_vdi.cpp" -o "$A/obj/vdi.o"
ohos-clang++ -shared -o "$A/libaudio_primary_impl.z.so" "$A"/obj/*.o -Wl,-soname,libaudio_primary_impl.z.so \
  -Wl,--no-undefined -lc++ -lc -lm -ldl
llvm-strip "$A/libaudio_primary_impl.z.so"
cp "$A/libaudio_primary_impl.z.so" "$P/31-audio/files/lib64/"

# ---- Bluetooth vendor library (WCN3990 over /dev/ttyHS0)
ohos-clang -O2 -fPIC -shared -fvisibility=hidden -o "$B/libbt_vendor.z.so" "$S/bluetooth/sweet_bt_vendor.c" \
  -Wl,-soname,libbt_vendor.z.so -ldl -lc
llvm-strip "$B/libbt_vendor.z.so"; cp "$B/libbt_vendor.z.so" "$P/35-bluetooth/files/lib64/"

# ---- sensor VDI: Snapdragon Sensor Core over QRTR/QMI
$CXX -fvisibility=hidden -I"$S/sensors/vdi" -I$O/hdf_core/interfaces/inner_api/host/uhdf -I$O/hdf_core/interfaces/inner_api/hdi \
  -I$O/hdf_core/interfaces/inner_api/utils -I$O/hdf_core/interfaces/inner_api/osal/shared \
  -I$O/hdf_core/interfaces/inner_api/osal/uhdf -I$O/communication_ipc/interfaces/innerkits/ipc_core/include \
  -I$O/commonlibrary_c_utils/base/include -c "$S/sensors/sweet_sensors_vdi.cpp" -o "$B/sensors_vdi.o"
ohos-clang++ -shared -o "$B/libhdi_sensor_impl.z.so" "$B/sensors_vdi.o" -Wl,-soname,libhdi_sensor_impl.z.so \
  -Wl,--no-undefined -lutils.z -lc++ -lc -ldl
llvm-strip "$B/libhdi_sensor_impl.z.so"; cp "$B/libhdi_sensor_impl.z.so" "$P/39-sensors-hal/files/lib64/"

# ---- user_auth CA stub
$CXX -shared -o "$B/libuserauth_daemon.z.so" "$S/userauth/userauth_daemon_stub.cpp" -Wl,-soname,libuserauth_daemon.z.so -lc++ -lc
llvm-strip "$B/libuserauth_daemon.z.so"; cp "$B/libuserauth_daemon.z.so" "$P/20-userauth-no-tee/files/lib64/"

# ---- software HUKS (OpenHarmony security_huks core on OpenSSL)
H=$B/huks; mkdir -p "$H"
HUKS_SRC=$O/security_huks OPENSSL_INC=$O/third_party_openssl/include BCF=$BCF HILOG_INC=$O/hiviewdfx_hilog/interfaces/native/innerkits/include \
  OUT=$H bash "$S/huks/build.sh"
cp "$H/libhuks_ca_hdi.z.so" "$P/21-huks-software/files/lib64/chipsetsdk/"
echo "HarmonyOS userspace components rebuilt"

# ---- telephony: RIL vendor library over QMI/QRTR (needs telephony_ril_adapter headers)
RA=$O/telephony_ril_adapter
[ -d "$RA" ] || git clone -q --depth 1 -b OpenHarmony-5.1.0-Release https://github.com/openharmony/telephony_ril_adapter "$RA"
R=$B/ril; mkdir -p "$R"
ohos-clang++ -std=c++17 -O2 -fPIC -Wno-c99-designator -I"$RA/interfaces/innerkits/include" -shared \
  -o "$R/libril_msgtransfer.z.so" "$S"/ril/*.cpp -Wl,-soname,libril_msgtransfer.z.so -lhilog -lc++ -lc
llvm-strip "$R/libril_msgtransfer.z.so"
cp "$R/libril_msgtransfer.z.so" "$P/42-ril/files/lib64/"

# ---- GNSS vendor adapter over QMI LOC, IR blaster driver
ohos-clang++ -std=c++17 -O2 -fPIC -fvisibility=hidden -DQMI_LOG_TAG='"SweetGnss"' -DQMI_KLOG_PREFIX='"sweet-gnss: "' \
  -I"$S/ril" -I"$S/gnss" -shared -o "$B/vendorGnssAdapter.so" "$S/gnss/sweet_gnss.cpp" "$S/ril/qmi.cpp" \
  -Wl,-soname,vendorGnssAdapter.so -lhilog -lc++ -lc
llvm-strip "$B/vendorGnssAdapter.so"; cp "$B/vendorGnssAdapter.so" "$P/44-gnss/files/lib64/"
ohos-clang -O2 -fPIC -fvisibility=hidden -shared -o "$B/libconsumerir.so" "$S/ir/sweet_consumerir.c" \
  -Wl,-soname,libconsumerir.so -lc
llvm-strip "$B/libconsumerir.so"; cp "$B/libconsumerir.so" "$P/43-consumer-ir/files/lib64/"

# ---- every shipped library must resolve all its symbols against its NEEDED libs:
#      iter31 shipped a composer with one undefined C++ symbol; the dynamic linker
#      refused to load it and the device stayed on the boot logo.
bad=0
for f in $(find "$P" -name '*.so'); do
  out=$("$REPO/build/check_syms.sh" "$f" "$(dirname "$f")")
  case "$out" in *" 0 missing;"*) ;; *) echo "$out"; bad=1;; esac
done
[ $bad = 0 ] || { echo "unresolved symbols, not shipping"; exit 1; }

# camera host (OpenHarmony camera HDI v1.0 on top of cambridge), patch 47
C=$B/camera; mkdir -p "$C"
CI="-I$S/camera/host -I$S/camera/include -I$S/camera/hdi-gen -I$S/camera/hdi-gen/camera \
 -I$O/hdf_core/interfaces/inner_api/hdi -I$O/hdf_core/interfaces/inner_api/hdi/base -I$O/hdf_core/interfaces/inner_api/utils \
 -I$O/hdf_core/interfaces/inner_api/osal/shared -I$O/hdf_core/interfaces/inner_api/osal/uhdf -I$O/hdf_core/interfaces/inner_api/ipc \
 -I$O/hdf_core/interfaces/inner_api/core -I$O/hdf_core/interfaces/inner_api/host/uhdf \
 -I$O/communication_ipc/interfaces/innerkits/ipc_core/include -I$O/commonlibrary_c_utils/base/include \
 -I$O/hiviewdfx_hilog/interfaces/native/innerkits/include -I$O/graphic_graphic_surface/interfaces/inner_api/surface \
 -I$O/graphic_graphic_surface/interfaces/inner_api/utils -I$O/graphic_graphic_surface/interfaces/inner_api/common -I$BCF"
for f in "$S"/camera/host/*.cpp "$S"/camera/hdi-gen/camera/v1_0/{camera_host_stub,camera_device_stub,stream_operator_stub,offline_stream_operator_stub,camera_host_callback_proxy,camera_device_callback_proxy,stream_operator_callback_proxy,types}.cpp; do
  ohos-clang++ -std=c++17 -O2 -fPIC -fno-rtti -Wno-everything $CI -c "$f" -o "$C/$(basename "${f%.cpp}").o"
done
# libmetadata / libbuffer_producer_sequenceable come from system.img (chipset sdk), copied by make_sysroot.sh
ohos-clang++ -fPIE -pie -o "$C/sweet_camera_host" "$C"/*.o -lmetadata.z -lbuffer_producer_sequenceable_1.0.z -lsurface.z \
  -lipc_single.z -lutils.z -lhilog -lhdi.z -lhdf_utils.z -lsec_shared.z -lc++ -lc
llvm-strip "$C/sweet_camera_host"; cp "$C/sweet_camera_host" "$P/47-camera/files/bin/"
