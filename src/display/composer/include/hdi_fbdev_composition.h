/*
 * fbdev presentation stage: one fullscreen client layer committed through
 * MSMFB_ATOMIC_COMMIT on /dev/graphics/fb0 (msm8998 mdp5, no DRM in kernel).
 */
#ifndef HDI_FBDEV_COMPOSITION_H
#define HDI_FBDEV_COMPOSITION_H
#include <mutex>
#include <vector>
#include "buffer_handle.h"
#include "hdi_composer.h"
#include "hdi_layer.h"
#include "mdp_atomic.h"

namespace OHOS {
namespace HDI {
namespace DISPLAY {
class HdiFbdevComposition : public HdiComposition {
public:
    HdiFbdevComposition(int fd, uint32_t width, uint32_t height);
    ~HdiFbdevComposition() override;
    int32_t Init() override;
    int32_t SetLayers(std::vector<HdiLayer *> &layers, HdiLayer &clientLayer) override;
    int32_t Apply(bool modeSet) override;

    /* draw and scan out a boot banner; kept on screen until the client sends a frame */
    int32_t InitBanner(const char *text);

private:
    struct LayerParams {
        int fd = -1;
        int32_t width = 0;
        int32_t height = 0;
        int32_t stride = 0;
        int32_t format = 0; /* HDI PixelFormat */
        uint32_t size = 0;  /* buffer allocation size, 0 if unknown */
        IRect crop {};
        IRect dst {};
        LayerAlpha alpha {};
        TransformType transform = ROTATE_NONE;
        bool preMulti = false;
    };

    int32_t Present(const LayerParams &params, int fenceFd);
    int32_t PresentClient(HdiLayer &layer);
    int32_t PresentBanner();
    int32_t Build(const LayerParams &params, uint32_t pipeNdx);
    int32_t Validate();
    int32_t Submit(int fenceFd);
    void SampleFirstFrame();
    int32_t PickPipe(const LayerParams &params);
    void MakeBanner(const char *text);

    int mFd = -1;
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    std::mutex mMutex;
    HdiLayer *mClientLayer = nullptr;
    uint32_t mPipeNdx = 0;
    bool mIgnoreFences = false;
    mdp::mdp_layer_commit mCommit {};
    mdp::mdp_input_layer mLayer {};
    BufferHandle *mBanner = nullptr;
    void *mBannerMap = nullptr;
    bool mBannerShown = false;
    uint32_t mFrames = 0;
    uint32_t mFailures = 0;
};
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS

#endif // HDI_FBDEV_COMPOSITION_H
