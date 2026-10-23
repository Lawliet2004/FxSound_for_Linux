/* Deterministic serial trace for checking Linux handoff against the original engine. */
#include "DfxDsp.h"
#ifdef FXSOUND_SERIAL_REFERENCE
#include "u_DfxDsp.h"
using Engine = DfxDspPrivate;
#else
using Engine = DfxDsp;
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    Engine dsp;
    dsp.setNumBands(10);
    dsp.powerOn(true);
    for (int effect = 0; effect < DfxDsp::NumEffects; ++effect)
        dsp.setEffectValue(static_cast<DfxDsp::Effect>(effect), 3.f + effect);
    for (int band = 0; band < 10; ++band) dsp.setEqBandBoostCut(band, 0.f);
    dsp.setSignalFormat(32, 2, 48000, 32);
    std::ofstream file(argv[1], std::ios::binary);
    std::array<float, 1024> input{}, output{};
    uint32_t noise = 17;
    for (int block = 0; block < 256; ++block) {
        if (block == 20) dsp.setEffectValue(DfxDsp::Ambience, 8.f);
        if (block == 40) dsp.setEqBandBoostCut(2, 6.f);
        if (block == 60) dsp.setEqBandFrequency(2, 900.f);
        if (block == 80) dsp.setEffectValue(DfxDsp::DynamicBoost, 1.f);
        if (block == 100) dsp.powerOn(false);
        if (block == 120) dsp.powerOn(true);
        if (block == 140) dsp.setFilterQ(2.f);
        if (block == 160) dsp.setNumBands(31);
        if (block == 180) { dsp.setBalance(3.f); dsp.setMasterGain(-2.f); }
        if (block == 200) dsp.setNormalization(-6.f);
        if (block == 220) dsp.setSignalFormat(32, 2, 44100, 32);
        if (block == 240) dsp.setSignalFormat(32, 2, 96000, 32);
        for (int frame = 0; frame < 512; ++frame) {
            noise = noise * 1664525u + 1013904223u;
            float n = (static_cast<int32_t>(noise) / 2147483648.f) * .04f;
            input[frame * 2] = n + .12f * std::sin((block * 512 + frame) * .091f);
            input[frame * 2 + 1] = -n + .09f * std::sin((block * 512 + frame) * .057f);
        }
        if (block == 10) input[0] = .8f;
        if (dsp.processAudio(reinterpret_cast<short*>(input.data()),
                             reinterpret_cast<short*>(output.data()), 512, 0) != 0) return 1;
        file.write(reinterpret_cast<const char*>(output.data()), sizeof(output));
    }
    return file ? 0 : 1;
}
