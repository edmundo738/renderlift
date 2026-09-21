#include "renderlift/alrr/EdgeUpscaler.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace rl::alrr {
namespace {

constexpr std::uint32_t kChannels = 4;  // RGBA8

// Luma gradient window (8-bit luma units): below kEdgeLow the area is
// considered flat (no sharpening); above kEdgeHigh it is full-on edge.
constexpr float kEdgeLow = 8.0f;
constexpr float kEdgeHigh = 48.0f;

void validate(ConstImageView src, ImageView dst) {
    if (!src.data || !src.width || !src.height) {
        throw std::invalid_argument("ALRR Edge: source view is empty");
    }
    if (!dst.data || !dst.width || !dst.height) {
        throw std::invalid_argument("ALRR Edge: destination view is empty");
    }
}

[[nodiscard]] float luma(const std::uint8_t* p) {
    return 0.2126f * static_cast<float>(p[0]) + 0.7152f * static_cast<float>(p[1]) +
           0.0722f * static_cast<float>(p[2]);
}

// Per-source-pixel luma gradient magnitude (central differences).
[[nodiscard]] std::vector<float> lumaGradientMap(ConstImageView src) {
    const std::uint32_t sStride = src.stride ? src.stride : src.width * kChannels;
    std::vector<float> grad(static_cast<std::size_t>(src.width) * src.height, 0.0f);

    for (std::uint32_t y = 0; y < src.height; ++y) {
        const std::uint32_t yp = y > 0 ? y - 1 : y;
        const std::uint32_t yn = std::min(y + 1, src.height - 1);
        for (std::uint32_t x = 0; x < src.width; ++x) {
            const std::uint32_t xp = x > 0 ? x - 1 : x;
            const std::uint32_t xn = std::min(x + 1, src.width - 1);
            const float gx =
                luma(src.data + static_cast<std::size_t>(y) * sStride + xn * kChannels) -
                luma(src.data + static_cast<std::size_t>(y) * sStride + xp * kChannels);
            const float gy =
                luma(src.data + static_cast<std::size_t>(yn) * sStride + x * kChannels) -
                luma(src.data + static_cast<std::size_t>(yp) * sStride + x * kChannels);
            grad[static_cast<std::size_t>(y) * src.width + x] = std::sqrt(gx * gx + gy * gy) * 0.5f;
        }
    }
    return grad;
}

[[nodiscard]] float sampleGradient(const std::vector<float>& grad, std::uint32_t w, std::uint32_t h,
                                   double fx, double fy) {
    fx = std::clamp(fx, 0.0, static_cast<double>(w - 1));
    fy = std::clamp(fy, 0.0, static_cast<double>(h - 1));
    const auto x0 = static_cast<std::uint32_t>(fx);
    const auto y0 = static_cast<std::uint32_t>(fy);
    const std::uint32_t x1 = std::min(x0 + 1, w - 1);
    const std::uint32_t y1 = std::min(y0 + 1, h - 1);
    const double tx = fx - static_cast<double>(x0);
    const double ty = fy - static_cast<double>(y0);
    const double g00 = grad[static_cast<std::size_t>(y0) * w + x0];
    const double g10 = grad[static_cast<std::size_t>(y0) * w + x1];
    const double g01 = grad[static_cast<std::size_t>(y1) * w + x0];
    const double g11 = grad[static_cast<std::size_t>(y1) * w + x1];
    const double top = g00 + (g10 - g00) * tx;
    const double bottom = g01 + (g11 - g01) * tx;
    return static_cast<float>(top + (bottom - top) * ty);
}

}  // namespace

