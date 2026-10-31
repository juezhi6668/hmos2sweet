#include "fbdev_device.h"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/fb.h>
#include "display_log.h"
#include "fbdev_display.h"
#include "hmos_klog.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
FdPtr FbdevDevice::mFbFd = nullptr;
uint32_t FbdevDevice::mWidth = 0;
uint32_t FbdevDevice::mHeight = 0;
std::shared_ptr<FbdevDevice> FbdevDevice::mInstance;

std::shared_ptr<HdiDeviceInterface> FbdevDevice::Create()
{
    DISPLAY_LOGD();
    if (mInstance == nullptr) {
        mInstance = std::make_shared<FbdevDevice>();
    }
    return mInstance;
}

int FbdevDevice::GetFbFd()
{
    if (mFbFd == nullptr) {
        return -1;
    }
    return mFbFd->GetFd();
}

uint32_t FbdevDevice::GetWidth()
{
    return mWidth;
}

uint32_t FbdevDevice::GetHeight()
{
    return mHeight;
}

int32_t FbdevDevice::Init()
{
    if (mFbFd != nullptr) {
        return DISPLAY_SUCCESS;
    }
    int fd = open("/dev/graphics/fb0", O_RDWR | O_CLOEXEC);
    DISPLAY_CHK_RETURN((fd < 0), DISPLAY_FAILURE, HmosKlog("hdifb: open fb0 failed errno %d", errno));
    struct fb_var_screeninfo var = {};
    struct fb_fix_screeninfo fix = {};
    if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0) {
        HmosKlog("hdifb: fb0 screen info failed errno %d", errno);
        close(fd);
        return DISPLAY_FAILURE;
    }
    mFbFd = std::make_shared<HdiFd>(fd);
    mWidth = var.xres;
    mHeight = var.yres;
    HmosKlog("hdifb: fb0 fd %d %ux%u bpp %u stride %u", fd, mWidth, mHeight, var.bits_per_pixel,
        fix.line_length);
    return DISPLAY_SUCCESS;
}

void FbdevDevice::DeInit()
{
    /* fb0 stays open: closing it would power the panel down */
}

std::unordered_map<uint32_t, std::shared_ptr<HdiDisplay>> FbdevDevice::DiscoveryDisplay()
{
    std::unordered_map<uint32_t, std::shared_ptr<HdiDisplay>> displays;
    auto display = std::make_shared<FbdevDisplay>();
    DISPLAY_CHK_RETURN((display == nullptr), displays, DISPLAY_LOGE("can not create fbdev display"));
    if (display->Init() != DISPLAY_SUCCESS) {
        HmosKlog("hdifb: display init failed");
        return displays;
    }
    displays[display->GetId()] = display;
    return displays;
}

bool FbdevDevice::HandleHotplug(uint32_t dispId, bool plugIn)
{
    (void)dispId;
    (void)plugIn;
    return false;
}
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS
