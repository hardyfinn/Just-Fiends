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
	B_GATED,     // sound, sustain
	B_PLUME,     // sound, sustain, run
	B_OSC,       // sound, cycle
	B_FLOOM,     // sound, cycle, run
};

inline const char* behaviourName(int b) {
	static const char* n[] = {"ENVELOPES", "SHIFT", "ASR", "STRATA", "LFOS", "VOLLEY",
		"IMPULSES", "SPILL", "GATED OSC", "PLUME", "OSCILLATORS", "FLOOM"};
	return n[b];
}

inline const char* behaviourHint(int b) {
	static const char* h[] = {
		"Each trigger fires one rise and fall. Triggers during the rise are ignored.",
		"RUN moves the retrigger point: low = any trigger restarts, high = only after the slope ends.",
		"The slope rises while the gate is high, holds at the top, falls when it goes low.",
		"Six slew limiters follow the RUN voltage. A low gate freezes that channel.",
		"Free-running LFOs. A trigger resets that channel to the start.",
		"Each trigger fires a burst of cycles. RUN sets how many (1 to 16).",
		"Each trigger plays one cycle of the wave: clicks, blips, impulse trains.",
		"Each trigger spills a burst of tone. RUN sets how long it lasts.",
		"Six oscillators that sound while their gate is high. Gate voltage sets the level.",
		"Triggers pluck the voices like a low pass gate. RUN sets the decay.",
		"Six free-running oscillators. A trigger hard-syncs that channel.",
		"Each voice frequency-modulates the next one. RUN sets the depth.",
	};
	return h[b];
}

struct Controls {
	float time = 0.5f;      // TIME knob, 0..1
	float voct[6] = {};     // TIME input volts per channel (all the same unless poly pitch)
	float intone = 0.f;     // -1..1, noon = 0
	float ramp = 0.f;       // -1..1, noon = triangle
	float curve = 0.f;      // -1..1, noon = linear
	float fmOct = 0.f;      // FM in octaves, already scaled by the FM knob
	bool run = false;
	float runV = 0.f;       // RUN knob + RUN input, volts
	int range = SHAPE;
	int mode = TRANSIENT;
	bool polyPitch = false; // "synth" mode: each channel plays its own pitch, Intone off
	bool geode = false;     // the geode rhythm machine drives the six gates
	int geodeRealign = 1;   // geode: realign the channels every N clocks
	bool velocity = true;   // sound range: gate voltage sets loudness (5 V = full)
};

struct Slope {
	double lin = 0.0;       // the raw slope, 0..1
	int seg = IDLE;
	float level = 1.f;      // velocity from the trigger voltage
	float amp = 0.f;        // sound range amplitude (smoothed)
	float ampTarget = 0.f;
	int burstLeft = 0;      // volley: cycles still to play
	float spillLeft = 0.f;  // spill: seconds of tone left
	float pluck = 0.f;      // plume envelope
	float pluckAtt = 0.f;   // plume: >0 while the pluck is still attacking
	bool gate = false;      // Schmitt state of this channel's gate
	bool wrapped = false;   // a cycle started this sample (for the display)
	float out = 0.f;        // volts
	float bip = 0.f;        // last bipolar value -1..1 (for floom)
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
	bool gateConn[6] = {};    // does anything reach this channel's gate?
	float mix = 0.f;
	int behaviour = B_ENVELOPE;
	float sampleRate = 48000.f;
	// geode clock
	bool geodeClockHigh = false;
	double geodePeriod = 0.5;
	double geodeSince = 1e9;  // seconds since the last clock
	bool geodeRunning = false;
	long geodeClockCount = 0;
	bool geodeGate[6] = {};

	static float runFrac(float v) { return std::min(1.f, std::max(0.f, v / 5.f)); }

	int computeBehaviour() const {
		int b = c.range == SHAPE ? c.mode * 2 : 6 + c.mode * 2;
		if (c.run) b += 1;
		return b;
	}

	// Ratio of channel n (1..6) to IDENTITY from the INTONE knob.
	static double intoneRatio(int n, float intone) { return std::pow((double) n, (double) intone); }

