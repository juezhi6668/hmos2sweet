/*
 * Minimal copy of the msm mdp atomic commit UAPI (Linux 4.4, msm8998).
 *
 * Mirrors include/uapi/linux/msm_mdp_ext.h and the helpers it pulls from
 * include/uapi/linux/msm_mdp.h.  Only the parts the fbdev composer submits
 * are kept; field order, widths and alignment are identical to the kernel
 * structures (they travel through MSMFB_ATOMIC_COMMIT verbatim).
 */
#ifndef MDP_ATOMIC_H
#define MDP_ATOMIC_H
#include <cstddef>
#include <cstdint>

namespace OHOS {
namespace HDI {
namespace DISPLAY {
namespace mdp {
constexpr uint32_t MAX_PLANES = 4;
constexpr uint32_t MSMFB_ATOMIC_COMMIT = 0xC0085380; /* _IOWR('S', 128, void *) */

/* validate/commit flags */
constexpr uint32_t MDP_VALIDATE_LAYER = 0x01;
constexpr uint32_t MDP_COMMIT_WAIT_FOR_FINISH = 0x02;
constexpr uint32_t MDP_COMMIT_UPDATE_BRIGHTNESS = 0x40;
constexpr uint32_t MDP_COMMIT_VERSION_1_0 = 0x00010000;

/* input layer flags */
constexpr uint32_t MDP_LAYER_FLIP_LR = 0x1;
constexpr uint32_t MDP_LAYER_FLIP_UD = 0x2;
constexpr uint32_t MDP_LAYER_ENABLE_PIXEL_EXT = 0x4;
constexpr uint32_t MDP_LAYER_FORGROUND = 0x8;
constexpr uint32_t MDP_LAYER_SOLID_FILL = 0x20;

constexpr int32_t BLEND_OP_NOT_DEFINED = 0;
constexpr int32_t BLEND_OP_OPAQUE = 1;
constexpr int32_t BLEND_OP_PREMULTIPLIED = 2;
constexpr int32_t BLEND_OP_COVERAGE = 3;

constexpr int32_t MDP_CSC_ITU_R_601 = 0;
constexpr int32_t MDP_CSC_ITU_R_601_FR = 1;
constexpr int32_t MDP_CSC_ITU_R_709 = 2;

/* stage 0 of the layer stack: mdp_input_layer.z_order carries this offset */
constexpr uint16_t MDSS_MDP_STAGE_0 = 2;

/*
 * Source pipe selection.  mdp_input_layer.pipe_ndx is BIT(sspp index) and the
 * kernel indexes its pipe table with these values (mdss_mdp_pipe_addr_setup:
 * pipe->ndx = BIT(pnums[i])).
 */
constexpr uint32_t MDP_SSPP_VIG0 = 1u << 0;
constexpr uint32_t MDP_SSPP_VIG1 = 1u << 1;
constexpr uint32_t MDP_SSPP_VIG2 = 1u << 2;
constexpr uint32_t MDP_SSPP_RGB0 = 1u << 3;
constexpr uint32_t MDP_SSPP_RGB1 = 1u << 4;
constexpr uint32_t MDP_SSPP_RGB2 = 1u << 5;
constexpr uint32_t MDP_SSPP_DMA0 = 1u << 6;
constexpr uint32_t MDP_SSPP_DMA1 = 1u << 7;
constexpr uint32_t MDP_SSPP_VIG3 = 1u << 8;
constexpr uint32_t MDP_SSPP_RGB3 = 1u << 9;
constexpr uint32_t MDP_SSPP_DMA2 = 1u << 12;
constexpr uint32_t MDP_SSPP_DMA3 = 1u << 13;

/*
 * Image formats.  The uapi comment says "DRM fourcc" but the kernel compares
 * mdp_layer_buffer.format against mdss_mdp_format_map[].format, which is this
 * enum (mdss_mdp_get_format_params -> mdp_format_map).  Values are the enum
 * positions in msm_mdp.h.
 */
constexpr uint32_t MDP_RGB_565 = 0;
constexpr uint32_t MDP_Y_CBCR_H2V2 = 2;
constexpr uint32_t MDP_ARGB_8888 = 4;
constexpr uint32_t MDP_RGB_888 = 5;
constexpr uint32_t MDP_Y_CRCB_H2V2 = 6;
constexpr uint32_t MDP_RGBA_8888 = 13;
constexpr uint32_t MDP_BGRA_8888 = 14;
constexpr uint32_t MDP_RGBX_8888 = 15;
constexpr uint32_t MDP_BGR_565 = 25;
constexpr uint32_t MDP_BGRX_8888 = 28;
constexpr uint32_t MDP_RGBA_5551 = 41;
constexpr uint32_t MDP_RGBA_4444 = 43;

struct mdp_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct mult_factor {
    uint32_t numer;
    uint32_t denom;
};

struct mdp_layer_plane {
    int32_t fd;
    uint32_t offset;
    uint32_t stride;
};

struct mdp_layer_buffer {
    uint32_t width;
    uint32_t height;
    uint32_t format; /* MDP_* enum, not fourcc */
    mdp_layer_plane planes[MAX_PLANES];
    uint32_t plane_count;
    mult_factor comp_ratio;
    int32_t fence;
    uint32_t reserved;
};

struct mdp_input_layer {
    uint32_t flags;
    uint32_t pipe_ndx;
    uint8_t horz_deci;
    uint8_t vert_deci;
    uint8_t alpha;
    uint16_t z_order;
    uint32_t transp_mask;
    uint32_t bg_color;
    int32_t blend_op;
    int32_t color_space;
    mdp_rect src_rect;
    mdp_rect dst_rect;
    void *scale;
    mdp_layer_buffer buffer;
    void *pp_info;
    int32_t error_code;
    uint32_t rect_num;
    uint32_t reserved[5];
};

struct mdp_output_layer {
    uint32_t flags;
    uint32_t writeback_ndx;
    mdp_layer_buffer buffer;
    int32_t color_space;
    uint32_t reserved[5];
};

struct mdp_layer_commit_v1 {
    uint32_t flags;
    int32_t release_fence;
    mdp_rect left_roi;
    mdp_rect right_roi;
    mdp_input_layer *input_layers;
    uint32_t input_layer_cnt;
    mdp_output_layer *output_layer;
    int32_t retire_fence;
    void *dest_scaler;
    uint32_t dest_scaler_cnt;
    uint32_t bl_level;
    /* MDP_LAYER_COMMIT_V1_PAD: 3 here because the kernel picks 3 whenever
     * __LP64 is undefined, which it is for both gcc and clang targets. */
    uint32_t reserved[3];
};

struct mdp_layer_commit {
    uint32_t version;
    mdp_layer_commit_v1 commit_v1;
};

/* ABI check against the kernel build (verified with a probe compiled against
 * include/uapi/linux/msm_mdp_ext.h): a mismatch would corrupt every commit. */
static_assert(sizeof(mdp_layer_plane) == 12, "mdp_layer_plane layout");
static_assert(sizeof(mdp_layer_buffer) == 80, "mdp_layer_buffer layout");
static_assert(sizeof(mdp_input_layer) == 192, "mdp_input_layer layout");
static_assert(sizeof(mdp_layer_commit_v1) == 104, "mdp_layer_commit_v1 layout");
static_assert(sizeof(mdp_layer_commit) == 112, "mdp_layer_commit layout");
static_assert(offsetof(mdp_input_layer, z_order) == 12, "z_order offset");
static_assert(offsetof(mdp_input_layer, transp_mask) == 16, "transp_mask offset");
static_assert(offsetof(mdp_input_layer, src_rect) == 32, "src_rect offset");
static_assert(offsetof(mdp_input_layer, dst_rect) == 48, "dst_rect offset");
static_assert(offsetof(mdp_input_layer, buffer) == 72, "buffer offset");
static_assert(offsetof(mdp_input_layer, error_code) == 160, "error_code offset");
static_assert(offsetof(mdp_layer_buffer, format) == 8, "buffer format offset");
static_assert(offsetof(mdp_layer_buffer, fence) == 72, "buffer fence offset");
static_assert(offsetof(mdp_layer_commit_v1, release_fence) == 4, "release fence offset");
static_assert(offsetof(mdp_layer_commit_v1, input_layers) == 40, "input_layers offset");
static_assert(offsetof(mdp_layer_commit_v1, retire_fence) == 64, "retire fence offset");
static_assert(offsetof(mdp_layer_commit_v1, reserved) == 88, "reserved offset");
} // namespace mdp
} // namespace DISPLAY
} // namespace HDI
} // namespace OHOS

#endif // MDP_ATOMIC_H
