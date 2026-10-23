/* FxSound: exercise the real GUI/audio calling pattern under ThreadSanitizer. */
#include "DfxDsp.h"
#include <array>
#include <atomic>
#include <cmath>
#include <thread>

int main()
{
    DfxDsp dsp;
    dsp.setSignalFormat(32, 2, 48000, 32);
    dsp.powerOn(true);
    for (int effect = 0; effect < DfxDsp::NumEffects; ++effect)
        dsp.setEffectValue(static_cast<DfxDsp::Effect>(effect), 5.0f);
    std::array<float, 512> input{}, output{};
    for (int frame = 0; frame < 256; ++frame)
        input[frame * 2] = input[frame * 2 + 1] = 0.2f * std::sin(frame * 0.1f);
    std::atomic<bool> processing{false}, done{false};
    std::atomic<int> blocks{0};
    std::atomic<bool> good{true};
    std::thread audio([&] {
        processing.store(true, std::memory_order_release);
        do {
            int block = blocks.fetch_add(1, std::memory_order_relaxed);
            if (block % 128 == 0) dsp.setSignalFormat(32, 2, block % 256 ? 44100 : 48000, 32);
            if (dsp.processAudio(reinterpret_cast<short*>(input.data()),
                             reinterpret_cast<short*>(output.data()), 256, 0) != 0) good.store(false);
            for (float value : output) if (!std::isfinite(value)) good.store(false);
        } while (!done.load(std::memory_order_acquire));
    });
    while (!processing.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int change = 0; change < 150; ++change) {
        dsp.setNumBands(change % 3 == 0 ? 5 : change % 3 == 1 ? 10 : 31);
        dsp.setFilterQ(change % 2 ? 1.f : 2.f);
        dsp.setEqBandBoostCut(2, change % 2 ? 6.0f : -6.0f);
        dsp.setEqBandFrequency(2, change % 2 ? 500.0f : 1000.0f);
        dsp.setEffectValue(DfxDsp::Bass, static_cast<float>(change % 11));
        dsp.powerOn(change % 3 != 0);
        dsp.setBalance(change % 2 ? 3.f : -3.f);
        dsp.setNormalization(change % 2 ? -3.f : 0.f);
        dsp.setMasterGain(change % 2 ? -2.f : 0.f);
        dsp.eqOn(change % 2 == 0);
        dsp.getNumEqBands();
        dsp.getEqBandBoostCut(2);
        dsp.getEqBandFrequency(2);
        dsp.getTotalAudioProcessedTime();
        if (change % 10 == 0) dsp.resetTotalAudioProcessedTime();
        std::array<float, 10> spectrum{};
        dsp.getSpectrumBandValues(spectrum.data(), static_cast<int>(spectrum.size()));
    }
    done.store(true, std::memory_order_release);
    audio.join();
    return good.load() && blocks.load() > 1 ? 0 : 1;
}
