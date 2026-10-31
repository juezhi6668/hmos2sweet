#!/bin/bash
# OpenHarmony 5.1 sources needed by the userspace components (headers + HUKS core).
set -e
. "$(dirname "$0")/env.sh"
TAG=OpenHarmony-v5.1.0-Release
# Huawei's display HALs match the OpenHarmony 5.0 composer ABI (IDisplayComposerVdi
# still has SetHardwareCursorPosition and no FastPresent there). Pin the three
# trees the composer/allocator build reads, otherwise display_composer_vdi_impl
# stops compiling.
DISP_TAG=OpenHarmony-v5.0.3-Release
mkdir -p "$OH_SRC"; cd "$OH_SRC"
# gitee name differs from the local directory for hdf_core
RENAME='s|^hdf_core$|drivers_hdf_core|'
for r in drivers_interface drivers_peripheral hdf_core graphic_graphic_surface commonlibrary_c_utils \
         communication_ipc hiviewdfx_hilog hiviewdfx_hitrace third_party_libdrm third_party_bounds_checking_function \
         security_huks third_party_openssl; do
  src=$(echo "$r" | sed "$RENAME")
  tag=$TAG
  case "$r" in drivers_interface|drivers_peripheral|hdf_core) tag=$DISP_TAG;; esac
  [ -d "$r" ] || git clone --depth 1 -b "$tag" "https://gitee.com/openharmony/$src.git" "$r"
done
echo "OpenHarmony sources in $OH_SRC"
