// Runs the Mangrave engine without Rack.
//   c++ -std=c++11 -O2 -o test/grove_test test/grove_test.cpp && test/grove_test
#include "../src/GroveEngine.hpp"
#include <cstdio>
#include <random>
using namespace mg;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); fails++; } else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)
static const float SR = 48000.f;

struct Count { int core = 0, imp = 0, hollow = 0; double rms = 0, peak = 0; };
static Count run(Params p, float secs = 1.f, float trigHz = 0.f, bool trigPatched = false) {
	GroveEngine e; Count c; float prevH = 0;
	int n = (int) (SR * secs);
	for (int i = 0; i < n; i++) {
		float tv = 0.f;
		if (trigHz > 0.f) { double ph = std::fmod(i * trigHz / SR, 1.0); tv = ph < 0.1 ? 5.f : 0.f; }
		e.process(p, 0.f, tv, trigPatched, SR);
		if (i > n / 10) {
			if (e.coreEdge) c.core++;
			if (e.started) c.imp++;
			if (prevH < 0 && e.hol > 0) c.hollow++;
			c.rms += e.out * e.out; c.peak = std::max(c.peak, (double) std::fabs(e.out));
		}
		prevH = e.hol;
	}
	c.rms = std::sqrt(c.rms / (n - n / 10));
	return c;
}

