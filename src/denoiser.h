#pragma once

#include <rnnoise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "denoise-chain.h"

// The stereo denoiser: RNNoise per channel, the resampling chain, and the Amount mix. Free of CLAP, so that the unit
// tests can call it directly.
namespace plugin {

constexpr double kMinAmount = 0.0;
constexpr double kMaxAmount = 100.0;
constexpr double kDefaultAmount = 100.0;

// One RNNoise state. RNNoise takes and returns samples in the range of 16-bit integers.
class RnnoiseEngine {
public:
	// Allocates. The built-in model is used, so no model file is read.
	RnnoiseEngine() : state_(rnnoise_create(nullptr)) {}
	~RnnoiseEngine()
	{
		if (state_)
			rnnoise_destroy(state_);
	}
	RnnoiseEngine(const RnnoiseEngine &) = delete;
	RnnoiseEngine &operator=(const RnnoiseEngine &) = delete;

	bool valid() const
	{
		return state_ != nullptr;
	}

	// Clears the state in the memory allocated at construction. With the built-in model, rnnoise_init allocates
	// nothing (src/denoise.c of RNNoise 0.2), so this is safe on the audio thread.
	void reset()
	{
		rnnoise_init(state_, nullptr);
	}

	void processFrame(float *frame)
	{
		constexpr float kScale = 32768.0f;
		for (int i = 0; i < denoise::kFrameSize; ++i)
			frame[i] *= kScale;
		rnnoise_process_frame(state_, frame, frame);
		for (int i = 0; i < denoise::kFrameSize; ++i)
			frame[i] *= 1.0f / kScale;
	}

private:
	DenoiseState *state_;
};

// Engine is RnnoiseEngine in the plugin. The tests also use stand-ins with the same interface.
template <class Engine> class StereoDenoiser {
public:
	static constexpr int kChannels = 2;

	// Allocates. Call from activate(), not from the audio thread.
	void prepare(double sampleRate)
	{
		for (int c = 0; c < kChannels; ++c)
			chains_[c].prepare(sampleRate, &engines_[c]);
		// The mix glides over 99 % of a change in about 20 ms, so that automation does not click.
		coefficient_ = float(1.0 - std::exp(-1.0 / (0.0043 * sampleRate)));
		reset();
	}

	// Clears all history. Real-time safe.
	void reset()
	{
		for (int c = 0; c < kChannels; ++c) {
			engines_[c].reset();
			chains_[c].reset();
		}
		current_ = target_;
	}

	void setAmountPercent(double percent)
	{
		target_ = float(std::clamp(percent, kMinAmount, kMaxAmount) / kMaxAmount);
	}

	// Samples by which the output is late. The dry part of the mix is delayed by the same amount.
	uint32_t latency() const
	{
		return uint32_t(chains_[0].latency());
	}

	Engine &engine(int channel)
	{
		return engines_[channel];
	}

	int64_t underruns() const
	{
		return chains_[0].underruns() + chains_[1].underruns();
	}

	float currentAmount() const
	{
		return current_;
	}

	// In place. Real-time safe: no allocation, no locks, no system calls.
	void process(float *left, float *right, uint32_t frames)
	{
		float *channels[kChannels] = {left, right};
		float a = current_;
		for (uint32_t i = 0; i < frames; ++i) {
			a += coefficient_ * (target_ - a);
			for (int c = 0; c < kChannels; ++c) {
				float dry = 0.0f, wet = 0.0f;
				chains_[c].process(channels[c][i], dry, wet);
				channels[c][i] = dry + a * (wet - dry);
			}
		}
		current_ = a;
	}

private:
	std::array<Engine, kChannels> engines_;
	std::array<denoise::Chain<Engine>, kChannels> chains_;
	float target_ = 1.0f;
	float current_ = 1.0f;
	float coefficient_ = 1.0f;
};

} // namespace plugin
