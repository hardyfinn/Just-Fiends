// FiendsEngine.hpp
// The sound engine for Just Fiends: six linked slope generators that share one
// set of controls. Plain C++ with no Rack dependency, so test/engine_test.cpp
// can run it on its own.
//
// Each channel is a slope that rises from 0 to 1 and falls back. How it is
// started, held and repeated depends on RANGE (shape or sound), MODE
// (transient, sustain, cycle), whether RUN is on, and two extra modes from the
// menu (poly pitch "synth" and the "geode" rhythm machine).
#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace jf {

enum Range { SHAPE = 0, SOUND = 1 };
enum Mode { TRANSIENT = 0, SUSTAIN = 1, CYCLE = 2 };
enum Seg { IDLE = 0, RISE, HOLD, FALL };

// What the module is doing right now, for the display and tooltips.
enum Behaviour {
	B_ENVELOPE,  // shape, transient
	B_SHIFT,     // shape, transient, run
	B_ASR,       // shape, sustain
	B_STRATA,    // shape, sustain, run
	B_LFO,       // shape, cycle
	B_VOLLEY,    // shape, cycle, run
	B_IMPULSE,   // sound, transient
	B_SPILL,     // sound, transient, run
	B_TRAPEZOID, // sound, sustain
	B_PLUME,     // sound, sustain, run
	B_OSC,       // sound, cycle
	B_FLOOM,     // sound, cycle, run
};

inline const char* behaviourName(int b) {
	static const char* n[] = {"ENVELOPES", "SHIFT", "ASR", "STRATA", "LFOS", "VOLLEY",
		"IMPULSES", "SPILL", "TRAPEZOIDS", "PLUME", "OSCILLATORS", "FLOOM"};
	return n[b];
}

inline const char* behaviourHint(int b) {
	static const char* h[] = {
		"Each trigger fires one rise and fall. Triggers are ignored until it has finished, so a fast clock gets divided.",
		"RUN sets when a slope can be retriggered: -5 V any time, 0 V after the rise, +5 V only when done. A retrigger restarts from 0 V.",
		"The slope rises while the gate is high, holds at the top, falls when it goes low.",
		"Rise to the top, fall to the RUN level, hold while the gate is high, then fall. Hold a gate high: six slew limiters.",
		"Free-running LFOs. A trigger resets that channel to the start.",
		"Each trigger fires a burst. RUN: below -4 V choked, -4 V one, 0 V six, +5 V 36 cycles.",
		"Patch an audio-rate clock: each edge plays one impulse. Edges are ignored while it plays, so long ones give undertones.",
		"IDENTITY runs free and fires 2N-6N as impulses. RUN sets when they can retrigger. INTONE anticlockwise: subharmonics.",
		"Patch an audio-rate pulse: each slope rises while it is high and falls when low, so the width follows the pulse.",
		"Six oscillators through low pass gates. Gates hold notes open, triggers pluck. RUN up: shorter, down: longer and touchier.",
		"Six free-running oscillators. A trigger hard-syncs that channel.",
		"Each voice has its own sine modulator. RUN sets the ratio (-5 V half, 0 V same, +5 V double). FM knob sets the depth.",
	};
	return h[b];
}

struct Controls {
	float time = 0.5f;      // TIME knob, 0..1
	float voct[6] = {};     // TIME input volts per channel (all the same unless poly pitch)
	float intone = 0.f;     // -1..1, noon = 0
	float ramp = 0.f;       // -1..1, noon = triangle
	float curve = 0.f;      // -1..1, noon = linear
	float fmKnob = 0.f;     // FM knob -1..1: clockwise = TIME (all equally), anticlockwise = INTONE style
	float fmV = 0.f;        // FM input, volts
	bool fmConnected = false;
	bool run = false;
	float runV = 0.f;       // RUN knob + RUN input, volts (-5..5 is the useful range)
	int range = SHAPE;
	int mode = TRANSIENT;
	bool polyPitch = false; // "synth" mode: each channel plays its own pitch, Intone off
	bool geode = false;     // the geode rhythm machine drives the six gates
	int geodeRealign = 1;   // geode: realign the channels every N clocks
	bool velocity = false;  // sound range: the trigger's peak voltage sets loudness (5 V = full)
};

// Trigger normalling, as on the hardware: each unpatched jack follows the jack
// to its RIGHT, so one cable into 6N fires all six.
inline void normalTriggers(const float in[6], const bool patched[6], float out[6], bool conn[6]) {
	float v = 0.f;
	bool c = false;
	for (int i = 5; i >= 0; i--) {
		if (patched[i]) { v = in[i]; c = true; }
		out[i] = v;
		conn[i] = c;
	}
}