int main() {
	const float S = 0.9f;  // measured span (after the first 10%)
	{
		Params p; Count c = run(p);
		CHECK(std::abs(c.core / S - 261.6) < 3 && std::abs(c.imp / S - 261.6) < 3, "noon: core and impulses at middle C (%.0f, %.0f Hz)", c.core / S, c.imp / S);
		Params q; q.voct = 1.f; Count d = run(q);
		CHECK(std::abs(d.core / S - 523.3) < 3, "1 V/oct doubles the pitch (%.0f Hz)", d.core / S);
	}
	{
		// undertones: a long impulse skips core cycles (BARREL at utone: almost all rise)
		Params p; p.barrel = -1.f; p.formant = 0.5f - 0.125f;  // ratio 1.6 x the period -> every 2nd cycle
		Count a = run(p);
		p.formant = 0.5f - 0.25f;             // ratio 3.2 -> every 4th cycle
		Count b = run(p);
		CHECK(std::abs(a.imp / S - 130.8) < 3 && std::abs(b.imp / S - 65.4) < 3, "FORMANT low gives undertones: /2 then /4 (%.0f, %.0f Hz)", a.imp / S, b.imp / S);
		CHECK(std::abs(a.hollow / S - 65.4) < 3, "HOLLOW divides the impulse rate by two (%.0f Hz)", a.hollow / S);
	}
	{
		// multiphonics: near noon BARREL a retrigger lands while the impulse is falling and turns it round
		Params p; p.formant = 0.5f - 0.125f;  // ratio 1.6, triangle: rise 0.8 periods, falling when the next edge comes
		Count a = run(p);
		CHECK(a.imp / S > 200.0, "a trigger while falling turns the impulse round (%.0f starts a second, not 131)", a.imp / S);
	}
	{
		// FM input is AC-coupled: a steady 2.5 V does not move the pitch once settled
		Params p; p.fm = 2.5f; Count c = run(p, 2.f);
		CHECK(std::abs(c.core / 1.8 - 261.6) < 6, "steady FM voltage is blocked (%.0f Hz)", c.core / 1.8);
		p.fmDcCoupled = true; Count d = run(p, 1.f);
		CHECK(std::abs(d.core / S - 392.4) < 4, "DC-coupled mod: 2.5 V shifts the core to 392 Hz (%.0f Hz)", d.core / S);
	}
	{
		// through-zero FM: the core still fires impulses while running backwards
		GroveEngine e; Params p; p.fmDcCoupled = true; p.fm = -10.f;  // f = -base
		int n = 0;
		for (int i = 0; i < (int) SR; i++) { e.process(p, 0, 0, false, SR); if (e.coreEdge) n++; }
		CHECK(std::abs(n - 262) < 3, "core running backwards still fires once a cycle (%d)", n);
	}
	{
		// constant formant: duration does not follow pitch
		GroveEngine e; Params p; p.constant = CONST_FORMANT;
		e.process(p, 0, 0, false, SR); double a = e.dur;
		p.voct = 1.f; e.process(p, 0, 0, false, SR); double b = e.dur;
		GroveEngine g; Params q; g.process(q, 0, 0, false, SR); double c = g.dur; q.voct = 1.f; g.process(q, 0, 0, false, SR); double d = g.dur;
		CHECK(std::fabs(a - b) < 1e-12 && std::fabs(c / d - 2.0) < 1e-9, "CONSTANT FORMANT keeps the duration, CONSTANT WAVE halves it an octave up");
	}
	{
		Params p; p.air = 0.f; Count a = run(p);
		p.air = 0.5f; Count b = run(p);
		p.air = 1.f; Count c = run(p);
		CHECK(a.peak < 1e-6, "AIR at 0 is silent (%.6f)", a.peak);
		CHECK(std::fabs(b.peak - 5.0) < 0.05, "AIR at noon peaks at 5 V, 10 V peak to peak (%.2f)", b.peak);
		CHECK(c.rms > b.rms * 1.3 && c.peak <= 5.7, "AIR at full overdrives towards a square (rms %.2f vs %.2f)", c.rms, b.rms);
	}
	{
		// PPM CYCLE: the impulse is its own oscillator, pitched by FORMANT
		Params p; p.ppm = PPM_CYCLE; p.constant = CONST_FORMANT; p.formant = 0.5f;  // 1/dur = 640 Hz
		Count c = run(p);
		CHECK(std::abs(c.imp / S - 640.0) < 4, "PPM CYCLE: impulse runs at 1/duration (%.0f Hz)", c.imp / S);
		p.pitch = 0.2f; Count d = run(p);
		CHECK(std::abs(d.imp - c.imp) < 4, "PPM CYCLE: core pitch does not move it in CONSTANT FORMANT");
	}
	{
		// PPM TRIGGER replaces the core: a 10 Hz clock with short impulses = 10 envelopes a second
		Params p; p.constant = CONST_FORMANT; p.formant = 0.3f;
		Count c = run(p, 1.f, 10.f, true);
		CHECK(std::abs(c.imp / S - 10.0) < 1.5, "PPM TRIGGER clocks the impulse (%.1f per second)", c.imp / S);
	}
	{
		// TO AIR adds a signal into the AIR stage
		Params p; p.air = 0.25f; p.toAir = 2.f; GroveEngine e; e.process(p, 0, 0, false, SR);
		Params q; q.air = 0.25f; GroveEngine f; f.process(q, 0, 0, false, SR);
		CHECK(e.out > f.out + 0.5f, "TO AIR shifts the AIR stage (%.2f vs %.2f)", e.out, f.out);
	}
	{
		std::mt19937 rng(5); std::uniform_real_distribution<float> u(0.f, 1.f); bool ok = true;
		for (int t = 0; t < 60; t++) {
			Params p; p.pitch = u(rng); p.voct = u(rng) * 8 - 4; p.fm = u(rng) * 20 - 10; p.formant = u(rng) * 1.4f - 0.2f;
			p.barrel = u(rng) * 2 - 1; p.air = u(rng); p.constant = t % 2; p.ppm = t % 3; p.toAir = u(rng) * 20 - 10;
			GroveEngine e;
			for (int i = 0; i < 20000; i++) {
				e.process(p, u(rng) > 0.999f ? 5.f : 0.f, u(rng) > 0.99f ? 5.f : 0.f, t % 4 == 0, SR);
				if (!std::isfinite(e.out) || std::fabs(e.out) > 5.7f) ok = false;
			}
		}
		CHECK(ok, "fuzz: outputs stay finite and within 5.7 V");
	}
	std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
	return fails ? 1 : 0;
}
