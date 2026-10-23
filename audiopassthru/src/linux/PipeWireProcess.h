/*
FxSound — Linux build: PipeWire realtime process block

The body of the pw_filter process callback, kept free of PipeWire so it can be
driven directly by tests. Takes planar stereo float buffers, runs DfxDsp on an
interleaved copy and hard-clips the result.
*/

#pragma once

#include "DfxDsp.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

class FxProcessor
{
public:
    static constexpr int kChannels = 2;
    // Interleave buffers are sized once for PipeWire's default max quantum so
    // the realtime path never allocates; larger quanta are processed in chunks.
    static constexpr uint32_t kMaxFrames = 8192;

    FxProcessor() : ileave_in_(kMaxFrames * kChannels), ileave_out_(kMaxFrames * kChannels) {}

    void setDsp(DfxDsp* dsp) { dsp_.store(dsp, std::memory_order_release); }
    void setMuted(bool mute) { muted_.store(mute, std::memory_order_relaxed); }

    // in[c] may be null (port not connected); out[c] must be valid for n frames.
    void process(const float* const in[kChannels], float* const out[kChannels], uint32_t n, int rate)
    {
        if (muted_.load(std::memory_order_relaxed)) {
            for (int c = 0; c < kChannels; ++c) std::memset(out[c], 0, n * sizeof(float));
            return;
        }

        bool have_in = in[0] && in[1];
        auto* dsp = dsp_.load(std::memory_order_acquire);
        if (dsp && have_in && !dsp->isPowerOn()) {
            for (int c = 0; c < kChannels; ++c) std::memcpy(out[c], in[c], n * sizeof(float));
            return;
        }
        if (dsp && have_in) {
            // Process in native 32-bit float (PipeWire's format) so there is no
            // 16-bit quantization loss. The DSP must be told the format or it
            // produces no output; bps=32 routes it through the float path.
            if (configured_rate_ != rate || configured_dsp_ != dsp) {
                if (dsp->setSignalFormat(32, kChannels, rate, 32) != 0) {
                    for (int c = 0; c < kChannels; ++c) std::memset(out[c], 0, n * sizeof(float));
                    return;
                }
                configured_rate_ = rate;
                configured_dsp_ = dsp;
            }
            for (uint32_t done = 0; done < n; done += kMaxFrames) {
                uint32_t len = n - done < kMaxFrames ? n - done : kMaxFrames;
                const float* chunk_in[kChannels] = {in[0] + done, in[1] + done};
                float* chunk_out[kChannels] = {out[0] + done, out[1] + done};
                processChunk(*dsp, chunk_in, chunk_out, len);
            }
        } else if (have_in) {
            for (int c = 0; c < kChannels; ++c) std::memcpy(out[c], in[c], n * sizeof(float));
        } else {
            for (int c = 0; c < kChannels; ++c) std::memset(out[c], 0, n * sizeof(float));
        }
    }

private:
    void processChunk(DfxDsp& dsp, const float* const in[kChannels], float* const out[kChannels], uint32_t n)
    {
        for (uint32_t i = 0; i < n; ++i) {
            ileave_in_[i*2]     = in[0][i];
            ileave_in_[i*2 + 1] = in[1][i];
        }
        // processAudio takes short*; for bps=32 the engine reinterprets the
        // buffers as 32-bit float internally.
        if (dsp.processAudio(reinterpret_cast<short*>(ileave_in_.data()),
                             reinterpret_cast<short*>(ileave_out_.data()), (int)n, 0) != 0) {
            for (int c = 0; c < kChannels; ++c) std::memset(out[c], 0, n * sizeof(float));
            return;
        }
        for (uint32_t i = 0; i < n; ++i) {
            float l = ileave_out_[i*2];
            float r = ileave_out_[i*2 + 1];
            // Final hard-clip to prevent digital distortion reaching PipeWire/mixer.
            if (l > 1.0f) l = 1.0f; else if (l < -1.0f) l = -1.0f;
            if (r > 1.0f) r = 1.0f; else if (r < -1.0f) r = -1.0f;
            out[0][i] = l;
            out[1][i] = r;
        }
    }

    static_assert(std::atomic<DfxDsp*>::is_always_lock_free, "Realtime attachment requires a lock-free pointer");
    // Attachment publishes a fully constructed engine. Its owner must keep it
    // alive until the PipeWire filter has stopped (as in application shutdown).
    std::atomic<DfxDsp*> dsp_{nullptr};
    DfxDsp* configured_dsp_ = nullptr; // used only by the audio thread
    std::atomic<bool> muted_{false};
    int configured_rate_ = 0;   // last rate passed to dsp->setSignalFormat
    std::vector<float> ileave_in_;     // interleaved L/R, 32-bit float
    std::vector<float> ileave_out_;
};
