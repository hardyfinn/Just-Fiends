// GroveEngine.hpp
// The engine for Mangrave, modelled on the Mangrove technical map plus the
// PPM ("Patch Programmable Mangrove") additions.
//
//  CORE     a triangle-core oscillator (PITCH, FINE, V/OCT, linear through-zero
//           FM). It gives SQUARE, and with PPM also SINE and SAW, all moulded
//           from the triangle.
//  IMPULSE  an AR envelope that rises to +1 and falls to -1, then rests. Each
//           rising edge of the core fires it, but only when it is resting, so
//           an impulse longer than the core period skips cycles: undertones.
//           FORMANT sets the impulse duration (clockwise = shorter), BARREL
//           the rise/fall balance (ramp, triangle, saw). CONSTANT WAVE adds the
//           core frequency to FORMANT so the duration is a fixed share of the
//           period; CONSTANT FORMANT keeps the duration fixed.
//  AIR      a VCA then a sine-shaping overdrive: clean low, 10 V peak to peak
//           at noon, squared off at full.
//  PPM      a mode switch: IMPULSE (classic), SYNC (the impulse cycles on its
//           own and the core soft-syncs it), CYCLE (the impulse is a second
//           free oscillator, pitched by FORMANT). TRIGGER replaces the core as
//           the impulse's trigger, TO AIR adds a signal into the AIR stage,
//           PWM is the impulse as a pulse, HOLLOW a divided pulse.
// Plain C++ with no Rack dependency, so test/grove_test.cpp can run it.
#pragma once
#include <cmath>
#include <algorithm>

namespace mg {

enum Constant { CONST_WAVE = 0, CONST_FORMANT = 1 };
enum PpmMode { PPM_IMPULSE = 0, PPM_SYNC = 1, PPM_CYCLE = 2 };
enum Seg { REST = 0, RISE, FALL };

struct Params {
	float pitch = 0.5f;     // 0..1, middle C at noon (8 Hz .. 8.4 kHz)
	float fine = 0.f;       // semitones, -6..6 (one octave in total)
	float voct = 0.f;       // V/OCT input
	float fm = 0.f;         // FM input volts x index (linear, through zero)
	float formant = 0.5f;   // 0..1 knob plus CV (+-5 V = half the knob)
	float barrel = 0.f;     // -1..1: ramp, triangle (noon), saw
	float air = 0.5f;       // 0..1: closed, clean, 10 Vpp at noon, overdriven
	int constant = CONST_WAVE;
	int ppm = PPM_IMPULSE;
	float toAir = 0.f;      // PPM TO AIR input, volts
};

inline double pitchHz(const Params& p) {
	return 261.6256 * std::pow(2.0, (p.pitch - 0.5) * 10.0 + p.fine / 12.0 + p.voct);
}
// CONSTANT WAVE: impulse duration as a share of the core period (x0.8 at noon;
// above x1 the impulse skips core cycles and the pitch drops into undertones).
inline double durationRatio(float formant) { return 0.8 * std::pow(2.0, -(formant - 0.5) * 8.0); }
// CONSTANT FORMANT: a fixed impulse duration, 50 ms .. 0.05 ms (1/duration = 20 Hz .. 20 kHz).
inline double formantHz(float formant) { return 20.0 * std::pow(2.0, std::max(0.f, std::min(1.f, formant)) * 10.0); }
// Rise share of the impulse: BARREL CCW = all rise (ramp), noon = half, CW = all fall (saw).
inline double riseShare(float barrel) { return std::max(0.002, std::min(0.998, 0.5 - 0.5 * barrel)); }
// AIR: gain into the shaper. 0 = closed, 1 at noon, 4 at full.
inline double airGain(float air) {
	air = std::max(0.f, std::min(1.f, air));
	return air < 0.5f ? 2.0 * air : 1.0 + (air - 0.5) * 6.0;
}
inline double shaper(double x) { return std::tanh(1.4 * x) / std::tanh(1.4); }

struct GroveEngine {
	double ph = 0.0;          // core phase 0..1 (rising edge of SQUARE at 0)
	double v = -1.0;          // impulse level -1..1
	int seg = REST;
	bool syncHigh = false, trigHigh = false;
	bool hollow = false;
	bool started = false;     // an impulse started this sample (for the display and tests)
	bool coreEdge = false;    // the core started a cycle this sample
	double f0 = 261.6, dur = 0.003;
	// outputs, volts
	float square = 0.f, sine = 0.f, saw = 0.f, tri = 0.f;
	float out = 0.f, pwm = 0.f, hol = 0.f;

