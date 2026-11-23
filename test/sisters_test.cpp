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

static double spanFor(double oct) { return oct / 5.7 + 0.3; }

int main() {
	Params p;
	p.freqKnob = std::log2(1000.0 / 20.0) / 10.0;  // 1 kHz
	p.span = (float) spanFor(2.0);  // 2 octaves: low at 250 Hz, high at 4 kHz
	double lo[4], mid[4], hi[4];
	rmsAt(p, 60.0, lo); rmsAt(p, 1000.0, mid); rmsAt(p, 16000.0, hi);
	CHECK(lo[0] > 3.0 && lo[1] < 0.3 && lo[2] < 0.01, "crossover: 60 Hz goes to LOW (%.2f %.2f %.3f)", lo[0], lo[1], lo[2]);
	CHECK(mid[1] > 3.0 && mid[0] < 0.3 && mid[2] < 0.3, "crossover: 1 kHz goes to CENTRE (%.2f %.2f %.2f)", mid[0], mid[1], mid[2]);
	CHECK(hi[2] > 3.0 && hi[0] < 0.01 && hi[1] < 0.3, "crossover: 16 kHz goes to HIGH (%.3f %.2f %.2f)", hi[0], hi[1], hi[2]);
	CHECK(lo[3] > 2.5 && hi[3] > 2.5 && mid[3] > 2.5, "ALL passes lows, mids and highs (%.2f %.2f %.2f)", lo[3], mid[3], hi[3]);

	// crossover CENTRE is the band between LOW's and HIGH's cutoffs, so SPAN sets its width
	{
		double w[4], n[4], shut[4];
		rmsAt(p, 400.0, w);
		Params q = p; q.span = (float) spanFor(0.3); rmsAt(q, 400.0, n);
		Params c = p; c.span = 0.f; rmsAt(c, 1000.0, shut);
		CHECK(w[1] > 3.0 && n[1] < 1.5, "SPAN widens CENTRE: 400 Hz passes at 2 octaves (%.2f), not at 0.3 (%.2f)", w[1], n[1]);
		CHECK(shut[1] < 0.6, "SPAN fully anticlockwise closes CENTRE like a VCA (%.2f at 1 kHz)", shut[1]);
	}

	Params f = p; f.mode = FORMANT;
	double a[4], b[4], m[4];
	rmsAt(f, 250.0, a); rmsAt(f, 4000.0, b); rmsAt(f, 1000.0, m);
	CHECK(a[0] > 2.5 && a[2] < 0.2, "formant: LOW is a band pass at 250 Hz (%.2f, HIGH %.2f)", a[0], a[2]);
	CHECK(b[2] > 2.5 && b[0] < 0.2, "formant: HIGH is a band pass at 4 kHz (%.2f, LOW %.2f)", b[2], b[0]);
	CHECK(std::fabs(m[1] - 3.54) < 0.3, "formant: CENTRE passes its centre at unity (%.2f)", m[1]);

	// anticlockwise QUALITY: dry/wet. Fully CCW, LOW passes highs again and CENTRE passes everything but notches
	{
		Params n = p; n.quality = -1.f;
		double h[4], l[4], c1[4];
		rmsAt(n, 10000.0, h); rmsAt(n, 60.0, l); rmsAt(n, 250.0, c1);
		CHECK(h[0] > 3.0 && l[2] > 3.0, "QUALITY CCW: LOW passes 10 kHz (%.2f), HIGH passes 60 Hz (%.2f)", h[0], l[2]);
		CHECK(h[1] > 2.5 && l[1] > 2.5 && c1[1] < 1.0, "QUALITY CCW: CENTRE passes 60 Hz and 10 kHz (%.2f %.2f), notch at LOW's cutoff (%.2f)", l[1], h[1], c1[1]);
	}

	// self-oscillation: from about 3 o'clock, not at 2 o'clock
	{
		float qs[3] = {0.33f, 0.5f, 1.f}; int cross[3]; float peak[3];
		for (int t = 0; t < 3; t++) {
			Params s = f; s.quality = qs[t];
			SistersEngine e;
			cross[t] = 0; peak[t] = 0.f; float prev = 0.f;
			for (int i = 0; i < (int) SR * 4; i++) {
				e.process(s, 0.f, 0.f, 0.f, SR);
				if (i >= (int) SR * 3) { if (prev < 0.f && e.out[1] >= 0.f) cross[t]++; peak[t] = std::max(peak[t], std::fabs(e.out[1])); }
				prev = e.out[1];
			}
		}
		CHECK(peak[0] < 0.01f, "2 o'clock does not sing on its own (%.4f V)", peak[0]);
		CHECK(peak[1] > 1.f && std::abs(cross[1] - 1000) < 30, "3 o'clock self-oscillates at 1 kHz (%d Hz, %.1f V)", cross[1], peak[1]);
		CHECK(std::abs(cross[2] - 1000) < 30 && peak[2] > 3.f && peak[2] < 6.5f, "full QUALITY sings at about 5 V (%d Hz, %.1f V)", cross[2], peak[2]);
	}
	// display response matches the engine at the band centres
	{
		double fc[3] = {250.0, 1000.0, 4000.0};
		CHECK(std::fabs(response(1000, fc, dampK(0.f), 0, CENTRE, FORMANT) - 1.0) < 1e-6, "display: formant CENTRE response is 1 at its centre");
		CHECK(std::fabs(response(1000, fc, dampK(0.f), 0, CENTRE, CROSSOVER) - 1.0) < 0.1, "display: crossover CENTRE passes the middle (%.2f)", response(1000, fc, dampK(0.f), 0, CENTRE, CROSSOVER));
	}
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
