/*
 * fbdev display device (msm8998 sagit): the kernel has no DRM, so the panel is
 * driven through the msm framebuffer's atomic commit ioctl on /dev/graphics/fb0.
 *
 * The fb0 fd is opened once and kept for the whole process lifetime: the last
 * close of fb0 powers the panel down and zeroes the backlight.
 */
#ifndef FBDEV_DEVICE_H
#define FBDEV_DEVICE_H
#include <memory>
#include <unordered_map>
#include "hdi_device_common.h"
#include "hdi_device_interface.h"
#include "hdi_display.h"
#include "hdi_shared_fd.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
class FbdevDevice : public HdiDeviceInterface, std::enable_shared_from_this<FbdevDevice> {
public:
    static std::shared_ptr<HdiDeviceInterface> Create();
    static int GetFbFd();
    static uint32_t GetWidth();
    static uint32_t GetHeight();

    FbdevDevice() {}
    ~FbdevDevice() override {}

    std::unordered_map<uint32_t, std::shared_ptr<HdiDisplay>> DiscoveryDisplay() override;
    int32_t Init() override;
    void DeInit() override;
    bool HandleHotplug(uint32_t dispId, bool plugIn) override;

private:
    static FdPtr mFbFd;
    static uint32_t mWidth;
    static uint32_t mHeight;
    static std::shared_ptr<FbdevDevice> mInstance;
};
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS

#endif // FBDEV_DEVICE_H
