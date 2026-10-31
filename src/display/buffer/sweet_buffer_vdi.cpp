/*
 * Display buffer VDI for Redmi Note 10 Pro (sweet, SM7150) and Xiaomi Mi 6
 * (sagit, MSM8998).
 *
 * Two allocation backends:
 *  - DRM "dumb" buffers on /dev/dri/card0 (sweet): the fds are scanned out by
 *    the composer (PRIME import + KMS) and rendered into by Mesa kms_swrast.
 *  - msm ION system heap on /dev/ion (sagit, no DRM in the kernel): the fds
 *    are imported by the mdp atomic commit path; software rendering maps the
 *    same dma-buf.
 *
 * Interface: OpenHarmony 5.0.x IDisplayBufferVdi (vtable verified against the
 * HarmonyOS 5.0.5 vendor library).  License: Apache-2.0
 */
#define SWEET_LOG_TAG "SweetGralloc"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/dma-buf.h>
#include <xf86drm.h>
#include <drm.h>
#include <drm_mode.h>

#include "idisplay_buffer_vdi.h"
#include "v1_0/display_composer_type.h"
#include "sweet_log.h"

using namespace OHOS::HDI::Display::Buffer::V1_0;
using namespace OHOS::HDI::Display::Composer::V1_0;

namespace {
constexpr int32_t DISPLAY_SUCCESS_ = 0;
constexpr int32_t DISPLAY_FAILURE_ = -1;
constexpr int32_t DISPLAY_PARAM_ERR_ = -2;
constexpr int32_t DISPLAY_NOMEM_ = -6;
constexpr int32_t DISPLAY_NOT_SUPPORT_ = -5;

/*
 * ION uapi of this kernel (include/uapi/linux/ion.h, 4.4 msm).  Declared here
 * because the OpenHarmony sysroot has no linux/ion.h.
 */
struct IonAllocationData {
    size_t len;
    size_t align;
    unsigned int heapIdMask;
    unsigned int flags;
    int handle;
};
struct IonFdData {
    int handle;
    int fd;
};
struct IonHandleData {
    int handle;
};
constexpr unsigned long ION_IOC_ALLOC_ = _IOWR('I', 0, IonAllocationData);
constexpr unsigned long ION_IOC_FREE_ = _IOWR('I', 1, IonHandleData);
constexpr unsigned long ION_IOC_SHARE_ = _IOWR('I', 4, IonFdData);
constexpr unsigned int ION_FLAG_CACHED_ = 1;
constexpr unsigned int ION_FLAG_CACHED_NEEDS_SYNC_ = 2;
/* msm8998-ion.dtsi: system_heap is qcom,ion-heap@25 */
constexpr unsigned int ION_HEAP_SYSTEM_SAGIT_ = 1u << 25;

struct FormatInfo {
    uint32_t bitsPerPixel;   /* for the first plane, used for the allocation */
    uint32_t heightNum;      /* total height = height * heightNum / heightDen */
    uint32_t heightDen;
};

bool GetFormatInfo(uint32_t format, FormatInfo &fi)
{
    switch (format) {
        case PIXEL_FMT_RGBX_8888:
        case PIXEL_FMT_RGBA_8888:
        case PIXEL_FMT_BGRX_8888:
        case PIXEL_FMT_BGRA_8888:
        case PIXEL_FMT_RGBA_1010102:
            fi = {32, 1, 1}; return true;
        case PIXEL_FMT_RGB_888:
            fi = {24, 1, 1}; return true;
        case PIXEL_FMT_RGB_565:
        case PIXEL_FMT_BGR_565:
        case PIXEL_FMT_RGBA_4444:
        case PIXEL_FMT_RGBX_4444:
        case PIXEL_FMT_BGRA_4444:
        case PIXEL_FMT_BGRX_4444:
        case PIXEL_FMT_RGBA_5551:
        case PIXEL_FMT_RGBX_5551:
        case PIXEL_FMT_BGRA_5551:
        case PIXEL_FMT_BGRX_5551:
        case PIXEL_FMT_YUYV_422_PKG:
        case PIXEL_FMT_UYVY_422_PKG:
        case PIXEL_FMT_YVYU_422_PKG:
        case PIXEL_FMT_VYUY_422_PKG:
        case PIXEL_FMT_YUV_422_I:
            fi = {16, 1, 1}; return true;
        case 38: /* PIXEL_FMT_BLOB: camera JPEG output, sized like a 4:2:0 frame */
        case PIXEL_FMT_YCBCR_420_SP:
        case PIXEL_FMT_YCRCB_420_SP:
        case PIXEL_FMT_YCBCR_420_P:
        case PIXEL_FMT_YCRCB_420_P:
            fi = {8, 3, 2}; return true;
        case PIXEL_FMT_YCBCR_422_SP:
        case PIXEL_FMT_YCRCB_422_SP:
        case PIXEL_FMT_YCBCR_422_P:
        case PIXEL_FMT_YCRCB_422_P:
            fi = {8, 2, 1}; return true;
        default:
            return false;
    }
}

/* stride in bytes, 64-byte aligned (the mdp and the software renderer both
 * assume at least that) */
uint32_t AlignedPitch(uint32_t width, const FormatInfo &fi)
{
    uint32_t bytes = (width * fi.bitsPerPixel + 7) / 8;
    return (bytes + 63) & ~63u;
}

BufferHandle *NewHandle(int fd, size_t size, int32_t stride, uint32_t width, uint32_t height, uint32_t format,
    uint32_t usage)
{
    auto *bh = static_cast<BufferHandle *>(calloc(1, sizeof(BufferHandle)));
    if (bh == nullptr) {
        return nullptr;
    }
    bh->fd = fd;
    bh->width = static_cast<int32_t>(width);
    bh->height = static_cast<int32_t>(height);
    bh->stride = stride;
    bh->size = static_cast<int32_t>(size);
    bh->format = static_cast<int32_t>(format);
    bh->usage = usage;
    bh->virAddr = nullptr;
    bh->phyAddr = 0;
    bh->reserveFds = 0;
    bh->reserveInts = 0;
    return bh;
}

int DrmFd()
{
    static int fd = -1;
    static std::once_flag once;
    std::call_once(once, [] {
        const char *nodes[] = {"/dev/dri/card0", "/dev/dri/renderD128"};
        for (const char *n : nodes) {
            fd = open(n, O_RDWR | O_CLOEXEC);
            if (fd >= 0) {
                /* never keep DRM master: the composer needs it for KMS */
                if (drmIsMaster(fd)) {
                    drmDropMaster(fd);
                }
                SLOGI("using %{public}s fd %{public}d", n, fd);
                SKLOG("drm node %s fd %d", n, fd);
                break;
            }
            SLOGE("open %{public}s failed: %{public}d", n, errno);
        }
        if (fd < 0) {
            SKLOG("no drm node, buffers will come from ion");
        }
    });
    return fd;
}

int IonFd()
{
    static int fd = -1;
    static std::once_flag once;
    std::call_once(once, [] {
        fd = open("/dev/ion", O_RDWR | O_CLOEXEC);
        if (fd >= 0) {
            SLOGI("using /dev/ion fd %{public}d", fd);
            SKLOG("ion fd %d", fd);
        } else {
            SLOGE("open /dev/ion failed: %{public}d", errno);
            SKLOG("open /dev/ion failed errno %d", errno);
        }
    });
    return fd;
}

/* allocate one dma-buf from an ion heap, return its fd */
int IonAllocBuffer(size_t len, unsigned int heapMask, int flags)
{
    int ion = IonFd();
    if (ion < 0) {
        return -1;
    }
    IonAllocationData alloc = {};
    alloc.len = len;
    alloc.align = 4096;
    alloc.heapIdMask = heapMask;
    alloc.flags = static_cast<unsigned int>(flags);
    alloc.handle = 0;
    if (ioctl(ion, ION_IOC_ALLOC_, &alloc) != 0) {
        SLOGE("ION_ALLOC len %{public}llu heap 0x%{public}x failed: %{public}d",
            (unsigned long long)len, heapMask, errno);
        SKLOG("ION_ALLOC len %zu heap 0x%x errno %d", len, heapMask, errno);
        return -1;
    }
    IonFdData share = {alloc.handle, -1};
    int ret = ioctl(ion, ION_IOC_SHARE_, &share);
    IonHandleData release = {alloc.handle};
    ioctl(ion, ION_IOC_FREE_, &release); /* the exported fd keeps the buffer alive */
    if (ret != 0 || share.fd < 0) {
        SLOGE("ION_SHARE failed: %{public}d", errno);
        SKLOG("ION_SHARE failed errno %d", errno);
        return -1;
    }
    return share.fd;
}

struct MetaKey {
    dev_t dev;
    ino_t ino;
    bool operator<(const MetaKey &o) const { return dev != o.dev ? dev < o.dev : ino < o.ino; }
};

class SweetBufferVdi : public IDisplayBufferVdi {
public:
    int32_t AllocMem(const AllocInfo &info, BufferHandle *&handle) const override
    {
        FormatInfo fi;
        if (info.width == 0 || info.height == 0 || !GetFormatInfo(info.format, fi)) {
            SLOGE("unsupported alloc %{public}ux%{public}u fmt %{public}u", info.width, info.height, info.format);
            SKLOG("unsupported alloc %ux%u fmt %u", info.width, info.height, info.format);
            return DISPLAY_NOT_SUPPORT_;
        }
        if (DrmFd() >= 0) {
            return AllocDrm(info, fi, handle);
        }
        return AllocIon(info, fi, handle);
    }