struct Slope {
	double lin = 0.0;       // the raw slope, 0..1
	int seg = IDLE;
	int dir = 1;            // -1 while through-zero FM runs it backwards
	float level = 1.f;      // velocity from the trigger voltage
	int velWait = 0;        // samples left to catch the trigger's peak
	int burstLeft = 0;      // volley: cycles still to play
	float lpg = 0.f;        // plume: low pass gate level
	float lpgHold = 0.f;    // plume: seconds a short trigger keeps the gate charging
	double mph = 0.0;       // floom: modulator phase
	bool gate = false;      // Schmitt state of this channel's gate
	bool wrapped = false;   // a cycle started this sample (for the display)
	float out = 0.f;        // volts
	double freq = 1.0;      // Hz
	float rise = 0.5f;      // rise fraction of the cycle
	// geode
	double gPhase = 0.0;
	int gClocksSinceReset = 0;
};

// The CURVE knob: rectangular, logarithmic, linear (noon), exponential, sine.
// rising = the slope is rising or holding (only matters for the rectangle).
inline float shapeCurve(float v, float c, bool rising) {
	v = std::min(1.f, std::max(0.f, v));
	if (c <= -0.5f) {
		float rect = rising ? 1.f : 0.f;
		float lg = 1.f - std::pow(1.f - v, 4.f);
		float w = (c + 1.f) / 0.5f;  // 0 at full CCW
		return rect + (lg - rect) * w;
	}
	if (c < 0.f) {
		float k = 1.f + 3.f * (-c / 0.5f);
		return 1.f - std::pow(1.f - v, k);
	}
	if (c <= 0.5f) {
		float k = 1.f + 3.f * (c / 0.5f);
		return std::pow(v, k);
	}
	float ex = std::pow(v, 4.f);
	float sn = 0.5f - 0.5f * std::cos((float) M_PI * v);
	float w = (c - 0.5f) / 0.5f;
	return ex + (sn - ex) * w;
}

struct SlopeEngine {
	Controls c;
	Slope s[6];
	float gateV[6] = {};      // gate/trigger voltage reaching each channel (after normalling)
	bool gateConn[6] = {};    // does anything reach this channel's gate? (display only)
	float mix = 0.f;
	int behaviour = B_ENVELOPE;
	float sampleRate = 48000.f;
	double fmDc = 0.0;        // DC blocker state for the FM input in sound range
	float plumeAtk = 0.004f, plumeDec = 0.25f;
	// geode clock
	bool geodeClockHigh = false;
	double geodePeriod = 0.5;
	double geodeSince = 1e9;  // seconds since the last clock
	bool geodeRunning = false;
	long geodeClockCount = 0;
	bool geodeGate[6] = {};

	// RUN as -1..1 (the hardware reads -5..+5 V; a dummy cable is 0 V)
	static float runPos(float v) { return std::min(1.f, std::max(-1.f, v / 5.f)); }
	// VOLLEY: envelopes per trigger. 0 = choked (below -4 V), 1 at -4 V, 6 at 0 V, 36 at +5 V.
	static int volleyCount(float v) {
		v = std::min(5.f, v);
		if (v < -4.f) return 0;
		if (v < 0.f) return (int) std::lround(1.f + (v + 4.f) * 1.25f);
		return (int) std::lround(6.f + v * 6.f);
	}
	// SHIFT and SPILL: how far through the slope (0 = start, rise = end of rise,
	// 1 = end) a slope must be before it takes a retrigger.
	static double retrigPoint(float v, double rise) {
		double x = runPos(v);
		return x < 0.0 ? rise * (1.0 + x) : rise + (1.0 - rise) * x;
	}
	// FLOOM: modulator : carrier ratio, x0.5 at -5 V, x1 at 0 V, x2 at +5 V.
	static double floomRatio(float v) { return std::pow(2.0, (double) runPos(v)); }
	// PLUME: vactrol attack and decay; RUN 0 V is VTL5C3-like, positive is snappier.
	static float plumeAttack(float v) { return 0.004f * std::pow(2.f, -runPos(v) * 2.5f); }
	static float plumeDecay(float v) { return 0.25f * std::pow(2.f, -runPos(v) * 3.f); }

	int computeBehaviour() const {
		int b = c.range == SHAPE ? c.mode * 2 : 6 + c.mode * 2;
		if (c.run) b += 1;
		return b;
	}

	// Ratio of channel n (1..6) to IDENTITY from the INTONE knob.
	static double intoneRatio(int n, float intone) { return std::pow((double) n, (double) intone); }

