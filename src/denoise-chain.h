#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// The signal path around a 48 kHz frame-based denoiser, kept free of CLAP so that unit tests can call it.
//
// Host rate r -> 48 kHz (windowed-sinc interpolation) -> frames of 480 samples -> engine -> 48 kHz -> host rate.
// Every resampled sample is computed at an exact point in time, so the output is the input delayed by exactly
// latency() host samples, at every host rate. The dry signal is the input delayed by the same amount, so a mix of
// dry and wet lines up. At 48 kHz the resampling is skipped.
namespace denoise {

constexpr double kEngineRate = 48000.0;
constexpr int kFrameSize = 480;
// RNNoise 0.2 outputs frame n as the reconstruction of input frame n - 2 (src/denoise.c, commit 84fe83bd).
constexpr int kEngineDelay = 2 * kFrameSize;

// Kaiser-windowed sinc, tabulated. at(phi) takes phi in zero crossings.
class Kernel {
public:
	static constexpr int kZeroCrossings = 16;
	static constexpr int kStepsPerCrossing = 512;

	void build()
	{
		constexpr double kBeta = 8.0;
		const int size = kZeroCrossings * kStepsPerCrossing + 2;
		table_.assign(size, 0.0f);
		const double norm = besselI0(kBeta);
		for (int i = 0; i < size; ++i) {
			const double phi = double(i) / kStepsPerCrossing;
			if (phi >= kZeroCrossings)
				break;
			const double x = phi / kZeroCrossings;
			const double window = besselI0(kBeta * std::sqrt(1.0 - x * x)) / norm;
			const double sinc = phi == 0.0 ? 1.0 : std::sin(kPi * phi) / (kPi * phi);
			table_[i] = float(sinc * window);
		}
	}

	float at(double phi) const
	{
		const double pos = std::fabs(phi) * kStepsPerCrossing;
		const auto i = int(pos);
		if (i >= int(table_.size()) - 1)
			return 0.0f;
		const float f = float(pos - i);
		return table_[i] + f * (table_[i + 1] - table_[i]);
	}

private:
	static constexpr double kPi = 3.14159265358979323846;

	static double besselI0(double x)
	{
		double sum = 1.0, term = 1.0;
		for (int k = 1; k < 64; ++k) {
			term *= (x / (2.0 * k)) * (x / (2.0 * k));
			sum += term;
			if (term < sum * 1e-17)
				break;
		}
		return sum;
	}