	double baseOctaves(int i) const {
		double o = c.voct[i] + c.fmOct;
		if (c.range == SHAPE) return (c.time - 0.5) * 14.0 + o;          // ~0.008 Hz .. 128 Hz, 1 Hz at noon
		return std::log2(261.6256) + (c.time - 0.5) * 10.0 + o;          // ~8 Hz .. 8.4 kHz, middle C at noon
	}

	bool freeRunning(int b) const {
		return b == B_LFO || b == B_GATED || b == B_PLUME || b == B_OSC || b == B_FLOOM || b == B_SPILL;
	}

	// geode: turn the clock at IDENTITY into six gate patterns.
	void processGeode(float dt) {
		bool clk = geodeClockHigh ? gateV[0] > 0.3f : gateV[0] > 0.6f;
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

	void trigger(Slope& sl, int b) {
		double progress;
		switch (b) {
			case B_ENVELOPE:
			case B_IMPULSE:
				if (sl.seg == IDLE || sl.seg == FALL) sl.seg = RISE;
				break;
			case B_SHIFT:
				progress = sl.seg == RISE ? sl.lin * sl.rise
					: sl.seg == HOLD ? sl.rise
					: sl.seg == FALL ? sl.rise + (1.0 - sl.lin) * (1.0 - sl.rise) : 1.0;
				if (sl.seg == IDLE || progress >= runFrac(c.runV) - 1e-6) {
					if (sl.seg == RISE) sl.lin = 0.0;
					sl.seg = RISE;
				}
				break;
			case B_ASR:
				sl.seg = RISE;
				break;
			case B_LFO:
			case B_OSC:
			case B_FLOOM:
				sl.lin = 0.0;
				sl.seg = RISE;
				break;
			case B_VOLLEY:
				sl.lin = 0.0;
				sl.seg = RISE;
				sl.burstLeft = (int) std::lround(runFrac(c.runV) * 15.f);
				break;
			case B_SPILL:
				if (sl.amp < 0.01f) { sl.lin = 0.0; sl.seg = RISE; }
				sl.spillLeft = 0.005f * std::pow(2.f, runFrac(c.runV) * 8.6f);
				break;
			case B_GATED:
				if (sl.amp < 0.01f) { sl.lin = 0.0; sl.seg = RISE; }
				break;
			case B_PLUME:
				sl.pluckAtt = sl.level;
				break;
			default: break;
		}
	}

	// Move the slope on by dt seconds. Carries leftover time across the top and
	// bottom so audio-rate cycles keep accurate pitch.
	void advance(Slope& sl, int b, double dt) {
		double T = 1.0 / sl.freq;
		double r = sl.rise;
		double riseRate = 1.0 / (r * T), fallRate = 1.0 / ((1.0 - r) * T);
		bool holdTop = b == B_ASR;
		bool loop = freeRunning(b);
		double t = dt;
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

	void process(float dt) {
		int b = computeBehaviour();
		behaviour = b;
		if (c.geode) processGeode(dt);
		float r = 0.5f + 0.5f * std::min(1.f, std::max(-1.f, c.ramp)) * 0.998f;
		float ampCoef = 1.f - std::exp(-dt / 0.002f);
		float decay = 0.02f * std::pow(2.f, runFrac(c.runV) * 7.6f);  // plume: 20 ms .. 3.9 s
		float decayCoef = std::exp(-dt / decay);
		float floomIdx = runFrac(c.runV) * 3.f;
		float prevBip[6];
		for (int i = 0; i < 6; i++) prevBip[i] = s[i].bip;

		for (int i = 0; i < 6; i++) {
			Slope& sl = s[i];
			sl.wrapped = false;
			sl.rise = r;
			// pitch / rate
			double oct = baseOctaves(i);
			double f = std::pow(2.0, oct);
			if (!c.polyPitch) f *= intoneRatio(i + 1, c.intone);
			if (b == B_FLOOM) f *= std::max(0.0, 1.0 + floomIdx * prevBip[(i + 5) % 6]);
			sl.freq = std::min((double) sampleRate * 0.45, std::max(1e-4, f));

			// gate edges
			float gv = c.geode ? (geodeGate[i] ? 5.f : 0.f) : gateV[i];
			bool high = sl.gate ? gv > 0.3f : gv > 0.6f;
			bool rising = high && !sl.gate, falling = !high && sl.gate;
			sl.gate = high;
			if (rising) {
				sl.level = c.velocity && c.range == SOUND ? std::min(1.f, std::max(0.f, gv / 5.f)) : 1.f;
				trigger(sl, b);
			}
			if (falling && b == B_ASR && (sl.seg == RISE || sl.seg == HOLD)) sl.seg = FALL;
			if (b == B_ASR && sl.seg == HOLD && !high) sl.seg = FALL;
			if (b != B_ASR && sl.seg == HOLD) sl.seg = FALL;
			if (freeRunning(b) && sl.seg == IDLE) sl.seg = RISE;

			// move
			if (b == B_STRATA) {
				bool track = high || !(gateConn[i] || c.geode);
				if (track) {
					double target = std::min(8.f, std::max(0.f, c.runV)) / 8.0;
					double T = 1.0 / sl.freq;
					double up = 1.0 / (r * T), down = 1.0 / ((1.0 - r) * T);
					if (sl.lin < target) sl.lin = std::min(target, sl.lin + up * dt);
					else sl.lin = std::max(target, sl.lin - down * dt);
				}
				sl.seg = IDLE;
			}
			else advance(sl, b, dt);

			// amplitude for sound range
			switch (b) {
				case B_GATED: sl.ampTarget = high ? sl.level : 0.f; break;
				case B_SPILL:
					sl.spillLeft -= dt;
					sl.ampTarget = sl.spillLeft > 0.f ? sl.level : 0.f;
					break;
				case B_OSC: case B_FLOOM: sl.ampTarget = 1.f; break;
				default: sl.ampTarget = 1.f; break;
			}
			sl.amp += (sl.ampTarget - sl.amp) * ampCoef;
			if (b == B_PLUME) {
				if (sl.pluckAtt > 0.f) {
					sl.pluck += (sl.pluckAtt - sl.pluck) * (1.f - std::exp(-dt / 0.001f));
					if (sl.pluck >= sl.pluckAtt * 0.98f) sl.pluckAtt = 0.f;
				}
				else sl.pluck *= decayCoef;
			}
			else { sl.pluck = 0.f; sl.pluckAtt = 0.f; }

			// output
			bool risingSeg = sl.seg == RISE || sl.seg == HOLD;
			if (c.range == SHAPE) {
				if (b == B_STRATA) sl.out = (float) sl.lin * 8.f;
				else sl.out = shapeCurve((float) sl.lin, c.curve, risingSeg) * 8.f;
				sl.bip = sl.out / 4.f - 1.f;
			}
			else if (b == B_IMPULSE) {
				sl.out = shapeCurve((float) sl.lin, c.curve, risingSeg) * 5.f * sl.level;
				sl.bip = sl.out / 5.f;
			}
			else {
				float cv = c.curve;
				float a = sl.amp;
				if (b == B_PLUME) {
					float e = std::min(1.f, sl.pluck);
					cv = 1.f - (1.f - c.curve) * e;  // darker (towards sine) as it decays
					a = sl.pluck;
				}
				float v = shapeCurve((float) sl.lin, cv, risingSeg) * 2.f - 1.f;
				sl.bip = v;
				sl.out = v * 5.f * a;
			}
			if (!std::isfinite(sl.out)) { sl.out = 0.f; sl.lin = 0.0; sl.seg = IDLE; }
		}

		// MIX: the highest slope in shape range, a soft-clipped sum in sound range
		if (c.range == SHAPE) {
			float m = 0.f;
			for (int i = 0; i < 6; i++) m = std::max(m, s[i].out);
			mix = m;
		}
		else {
			float sum = 0.f;
			for (int i = 0; i < 6; i++) sum += s[i].out;
			mix = 10.f * std::tanh(sum * 0.4f / 10.f);
		}
	}
};

}  // namespace jf
