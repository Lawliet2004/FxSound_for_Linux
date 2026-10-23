/*
FxSound
Copyright (C) 2025  FxSound LLC

Contributors:
	www.theremino.com (2025)
	
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "DfxDsp.h"
#include "u_DfxDsp.h"
#ifdef __linux__
#include "linux/LinuxDspRuntime.h"
#define FX_CONTROL std::lock_guard<std::mutex> controlGuard(control_mutex_)
#define FX_PUBLISH runtime_->publish(*data_)
#else
#define FX_CONTROL
#define FX_PUBLISH
#endif

DfxDsp::DfxDsp()
{
#ifdef __linux__
    auto control = std::make_unique<DfxDspPrivate>();
    runtime_ = new LinuxDspRuntime(*control);
    data_ = control.release();
    power_on_.store(data_->isPowerOn(), std::memory_order_relaxed);
#else
    data_ = new DfxDspPrivate();
#endif
}


DfxDsp::~DfxDsp()
{
#ifdef __linux__
    delete runtime_;
#endif
	delete data_;
}

void DfxDsp::powerOn(bool on)
{
	FX_CONTROL;
	data_->powerOn(on);
	FX_PUBLISH;
#ifdef __linux__
    power_on_.store(on, std::memory_order_release);
#endif
}

bool DfxDsp::isPowerOn()
{
#ifdef __linux__
    return power_on_.load(std::memory_order_acquire);
#else
	return data_->isPowerOn();
#endif
}

float DfxDsp::getEffectValue(Effect effect)
{
	FX_CONTROL;
	return data_->getEffectValue(effect);
}

void DfxDsp::setEffectValue(Effect effect, float value)
{
	FX_CONTROL;
	data_->setEffectValue(effect, value);
	FX_PUBLISH;
}

int DfxDsp::loadPreset(std::wstring preset_file_full_path)
{
	FX_CONTROL;
	int result = data_->loadPreset(preset_file_full_path);
    if (result == OKAY) { FX_PUBLISH; }
    return result;
}

int DfxDsp::savePreset(std::wstring preset_name, std::wstring preset_file_full_path)
{
	FX_CONTROL;
	return data_->savePreset(preset_name, preset_file_full_path);
}

int DfxDsp::exportPreset(std::wstring preset_source_file_full_path, std::wstring preset_name, std::wstring preset_export_path)
{
	FX_CONTROL;
	int result = data_->exportPreset(preset_source_file_full_path, preset_name, preset_export_path);
    if (result == OKAY) { FX_PUBLISH; }
    return result;
}

void DfxDsp::eqOn(bool on)
{
	FX_CONTROL;
	data_->eqOn(on);
	FX_PUBLISH;
}

int DfxDsp::getNumEqBands()
{
	FX_CONTROL;
	return data_->getNumEqBands();
}

DfxPreset DfxDsp::getPresetInfo(std::wstring preset_file_full_path)
{
	FX_CONTROL;
	return data_->getPresetInfo(preset_file_full_path);
}

uint32_t DfxDsp::getTotalAudioProcessedTime()
{
	FX_CONTROL;
#ifdef __linux__
    return runtime_->processedTime();
#else
	return data_->getTotalAudioProcessedTime();
#endif
}

void DfxDsp::resetTotalAudioProcessedTime()
{
	FX_CONTROL;
#ifdef __linux__
    runtime_->resetTime(*data_);
#else
	data_->resetTotalAudioProcessedTime();
#endif
}

int DfxDsp::setSignalFormat(int i_bps, int i_nch, int i_srate, int i_valid_bits)
{
#ifdef __linux__
    return runtime_->format(i_bps, i_nch, i_srate, i_valid_bits);
#else
	if (data_->being_destroyed_)
	{
		return OKAY;
	} else
	{
		return data_->setSignalFormat(i_bps, i_nch, i_srate, i_valid_bits);
	}
#endif
}

int DfxDsp::processAudio(short int *si_input_samples, short int *si_output_samples, int i_num_sample_sets, int i_check_for_duplicate_buffers)
{
#ifdef __linux__
    return runtime_->process(si_input_samples, si_output_samples, i_num_sample_sets, i_check_for_duplicate_buffers);
#else
	if (data_->being_destroyed_)
	{
		return OKAY;
	}
	else
	{
		return data_->processAudio(si_input_samples, si_output_samples, i_num_sample_sets, i_check_for_duplicate_buffers);
	}
#endif
}

float DfxDsp::getBalance()
{
	FX_CONTROL;
	return data_->getBalance();
}

void DfxDsp::setBalance(float gain_db)
{
	FX_CONTROL;
	data_->setBalance(gain_db);
	FX_PUBLISH;
}

float DfxDsp::getNormalization()
{
	FX_CONTROL;
	return data_->getNormalization();
}

void DfxDsp::setNormalization(float gain_db)
{
	FX_CONTROL;
	data_->setNormalization(gain_db);
	FX_PUBLISH;
}

float DfxDsp::getMasterGain()
{
	FX_CONTROL;
	return data_->getMasterGain();
}

void DfxDsp::setMasterGain(float gain_db)
{
	FX_CONTROL;
	data_->setMasterGain(gain_db);
	FX_PUBLISH;
}

float DfxDsp::getFilterQ()
{
	FX_CONTROL;
	return data_->getFilterQ();
}

void DfxDsp::setFilterQ(float q_multiplier)
{
	FX_CONTROL;
	data_->setFilterQ(q_multiplier);
	FX_PUBLISH;
}

void DfxDsp::setNumBands(int num_bands)
{
	FX_CONTROL;
	data_->setNumBands(num_bands);
	FX_PUBLISH;
}

float DfxDsp::getEqBandFrequency(int band_num)
{
	FX_CONTROL;
	return data_->getEqBandFrequency(band_num);
}

void DfxDsp::setEqBandFrequency(int band_num, float freq)
{
	FX_CONTROL;
    data_->setEqBandFrequency(band_num, freq);
	FX_PUBLISH;
}

void DfxDsp::getEqBandFrequencyRange(int band_num, float* min_freq, float* max_freq)
{
	FX_CONTROL;
    data_->getEqBandFrequencyRange(band_num, min_freq, max_freq);
}

float DfxDsp::getEqBandBoostCut(int band_num)
{
	FX_CONTROL;
	return data_->getEqBandBoostCut(band_num);
}

void DfxDsp::setEqBandBoostCut(int band_num, float boost)
{
	FX_CONTROL;
	data_->setEqBandBoostCut(band_num, boost);
	FX_PUBLISH;
}

void DfxDsp::getSpectrumBandValues(float* rp_band_values, int i_array_size)
{
	FX_CONTROL;
#ifdef __linux__
    runtime_->spectrum(rp_band_values, i_array_size);
#else
    data_->getSpectrumBandValues(rp_band_values, i_array_size);
#endif
}

