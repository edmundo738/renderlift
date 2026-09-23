// RenderLift — ALRR Core: frame timing and rolling statistics.
//
// The dynamic resolution controller eats FrameSamples; this module is how the
// host measures them (CPU-side frame time here; GPU busy% comes from backend
// telemetry later). Header-only, allocation-free after construction.
#pragma once

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace rl::prof {

class RollingAverage {
public:
    explicit RollingAverage(std::size_t window) : buffer_(window, 0.0) {
        if (window == 0) {
            throw std::invalid_argument("RollingAverage: window must be at least 1");
        }
    }

    void push(double value) {
        sum_ -= buffer_[index_];
        buffer_[index_] = value;
        sum_ += value;
        index_ = (index_ + 1) % buffer_.size();
        if (count_ < buffer_.size()) ++count_;
    }

    void reset() {
        sum_ = 0.0;
        index_ = 0;
        count_ = 0;
        std::fill(buffer_.begin(), buffer_.end(), 0.0);
    }

    [[nodiscard]] double mean() const { return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_); }
    [[nodiscard]] std::size_t count() const { return count_; }
    [[nodiscard]] std::size_t window() const { return buffer_.size(); }

private:
    std::vector<double> buffer_;
    std::size_t index_ = 0;
    std::size_t count_ = 0;
    double sum_ = 0.0;
};

class FrameTimer {
public:
    explicit FrameTimer(std::size_t window = 120) : average_(window) {}

    void begin() { start_ = clock::now(); }

    void end() {
        const auto elapsed =
            std::chrono::duration<double, std::milli>(clock::now() - start_).count();
        lastMs_ = elapsed;
        average_.push(elapsed);
    }

    // Convenience for platforms where begin/end can't bracket the frame.
    void recordFrameMs(double ms) {
        lastMs_ = ms;
        average_.push(ms);
    }

    [[nodiscard]] double lastFrameMs() const { return lastMs_; }
    [[nodiscard]] double averageFrameMs() const { return average_.mean(); }
    [[nodiscard]] double fps() const {
        const double avg = average_.mean();
        return avg > 0.0 ? 1000.0 / avg : 0.0;
    }

private:
    using clock = std::chrono::steady_clock;

    clock::time_point start_{};
    RollingAverage average_;
    double lastMs_ = 0.0;
};

}  // namespace rl::prof
