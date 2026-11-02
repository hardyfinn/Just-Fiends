// Runs the Just Fiends engine without Rack and checks the main behaviours.
//   c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
#include "../src/FiendsEngine.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace jf;
static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); fails++; } else { std::printf("ok:   " __VA_ARGS__); std::printf("\n"); } } while (0)

static const float SR = 48000.f, DT = 1.f / SR;

static SlopeEngine make(int range, int mode) {
	SlopeEngine e;
	e.sampleRate = SR;
	e.c.range = range;
	e.c.mode = mode;
	for (int i = 0; i < 6; i++) e.gateConn[i] = true;
	return e;
}

static void allGates(SlopeEngine& e, float v) { for (int i = 0; i < 6; i++) e.gateV[i] = v; }

// count cycle starts on channel ch over `secs`
static int countWraps(SlopeEngine& e, int ch, float secs) {
	int n = 0;
	for (long k = 0; k < (long) (secs * SR); k++) { e.process(DT); if (e.s[ch].wrapped) n++; }
	return n;
}

int main() {
	// 1. sound cycle at noon = middle C, and Intone fully CW makes channel N run N times faster
	{
		SlopeEngine e = make(SOUND, CYCLE);
		e.c.intone = 1.f;
		int w1 = countWraps(e, 0, 1.f);
		SlopeEngine e2 = make(SOUND, CYCLE); e2.c.intone = 1.f;
		int w3 = countWraps(e2, 2, 1.f);
		CHECK(std::abs(w1 - 262) <= 1, "sound cycle at noon is middle C (%d Hz)", w1);
		CHECK(std::abs(w3 - 785) <= 2, "Intone CW: 3N runs 3x (%d Hz)", w3);
		SlopeEngine e3 = make(SOUND, CYCLE); e3.c.intone = -1.f;
		int w6 = countWraps(e3, 5, 1.f);
		CHECK(std::abs(w6 - 44) <= 1, "Intone CCW: 6N runs at 1/6 (%d Hz)", w6);
		SlopeEngine e4 = make(SOUND, CYCLE); e4.c.voct[0] = 1.f;
		int wo = countWraps(e4, 0, 1.f);
		CHECK(std::abs(wo - 523) <= 1, "1 V/oct on TIME doubles the pitch (%d Hz)", wo);
	}
	// 2. shape transient: one rise and fall per trigger, 1 s at noon, peak 8 V
	{
		SlopeEngine e = make(SHAPE, TRANSIENT);
		e.gateV[0] = 5.f; e.process(DT); e.gateV[0] = 0.f;
		float peak = 0.f; long len = 0;
		for (long k = 0; k < (long) (3 * SR); k++) { e.process(DT); peak = std::max(peak, e.s[0].out); if (e.s[0].seg != IDLE) len = k; }
		CHECK(std::fabs(peak - 8.f) < 0.01f, "transient envelope peaks at 8 V (%.3f)", peak);
		CHECK(std::fabs(len / SR - 1.f) < 0.01f, "transient envelope lasts 1 s at noon (%.3f s)", len / SR);
		CHECK(e.s[1].seg == IDLE && e.s[1].out == 0.f, "untriggered channel stays at 0 V");
	}
	// 3. sustain holds at the top while the gate is high
	{
		SlopeEngine e = make(SHAPE, SUSTAIN);
		e.c.time = 1.f;  // fast
		e.gateV[0] = 5.f;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(e.s[0].seg == HOLD && std::fabs(e.s[0].out - 8.f) < 1e-3f, "sustain holds 8 V while the gate is high");
		e.gateV[0] = 0.f;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(e.s[0].seg == IDLE && e.s[0].out < 1e-3f, "sustain falls when the gate goes low");
	}
	// 4. volley: RUN at 5 V fires 16 cycles per trigger
	{
		SlopeEngine e = make(SHAPE, CYCLE);
		e.c.run = true; e.c.runV = 5.f; e.c.time = 0.9f;
		// let the free-running cycle finish first
		for (int k = 0; k < (int) SR * 2; k++) e.process(DT);
		e.gateV[0] = 5.f; e.process(DT); e.gateV[0] = 0.f;
		int cycles = 1 + countWraps(e, 0, 3.f);
		CHECK(cycles == 16, "volley with RUN 5 V plays 16 cycles (%d)", cycles);
	}
	// 5. shift: RUN 5 V ignores triggers until the slope is done (a clock divider)
	{
		SlopeEngine e = make(SHAPE, TRANSIENT);
		e.c.run = true; e.c.runV = 5.f; e.c.time = 0.5f; // 1 s slope
		int starts = 0; int prevSeg = IDLE;
		for (long k = 0; k < (long) (4 * SR); k++) {
			e.gateV[0] = (k % 12000) < 100 ? 5.f : 0.f;  // 4 triggers per second
			e.process(DT);
			if (e.s[0].seg == RISE && prevSeg != RISE) starts++;
			prevSeg = e.s[0].seg;
		}
		CHECK(starts >= 3 && starts <= 4, "shift at 5 V divides 16 triggers down to ~4 slopes (%d)", starts);
	}
	// 6. strata follows RUN
	{
		SlopeEngine e = make(SHAPE, SUSTAIN);
		e.c.run = true; e.c.runV = 4.f; e.c.time = 1.f;
		allGates(e, 5.f);
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(std::fabs(e.s[3].out - 4.f) < 0.01f, "strata settles on the RUN voltage (%.3f V)", e.s[3].out);
		e.gateV[3] = 0.f; e.c.runV = 1.f;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(std::fabs(e.s[3].out - 4.f) < 0.01f && std::fabs(e.s[2].out - 1.f) < 0.01f, "a low gate freezes that channel in strata");
	}
	// 7. plume: a pluck decays to silence
	{
		SlopeEngine e = make(SOUND, SUSTAIN);
		e.c.run = true; e.c.runV = 0.f;  // 20 ms decay
		e.gateV[0] = 5.f; e.process(DT);
		float peak = 0.f;
		for (int k = 0; k < 2400; k++) { e.process(DT); peak = std::max(peak, std::fabs(e.s[0].out)); }
		for (int k = 0; k < (int) SR / 2; k++) e.process(DT);
		CHECK(peak > 4.f && std::fabs(e.s[0].out) < 0.01f, "plume plucks (peak %.2f) and decays (%.4f)", peak, e.s[0].out);
	}
	// 8. velocity in sound range
	{
		SlopeEngine e = make(SOUND, SUSTAIN);
		e.gateV[0] = 2.5f;
		float peak = 0.f;
		for (int k = 0; k < (int) SR / 4; k++) { e.process(DT); peak = std::max(peak, std::fabs(e.s[0].out)); }
		CHECK(std::fabs(peak - 2.5f) < 0.1f, "a 2.5 V gate plays at half level (%.2f V)", peak);
	}
	// 9. geode: Intone CW makes channel N fire N times per clock
	{
		SlopeEngine e = make(SHAPE, TRANSIENT);
		e.c.geode = true; e.c.intone = 1.f; e.c.time = 1.f;
		int starts[6] = {}; int prev[6] = {};
		long clk = (long) (SR / 2);
		for (long k = 0; k < 8 * clk; k++) {
			e.gateV[0] = (k % clk) < 200 ? 5.f : 0.f;
			e.process(DT);
			if (k >= 4 * clk) for (int i = 0; i < 6; i++) { if (e.s[i].seg == RISE && prev[i] != RISE) starts[i]++; }
			for (int i = 0; i < 6; i++) prev[i] = e.s[i].seg;
		}
		CHECK(starts[0] == 4 && starts[3] == 16 && starts[5] == 24, "geode: 1N, 4N, 6N fire 4, 16, 24 times in 4 clocks (%d, %d, %d)", starts[0], starts[3], starts[5]);
	}
	// 10. fuzz: random settings never give NaN or wild voltages
	{
		std::mt19937 rng(7);
		std::uniform_real_distribution<float> u(0.f, 1.f);
		bool okv = true;
		for (int trial = 0; trial < 60; trial++) {
			SlopeEngine e = make(trial % 2, (trial / 2) % 3);
			e.c.run = u(rng) > 0.5f; e.c.runV = u(rng) * 12.f - 4.f;
			e.c.time = u(rng); e.c.intone = u(rng) * 2 - 1; e.c.ramp = u(rng) * 2 - 1; e.c.curve = u(rng) * 2 - 1;
			e.c.fmOct = u(rng) * 6 - 3; e.c.geode = trial % 7 == 0; e.c.polyPitch = trial % 5 == 0;
			for (int k = 0; k < 20000; k++) {
				for (int i = 0; i < 6; i++) e.gateV[i] = u(rng) > 0.995f ? 10.f * u(rng) : (k % 3000 < 1500 ? 5.f : 0.f);
				e.process(DT);
				for (int i = 0; i < 6; i++) if (!std::isfinite(e.s[i].out) || std::fabs(e.s[i].out) > 8.01f) okv = false;
				if (!std::isfinite(e.mix) || std::fabs(e.mix) > 10.01f) okv = false;
			}
		}
		CHECK(okv, "fuzz: outputs stay finite and in range");
	}
	std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
	return fails ? 1 : 0;
}