    void FreeMem(const BufferHandle &handle) const override
    {
        auto *bh = const_cast<BufferHandle *>(&handle);
        if (bh->virAddr != nullptr) {
            munmap(bh->virAddr, bh->size);
            bh->virAddr = nullptr;
        }
        if (bh->fd >= 0) {
            close(bh->fd);
        }
        for (uint32_t i = 0; i < bh->reserveFds; i++) {
            if (bh->reserve[i] >= 0) {
                close(bh->reserve[i]);
            }
        }
        free(bh);
    }

    void *Mmap(const BufferHandle &handle) const override
    {
        auto *bh = const_cast<BufferHandle *>(&handle);
        if (bh->virAddr != nullptr) {
            return bh->virAddr;
        }
        void *p = mmap(nullptr, bh->size, PROT_READ | PROT_WRITE, MAP_SHARED, bh->fd, 0);
        if (p == MAP_FAILED) {
            SLOGE("mmap fd %{public}d size %{public}d failed: %{public}d", bh->fd, bh->size, errno);
            SKLOG("mmap fd %d size %d errno %d", bh->fd, bh->size, errno);
            return nullptr;
        }
        bh->virAddr = p;
        return p;
    }

    int32_t Unmap(const BufferHandle &handle) const override
    {
        auto *bh = const_cast<BufferHandle *>(&handle);
        if (bh->virAddr == nullptr) {
            return DISPLAY_PARAM_ERR_;
        }
        munmap(bh->virAddr, bh->size);
        bh->virAddr = nullptr;
        return DISPLAY_SUCCESS_;
    }

