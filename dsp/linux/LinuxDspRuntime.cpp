/* FxSound Linux: block-boundary configuration, original persistent audio engine. */
#include "LinuxDspRuntime.h"
#include "u_DfxDsp.h"
#include "dfxp.h"
#include <algorithm>
#include <stdexcept>

LinuxDspRuntime::LinuxDspRuntime(DfxDspPrivate& control)
{
    publish(control);
    synchronize();
    // Initialization/allocation happens here, before attachment to PipeWire.
    // The second engine is audio-owned; it never touches the persistent store.
    fxsound::RegistryScope scope(registry_);
    fxsound::QntScope qntScope(qntStorage_);
    engine_ = std::make_unique<DfxDspPrivate>();
}
LinuxDspRuntime::~LinuxDspRuntime() {
    fxsound::QntScope scope(qntStorage_);
    engine_.reset();
}

void LinuxDspRuntime::publish(DfxDspPrivate& control)
{
    auto& state = settings_.writeBuffer();
    control.captureRealtimeControl(state.control);
    if (!fxsound::captureRegistry(state.registry))
        throw std::runtime_error("DSP settings exceed the bounded realtime settings capacity");
    state.revision = ++nextRevision_;
    state.reset = reset_;
    settings_.publish();
}

const fxsound::DspSettings& LinuxDspRuntime::synchronize()
{
    const auto& state = settings_.read();
    if (state.revision != registryRevision_) {
        registry_ = state.registry;
        registryRevision_ = state.revision;
    }
    return state;
}

int LinuxDspRuntime::format(int bits, int channels, int rate, int validBits)
{
    synchronize();
    fxsound::RegistryScope scope(registry_);
    fxsound::QntScope qntScope(qntStorage_);
    return engine_->setSignalFormat(bits, channels, rate, validBits);
}

int LinuxDspRuntime::process(short* input, short* output, int frames, int duplicate)
{
    const auto& state = synchronize();
    fxsound::RegistryScope scope(registry_);
    fxsound::QntScope qntScope(qntStorage_);
    if (state.revision != appliedRevision_) {
        if (engine_->applyRealtimeControl(state.control, appliedRevision_ ? &applied_ : nullptr) != OKAY)
            return NOT_OKAY;
        applied_ = state.control;
        appliedRevision_ = state.revision;
    }
    if (state.reset != appliedReset_) {
        engine_->resetTotalAudioProcessedTime();
        appliedReset_ = state.reset;
    }
    int result = engine_->processAudio(input, output, frames, duplicate);
    auto& meters = meters_.writeBuffer();
    engine_->getSpectrumBandValues(meters.spectrum.data(), static_cast<int>(meters.spectrum.size()));
    meters.processed = engine_->getTotalAudioProcessedTime();
    meters.reset = appliedReset_;
    meters_.publish();
    return (registry_.failed || qntStorage_.failed) ? NOT_OKAY : result;
}

uint32_t LinuxDspRuntime::processedTime()
{
    const auto& meters = meters_.read();
    return meters.reset == reset_ ? meters.processed : 0;
}
void LinuxDspRuntime::resetTime(DfxDspPrivate& control) { ++reset_; publish(control); }
void LinuxDspRuntime::spectrum(float* values, int count)
{
    if (!values || count <= 0) return;
    const auto& meters = meters_.read();
    for (int i = 0; i < count; ++i) values[i] = i < 10 ? meters.spectrum[i] : 0.f;
}

void DfxDspPrivate::captureRealtimeControl(fxsound::DspControl& state)
{
    state.bands = getNumEqBands();
    state.balance = getBalance();
    state.normalization = getNormalization();
    state.gain = getMasterGain();
    state.q = getFilterQ();
    dfxpEqGetProcessingOn(dfxp_handle_, DFXP_STORAGE_TYPE_REGISTRY, &state.eqOn);
    for (int band = 0; band < state.bands; ++band) {
        state.frequency[band] = getEqBandFrequency(band);
        state.boost[band] = getEqBandBoostCut(band);
    }
}

int DfxDspPrivate::applyRealtimeControl(const fxsound::DspControl& state, const fxsound::DspControl* old)
{
    bool bandsChanged = !old || old->bands != state.bands;
    bool qChanged = !old || old->q != state.q;
    if (bandsChanged) setNumBands(state.bands);
    if (qChanged) setFilterQ(state.q);
    if (!old || old->balance != state.balance) setBalance(state.balance);
    if (!old || old->normalization != state.normalization) setNormalization(state.normalization);
    if (!old || old->gain != state.gain) setMasterGain(state.gain);
    if (dfxpEqSetProcessingOn(dfxp_handle_, DFXP_STORAGE_TYPE_MEMORY, state.eqOn) != OKAY) return NOT_OKAY;
    for (int band = 0; band < state.bands; ++band) {
        if (bandsChanged || qChanged || old->frequency[band] != state.frequency[band])
            setEqBandFrequency(band, state.frequency[band]);
        if (bandsChanged || qChanged || old->boost[band] != state.boost[band])
            if (dfxpEqSetBandBoostCut(dfxp_handle_, DFXP_STORAGE_TYPE_MEMORY, band + 1, state.boost[band]) != OKAY)
                return NOT_OKAY;
    }
    // The immutable registry image contains the exact original MIDI/button
    // values. Reuse the original mappings, without a second quantization pass.
    if (dfxpCommunicateAllNonFixed(dfxp_handle_, IS_FALSE) != OKAY) return NOT_OKAY;
    update_from_registry_ = false;
    return OKAY;
}
