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
	// 2b. transient ignores triggers for the whole slope: a 1 s envelope clocked every 0.7 s fires every other clock
	{
		SlopeEngine e = make(SHAPE, TRANSIENT);
		int starts = 0, prev = IDLE;
		long clk = (long) (0.7 * SR);
		for (long k = 0; k < 6 * clk; k++) {
			e.gateV[0] = (k % clk) < 100 ? 5.f : 0.f;
			e.process(DT);
			if (e.s[0].seg == RISE && prev != RISE) starts++;
			prev = e.s[0].seg;
		}
		CHECK(starts == 3, "transient: 6 clocks at 0.7 s into a 1 s envelope fire 3 times (%d)", starts);
	}
	// normalling: one cable into 6N fires all six; 6N and 2N patched splits at 2N
	{
		float in[6] = {0, 3, 0, 0, 0, 7}; bool pat[6] = {false, true, false, false, false, true};
		float out[6]; bool conn[6];
		normalTriggers(in, pat, out, conn);
		CHECK(out[0] == 3 && out[1] == 3 && out[2] == 7 && out[5] == 7, "normalling: 6N feeds 5N-3N, 2N feeds IDENTITY");
		bool only6[6] = {false, false, false, false, false, true};
		normalTriggers(in, only6, out, conn);
		CHECK(out[0] == 7 && conn[0], "normalling: a cable in 6N alone reaches IDENTITY");
	}
	// 4. volley: -4 V one, 0 V six, +5 V 36 envelopes; below -4 V choked
	{
		float rv[4] = {-4.5f, -4.f, 0.f, 5.f}; int got[4];
		for (int t = 0; t < 4; t++) {
			SlopeEngine e = make(SHAPE, CYCLE);
			e.c.run = true; e.c.runV = rv[t]; e.c.time = 0.9f;
			e.gateV[0] = 5.f; e.process(DT); e.gateV[0] = 0.f;
			int n = 0, prev = e.s[0].seg;
			if (e.s[0].seg == RISE) n = 1;
			for (long k = 0; k < (long) (4 * SR); k++) { e.process(DT); if (e.s[0].wrapped) n++; prev = e.s[0].seg; }
			(void) prev;
			got[t] = n;
		}
		CHECK(got[0] == 0 && got[1] == 1 && got[2] == 6 && got[3] == 36, "volley: %d, %d, %d, %d envelopes at -4.5, -4, 0, +5 V", got[0], got[1], got[2], got[3]);
	}
	// 5. shift: +5 V ignores triggers until done; -5 V restarts from 0 V every time
	{
		int res[2];
		float rv[2] = {5.f, -5.f};
		for (int t = 0; t < 2; t++) {
			SlopeEngine e = make(SHAPE, TRANSIENT);
			e.c.run = true; e.c.runV = rv[t];
			int starts = 0, prevSeg = IDLE; double prevLin = 0.0;
			for (long k = 0; k < (long) (4 * SR); k++) {
				e.gateV[0] = (k % 12000) < 100 ? 5.f : 0.f;  // 4 triggers per second
				e.process(DT);
				if (e.s[0].seg == RISE && (prevSeg != RISE || e.s[0].lin < prevLin - 1e-3)) starts++;
				prevSeg = e.s[0].seg;
				prevLin = e.s[0].lin;
			}
			res[t] = starts;
		}
		CHECK(res[0] >= 3 && res[0] <= 4 && res[1] == 16, "shift: +5 V gives %d slopes, -5 V restarts %d of 16", res[0], res[1]);
	}
	// 6. strata: rise to 8 V, fall to the RUN level (0 V -> 4 V), follow it, release when the gate drops
	{
		SlopeEngine e = make(SHAPE, SUSTAIN);
		e.c.run = true; e.c.runV = 0.f; e.c.time = 1.f;
		allGates(e, 5.f);
		float peak = 0.f;
		for (int k = 0; k < (int) SR; k++) { e.process(DT); peak = std::max(peak, e.s[3].out); }
		CHECK(peak > 7.99f && std::fabs(e.s[3].out - 4.f) < 0.01f, "strata: peaks at 8 V then sustains at 4 V for RUN 0 V (%.2f, %.2f)", peak, e.s[3].out);
		e.c.runV = 2.5f;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(std::fabs(e.s[3].out - 6.f) < 0.01f, "strata: follows RUN like a slew limiter (%.2f V for 2.5 V)", e.s[3].out);
		e.gateV[3] = 0.f;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(e.s[3].out < 1e-3f && std::fabs(e.s[2].out - 6.f) < 0.01f, "strata: a low gate releases to 0 V, the others hold");
		e.c.run = false;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		allGates(e, 0.f);
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(e.s[2].out < 1e-3f, "leaving strata does not leave a stuck voltage (%.2f)", e.s[2].out);
	}
	// 7. plume: a trigger plucks and decays; a held gate keeps the note open
	{
		SlopeEngine e = make(SOUND, SUSTAIN);
		e.c.run = true; e.c.runV = 0.f;
		e.gateV[0] = 5.f; e.gateV[1] = 5.f; e.process(DT); e.gateV[0] = 0.f;
		float peak = 0.f;
		for (int k = 0; k < 4800; k++) { e.process(DT); peak = std::max(peak, std::fabs(e.s[0].out)); }
		for (int k = 0; k < (int) SR * 2; k++) e.process(DT);
		float held = 0.f;
		for (int k = 0; k < 4800; k++) { e.process(DT); held = std::max(held, std::fabs(e.s[1].out)); }
		CHECK(peak > 3.f && std::fabs(e.s[0].out) < 0.05f && held > 4.5f, "plume: pluck peaks %.2f and dies away, held note stays at %.2f", peak, held);
	}
	// 8. sound range levels: impulses and trapezoids rest at -5 V and reach +5 V
	{
		SlopeEngine e = make(SOUND, TRANSIENT);
		e.process(DT);
		float rest = e.s[0].out;
		e.gateV[0] = 5.f; float peak = -9.f;
		for (int k = 0; k < 2000; k++) { e.process(DT); peak = std::max(peak, e.s[0].out); }
		CHECK(std::fabs(rest + 5.f) < 1e-3f && peak > 4.9f, "impulse rests at -5 V (%.2f) and peaks at +5 V (%.2f)", rest, peak);
		// trapezoid: a 100 Hz pulse, 30% high, with fast slopes: the output is high about 30% of the time
		SlopeEngine t = make(SOUND, SUSTAIN); t.c.time = 1.f;
		long hi = 0, n = (long) SR;
		for (long k = 0; k < n; k++) { t.gateV[0] = (k % 480) < 144 ? 5.f : 0.f; t.process(DT); if (t.s[0].out > 0.f) hi++; }
		CHECK(std::fabs(hi / (double) n - 0.3) < 0.03, "trapezoid follows the pulse width (%.0f%% high)", 100.0 * hi / n);
	}
	// MIX in shape range divides by channel number: only 2N held at the top gives 4 V
	{
		SlopeEngine e = make(SHAPE, SUSTAIN); e.c.time = 1.f;
		e.gateV[1] = 5.f;
		for (int k = 0; k < (int) SR; k++) e.process(DT);
		CHECK(std::fabs(e.mix - 4.f) < 0.01f, "shape MIX: 2N alone at 8 V gives 4 V (%.2f)", e.mix);
	}
	// FM: linear; clockwise moves all six, anticlockwise leaves IDENTITY alone
	{
		SlopeEngine a = make(SHAPE, CYCLE); a.c.time = 0.9f; a.c.fmKnob = 1.f; a.c.fmV = 2.5f; a.c.fmConnected = true;
		a.process(DT);
		double base = std::pow(2.0, (0.9 - 0.5) * 14.0);
		CHECK(std::fabs(a.s[0].freq / base - 1.5) < 1e-6, "FM clockwise: 2.5 V at full speeds up by 50%% (x%.3f)", a.s[0].freq / base);
		SlopeEngine b = make(SOUND, CYCLE); b.c.fmKnob = -1.f; b.c.fmConnected = true;
		b.c.fmV = 0.f; b.process(DT); double f0 = b.s[0].freq, f5 = b.s[5].freq;
		for (int k = 0; k < 100; k++) { b.c.fmV = (k % 2) ? 5.f : -5.f; b.process(DT); }
		CHECK(std::fabs(b.s[0].freq - f0) < 1e-6 && std::fabs(b.s[5].freq - f5) > 1.0, "FM anticlockwise: IDENTITY untouched, 6N moves");
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
			e.c.fmKnob = u(rng) * 2 - 1; e.c.fmV = u(rng) * 20 - 10; e.c.fmConnected = trial % 3 == 0; e.c.geode = trial % 7 == 0; e.c.polyPitch = trial % 5 == 0;
			for (int k = 0; k < 20000; k++) {
				for (int i = 0; i < 6; i++) e.gateV[i] = u(rng) > 0.995f ? 10.f * u(rng) : (k % 3000 < 1500 ? 5.f : 0.f);
				e.process(DT);
				for (int i = 0; i < 6; i++) if (!std::isfinite(e.s[i].out) || std::fabs(e.s[i].out) > 8.01f) okv = false;
				if (!std::isfinite(e.mix) || std::fabs(e.mix) > 8.01f) okv = false;
			}
		}
		CHECK(okv, "fuzz: outputs stay finite and in range");
	}
	std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
	return fails ? 1 : 0;
}
