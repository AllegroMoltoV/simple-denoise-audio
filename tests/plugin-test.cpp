// Unit tests of the signal processing. Each check guards a behaviour that a change could break; each one was seen to
// fail once with the code it guards broken.
#include "denoiser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char *what, double value = 0.0)
{
	std::printf("%s %s (value %.6g)\n", ok ? "ok  " : "FAIL", what, value);
	if (!ok)
		++failures;
}

constexpr double kPi = 3.14159265358979323846;

// The rates of the VST 3 SDK validator, which pluginval runs.
constexpr double kValidatorRates[] = {22050,     32000,     44100,     48000,      88200,     96000,
				      192000,    384000,    1234.5678, 12345.678, 123456.78, 1234567.8};

// Stands in for RNNoise: a pure delay of kEngineDelay samples, so that the chain can be checked exactly.
struct DelayEngine {
	std::vector<float> history = std::vector<float>(denoise::kEngineDelay, 0.0f);
	bool valid() const
	{
		return true;
	}
	void reset()
	{
		std::fill(history.begin(), history.end(), 0.0f);
	}
	void processFrame(float *frame)
	{
		std::vector<float> out(history.begin(), history.begin() + denoise::kFrameSize);
		history.erase(history.begin(), history.begin() + denoise::kFrameSize);
		history.insert(history.end(), frame, frame + denoise::kFrameSize);
		std::copy(out.begin(), out.end(), frame);
	}
};

// Stands in for a denoiser that removes everything, so that the wet and the dry signal differ as much as possible.
struct SilenceEngine {
	bool valid() const
	{
		return true;
	}
	void reset() {}
	void processFrame(float *frame)
	{
		std::fill(frame, frame + denoise::kFrameSize, 0.0f);
	}
};

// A voice-like signal: a 140 Hz pulse-like tone with 20 harmonics, slight vibrato, and syllables of 250 ms.
std::vector<float> voiceLike(size_t n, double rate)
{
	std::vector<float> x(n);
	for (size_t i = 0; i < n; ++i) {
		const double t = double(i) / rate;
		const double f0 = 140.0 * (1.0 + 0.05 * std::sin(2 * kPi * 3.0 * t));
		double v = 0.0;
		for (int h = 1; h <= 20; ++h)
			v += std::sin(2 * kPi * f0 * h * t) / h;
		x[i] = float(0.1 * v * std::max(0.0, std::sin(2 * kPi * 2.0 * t)));
	}
	return x;
}

std::vector<float> whiteNoise(size_t n, float rms, unsigned seed)
{
	std::mt19937 rng(seed);
	std::normal_distribution<float> gauss(0.0f, rms);
	std::vector<float> x(n);
	for (auto &v : x)
		v = gauss(rng);
	return x;
}

double energy(const std::vector<float> &x, size_t from, size_t to)
{
	double e = 0.0;
	for (size_t i = from; i < to && i < x.size(); ++i)
		e += double(x[i]) * x[i];
	return e;
}

// Runs a mono signal through both channels of a stereo denoiser and returns the left output.
template <class Engine>
std::vector<float> run(plugin::StereoDenoiser<Engine> &denoiser, const std::vector<float> &in, uint32_t block = 512)
{
	std::vector<float> l = in, r = in;
	for (size_t at = 0; at < l.size(); at += block) {
		const auto n = uint32_t(std::min<size_t>(block, l.size() - at));
		denoiser.process(l.data() + at, r.data() + at, n);
	}
	return l;
}

// The wet output of the chain must be the input delayed by latency(), the same as the dry output, at every rate.
// This is what lets the Amount mix line up, and what the latency reported to the host promises.
void testChainLinesUpAtEveryRate()
{
	for (double rate : kValidatorRates) {
		DelayEngine engine;
		denoise::Chain<DelayEngine> chain;
		chain.prepare(rate, &engine);
		// A sine well inside the pass band of both rates.
		const double f = 0.2 * std::min(rate, denoise::kEngineRate) / 2.0;
		const auto n = size_t(std::max(rate, 20000.0));
		const int64_t settle = chain.latency() + int64_t(rate * 0.01) + 200;
		double worst = 0.0;
		for (size_t i = 0; i < n; ++i) {
			float dry = 0.0f, wet = 0.0f;
			chain.process(float(0.5 * std::sin(2 * kPi * f * double(i) / rate)), dry, wet);
			if (int64_t(i) > settle)
				worst = std::max(worst, double(std::fabs(dry - wet)));
		}
		char what[160];
		std::snprintf(what, sizeof(what), "at %.4f Hz the wet signal lines up with the dry signal", rate);
		check(worst < 1e-4, what, worst);
		std::snprintf(what, sizeof(what), "at %.4f Hz the output never needs samples that do not exist yet", rate);
		check(chain.underruns() == 0, what, double(chain.underruns()));
	}
}

// The latencies in the README.
void testLatencyMatchesTheReadme()
{
	const struct {
		double rate;
		int64_t samples;
	} expected[] = {{44100, 1362}, {48000, 1439}, {96000, 2955}};
	for (const auto &e : expected) {
		DelayEngine engine;
		denoise::Chain<DelayEngine> chain;
		chain.prepare(e.rate, &engine);
		check(chain.latency() == e.samples, "the latency is the one the README states", double(chain.latency()));
	}
}