	std::vector<float> table_;
};

// Engine: a type with `void processFrame(float *frame)` that replaces kFrameSize samples at 48 kHz in place, with
// the output delayed by kEngineDelay samples.
template <class Engine> class Chain {
public:
	// Allocates. Call from activate(), not from the audio thread.
	void prepare(double hostRate, Engine *engine)
	{
		engine_ = engine;
		identity_ = hostRate == kEngineRate;
		kernel_.build();
		// Pass band edge relative to the lower Nyquist frequency of the two rates.
		constexpr double kCutoff = 0.92;
		const double z = Kernel::kZeroCrossings;
		upCut_ = kCutoff * std::min(1.0, kEngineRate / hostRate);
		downCut_ = kCutoff * std::min(1.0, hostRate / kEngineRate);
		upHalf_ = identity_ ? 0.0 : z / upCut_;     // host samples
		downHalf_ = identity_ ? 0.0 : z / downCut_; // engine samples
		hostPerEngine_ = hostRate / kEngineRate;
		enginePerHost_ = kEngineRate / hostRate;
		if (identity_) {
			// The newest complete frame ends at most kFrameSize - 1 samples before the newest input.
			latency_ = kEngineDelay + kFrameSize - 1;
		} else {
			// Output m needs engine output up to t + downHalf_, where t = (m - L) / hostPerEngine_ + delay.
			// Engine output exists up to the frame that holds the newest upsampled sample, which needs host input
			// up to its time + upHalf_. Two host samples of margin cover rounding.
			latency_ = int64_t(std::ceil((kEngineDelay + kFrameSize + 1 + downHalf_) * hostPerEngine_ + upHalf_ + 1)) + 2;
		}
		const int64_t hostSpan = std::max<int64_t>(latency_ + 2, int64_t(2 * upHalf_) + 4);
		hostRing_.assign(nextPow2(hostSpan), 0.0f);
		hostMask_ = int64_t(hostRing_.size()) - 1;
		const auto engineSpan =
			int64_t(2 * kFrameSize + 2 * downHalf_ + 8 * std::max(1.0, enginePerHost_) + 64);
		engineRing_.assign(nextPow2(engineSpan), 0.0f);
		engineMask_ = int64_t(engineRing_.size()) - 1;
		reset();
	}

	// Clears the history. Real-time safe.
	void reset()
	{
		std::fill(hostRing_.begin(), hostRing_.end(), 0.0f);
		std::fill(engineRing_.begin(), engineRing_.end(), 0.0f);
		hostCount_ = 0;
		upNext_ = 0;
		engineCount_ = 0;
		fill_ = 0;
		underruns_ = 0;
	}

	int64_t latency() const
	{
		return latency_;
	}

	// Times the output needed engine samples that did not exist yet. Must stay 0; the tests check it.
	int64_t underruns() const
	{
		return underruns_;
	}

	// Takes one host sample and returns the dry and the wet sample, both delayed by latency().
	void process(float in, float &dry, float &wet)
	{
		const int64_t m = hostCount_;
		hostRing_[m & hostMask_] = in;
		++hostCount_;

		if (identity_) {
			pushEngine(in);
		} else {
			for (;;) {
				const double center = double(upNext_) * hostPerEngine_;
				const auto last = int64_t(std::floor(center + upHalf_));
				if (last > m)
					break;
				const auto first = int64_t(std::ceil(center - upHalf_));
				float sum = 0.0f;
				for (int64_t n = std::max<int64_t>(first, 0); n <= last; ++n)
					sum += hostRing_[n & hostMask_] * kernel_.at((center - double(n)) * upCut_);
				++upNext_;
				pushEngine(float(sum * upCut_));
			}
		}

		const int64_t delayed = m - latency_;
		dry = delayed >= 0 ? hostRing_[delayed & hostMask_] : 0.0f;

		if (identity_) {
			const int64_t k = delayed + kEngineDelay;
			if (k >= engineCount_)
				++underruns_;
			wet = k >= 0 && k < engineCount_ ? engineRing_[k & engineMask_] : 0.0f;
			return;
		}
		const double t = double(delayed) * enginePerHost_ + kEngineDelay;
		const auto first = int64_t(std::ceil(t - downHalf_));
		const auto last = int64_t(std::floor(t + downHalf_));
		if (last >= engineCount_)
			++underruns_;
		float sum = 0.0f;
		for (int64_t k = std::max<int64_t>(first, 0); k <= std::min(last, engineCount_ - 1); ++k)
			sum += engineRing_[k & engineMask_] * kernel_.at((t - double(k)) * downCut_);
		wet = float(sum * downCut_);
	}

private:
	static size_t nextPow2(int64_t n)
	{
		size_t p = 1;
		while (int64_t(p) < n)
			p <<= 1;
		return p;
	}

	void pushEngine(float sample)
	{
		frame_[fill_++] = sample;
		if (fill_ < kFrameSize)
			return;
		fill_ = 0;
		engine_->processFrame(frame_);
		for (int i = 0; i < kFrameSize; ++i)
			engineRing_[(engineCount_ + i) & engineMask_] = frame_[i];
		engineCount_ += kFrameSize;
	}

	Engine *engine_ = nullptr;
	Kernel kernel_;
	bool identity_ = true;
	double upCut_ = 1.0, downCut_ = 1.0, upHalf_ = 0.0, downHalf_ = 0.0;
	double hostPerEngine_ = 1.0, enginePerHost_ = 1.0;
	int64_t latency_ = 0;
	std::vector<float> hostRing_, engineRing_;
	int64_t hostMask_ = 0, engineMask_ = 0;
	int64_t hostCount_ = 0, upNext_ = 0, engineCount_ = 0;
	float frame_[kFrameSize] = {};
	int fill_ = 0;
	int64_t underruns_ = 0;
};

} // namespace denoise
