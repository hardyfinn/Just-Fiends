// GroveEngine.hpp
// The engine for Mangrave: a formant oscillator. A square core runs at PITCH.
// Every core cycle fires a pulse of "pressure": a burst of a second wave at the
// FORMANT frequency that starts afresh each cycle and dies away. BARREL skews
// the burst's shape, AIR sets how long it lasts and how loud it is. In
// CONSTANT WAVE the formant is a fixed ratio of the pitch (the waveshape stays
// put as you play); in CONSTANT FORMANT it stays at a fixed frequency (like a
// vowel). Plain C++ with no Rack dependency, so test/grove_test.cpp can run it.
#pragma once
#include <cmath>
#include <algorithm>

namespace mg {

enum Constant { CONST_WAVE = 0, CONST_FORMANT = 1 };

struct Params {
	float pitch = 0.5f;     // 0..1, middle C at noon (8 Hz .. 8.4 kHz)
	float fine = 0.f;       // semitones, -1..1
	float voct = 0.f;       // V/OCT input
	float fm = 0.f;         // FM input volts x index (linear, through zero)
	float formant = 0.5f;   // 0..1
	float formantCv = 0.f;  // octaves
	float barrel = 0.f;     // -1..1
	float air = 0.7f;       // 0..1 (knob + CV)
	int constant = CONST_WAVE;
	bool strikePlucks = true;
};

inline double pitchHz(const Params& p) {
	return 261.6256 * std::pow(2.0, (p.pitch - 0.5) * 10.0 + p.fine / 12.0 + p.voct);
}
// CONSTANT WAVE: ratio of formant to pitch, 1/4 .. 16 (noon = x2)
inline double formantRatio(const Params& p) { return std::pow(2.0, (p.formant - 0.5) * 6.0 + 1.0 + p.formantCv); }
// CONSTANT FORMANT: fixed frequency, 60 Hz .. 7.7 kHz (noon = 680 Hz)
inline double formantHz(const Params& p) { return 60.0 * std::pow(2.0, p.formant * 7.0 + p.formantCv); }

// One burst of the formant wave: a skewed triangle that starts at 0 V.
inline double barrelWave(double ph, double barrel) {
	double r = 0.5 + 0.47 * std::max(-1.0, std::min(1.0, barrel));
	double x = ph + r * 0.5;
	x -= std::floor(x);
	return x < r ? -1.0 + 2.0 * x / r : 1.0 - 2.0 * (x - r) / (1.0 - r);
}

struct GroveEngine {
	double ph = 0.0;      // core phase 0..1
	double fph = 0.0;     // formant phase
	double t = 0.0;       // seconds since the burst started
	float pluck = 0.f;    // strike envelope
	float airSmooth = 0.f;
	bool strikeHigh = false;
	bool wrapped = false; // a core cycle started this sample (for the display)
	float square = 0.f, out = 0.f, env = 0.f;
	double f0 = 261.6, ff = 523.2;

	void burst() { fph = 0.0; t = 0.0; wrapped = true; }

	void process(const Params& p, float strikeV, float sr) {
		double dt = 1.0 / sr;
		wrapped = false;
		// strike: a trigger restarts the cycle and (optionally) puffs air through
		bool hi = strikeHigh ? strikeV > 0.3f : strikeV > 1.f;
		if (hi && !strikeHigh) {
			ph = 0.0;
			burst();
			if (p.strikePlucks) pluck = 1.f;
		}
		strikeHigh = hi;
		pluck *= (float) std::exp(-dt / 0.25);

		double base = pitchHz(p);
		double f = base * (1.0 + p.fm / 5.0);  // linear FM, can go through zero
		f = std::max(-sr * 0.45, std::min((double) sr * 0.45, f));
		f0 = f;
		ph += f * dt;
		if (ph >= 1.0) { ph -= std::floor(ph); burst(); }
		else if (ph < 0.0) { ph -= std::floor(ph); burst(); }

		ff = p.constant == CONST_WAVE ? std::fabs(base) * formantRatio(p) : formantHz(p);
		ff = std::min(ff, sr * 0.45);
		fph += ff * dt;
		t += dt;

		float air = std::max(0.f, std::min(1.f, std::max(p.air, p.strikePlucks ? pluck : 0.f)));
		airSmooth += (air - airSmooth) * (float) (1.0 - std::exp(-dt / 0.003));
		double a = airSmooth;
		// the burst dies away; more AIR = longer bursts (up to a steady tone)
		double period = 1.0 / std::max(1.0, std::fabs(f));
		double tau = period * (0.08 + 2.5 * a * a);
		env = (float) (std::exp(-t / tau) * a);
		out = (float) (5.0 * barrelWave(fph, p.barrel) * env);
		square = ph < 0.5 ? 5.f : -5.f;
		if (!std::isfinite(out)) { out = 0.f; ph = fph = t = 0.0; }
	}
};

}  // namespace mg