	double baseOctaves(int i) const {
		double o = c.polyPitch ? c.voct[i] : std::min(5.f, std::max(-2.f, c.voct[i]));  // the TIME jack clamps at -2..+5 V
		if (c.range == SHAPE) return (c.time - 0.5) * 14.0 + o;          // ~0.008 Hz .. 128 Hz, 1 Hz at noon
		return std::log2(261.6256) + (c.time - 0.5) * 10.0 + o;          // ~8 Hz .. 8.4 kHz, middle C at noon
	}

	bool freeRunning(int b, int i) const {
		return b == B_LFO || b == B_PLUME || b == B_OSC || b == B_FLOOM || (b == B_SPILL && i == 0);
	}

	static double progress(const Slope& sl) {
		switch (sl.seg) {
			case RISE: return sl.lin * sl.rise;
			case HOLD: return sl.rise;
			case FALL: return sl.rise + (1.0 - sl.lin) * (1.0 - sl.rise);
			default: return 1.0;
		}
	}
	bool receptive(const Slope& sl) const {
		return sl.seg == IDLE || progress(sl) >= retrigPoint(c.runV, sl.rise) - 1e-9;
	}

	// geode: turn the clock at IDENTITY into six gate patterns.
	void processGeode(float dt) {
		bool clk = geodeClockHigh ? gateV[0] > 0.5f : gateV[0] > 1.f;
		bool edge = clk && !geodeClockHigh;
		geodeClockHigh = clk;
		geodeSince += dt;
		if (edge) {
			if (geodeSince < 30.0 && geodeSince > 0.001) geodePeriod = geodeSince;
			geodeSince = 0.0;
			geodeRunning = true;
			geodeClockCount++;
			int every = std::max(1, c.geodeRealign);
			for (int i = 0; i < 6; i++) {
				Slope& sl = s[i];
				double r = intoneRatio(i + 1, c.intone);
				sl.gClocksSinceReset++;
				int need = every;
				if (r < 0.999) need = every * std::max(1, (int) std::lround(1.0 / r));
				if (sl.gClocksSinceReset >= need || geodeClockCount == 1) {
					sl.gPhase = 0.0;
					sl.gClocksSinceReset = 0;
				}
			}
		}
		if (geodeRunning && geodeSince > 4.0 * geodePeriod + 0.05) geodeRunning = false;
		for (int i = 0; i < 6; i++) {
			Slope& sl = s[i];
			if (!geodeRunning) { geodeGate[i] = false; continue; }
			double r = intoneRatio(i + 1, c.intone);
			sl.gPhase += r * dt / geodePeriod;
			double f = sl.gPhase - std::floor(sl.gPhase);
			geodeGate[i] = f < 0.5;
		}
	}

	void trigger(Slope& sl, int b, int i) {
		switch (b) {
			case B_ENVELOPE:
			case B_IMPULSE:
				// the hardware ignores triggers for the whole rise and fall
				if (sl.seg == IDLE) { sl.lin = 0.0; sl.seg = RISE; }
				break;
			case B_SHIFT:
				if (receptive(sl)) { sl.lin = 0.0; sl.seg = RISE; }
				break;
			case B_ASR:
			case B_TRAPEZOID:
			case B_STRATA:
				sl.seg = RISE;  // resumes rising from wherever it is
				break;
			case B_LFO:
			case B_OSC:
			case B_FLOOM:
				sl.lin = 0.0;
				sl.seg = RISE;
				break;
			case B_SPILL:
				if (i == 0) { sl.lin = 0.0; sl.seg = RISE; }
				else spillFire(sl);
				break;
			case B_VOLLEY: {
				int n = volleyCount(c.runV);
				if (n <= 0) break;
				sl.lin = 0.0;
				sl.seg = RISE;
				sl.burstLeft = n - 1;
				break;
			}
			case B_PLUME: {
				float x = runPos(c.runV);
				// a short trigger still plucks fully at 0 V and above; below 0 V
				// the gate length matters more and more (shorter = softer)
				sl.lpgHold = 3.f * plumeAtk * (x >= 0.f ? 1.f : 1.f + x);
				break;
			}
			default: break;
		}
	}

	// SPILL: IDENTITY's end of cycle (or a trigger) fires a slope; an active
	// slope that is receptive turns round from where it is.
	void spillFire(Slope& sl) {
		if (sl.seg == IDLE) { sl.lin = 0.0; sl.seg = RISE; }
		else if (sl.seg == FALL && receptive(sl)) sl.seg = RISE;
	}

