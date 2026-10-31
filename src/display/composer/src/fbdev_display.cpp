/*
 * Display for sagit (msm8998): fbdev scanout, no DRM in the kernel.
 *
 * Same shape as DrmDisplay (gfx pre stage + scanout post stage) except that
 * the post stage speaks MSMFB_ATOMIC_COMMIT and the vsync comes from the
 * software worker (fbdev has no vblank event interface).
 */
#include "fbdev_display.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include "display_log.h"
#include "drm_vsync_worker.h"
#include "fbdev_device.h"
#include "hdi_composer.h"
#include "hdi_fbdev_composition.h"
#include "hdi_gfx_composition.h"
#include "hmos_klog.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
extern std::atomic<bool> g_hmosDisplayOff;
extern std::atomic<int> g_hmosVirtualMode;
extern std::atomic<uint32_t> g_hmosSynthPeriodNs;

namespace {
constexpr uint32_t kPanelWidthMm = 68;
constexpr uint32_t kPanelHeightMm = 121;
constexpr uint32_t kFramePeriodNs = 16666667; /* 60 Hz */

uint32_t g_hmosSavedLevel = 0;
} // namespace

int32_t FbdevDisplay::Init()
{
    DISPLAY_LOGD();
    int32_t ret = HdiDisplay::Init();
    DISPLAY_CHK_RETURN((ret != DISPLAY_SUCCESS), DISPLAY_FAILURE, DISPLAY_LOGE("hdi display init failed"));

    auto preComp = std::make_unique<HdiGfxComposition>();
    DISPLAY_CHK_RETURN((preComp == nullptr), DISPLAY_FAILURE, DISPLAY_LOGE("can not new HdiGfxComposition"));
    ret = preComp->Init();
    DISPLAY_CHK_RETURN((ret != DISPLAY_SUCCESS), DISPLAY_FAILURE, DISPLAY_LOGE("can not init HdiGfxComposition"));

    auto postComp = std::make_unique<HdiFbdevComposition>(FbdevDevice::GetFbFd(), FbdevDevice::GetWidth(),
        FbdevDevice::GetHeight());
    DISPLAY_CHK_RETURN((postComp == nullptr), DISPLAY_FAILURE, DISPLAY_LOGE("can not new HdiFbdevComposition"));
    ret = postComp->Init();
    DISPLAY_CHK_RETURN((ret != DISPLAY_SUCCESS), DISPLAY_FAILURE, DISPLAY_LOGE("can not init HdiFbdevComposition"));
    mComposer = std::make_unique<HdiComposer>(std::move(preComp), std::move(postComp));
    DISPLAY_CHK_RETURN((mComposer == nullptr), DISPLAY_FAILURE, DISPLAY_LOGE("can not new HdiComposer"));

    /* fbdev has no vblank ioctl here: the worker synthesizes the vsyncs */
    g_hmosSynthPeriodNs.store(kFramePeriodNs);
    DrmVsyncWorker::GetInstance();
    HmosKlog("hdifb: display %u ready, software vsync %u ns", GetId(), kFramePeriodNs);

    ret = SetDisplayMode(0);
    DISPLAY_CHK_RETURN((ret != DISPLAY_SUCCESS), DISPLAY_FAILURE, DISPLAY_LOGE("set mode 0 failed"));
    if (PushFirstFrame() != DISPLAY_SUCCESS) {
        /* a failed banner must not cost the system its display: every later
         * commit retries, and the kmsg line above says why it failed */
        HmosKlog("hdifb: first frame failed, display kept");
    }
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::GetDisplayCapability(DisplayCapability *info)
{
    DISPLAY_CHK_RETURN((info == nullptr), DISPLAY_NULL_PTR, DISPLAY_LOGE("info is nullptr"));
    info->name = "fbdev";
    info->type = DISP_INTF_MIPI;
    info->phyWidth = kPanelWidthMm;
    info->phyHeight = kPanelHeightMm;
    info->supportLayers = 1;
    info->virtualDispCount = 0;
    info->supportWriteBack = false;
    info->propertyCount = 0;
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::GetDisplaySupportedModes(uint32_t *num, DisplayModeInfo *modes)
{
    DISPLAY_CHK_RETURN((num == nullptr), DISPLAY_NULL_PTR, DISPLAY_LOGE("num is nullptr"));
    if (modes != nullptr) {
        modes[0].width = static_cast<int32_t>(FbdevDevice::GetWidth());
        modes[0].height = static_cast<int32_t>(FbdevDevice::GetHeight());
        modes[0].freshRate = 60;
        modes[0].id = 0;
    }
    *num = 1;
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::GetDisplayMode(uint32_t *modeId)
{
    DISPLAY_CHK_RETURN((modeId == nullptr), DISPLAY_NULL_PTR, DISPLAY_LOGE("modeId is nullptr"));
    *modeId = 0;
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::SetDisplayMode(uint32_t modeId)
{
    if (modeId != 0) {
        HmosKlog("hdifb: mode %u not supported", modeId);
        return DISPLAY_NOT_SUPPORT;
    }
    g_hmosVirtualMode.store(0);
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::GetDisplayPowerStatus(DispPowerStatus *status)
{
    DISPLAY_CHK_RETURN((status == nullptr), DISPLAY_NULL_PTR, DISPLAY_LOGE("status is nullptr"));
    *status = mPowerStatus;
    return DISPLAY_SUCCESS;
}

/*
 * Like the sweet DRM path: keep the mdp pipeline and the fb0 mapping alive and
 * model screen-off as backlight 0 + stop committing. Blanking fbdev itself
 * (FBIOBLANK) powers the panel down and the first unblank after it came back
 * frozen on this kernel family.
 */
int32_t FbdevDisplay::SetDisplayPowerStatus(DispPowerStatus status)
{
    /* 4 = OFF_FAKE, 5 = PRE_ON, 6 = PRE_OFF (display composer 1.2) */
    uint32_t st = static_cast<uint32_t>(status);
    bool off = (status == POWER_STATUS_OFF || status == POWER_STATUS_STANDBY ||
        status == POWER_STATUS_SUSPEND || st == 4);
    HmosKlog("hdifb: power %u (was %u, panel %s)", st, static_cast<uint32_t>(mPowerStatus),
        g_hmosDisplayOff.load() ? "off" : "on");
    if (!off && status != POWER_STATUS_ON) {
        return DISPLAY_SUCCESS; /* PRE_ON / PRE_OFF hints: keep the current state */
    }
    if (off) {
        if (!g_hmosDisplayOff.load() && mBrightnessLevel != 0) {
            g_hmosSavedLevel = mBrightnessLevel;
        }
        g_hmosDisplayOff.store(true);
        SetBrightness(0);
    } else {
        g_hmosDisplayOff.store(false);
        if (mBrightnessLevel == 0 && g_hmosSavedLevel != 0) {
            SetBrightness(g_hmosSavedLevel);
        }
    }
    mPowerStatus = status;
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::GetDisplayBacklight(uint32_t *value)
{
    DISPLAY_CHK_RETURN((value == nullptr), DISPLAY_NULL_PTR, DISPLAY_LOGE("value is nullptr"));
    *value = mBrightnessLevel;
    return DISPLAY_SUCCESS;
}

void FbdevDisplay::InitBacklight()
{
    if (mBacklightFd >= 0) {
        return;
    }
    /* sagit: pmi8998 wled registers as leds/wled (the panel node has
     * bl_ctrl_wled, max level 4095); keep the other names for kernels that
     * move it to a backlight class */
    const char *dirs[] = {
        "/sys/class/backlight/panel0-backlight",
        "/sys/class/backlight/backlight",
        "/sys/class/leds/lcd-backlight",
        "/sys/class/leds/wled",
    };
    for (const char *d : dirs) {
        char path[128];
        snprintf(path, sizeof(path), "%s/brightness", d);
        mBacklightFd = open(path, O_RDWR | O_CLOEXEC);
        if (mBacklightFd < 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/max_brightness", d);
        int mfd = open(path, O_RDONLY | O_CLOEXEC);
        if (mfd >= 0) {
            char mb[16] = {};
            if (read(mfd, mb, sizeof(mb) - 1) > 0) {
                mBacklightMax = static_cast<uint32_t>(strtoul(mb, nullptr, 10));
            }
            close(mfd);
        }
        if (mBacklightMax == 0) {
            mBacklightMax = 255;
        }
        HmosKlog("hdifb: backlight %s max %u", d, mBacklightMax);
        return;
    }
    HmosKlog("hdifb: no backlight node, errno %d", errno);
}

int32_t FbdevDisplay::SetBrightness(uint32_t level)
{
    InitBacklight();
    DISPLAY_CHK_RETURN((mBacklightFd < 0), DISPLAY_NOT_SUPPORT, DISPLAY_LOGE("no backlight node"));
    uint64_t hw;
    if (level == 0) {
        hw = 0;
    } else if (level <= 255) {
        hw = static_cast<uint64_t>(level) * mBacklightMax / 255;
    } else {
        hw = static_cast<uint64_t>(level > 65535 ? 65535 : level) * mBacklightMax / 65535;
    }
    if (level != 0 && hw == 0) {
        hw = 1;
    }
    char buffer[16];
    int bytes = snprintf(buffer, sizeof(buffer), "%u\n", static_cast<unsigned>(hw));
    if (pwrite(mBacklightFd, buffer, bytes, 0) < 0) {
        DISPLAY_LOGE("backlight write %{public}u failed errno %{public}d", static_cast<unsigned>(hw), errno);
    }
    mBrightnessLevel = level;
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::SetDisplayBacklight(uint32_t value)
{
    if (g_hmosDisplayOff.load()) {
        /* keep the remembered level for the wake-up, leave the panel dark */
        if (value != 0) {
            g_hmosSavedLevel = value;
        }
        return DISPLAY_SUCCESS;
    }
    static uint32_t lastLogged = 1;
    if ((value == 0) != (lastLogged == 0)) {
        HmosKlog("hdifb: backlight %u", value);
    }
    lastLogged = value;
    return SetBrightness(value);
}

int32_t FbdevDisplay::RegDisplayVBlankCallback(VBlankCallback cb, void *data)
{
    if (cb == nullptr) {
        return DISPLAY_PARAM_ERR;
    }
    std::shared_ptr<VsyncCallBack> vsyncCb = std::make_shared<VsyncCallBack>(cb, data, 0);
    DrmVsyncWorker::GetInstance().ReqesterVBlankCb(vsyncCb);
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::SetDisplayVsyncEnabled(bool enabled)
{
    DISPLAY_LOGD("enable %{public}d", enabled);
    DrmVsyncWorker::GetInstance().EnableVsync(enabled);
    return DISPLAY_SUCCESS;
}

int32_t FbdevDisplay::WaitForVBlank(uint64_t *ns)
{
    DISPLAY_CHK_RETURN((ns == nullptr), DISPLAY_NULL_PTR, DISPLAY_LOGE("in ns is nullptr"));
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    *ns = static_cast<uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<uint64_t>(now.tv_nsec);
    return DISPLAY_SUCCESS;
}

bool FbdevDisplay::IsConnected()
{
    return true;
}

HdiFbdevComposition *FbdevDisplay::GetFbdevComposition()
{
    if (mComposer == nullptr) {
        return nullptr;
    }
    return static_cast<HdiFbdevComposition *>(mComposer->GetPostCompostion());
}

int32_t FbdevDisplay::PushFirstFrame()
{
    HdiFbdevComposition *comp = GetFbdevComposition();
    DISPLAY_CHK_RETURN((comp == nullptr), DISPLAY_FAILURE, DISPLAY_LOGE("no fbdev composition"));
    char text[256];
    snprintf(text, sizeof(text),
        "HarmonyOS 5.1 on Xiaomi Mi 6 (sagit)\n"
        "composer: fbdev mdp atomic commit (no drm)\n"
        "fb0 %ux%u, software vsync 60 Hz\n"
        "waiting for render_service...",
        FbdevDevice::GetWidth(), FbdevDevice::GetHeight());
    int32_t ret = comp->InitBanner(text);
    DISPLAY_CHK_RETURN((ret != DISPLAY_SUCCESS), DISPLAY_FAILURE, DISPLAY_LOGE("first frame failed"));
    return DISPLAY_SUCCESS;
}
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS
