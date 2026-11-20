// SistersEngine.hpp
// The engine for Weird Sisters: three linked filters (LOW, CENTRE, HIGH) that
// share one FREQ. SPAN pushes LOW down and HIGH up from the centre, QUALITY
// sets resonance (clockwise) or anti-resonance (anticlockwise), and MODE picks
// crossover (low pass / band pass / high pass) or formant (three band passes).
// Plain C++ with no Rack dependency, so test/sisters_test.cpp can run it alone.
#pragma once
#include <cmath>
#include <algorithm>
#include <complex>

namespace ws {

enum Mode { CROSSOVER = 0, FORMANT = 1 };

struct Params {
	float freqKnob = 0.5f;  // 0..1 (20 Hz .. 20 kHz, about 640 Hz at noon)
	float voct = 0.f;       // FREQ input, 1 V/oct
	float fmOct = 0.f;      // FM in octaves (already scaled)
	float span = 0.3f;      // 0..1 knob
	float spanCv = 0.f;     // octaves added to the span
	float quality = 0.f;    // -1..1, noon = flat
	int mode = CROSSOVER;
};

inline double knobHz(float knob, float oct) { return 20.0 * std::pow(2.0, knob * 10.0 + oct); }
inline double spanOctaves(float span, float cv) { return std::min(6.0, std::max(0.0, span * 4.0 + cv)); }
// Resonance damping: 1.414 (flat, Butterworth) at noon down to near 0 (self-oscillation).
inline double dampK(float q) {
	if (q <= 0.f) return 1.41421356;
	double r = 1.0 - q;
	return 1.41421356 * r * r - 0.015;  // slightly negative at full: it sings on its own
}

// Gain for the band pass output: unity at the centre while flat, rising towards
// 1 / k near self-oscillation so the ringing is heard.
inline double bpGain(double k) { return k >= 0.25 ? k : k > 0.0 ? 1.0 - 3.0 * k : 1.0; }

// One 2-pole state-variable filter (Zavalishin's topology-preserving form).
struct SVF {
	double ic1 = 0.0, ic2 = 0.0;
	double lp = 0.0, bp = 0.0, hp = 0.0;
	void process(double x, double g, double k) {
		// soft amplitude control: loud ringing adds damping, so self-oscillation settles at about 4 V
		k += 0.03 * (bp / 6.0) * (bp / 6.0);
		double a1 = 1.0 / (1.0 + g * (g + k));
		double a2 = g * a1, a3 = g * a2;
		double v3 = x - ic2;
		double v1 = a1 * ic1 + a2 * v3;
		double v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.0 * v1 - ic1;
		ic2 = 2.0 * v2 - ic2;
		ic1 = std::max(-60.0, std::min(60.0, ic1));
		lp = v2; bp = v1; hp = x - k * v1 - v2;
		if (!std::isfinite(ic1) || !std::isfinite(ic2)) { ic1 = ic2 = 0.0; lp = bp = hp = 0.0; }
	}
};

// One sister: two filters in series (4 poles). The second stage carries the resonance.
struct Sister {
	SVF a, b;
	// kind: 0 = low pass, 1 = band pass, 2 = high pass
	double process(double x, double fc, double sr, double k, double anti, int kind) {
		fc = std::min(fc, sr * 0.45);
		double g = std::tan(M_PI * fc / sr);
		a.process(x, g, 1.41421356);
		double s1 = kind == 0 ? a.lp : kind == 2 ? a.hp : a.bp * 1.41421356;
		b.process(s1, g, k);
		// band pass: unity at the centre while flat; at high resonance it is allowed to ring louder
		double y = kind == 0 ? b.lp : kind == 2 ? b.hp : b.bp * bpGain(k);
		double bpN = b.bp * k;  // band pass with unity gain at the cutoff
		if (anti > 0.0) {
			if (kind == 1) y = (1.0 - anti) * y + anti * (x - y);  // centre turns into a notch
			else y -= anti * bpN;                                  // edges get a dip at the cutoff
		}
		return y;
	}
};

struct SistersEngine {
	Sister s[3];
	float out[3] = {};
	float all = 0.f;
	double fc[3] = {640.0, 640.0, 640.0};
	unsigned noise = 12345u;

	void process(const Params& p, float inLow, float inCentre, float inHigh, float sr) {
		double f = knobHz(p.freqKnob, p.voct + p.fmOct);
		double so = spanOctaves(p.span, p.spanCv);
		fc[0] = f * std::pow(2.0, -so);
		fc[1] = f;
		fc[2] = f * std::pow(2.0, so);
		for (int i = 0; i < 3; i++) fc[i] = std::max(5.0, std::min(fc[i], (double) sr * 0.45));
		double k = dampK(p.quality);
		double anti = p.quality < 0.f ? -p.quality : 0.0;
		// a whisper of noise lets a silent filter start self-oscillating
		noise = noise * 1664525u + 1013904223u;
		double nz = ((noise >> 9) * (1.0 / 8388608.0) - 1.0) * 1e-4;
		const float ins[3] = {inLow, inCentre, inHigh};
		for (int i = 0; i < 3; i++) {
			int kind = p.mode == FORMANT ? 1 : i;
			double y = s[i].process(ins[i] + nz, fc[i], sr, k, anti, kind);
			out[i] = (float) (10.0 * std::tanh(y / 10.0));
		}
		all = (float) (10.0 * std::tanh((out[0] + out[1] + out[2]) / 10.0));
	}
};

// Magnitude (linear) of one sister at frequency hz, for the display.
inline double response(double hz, double fc, double k, double anti, int kind) {
	std::complex<double> s(0.0, hz / fc);
	std::complex<double> d1 = s * s + 1.41421356 * s + 1.0;
	std::complex<double> d2 = s * s + k * s + 1.0;
	std::complex<double> h1 = kind == 0 ? 1.0 / d1 : kind == 2 ? s * s / d1 : 1.41421356 * s / d1;
	std::complex<double> h2 = kind == 0 ? 1.0 / d2 : kind == 2 ? s * s / d2 : bpGain(k) * s / d2;
	std::complex<double> bpN = k * s / d2;
	std::complex<double> y = h1 * h2;
	if (anti > 0.0) {
		if (kind == 1) y = (1.0 - anti) * y + anti * (1.0 - y);
		else y -= anti * h1 * bpN;
	}
	return std::abs(y);
}

}  // namespace ws