	// Move the slope on by dt seconds. Carries leftover time across the top and
	// bottom so audio-rate cycles keep accurate pitch. Looping slopes can run
	// backwards (through-zero FM).
	void advance(Slope& sl, int b, double dt, bool loop) {
		double T = 1.0 / sl.freq;
		double r = sl.rise;
		double riseRate = 1.0 / (r * T), fallRate = 1.0 / ((1.0 - r) * T);
		bool holdTop = b == B_ASR || b == B_TRAPEZOID;
		double t = dt;
		if (loop && sl.dir < 0) {
			if (sl.seg == IDLE || sl.seg == HOLD) sl.seg = RISE;
			for (int k = 0; k < 6 && t > 0.0; k++) {
				if (sl.seg == RISE) {
					double need = sl.lin / riseRate;
					if (t < need) { sl.lin -= riseRate * t; t = 0.0; }
					else { t -= need; sl.lin = 0.0; sl.seg = FALL; sl.wrapped = true; }
				}
				else {
					double need = (1.0 - sl.lin) / fallRate;
					if (t < need) { sl.lin += fallRate * t; t = 0.0; }
					else { t -= need; sl.lin = 1.0; sl.seg = RISE; }
				}
			}
			return;
		}
		for (int k = 0; k < 6 && t > 0.0; k++) {
			if (sl.seg == RISE) {
				double need = (1.0 - sl.lin) / riseRate;
				if (t < need) { sl.lin += riseRate * t; t = 0.0; }
				else {
					t -= need;
					sl.lin = 1.0;
					if (holdTop) { sl.seg = HOLD; t = 0.0; }
					else sl.seg = FALL;
				}
			}
			else if (sl.seg == FALL) {
				double need = sl.lin / fallRate;
				if (t < need) { sl.lin -= fallRate * t; t = 0.0; }
				else {
					t -= need;
					sl.lin = 0.0;
					if (loop) { sl.seg = RISE; sl.wrapped = true; }
					else if (b == B_VOLLEY && sl.burstLeft > 0) { sl.burstLeft--; sl.seg = RISE; sl.wrapped = true; }
					else { sl.seg = IDLE; t = 0.0; }
				}
			}
			else t = 0.0;
		}
	}

	// STRATA: attack to the top, release-1 to the sustain level, hold while the
	// gate is high (following the level like a slew limiter), release-2 to 0.
	void advanceStrata(Slope& sl, bool high, double sus, double dt) {
		double T = 1.0 / sl.freq;
		double up = 1.0 / (sl.rise * T), down = 1.0 / ((1.0 - sl.rise) * T);
		if (sl.seg == RISE) {
			sl.lin += up * dt;
			if (sl.lin >= 1.0) { sl.lin = 1.0; sl.seg = FALL; }
		}
		else if (sl.seg == FALL) {
			double before = sl.lin;
			sl.lin -= down * dt;
			if (high && before >= sus && sl.lin <= sus) { sl.lin = sus; sl.seg = HOLD; }
			else if (high && before < sus) sl.seg = HOLD;  // the level moved above it
			else if (sl.lin <= 0.0) { sl.lin = 0.0; sl.seg = IDLE; }
		}
		else if (sl.seg == HOLD) {
			if (sl.lin < sus) sl.lin = std::min(sus, sl.lin + up * dt);
			else sl.lin = std::max(sus, sl.lin - down * dt);
		}
	}

