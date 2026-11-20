// Runs the Weird Sisters engine without Rack.
//   c++ -std=c++11 -O2 -o test/sisters_test test/sisters_test.cpp && test/sisters_test
#include "../src/SistersEngine.hpp"
#include <cstdio>
#include <random>
using namespace ws;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); fails++; } else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)
static const float SR = 48000.f;

// RMS of each output for a 5 V sine at hz
static void rmsAt(Params p, double hz, double r[4]) {
	SistersEngine e;
	double acc[4] = {};
	int n = (int) SR, skip = 4800;
	for (int i = 0; i < n + skip; i++) {
		float x = 5.f * std::sin(2 * M_PI * hz * i / SR);
		e.process(p, x, x, x, SR);
		if (i >= skip) { for (int k = 0; k < 3; k++) acc[k] += e.out[k] * e.out[k]; acc[3] += e.all * e.all; }
	}
	for (int k = 0; k < 4; k++) r[k] = std::sqrt(acc[k] / n);
}

int main() {
	Params p;
	p.freqKnob = std::log2(1000.0 / 20.0) / 10.0;  // 1 kHz
	p.span = 0.5f;  // 2 octaves: low at 250 Hz, high at 4 kHz
	double lo[4], mid[4], hi[4];
	rmsAt(p, 60.0, lo); rmsAt(p, 1000.0, mid); rmsAt(p, 16000.0, hi);
	CHECK(lo[0] > 3.0 && lo[1] < 0.3 && lo[2] < 0.01, "crossover: 60 Hz goes to LOW (%.2f %.2f %.3f)", lo[0], lo[1], lo[2]);
	CHECK(mid[1] > 2.5 && mid[0] < 0.6 && mid[2] < 0.6, "crossover: 1 kHz goes to CENTRE (%.2f %.2f %.2f)", mid[0], mid[1], mid[2]);
	CHECK(hi[2] > 3.0 && hi[0] < 0.01 && hi[1] < 0.3, "crossover: 16 kHz goes to HIGH (%.3f %.2f %.2f)", hi[0], hi[1], hi[2]);
	CHECK(lo[3] > 2.5 && hi[3] > 2.5, "ALL passes lows and highs (%.2f %.2f)", lo[3], hi[3]);

	Params f = p; f.mode = FORMANT;
	double a[4], b[4];
	rmsAt(f, 250.0, a); rmsAt(f, 4000.0, b);
	CHECK(a[0] > 2.5 && a[2] < 0.2, "formant: LOW is a band pass at 250 Hz (%.2f, HIGH %.2f)", a[0], a[2]);
	CHECK(b[2] > 2.5 && b[0] < 0.2, "formant: HIGH is a band pass at 4 kHz (%.2f, LOW %.2f)", b[2], b[0]);

	Params n = p; n.quality = -1.f; n.span = 0.f;
	double c[4], d[4];
	rmsAt(n, 1000.0, c); rmsAt(n, 100.0, d);
	CHECK(c[1] < 0.3 && d[1] > 2.5, "anti-resonance turns CENTRE into a notch (%.2f at 1 kHz, %.2f at 100 Hz)", c[1], d[1]);

	// self-oscillation: no input, quality full, count zero crossings
	{
		Params s = p; s.quality = 1.f; s.span = 0.f;
		SistersEngine e;
		int cross = 0; float prev = 0.f, peak = 0.f;
		for (int i = 0; i < (int) SR * 3; i++) {
			e.process(s, 0.f, 0.f, 0.f, SR);
			if (i >= (int) SR * 2) { if (prev < 0.f && e.out[1] >= 0.f) cross++; peak = std::max(peak, std::fabs(e.out[1])); }
			prev = e.out[1];
		}
		CHECK(std::abs(cross - 1000) < 30 && peak > 1.f && peak <= 10.f, "QUALITY full self-oscillates near 1 kHz (%d Hz, %.1f V)", cross, peak);
	}
	// display response matches: centre peak at fc
	CHECK(std::fabs(response(1000, 1000, dampK(0.f), 0, 1) - 1.0) < 1e-6, "display: CENTRE response is 1 at its centre");
	// fuzz
	{
		std::mt19937 rng(3); std::uniform_real_distribution<float> u(0.f, 1.f);
		bool ok = true;
		for (int t = 0; t < 40; t++) {
			Params q; q.freqKnob = u(rng); q.voct = u(rng) * 6 - 3; q.span = u(rng); q.spanCv = u(rng) * 4 - 2;
			q.quality = u(rng) * 2 - 1; q.mode = t % 2; q.fmOct = u(rng) * 4 - 2;
			SistersEngine e;
			for (int i = 0; i < 20000; i++) {
				float x = (u(rng) * 2 - 1) * 10.f;
				e.process(q, x, x * 0.5f, -x, SR);
				for (int k = 0; k < 3; k++) if (!std::isfinite(e.out[k]) || std::fabs(e.out[k]) > 10.01f) ok = false;
			}
		}
		CHECK(ok, "fuzz: outputs stay finite and within 10 V");
	}
	std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
	return fails ? 1 : 0;
}