    int32_t FlushCache(const BufferHandle &handle) const override
    {
        struct dma_buf_sync s = {DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW};
        ioctl(handle.fd, DMA_BUF_IOCTL_SYNC, &s);
        return DISPLAY_SUCCESS_;
    }

    int32_t InvalidateCache(const BufferHandle &handle) const override
    {
        struct dma_buf_sync s = {DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW};
        ioctl(handle.fd, DMA_BUF_IOCTL_SYNC, &s);
        return DISPLAY_SUCCESS_;
    }

    int32_t IsSupportedAlloc(const std::vector<VerifyAllocInfo> &infos, std::vector<bool> &supporteds) const override
    {
        supporteds.clear();
        for (const auto &i : infos) {
            FormatInfo fi;
            supporteds.push_back(GetFormatInfo(i.format, fi));
        }
        return DISPLAY_SUCCESS_;
    }

    int32_t RegisterBuffer(const BufferHandle &handle) override
    {
        return DISPLAY_SUCCESS_;
    }

    /* Metadata is kept per process, keyed by the dma-buf inode. */
    int32_t SetMetadata(const BufferHandle &handle, uint32_t key, const std::vector<uint8_t> &value) override
    {
        MetaKey k;
        if (!KeyOf(handle, k)) {
            return DISPLAY_PARAM_ERR_;
        }
        std::lock_guard<std::mutex> l(mutex_);
        meta_[k][key] = value;
        return DISPLAY_SUCCESS_;
    }

    int32_t GetMetadata(const BufferHandle &handle, uint32_t key, std::vector<uint8_t> &value) override
    {
        MetaKey k;
        if (!KeyOf(handle, k)) {
            return DISPLAY_PARAM_ERR_;
        }
        std::lock_guard<std::mutex> l(mutex_);
        auto it = meta_.find(k);
        if (it == meta_.end() || it->second.find(key) == it->second.end()) {
            return DISPLAY_NOT_SUPPORT_;
        }
        value = it->second[key];
        return DISPLAY_SUCCESS_;
    }

    int32_t ListMetadataKeys(const BufferHandle &handle, std::vector<uint32_t> &keys) override
    {
        keys.clear();
        MetaKey k;
        if (!KeyOf(handle, k)) {
            return DISPLAY_PARAM_ERR_;
        }
        std::lock_guard<std::mutex> l(mutex_);
        auto it = meta_.find(k);
        if (it != meta_.end()) {
            for (auto &kv : it->second) {
                keys.push_back(kv.first);
            }
        }
        return DISPLAY_SUCCESS_;
    }

    int32_t EraseMetadataKey(const BufferHandle &handle, uint32_t key) override
    {
        MetaKey k;
        if (!KeyOf(handle, k)) {
            return DISPLAY_PARAM_ERR_;
        }
        std::lock_guard<std::mutex> l(mutex_);
        auto it = meta_.find(k);
        if (it != meta_.end()) {
            it->second.erase(key);
        }
        return DISPLAY_SUCCESS_;
    }

