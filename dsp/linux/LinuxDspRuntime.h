/* Linux ownership bridge; DSP algorithms and their audio histories stay original. */
#pragma once
#include "RealtimeRegistry.h"
#include "RealtimeQnt.h"
#include <atomic>
#include <memory>

class DfxDspPrivate;

namespace fxsound {
// One producer and one consumer. Each owns a slot; the third slot is exchanged.
// A slow consumer takes the latest complete value, without retries or waiting.
template<class T> class LatestValue {
public:
    T& writeBuffer() noexcept { return slots_[back_]; }
    void publish() noexcept { back_ = middle_.exchange(back_ | dirty, std::memory_order_acq_rel) & indexMask; }
    const T& read() noexcept {
        if (middle_.load(std::memory_order_acquire) & dirty)
            front_ = middle_.exchange(front_, std::memory_order_acq_rel) & indexMask;
        return slots_[front_];
    }
private:
    static constexpr unsigned dirty = 4, indexMask = 3;
    static_assert(std::atomic<unsigned>::is_always_lock_free, "Audio handoff must be lock-free");
    std::array<T, 3> slots_{};
    std::atomic<unsigned> middle_{1};
    unsigned back_ = 2;  // producer only
    unsigned front_ = 0; // consumer only
};

struct DspControl {
    int bands = 31;
    int eqOn = 1;
    float balance = 0, normalization = 0, gain = 0, q = 1;
    std::array<float, 31> frequency{}, boost{};
};
struct DspSettings {
    RegistryImage registry;
    DspControl control;
    uint64_t revision = 0;
    uint32_t reset = 0;
};
struct DspMeters {
    std::array<float, 10> spectrum{};
    uint32_t processed = 0, reset = 0;
};
}

class LinuxDspRuntime {
public:
    explicit LinuxDspRuntime(DfxDspPrivate& control);
    ~LinuxDspRuntime();
    // Control-thread methods (the facade serializes producers/consumers).
    void publish(DfxDspPrivate& control);
    uint32_t processedTime();
    void resetTime(DfxDspPrivate& control);
    void spectrum(float* values, int count);
    // Audio-thread methods. Engine lifetime must extend beyond the last call.
    int format(int bits, int channels, int rate, int validBits);
    int process(short* input, short* output, int frames, int duplicate);
private:
    const fxsound::DspSettings& synchronize();
    fxsound::LatestValue<fxsound::DspSettings> settings_;
    fxsound::LatestValue<fxsound::DspMeters> meters_;
    fxsound::RegistryImage registry_;
    fxsound::QntStorage qntStorage_;
    std::unique_ptr<DfxDspPrivate> engine_;
    fxsound::DspControl applied_{};
    uint64_t nextRevision_ = 0; // control only
    uint32_t reset_ = 0;       // control only
    uint64_t registryRevision_ = 0, appliedRevision_ = 0; // audio only
    uint32_t appliedReset_ = 0; // audio only
};
