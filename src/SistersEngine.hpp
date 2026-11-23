// SistersEngine.hpp
// The engine for Weird Sisters: three linked filters (LOW, CENTRE, HIGH) that
// share one FREQ, built as in the Three Sisters technical map. SPAN pushes
// LOW's cutoff down and HIGH's up; in crossover mode CENTRE passes the band
// between them. QUALITY sets resonance (clockwise) or a dry/wet blend with
// notches (anticlockwise). MODE picks crossover or formant (three band passes).
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
// SPAN in octaves each way. Zero at about 10 o'clock (knob 0.3); below that the
// LOW and HIGH cutoffs cross, which closes the crossover CENTRE band like a VCA.
inline double spanOctaves(float span, float cv) { return std::min(6.0, std::max(-3.0, (span - 0.3) * 5.7 + cv)); }
static const double SQRT2 = 1.41421356;
// Resonance damping: 1.414 (flat) at noon, reaching zero at about 3 o'clock
// (quality 0.5), where the filters start to sing; slightly negative beyond.
inline double dampK(float q) {
	if (q <= 0.f) return SQRT2;
	if (q < 0.5f) { double r = 1.0 - q / 0.5; return SQRT2 * r * r - 0.01 * (q / 0.5); }
	return -0.01 - 0.01 * std::min(1.0, (q - 0.5) / 0.5);
}

// Gain for the formant band passes: unity at the centre while flat, letting the
// ringing come through louder towards self-oscillation (about 5 V peak).
inline double formantGain(double k) {
	if (k >= 0.25) return SQRT2 * k;
	if (k <= 0.0) return 1.0;
	return 1.0 - (1.0 - SQRT2 * 0.25) * (k / 0.25);
}

// One 2-pole state-variable filter (Zavalishin's topology-preserving form).
struct SVF {
	double ic1 = 0.0, ic2 = 0.0;
	double lp = 0.0, bp = 0.0, hp = 0.0;
	void process(double x, double g, double k) {
		// soft amplitude control: loud ringing adds damping, so self-oscillation settles at about 5 V
		k += 0.03 * (bp / 6.0) * (bp / 6.0);
		double a1 = 1.0 / (1.0 + g * (g + k));
		double a2 = g * a1, a3 = g * a2;
		double v3 = x - ic2;
		double v1 = a1 * ic1 + a2 * v3;
		double v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.0 * v1 - ic1;
		ic2 = 2.0 * v2 - ic2;
		ic1 = std::max(-60.0, std::min(60.0, ic1));
		ic2 = std::max(-60.0, std::min(60.0, ic2));
		lp = v2; bp = v1; hp = x - k * v1 - v2;
		if (!std::isfinite(ic1) || !std::isfinite(ic2)) { ic1 = ic2 = 0.0; lp = bp = hp = 0.0; }
	}
};

enum Block { LOW = 0, CENTRE = 1, HIGH = 2 };

// The two stages of each block, as in the Three Sisters technical map.
//   LOW:    low pass -> low pass (crossover) or high pass (formant)
//   HIGH:   high pass -> high pass (crossover) or low pass (formant)
//   CENTRE: high pass (at LOW's cutoff) -> low pass (at HIGH's cutoff)
// stage type: true = low pass
inline bool stage1Low(int block) { return block == LOW; }
inline bool stage2Low(int block, int mode) {
	if (block == LOW) return mode == CROSSOVER;
	if (block == HIGH) return mode == FORMANT;
	return true;
}

// One sister. The second stage carries the resonance. Anticlockwise QUALITY
// mixes the first stage's unused output back in (and, for CENTRE, the second
// stage's high pass too): a dry/wet blend with notches near the cutoffs.
struct Sister {
	SVF a, b;
	double process(double x, double fA, double fB, double sr, double k, double anti, int block, int mode) {
		double gA = std::tan(M_PI * std::min(fA, sr * 0.45) / sr);
		double gB = std::tan(M_PI * std::min(fB, sr * 0.45) / sr);
		a.process(x, gA, SQRT2);
		double s1 = stage1Low(block) ? a.lp : a.hp;
		b.process(s1, gB, k);
		double y = stage2Low(block, mode) ? b.lp : b.hp;
		if (mode == FORMANT) y *= formantGain(k);
		if (anti > 0.0) {
			double comp = block == LOW ? a.hp : block == HIGH ? a.lp : a.lp + b.hp;
			y += anti * comp;
		}
		return y;
	}
};

// Cutoffs of the two stages of each block.
inline void blockCutoffs(int block, int mode, const double fc[3], double& fA, double& fB) {
	if (block == CENTRE && mode == CROSSOVER) { fA = fc[0]; fB = fc[2]; }
	else fA = fB = fc[block];
}

struct SistersEngine {
	Sister s[3];
	float out[3] = {};
	float all = 0.f;
	double fc[3] = {640.0, 640.0, 640.0};  // LOW cutoff, FREQ, HIGH cutoff
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
			double fA, fB;
			blockCutoffs(i, p.mode, fc, fA, fB);
			double y = s[i].process(ins[i] + nz, fA, fB, sr, k, anti, i, p.mode);
			out[i] = (float) (10.0 * std::tanh(y / 10.0));
		}
		all = (float) (10.0 * std::tanh((out[0] + out[1] + out[2]) / 10.0));
	}
};

// Complex response of one block at hz, for the display (summed with phase for ALL).
inline std::complex<double> responseC(double hz, const double fc[3], double k, double anti, int block, int mode) {
	typedef std::complex<double> C;
	double fA, fB;
	blockCutoffs(block, mode, fc, fA, fB);
	C sA(0.0, hz / fA), sB(0.0, hz / fB);
	C dA = sA * sA + SQRT2 * sA + 1.0, dB = sB * sB + k * sB + 1.0;
	C lpA = 1.0 / dA, hpA = sA * sA / dA, lpB = 1.0 / dB, hpB = sB * sB / dB;
	C s1 = stage1Low(block) ? lpA : hpA;
	C y = s1 * (stage2Low(block, mode) ? lpB : hpB);
	if (mode == FORMANT) y *= formantGain(k);
	if (anti > 0.0) {
		C comp = block == LOW ? hpA : block == HIGH ? lpA : lpA + s1 * hpB;
		y += anti * comp;
	}
	return y;
}
inline double response(double hz, const double fc[3], double k, double anti, int block, int mode) {
	return std::abs(responseC(hz, fc, k, anti, block, mode));
}

}  // namespace ws