    int32_t GetImageLayout(const BufferHandle &handle, OHOS::HDI::Display::Buffer::V1_2::ImageLayout &layout) const override
    {
        return DISPLAY_NOT_SUPPORT_;
    }

private:
    static int32_t AllocDrm(const AllocInfo &info, const FormatInfo &fi, BufferHandle *&handle)
    {
        int drm = DrmFd();
        uint32_t alignedW = (info.width + 63) & ~63u;
        uint32_t allocH = (info.height * fi.heightNum + fi.heightDen - 1) / fi.heightDen;
        struct drm_mode_create_dumb create = {};
        create.width = alignedW;
        create.height = allocH;
        create.bpp = fi.bitsPerPixel;
        if (drmIoctl(drm, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) {
            SLOGE("CREATE_DUMB %{public}ux%{public}u bpp %{public}u failed: %{public}d", alignedW, allocH,
                fi.bitsPerPixel, errno);
            return DISPLAY_NOMEM_;
        }
        int dmaFd = -1;
        int ret = drmPrimeHandleToFD(drm, create.handle, DRM_CLOEXEC | DRM_RDWR, &dmaFd);
        struct drm_mode_destroy_dumb destroy = {create.handle};
        drmIoctl(drm, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy); /* the dma-buf keeps the object alive */
        if (ret != 0 || dmaFd < 0) {
            SLOGE("PRIME export failed: %{public}d", errno);
            return DISPLAY_FAILURE_;
        }
        auto *bh = NewHandle(dmaFd, create.size, static_cast<int32_t>(create.pitch), info.width, info.height,
            info.format, info.usage);
        if (bh == nullptr) {
            close(dmaFd);
            return DISPLAY_NOMEM_;
        }
        handle = bh;
        SLOGI("drm alloc %{public}ux%{public}u fmt %{public}u -> fd %{public}d stride %{public}u size %{public}llu",
            info.width, info.height, info.format, dmaFd, create.pitch, (unsigned long long)create.size);
        SKLOG("drm %ux%u fmt %u -> fd %d pitch %u size %llu", info.width, info.height, info.format, dmaFd,
            create.pitch, (unsigned long long)create.size);
        return DISPLAY_SUCCESS_;
    }

    static int32_t AllocIon(const AllocInfo &info, const FormatInfo &fi, BufferHandle *&handle)
    {
        uint32_t pitch = AlignedPitch(info.width, fi);
        uint32_t allocH = (info.height * fi.heightNum + fi.heightDen - 1) / fi.heightDen;
        /* the ion system heap rounds up to pages anyway; do it here so size
         * and mmap cover exactly what the heap holds */
        size_t len = (static_cast<size_t>(pitch) * allocH + 4095) & ~static_cast<size_t>(4095);
        int flags = ION_FLAG_CACHED_ | ION_FLAG_CACHED_NEEDS_SYNC_;
        int dmaFd = IonAllocBuffer(len, ION_HEAP_SYSTEM_SAGIT_, flags);
        if (dmaFd < 0) {
            /* some kernels export the plain ION_HEAP_SYSTEM_MASK instead */
            dmaFd = IonAllocBuffer(len, 1u << 0, flags);
        }
        if (dmaFd < 0) {
            return DISPLAY_NOMEM_;
        }
        auto *bh = NewHandle(dmaFd, len, static_cast<int32_t>(pitch), info.width, info.height, info.format,
            info.usage);
        if (bh == nullptr) {
            close(dmaFd);
            return DISPLAY_NOMEM_;
        }
        handle = bh;
        SLOGI("ion alloc %{public}ux%{public}u fmt %{public}u -> fd %{public}d stride %{public}u size %{public}llu",
            info.width, info.height, info.format, dmaFd, pitch, (unsigned long long)len);
        SKLOG("ion %ux%u fmt %u -> fd %d pitch %u size %llu", info.width, info.height, info.format, dmaFd,
            pitch, (unsigned long long)len);
        return DISPLAY_SUCCESS_;
    }

    static bool KeyOf(const BufferHandle &h, MetaKey &k)
    {
        struct stat st;
        if (h.fd < 0 || fstat(h.fd, &st) != 0) {
            return false;
        }
        k = {st.st_dev, st.st_ino};
        return true;
    }
    std::mutex mutex_;
    std::map<MetaKey, std::map<uint32_t, std::vector<uint8_t>>> meta_;
};
} // namespace

extern "C" IDisplayBufferVdi *CreateDisplayBufferVdi()
{
    SLOGI("sweet display buffer VDI (DRM dumb + PRIME, ION fallback)");
    SKLOG("buffer vdi loaded");
    return new SweetBufferVdi();
}

extern "C" void DestroyDisplayBufferVdi(IDisplayBufferVdi *vdi)
{
    delete vdi;
}