	void process(float dt) {
		int b = computeBehaviour();
		behaviour = b;
		if (c.geode) processGeode(dt);
		float r = 0.5f + 0.5f * std::min(1.f, std::max(-1.f, c.ramp)) * 0.998f;
		float x = runPos(c.runV);
		plumeAtk = plumeAttack(c.runV);
		plumeDec = plumeDecay(c.runV);
		double sus = (x + 1.0) * 0.5;  // strata: -5..+5 V -> 0..8 V, before CURVE

		// FM input: DC-coupled in shape range, DC-blocked in sound range
		fmDc += (c.fmV - fmDc) * (1.0 - std::exp(-2.0 * M_PI * 4.0 * dt));
		double fmIn = c.range == SOUND ? c.fmV - fmDc : c.fmV;
		double fmLin = 0.0, fmInt = 0.0, floomDepth = 0.0;
		if (b == B_FLOOM) {
			// the FM jack becomes a depth CV for the internal modulators
			floomDepth = std::fabs(c.fmKnob) * (c.fmConnected ? c.fmV / 5.0 : 1.0);
		}
		else if (c.fmKnob > 0.f) fmLin = c.fmKnob * fmIn / 5.0;
		else fmInt = -c.fmKnob * fmIn / 5.0;
		double ratio = floomRatio(c.runV);

		for (int i = 0; i < 6; i++) {
			Slope& sl = s[i];
			sl.wrapped = false;
			sl.rise = r;
			bool loop = freeRunning(b, i);
			// pitch / rate: TIME (exponential), INTONE, then linear through-zero FM
			double f = std::pow(2.0, baseOctaves(i));
			if (!c.polyPitch) f *= intoneRatio(i + 1, std::min(2.0, std::max(-2.0, c.intone + fmInt)));
			double fs = f * (1.0 + fmLin);
			if (b == B_FLOOM) {
				sl.mph += f * ratio * dt;
				sl.mph -= std::floor(sl.mph);
				double w = c.fmKnob >= 0.f ? 1.0 : i / 5.0;  // anticlockwise: by index, IDENTITY untouched
				fs = f * (1.0 + 3.0 * floomDepth * w * std::sin(2.0 * M_PI * sl.mph));
			}
			sl.dir = (loop && fs < 0.0) ? -1 : 1;
			sl.freq = std::min((double) sampleRate * 0.45, std::max(1e-4, std::fabs(fs)));
			if (!loop && fs < 0.0) sl.freq = 1e-4;  // one-shots stall rather than run backwards

			// gate edges (threshold around 1 V, as on the hardware)
			float gv = c.geode ? (geodeGate[i] ? 5.f : 0.f) : gateV[i];
			bool high = sl.gate ? gv > 0.5f : gv > 1.f;
			bool rising = high && !sl.gate;
			sl.gate = high;
			if (rising) {
				sl.level = c.velocity ? std::min(1.f, std::max(0.f, gv / 5.f)) : 1.f;
				sl.velWait = c.velocity ? (int) (0.003f / dt) : 0;
				trigger(sl, b, i);
			}
			else if (sl.velWait > 0) {
				// catch the trigger's peak, so slow edges still play loud
				sl.velWait--;
				sl.level = std::max(sl.level, std::min(1.f, gv / 5.f));
			}
			if (b == B_SPILL && i > 0 && s[0].wrapped) spillFire(sl);

			bool gated = b == B_ASR || b == B_TRAPEZOID || b == B_STRATA;
			if (gated && !high && (sl.seg == RISE || sl.seg == HOLD)) sl.seg = FALL;
			if (!gated && sl.seg == HOLD) sl.seg = FALL;
			if (b == B_VOLLEY && volleyCount(c.runV) <= 0) sl.burstLeft = 0;
			if (loop && sl.seg == IDLE) sl.seg = RISE;

			// move
			if (b == B_STRATA) advanceStrata(sl, high, sus, dt);
			else advance(sl, b, dt, loop);

			// plume: a vactrol-style low pass gate, held open by a high gate
			if (b == B_PLUME) {
				if (sl.lpgHold > 0.f) sl.lpgHold -= dt;
				float target = (high || sl.lpgHold > 0.f) ? sl.level : 0.f;
				float tau = target > sl.lpg ? plumeAtk : plumeDec;
				sl.lpg += (target - sl.lpg) * (1.f - std::exp(-dt / tau));
			}
			else { sl.lpg = 0.f; sl.lpgHold = 0.f; }

			// output
			bool risingSeg = sl.seg == RISE || sl.seg == HOLD;
			if (c.range == SHAPE) {
				sl.out = shapeCurve((float) sl.lin, c.curve, risingSeg) * 8.f;
			}
			else {
				float cv = c.curve, a = 1.f;
				if (b == B_PLUME) {
					float e = std::min(1.f, sl.lpg);
					cv = 1.f - (1.f - c.curve) * e;  // darker (towards sine) as the gate closes
					a = sl.lpg;
				}
				else if (b == B_IMPULSE || b == B_TRAPEZOID || (b == B_SPILL && i > 0)) a = sl.level;
				// -5..+5 V; impulses and trapezoids rest at -5 V
				sl.out = (shapeCurve((float) sl.lin, cv, risingSeg) * 2.f - 1.f) * 5.f * a;
			}
			if (!std::isfinite(sl.out)) { sl.out = 0.f; sl.lin = 0.0; sl.seg = IDLE; }
		}

		// MIX. Shape: each slope divided by its number, then the highest.
		// Sound: an equal mix, tanh-limited to about 15 V peak to peak.
		if (c.range == SHAPE) {
			float m = 0.f;
			for (int i = 0; i < 6; i++) m = std::max(m, s[i].out / (float) (i + 1));
			mix = m;
		}
		else {
			float sum = 0.f;
			for (int i = 0; i < 6; i++) sum += s[i].out;
			mix = 7.5f * std::tanh(sum / 7.5f);
		}
	}
};

}  // namespace jf