// kEngineDelay must match what RNNoise really does; a new RNNoise version could change it.
void testRnnoiseDelayIs960Samples()
{
	const size_t n = 48000 * 4;
	auto x = voiceLike(n, 48000.0);
	const auto noise = whiteNoise(n, 0.01f, 1);
	for (size_t i = 0; i < n; ++i)
		x[i] += noise[i];
	auto y = x;
	plugin::RnnoiseEngine engine;
	for (size_t at = 0; at + denoise::kFrameSize <= n; at += denoise::kFrameSize)
		engine.processFrame(y.data() + at);
	int bestLag = -1;
	double best = -1e300;
	for (int lag = 0; lag <= 2000; ++lag) {
		double s = 0.0;
		for (size_t i = 48000; i + size_t(lag) < n; ++i)
			s += double(x[i]) * y[i + size_t(lag)];
		if (s > best) {
			best = s;
			bestLag = lag;
		}
	}
	check(bestLag == denoise::kEngineDelay, "RNNoise delays its output by kEngineDelay samples", bestLag);
}

// The main scenario: background noise goes away, and a voice stays.
void testNoiseIsReducedAndVoiceStays()
{
	for (double rate : {44100.0, 48000.0}) {
		const auto n = size_t(rate * 4);
		const auto skip = size_t(rate);
		{
			plugin::StereoDenoiser<plugin::RnnoiseEngine> denoiser;
			denoiser.prepare(rate);
			const auto in = whiteNoise(n, 0.03f, 2);
			const auto out = run(denoiser, in);
			const double db = 10 * std::log10(energy(out, skip, n) / energy(in, skip, n));
			check(db < -20.0, "white noise at -30 dBFS drops by more than 20 dB", db);
		}
		{
			plugin::StereoDenoiser<plugin::RnnoiseEngine> denoiser;
			denoiser.prepare(rate);
			const auto in = voiceLike(n, rate);
			const auto out = run(denoiser, in);
			const double db = 10 * std::log10(energy(out, skip, n) / energy(in, skip, n));
			check(db > -3.0, "a voice without noise keeps its level within 3 dB", db);
		}
	}
}

// At 0 % the output is the input, delayed by the latency, bit for bit.
void testAmountZeroIsTheOriginal()
{
	for (double rate : {44100.0, 48000.0}) {
		plugin::StereoDenoiser<plugin::RnnoiseEngine> denoiser;
		denoiser.setAmountPercent(0.0);
		denoiser.prepare(rate);
		auto in = voiceLike(size_t(rate * 2), rate);
		const auto noise = whiteNoise(in.size(), 0.01f, 3);
		for (size_t i = 0; i < in.size(); ++i)
			in[i] += noise[i];
		const auto out = run(denoiser, in);
		const size_t latency = denoiser.latency();
		bool same = true;
		for (size_t i = 0; i < out.size(); ++i)
			same = same && out[i] == (i >= latency ? in[i - latency] : 0.0f);
		check(same, "Amount 0 % outputs the original, delayed by the latency", rate);
	}
}

// A jump of Amount between 0 % and 100 % glides instead of jumping.
void testAmountChangeHasNoClick()
{
	plugin::StereoDenoiser<SilenceEngine> denoiser;
	denoiser.setAmountPercent(0.0);
	denoiser.prepare(48000.0);
	std::vector<float> l(48000, 1.0f), r(48000, 1.0f);
	denoiser.process(l.data(), r.data(), 24000);
	const float before = l[23999];
	denoiser.setAmountPercent(100.0);
	denoiser.process(l.data() + 24000, r.data() + 24000, 24000);
	float worst = std::fabs(l[24000] - before);
	for (size_t i = 24001; i < l.size(); ++i)
		worst = std::max(worst, std::fabs(l[i] - l[i - 1]));
	check(before == 1.0f, "before the change, the dry signal passes", before);
	check(worst < 0.02f, "an Amount change glides instead of jumping", worst);
	check(l.back() < 0.001f, "the glide reaches the new Amount within 0.5 s", l.back());
}

// The output must not depend on how the host splits the audio into blocks.
void testBlockSizesGiveTheSameOutput()
{
	auto in = voiceLike(44100, 44100.0);
	const auto noise = whiteNoise(in.size(), 0.01f, 4);
	for (size_t i = 0; i < in.size(); ++i)
		in[i] += noise[i];
	std::vector<float> reference;
	for (uint32_t block : {512u, 37u, 1u}) {
		plugin::StereoDenoiser<plugin::RnnoiseEngine> denoiser;
		denoiser.prepare(44100.0);
		const auto out = run(denoiser, in, block);
		if (reference.empty())
			reference = out;
		check(out == reference, "the host's block size does not change the output", block);
	}
}

// Silence in gives silence out, also after reset() clears earlier sound.
void testSilenceAndReset()
{
	plugin::StereoDenoiser<plugin::RnnoiseEngine> denoiser;
	denoiser.prepare(44100.0);
	const auto zeros = std::vector<float>(44100, 0.0f);
	auto out = run(denoiser, zeros);
	check(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }), "silence in gives silence out");

	run(denoiser, voiceLike(44100, 44100.0));
	denoiser.reset();
	out = run(denoiser, zeros);
	check(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }),
	      "after reset, no earlier sound comes out");
	check(denoiser.underruns() == 0, "the denoiser never runs out of samples", double(denoiser.underruns()));
}

} // namespace

int main()
{
	testChainLinesUpAtEveryRate();
	testLatencyMatchesTheReadme();
	testRnnoiseDelayIs960Samples();
	testNoiseIsReducedAndVoiceStays();
	testAmountZeroIsTheOriginal();
	testAmountChangeHasNoClick();
	testBlockSizesGiveTheSameOutput();
	testSilenceAndReset();
	std::printf(failures == 0 ? "all tests passed\n" : "%d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