	void fire(bool softOnly) {
		// soft sync / retrigger rules: a resting impulse always starts; in
		// SYNC and CYCLE a trigger also restarts it while it is falling
		if (seg == REST || (softOnly && seg == FALL)) {
			seg = RISE;
			started = true;
			hollow = !hollow;
		}
	}

	void process(const Params& p, float syncV, float trigV, bool trigPatched, float sr) {
		double dt = 1.0 / sr;
		started = false;
		coreEdge = false;

		// SYNC input: resets the core
		bool sh = syncHigh ? syncV > 0.3f : syncV > 1.f;
		if (sh && !syncHigh) { ph = 0.0; coreEdge = true; }
		syncHigh = sh;

		// core
		double base = pitchHz(p);
		double f = base * (1.0 + p.fm / 5.0);
		f = std::max(-sr * 0.45, std::min((double) sr * 0.45, f));
		f0 = f;
		ph += f * dt;
		if (ph >= 1.0) { ph -= std::floor(ph); coreEdge = true; }
		else if (ph < 0.0) { ph -= std::floor(ph); }
		double t = ph < 0.5 ? 4.0 * ph - 1.0 : 3.0 - 4.0 * ph;
		tri = (float) (5.0 * t);
		sine = (float) (5.0 * std::sin(M_PI * 0.5 * t));
		saw = (float) (5.0 * (2.0 * ph - 1.0));
		square = ph < 0.5 ? 5.f : -5.f;

		// impulse duration
		double period = 1.0 / std::max(0.5, std::fabs(base));
		dur = p.constant == CONST_WAVE ? period * durationRatio(p.formant) : 1.0 / formantHz(p.formant);
		dur = std::max(dur, 2.0 / sr);
		double r = riseShare(p.barrel);
		double riseRate = 2.0 / (dur * r), fallRate = 2.0 / (dur * (1.0 - r));

		// triggers
		bool th = trigHigh ? trigV > 0.3f : trigV > 1.f;
		bool trigEdge = th && !trigHigh;
		trigHigh = th;
		bool coreTrig = coreEdge && !trigPatched;  // TRIGGER overrides the core's normal
		if (p.ppm == PPM_IMPULSE) {
			if (trigPatched ? trigEdge : coreTrig) fire(false);
		}
		else if (p.ppm == PPM_SYNC) {
			if (trigPatched ? trigEdge : coreTrig) fire(true);
		}
		else {  // CYCLE: only the TRIGGER jack soft-syncs it
			if (trigPatched && trigEdge) fire(true);
		}
		bool loop = p.ppm != PPM_IMPULSE;
		if (loop && seg == REST) fire(false);

		// move the impulse, carrying leftover time so cycling keeps its pitch
		double left = dt;
		for (int k = 0; k < 6 && left > 0.0; k++) {
			if (seg == RISE) {
				double need = (1.0 - v) / riseRate;
				if (left < need) { v += riseRate * left; left = 0.0; }
				else { left -= need; v = 1.0; seg = FALL; }
			}
			else if (seg == FALL) {
				double need = (v + 1.0) / fallRate;
				if (left < need) { v -= fallRate * left; left = 0.0; }
				else {
					left -= need; v = -1.0; seg = REST;
					if (loop) fire(false);
					else left = 0.0;
				}
			}
			else left = 0.0;
		}

		// AIR: VCA, then the sine-shaping overdrive
		double u = v + p.toAir / 5.0;
		double g = airGain(p.air);
		out = (float) (5.0 * shaper(g * u));
		pwm = seg == RISE ? 5.f : -5.f;
		hol = hollow ? 5.f : -5.f;
		if (!std::isfinite(out)) { out = 0.f; v = -1.0; seg = REST; ph = 0.0; }
	}
};

}  // namespace mg
