// Runs the Mangrave engine without Rack.
//   c++ -std=c++11 -O2 -o test/grove_test test/grove_test.cpp && test/grove_test
#include "../src/GroveEngine.hpp"
#include <cstdio>
#include <random>
using namespace mg;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); fails++; } else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)
static const float SR = 48000.f;

static double rms(Params p, int n = (int) SR) {
	GroveEngine e; double acc = 0;
	for (int i = 0; i < n; i++) { e.process(p, 0.f, SR); acc += e.out * e.out; }
	return std::sqrt(acc / n);
}

int main() {
	{
		Params p; GroveEngine e; int w = 0, sq = 0; float prev = 0;
		for (int i = 0; i < (int) SR; i++) { e.process(p, 0.f, SR); if (e.wrapped) w++; if (prev < 0 && e.square > 0) sq++; prev = e.square; }
		CHECK(std::abs(w - 262) <= 1 && std::abs(sq - 262) <= 1, "pitch at noon is middle C (%d bursts, %d square cycles)", w, sq);
	}
	{
		Params p; p.voct = 1.f; GroveEngine e; int w = 0;
		for (int i = 0; i < (int) SR; i++) { e.process(p, 0.f, SR); if (e.wrapped) w++; }
		CHECK(std::abs(w - 523) <= 1, "1 V/oct doubles the pitch (%d Hz)", w);
	}
	{
		Params p; p.air = 0.f;
		double r0 = rms(p); p.air = 0.3f; double r1 = rms(p); p.air = 1.f; double r2 = rms(p);
		CHECK(r0 < 1e-3 && r1 > 0.05 && r2 > r1 * 2, "AIR: silent at 0, louder and fuller as it opens (%.3f %.3f %.3f)", r0, r1, r2);
	}
	{
		// constant formant: the formant stays put when the pitch moves
		Params p; p.constant = CONST_FORMANT; GroveEngine e;
		e.process(p, 0.f, SR); double a = e.ff;
		p.voct = 1.f; e.process(p, 0.f, SR); double b = e.ff;
		Params q; GroveEngine g; g.process(q, 0.f, SR); double c = g.ff; q.voct = 1.f; g.process(q, 0.f, SR); double d = g.ff;
		CHECK(std::fabs(a - b) < 1e-6 && std::fabs(d / c - 2.0) < 1e-6, "CONSTANT FORMANT holds the formant, CONSTANT WAVE moves it with the pitch");
	}
	{
		// strike resets and plucks
		Params p; p.air = 0.f; GroveEngine e; double peak = 0, late = 0;
		for (int i = 0; i < (int) SR; i++) {
			e.process(p, i < 100 ? 5.f : 0.f, SR);
			if (i < 4800) peak = std::max(peak, (double) std::fabs(e.out));
			else if (i > (int) SR / 2) late = std::max(late, (double) std::fabs(e.out));
		}
		CHECK(peak > 2.0 && late < 0.3, "STRIKE puffs air through a closed Mangrave (%.2f then %.2f)", peak, late);
	}
	{
		std::mt19937 rng(5); std::uniform_real_distribution<float> u(0.f, 1.f); bool ok = true;
		for (int t = 0; t < 40; t++) {
			Params p; p.pitch = u(rng); p.voct = u(rng) * 8 - 4; p.fm = u(rng) * 20 - 10; p.formant = u(rng);
			p.formantCv = u(rng) * 4 - 2; p.barrel = u(rng) * 2 - 1; p.air = u(rng); p.constant = t % 2;
			GroveEngine e;
			for (int i = 0; i < 20000; i++) { e.process(p, u(rng) > 0.999f ? 5.f : 0.f, SR); if (!std::isfinite(e.out) || std::fabs(e.out) > 5.01f) ok = false; }
		}
		CHECK(ok, "fuzz: outputs stay finite and within 5 V");
	}
	std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
	return fails ? 1 : 0;
}