void EdgeUpscaler::apply(ConstImageView src, ImageView dst, const ReconstructParams& params) {
    validate(src, dst);

    const std::uint32_t sStride = src.stride ? src.stride : src.width * kChannels;
    const std::uint32_t dStride = dst.stride ? dst.stride : dst.width * kChannels;
    const float sharpen = std::clamp(params.sharpening, 0.0f, 1.0f);

    const std::vector<float> grad = lumaGradientMap(src);

    // Pass 1: center-aligned bilinear resample (same as ALRR Spatial).
    const double xRatio = static_cast<double>(src.width) / dst.width;
    const double yRatio = static_cast<double>(src.height) / dst.height;
    for (std::uint32_t y = 0; y < dst.height; ++y) {
        const double fy = std::clamp((static_cast<double>(y) + 0.5) * yRatio - 0.5, 0.0,
                                     static_cast<double>(src.height - 1));
        const auto y0 = static_cast<std::uint32_t>(fy);
        const std::uint32_t y1 = std::min(y0 + 1, src.height - 1);
        const double ty = fy - static_cast<double>(y0);

        std::uint8_t* dRow = dst.data + static_cast<std::size_t>(y) * dStride;
        const std::uint8_t* sRow0 = src.data + static_cast<std::size_t>(y0) * sStride;
        const std::uint8_t* sRow1 = src.data + static_cast<std::size_t>(y1) * sStride;

        for (std::uint32_t x = 0; x < dst.width; ++x) {
            const double fx = std::clamp((static_cast<double>(x) + 0.5) * xRatio - 0.5, 0.0,
                                         static_cast<double>(src.width - 1));
            const auto x0 = static_cast<std::uint32_t>(fx);
            const std::uint32_t x1 = std::min(x0 + 1, src.width - 1);
            const double tx = fx - static_cast<double>(x0);

            for (std::uint32_t c = 0; c < kChannels; ++c) {
                const double top =
                    sRow0[x0 * kChannels + c] +
                    (static_cast<double>(sRow0[x1 * kChannels + c]) - sRow0[x0 * kChannels + c]) * tx;
                const double bottom =
                    sRow1[x0 * kChannels + c] +
                    (static_cast<double>(sRow1[x1 * kChannels + c]) - sRow1[x0 * kChannels + c]) * tx;
                dRow[x * kChannels + c] = static_cast<std::uint8_t>(
                    std::clamp(std::lround(top + (bottom - top) * ty), 0L, 255L));
            }
        }
    }

    if (sharpen <= 0.0f) return;

    // Snapshot for the 3×3 high-pass reads.
    std::vector<std::uint8_t> base(static_cast<std::size_t>(dst.height) * dStride);
    for (std::uint32_t y = 0; y < dst.height; ++y) {
        std::memcpy(base.data() + static_cast<std::size_t>(y) * dStride,
                    dst.data + static_cast<std::size_t>(y) * dStride,
                    static_cast<std::size_t>(dst.width) * kChannels);
    }

    // Pass 2: unsharp mask modulated by the (resampled) luma gradient.
    for (std::uint32_t y = 0; y < dst.height; ++y) {
        const double fy = (static_cast<double>(y) + 0.5) * yRatio - 0.5;
        for (std::uint32_t x = 0; x < dst.width; ++x) {
            const double fx = (static_cast<double>(x) + 0.5) * xRatio - 0.5;
            const float g = sampleGradient(grad, src.width, src.height, fx, fy);
            const float weight =
                std::clamp((g - kEdgeLow) / (kEdgeHigh - kEdgeLow), 0.0f, 1.0f);
            const float amount = sharpen * weight;
            if (amount <= 0.0f) continue;

            for (std::uint32_t c = 0; c < 3; ++c) {
                double blur = 0.0;
                for (int dy = -1; dy <= 1; ++dy) {
                    const std::uint32_t yy = std::clamp(
                        static_cast<int>(y) + dy, 0, static_cast<int>(dst.height) - 1);
                    for (int dx = -1; dx <= 1; ++dx) {
                        const std::uint32_t xx = std::clamp(
                            static_cast<int>(x) + dx, 0, static_cast<int>(dst.width) - 1);
                        blur += base[static_cast<std::size_t>(yy) * dStride + xx * kChannels + c];
                    }
                }
                blur /= 9.0;
                const std::uint8_t center =
                    base[static_cast<std::size_t>(y) * dStride + x * kChannels + c];
                const double v = static_cast<double>(center) +
                                 static_cast<double>(amount) * (static_cast<double>(center) - blur);
                dst.data[static_cast<std::size_t>(y) * dStride + x * kChannels + c] =
                    static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            }
        }
    }
}

}  // namespace rl::alrr
