#include "renderlift/alrr/SpatialUpscaler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace rl::alrr {
namespace {

constexpr std::uint32_t kChannels = 4;  // RGBA8

void validate(ConstImageView src, ImageView dst) {
    if (!src.data || !src.width || !src.height) {
        throw std::invalid_argument("ALRR Spatial: source view is empty");
    }
    if (!dst.data || !dst.width || !dst.height) {
        throw std::invalid_argument("ALRR Spatial: destination view is empty");
    }
    const std::uint32_t srcStride = src.stride ? src.stride : src.width * kChannels;
    const std::uint32_t dstStride = dst.stride ? dst.stride : dst.width * kChannels;
    if (srcStride < src.width * kChannels || dstStride < dst.width * kChannels) {
        throw std::invalid_argument("ALRR Spatial: stride smaller than width × 4");
    }
}

// Center-aligned bilinear resample, RGBA8. Works for any scale ratio.
void bilinearResample(ConstImageView src, ImageView dst) {
    const std::uint32_t sStride = src.stride ? src.stride : src.width * kChannels;
    const std::uint32_t dStride = dst.stride ? dst.stride : dst.width * kChannels;
    const double xRatio = static_cast<double>(src.width) / dst.width;
    const double yRatio = static_cast<double>(src.height) / dst.height;

    for (std::uint32_t y = 0; y < dst.height; ++y) {
        const double fy = std::clamp((static_cast<double>(y) + 0.5) * yRatio - 0.5, 0.0,
                                     static_cast<double>(src.height - 1));
        const std::uint32_t y0 = static_cast<std::uint32_t>(fy);
        const std::uint32_t y1 = std::min(y0 + 1, src.height - 1);
        const double ty = fy - static_cast<double>(y0);

        std::uint8_t* dRow = dst.data + static_cast<std::size_t>(y) * dStride;
        const std::uint8_t* sRow0 = src.data + static_cast<std::size_t>(y0) * sStride;
        const std::uint8_t* sRow1 = src.data + static_cast<std::size_t>(y1) * sStride;

        for (std::uint32_t x = 0; x < dst.width; ++x) {
            const double fx = std::clamp((static_cast<double>(x) + 0.5) * xRatio - 0.5, 0.0,
                                         static_cast<double>(src.width - 1));
            const std::uint32_t x0 = static_cast<std::uint32_t>(fx);
            const std::uint32_t x1 = std::min(x0 + 1, src.width - 1);
            const double tx = fx - static_cast<double>(x0);

            for (std::uint32_t c = 0; c < kChannels; ++c) {
                const double p00 = sRow0[x0 * kChannels + c];
                const double p10 = sRow0[x1 * kChannels + c];
                const double p01 = sRow1[x0 * kChannels + c];
                const double p11 = sRow1[x1 * kChannels + c];
                const double top = p00 + (p10 - p00) * tx;
                const double bottom = p01 + (p11 - p01) * tx;
                const double v = top + (bottom - top) * ty;
                dRow[x * kChannels + c] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
        }
    }
}

// Unsharp mask on the destination: out = c + amount × (c − blur3×3(c)).
void unsharpMask(ImageView img, float amount) {
    const std::uint32_t stride = img.stride ? img.stride : img.width * kChannels;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(img.height) * stride);
    for (std::uint32_t y = 0; y < img.height; ++y) {
        std::memcpy(src.data() + static_cast<std::size_t>(y) * stride,
                    img.data + static_cast<std::size_t>(y) * stride,
                    static_cast<std::size_t>(img.width) * kChannels);
    }

    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            for (std::uint32_t c = 0; c < 3; ++c) {  // leave alpha untouched
                double blur = 0.0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const std::uint32_t yy = std::clamp(
                        static_cast<int>(y) + dy, 0, static_cast<int>(img.height) - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const std::uint32_t xx = std::clamp(
                            static_cast<int>(x) + dx, 0, static_cast<int>(img.width) - 1);
                        blur += src[static_cast<std::size_t>(yy) * stride + xx * kChannels + c];
                    }
                }
                blur /= 9.0;
                const std::uint8_t center =
                    src[static_cast<std::size_t>(y) * stride + x * kChannels + c];
                const double v = static_cast<double>(center) +
                                 static_cast<double>(amount) * (static_cast<double>(center) - blur);
                img.data[static_cast<std::size_t>(y) * stride + x * kChannels + c] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
        }
    }
}

}  // namespace

void SpatialUpscaler::apply(ConstImageView src, ImageView dst, const ReconstructParams& params) {
    validate(src, dst);
    bilinearResample(src, dst);
    if (params.sharpening > 0.0f) {
        unsharpMask(dst, std::clamp(params.sharpening, 0.0f, 1.0f));
    }
}

}  // namespace rl::alrr
