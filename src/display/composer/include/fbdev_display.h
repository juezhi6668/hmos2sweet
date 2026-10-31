/*
 * Display for sagit (msm8998) driven through fbdev: the kernel has no DRM, so
 * the pre stage is the (absent) gfx composer and the post stage is the msm
 * fbdev atomic commit on /dev/graphics/fb0.
 */
#ifndef FBDEV_DISPLAY_H
#define FBDEV_DISPLAY_H
#include <atomic>
#include <memory>
#include "hdi_display.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
class HdiFbdevComposition;

class FbdevDisplay : public HdiDisplay {
public:
    FbdevDisplay() {}
    ~FbdevDisplay() override {}

    int32_t Init() override;
    int32_t GetDisplayCapability(DisplayCapability *info) override;
    int32_t GetDisplaySupportedModes(uint32_t *num, DisplayModeInfo *modes) override;
    int32_t GetDisplayMode(uint32_t *modeId) override;
    int32_t SetDisplayMode(uint32_t modeId) override;
    int32_t GetDisplayPowerStatus(DispPowerStatus *status) override;
    int32_t SetDisplayPowerStatus(DispPowerStatus status) override;
    int32_t GetDisplayBacklight(uint32_t *value) override;
    int32_t SetDisplayBacklight(uint32_t value) override;
    int32_t RegDisplayVBlankCallback(VBlankCallback cb, void *data) override;
    int32_t SetDisplayVsyncEnabled(bool enabled) override;
    int32_t WaitForVBlank(uint64_t *ns) override;
    bool IsConnected() override;

    HdiFbdevComposition *GetFbdevComposition();

private:
    int32_t PushFirstFrame();
    int32_t SetBrightness(uint32_t level);
    void InitBacklight();

    DispPowerStatus mPowerStatus = POWER_STATUS_ON;
    uint32_t mBrightnessLevel = 0;
    int32_t mBacklightFd = -1;
    uint32_t mBacklightMax = 0;
};
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS

#endif // FBDEV_DISPLAY_H
