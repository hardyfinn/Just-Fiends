// JustFiends.cpp
// Six linked slopes with a clear interface: every mode and Run mode is
// labelled, tooltips change with the mode, and a display draws all six
// channels. The engine is in FiendsEngine.hpp; this file connects it to the
// panel. Positions are in mm and match res/make_panel.py.
#include "plugin.hpp"
#include "FiendsEngine.hpp"
#include <cstring>
#include <functional>

using namespace jf;

static const char* CH_NAMES[6] = {"IDENTITY", "2N", "3N", "4N", "5N", "6N"};

static std::string fmtHz(double f) {
	if (f >= 1000.0) return string::f("%.2f kHz", f / 1000.0);
	if (f >= 1.0) return string::f("%.2f Hz", f);
	return string::f("%.2f s", 1.0 / f);
}

static std::string curveWords(float c) {
	if (c < -0.85f) return "rectangle (a gate or square wave)";
	if (c < -0.1f) return "logarithmic (fast start, slow finish)";
	if (c < 0.1f) return "linear (triangle)";
	if (c < 0.6f) return "exponential (slow start, snappy)";
	if (c < 0.9f) return "exponential towards sine";
	return "sine";
}

static std::string rampWords(float r) {
	int rise = (int) std::round((0.5f + 0.5f * r) * 100.f);
	if (rise <= 2) return "instant rise, all fall (saw down)";
	if (rise >= 98) return "all rise, instant fall (saw up)";
	if (rise == 50) return "even rise and fall (triangle)";
	return string::f("%d%% rise, %d%% fall", rise, 100 - rise);
}

struct JustFiends;

// Tooltips that explain the control in the current mode.
struct FiendQuantity : ParamQuantity {
	std::function<std::string()> describe;
	std::string getDescription() override { return describe ? describe() : description; }
};
struct FiendPort : PortInfo {
	std::function<std::string()> describe;
	std::string getDescription() override { return describe ? describe() : description; }
};

struct JustFiends : Module {
	enum ParamId {
		TIME_PARAM, INTONE_PARAM, RAMP_PARAM, CURVE_PARAM, FM_PARAM, RUN_PARAM,
		INTONE_ATT_PARAM, RAMP_ATT_PARAM, CURVE_ATT_PARAM,
		RANGE_PARAM, MODE_PARAM, RUN_BUTTON,
		ENUMS(TRIG_BUTTON, 6),
		PARAMS_LEN
	};
	enum InputId {
		TIME_INPUT, FM_INPUT, INTONE_INPUT, RAMP_INPUT, CURVE_INPUT, RUN_INPUT,
		ENUMS(TRIG_INPUT, 6),
		POLY_TRIG_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(OUT_OUTPUT, 6),
		MIX_OUTPUT, POLY_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		RUN_LIGHT,
		ENUMS(TRIG_LIGHT, 6),
		ENUMS(OUT_LIGHT, 6),
		LIGHTS_LEN
	};

	SlopeEngine e;
	bool polyPitch = false, geode = false, velocity = true;
	int geodeRealign = 1;

	// display buffers
	static const int N = 256;
	float scope[7][N] = {};
	float shown[7][N] = {};
	int scopePos = 0;
	double scopeAcc = 0.0;
	bool capturing = false;
	float captureWait = 0.f;
	int lastRange = -1;
	dsp::ClockDivider lightDiv;

	FiendQuantity* q(int id) { return (FiendQuantity*) paramQuantities[id]; }
	FiendPort* pin(int id) { return (FiendPort*) inputInfos[id]; }

	bool runActive() { return params[RUN_BUTTON].getValue() > 0.5f || inputs[RUN_INPUT].isConnected(); }

	JustFiends() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<FiendQuantity>(TIME_PARAM, 0.f, 1.f, 0.5f, "Time");
		configParam<FiendQuantity>(INTONE_PARAM, -1.f, 1.f, 0.f, "Intone");
		configParam<FiendQuantity>(RAMP_PARAM, -1.f, 1.f, 0.f, "Ramp");
		configParam<FiendQuantity>(CURVE_PARAM, -1.f, 1.f, 0.f, "Curve");
		configParam<FiendQuantity>(FM_PARAM, -1.f, 1.f, 0.f, "FM amount", "%", 0.f, 100.f);
		configParam<FiendQuantity>(RUN_PARAM, -5.f, 5.f, 0.f, "Run", " V");
		configParam(INTONE_ATT_PARAM, -1.f, 1.f, 1.f, "Intone CV amount", "%", 0.f, 100.f);
		configParam(RAMP_ATT_PARAM, -1.f, 1.f, 1.f, "Ramp CV amount", "%", 0.f, 100.f);
		configParam(CURVE_ATT_PARAM, -1.f, 1.f, 1.f, "Curve CV amount", "%", 0.f, 100.f);
		configSwitch(RANGE_PARAM, 0.f, 1.f, 0.f, "Range", {"Shape: envelopes and LFOs", "Sound: oscillators"});
		configSwitch(MODE_PARAM, 0.f, 2.f, 0.f, "Mode", {"Transient: one shot per trigger", "Sustain: follows the gate", "Cycle: loops"});
		configButton(RUN_BUTTON, "Run mode on (also turns on when the RUN jack is patched)");
		for (int i = 0; i < 6; i++) {
			configButton(TRIG_BUTTON + i, string::f("Trigger %s", CH_NAMES[i]));
			configInput<FiendPort>(TRIG_INPUT + i, string::f("%s trigger / gate", CH_NAMES[i]));
			configOutput(OUT_OUTPUT + i, CH_NAMES[i]);
		}
		configInput<FiendPort>(TIME_INPUT, "Time (1 V/oct)");
		configInput<FiendPort>(FM_INPUT, "FM");
		configInput<FiendPort>(INTONE_INPUT, "Intone CV (+-5 V)");
		configInput<FiendPort>(RAMP_INPUT, "Ramp CV (+-5 V)");
		configInput<FiendPort>(CURVE_INPUT, "Curve CV (+-5 V)");
		configInput<FiendPort>(RUN_INPUT, "Run CV (patching it turns Run mode on)");
		configInput<FiendPort>(POLY_TRIG_INPUT, "Poly triggers / gates (channel 1 = IDENTITY ... 6 = 6N)");
		configOutput(MIX_OUTPUT, "Mix (shape: the highest slope, sound: all six summed)");
		configOutput(POLY_OUTPUT, "Poly: all six channels");
		configLight(RUN_LIGHT, "Run mode");
		lightDiv.setDivision(64);
		setupTooltips();
	}

	void setupTooltips() {
		q(TIME_PARAM)->describe = [=]() {
			std::string s = e.c.range == SHAPE
				? "Shape range: how long one rise and fall takes. Clockwise = faster. "
				: "Sound range: the pitch of IDENTITY. Noon = middle C. ";
			s += "IDENTITY now: " + fmtHz(e.s[0].freq) + ".";
			if (polyPitch) s += " Poly pitch is on: each channel reads its own channel of the TIME input.";
			return s;
		};
		q(INTONE_PARAM)->describe = [=]() {
			if (polyPitch) return std::string("Poly pitch is on, so Intone is ignored: each channel plays its own pitch.");
			std::string s = "Spreads the six channels. Noon: all the same. Clockwise: channel N runs N times faster (harmonics). Anticlockwise: N times slower (subharmonics). Now: ";
			for (int i = 1; i < 6; i++) s += string::f("%dN x%.2f%s", i + 1, JfRatio(i), i < 5 ? ", " : ".");
			if (geode) s += " In Geode, this sets how many hits each channel plays per clock.";
			return s;
		};
		q(RAMP_PARAM)->describe = [=]() {
			return "The balance of rise and fall. Anticlockwise: fast rise, slow fall (plucky). Clockwise: slow rise, fast fall (swells). Now: " + rampWords(e.c.ramp) + ".";
		};
		q(CURVE_PARAM)->describe = [=]() {
			return "Bends the slope: rectangle, logarithmic, linear at noon, exponential, sine. Now: " + curveWords(e.c.curve) + ".";
		};
		q(FM_PARAM)->describe = [=]() {
			return std::string("How much the FM input bends TIME, in octaves per volt. Anticlockwise inverts it. Exponential, so it suits vibrato and pitch sweeps.");
		};
		q(RUN_PARAM)->describe = [=]() {
			std::string s = "Sets the Run voltage (added to the RUN jack). ";
			if (!runActive()) return s + "Run is off now: press RUN or patch the RUN jack.";
			switch (e.behaviour) {
				case B_SHIFT: return s + string::f("Shift: triggers are accepted after %.0f%% of the slope.", e.runFrac(e.c.runV) * 100.f);
				case B_STRATA: return s + string::f("Strata: the channels slew towards %.2f V.", std::min(8.f, std::max(0.f, e.c.runV)));
				case B_VOLLEY: return s + string::f("Volley: %d cycles per trigger.", 1 + (int) std::lround(e.runFrac(e.c.runV) * 15.f));
				case B_SPILL: return s + string::f("Spill: each burst lasts %.0f ms.", 5.f * std::pow(2.f, e.runFrac(e.c.runV) * 8.6f));
				case B_PLUME: return s + string::f("Plume: plucks decay over %.0f ms.", 20.f * std::pow(2.f, e.runFrac(e.c.runV) * 7.6f));
				case B_FLOOM: return s + string::f("Floom: FM depth %.0f%%.", e.runFrac(e.c.runV) * 100.f);
				default: return s;
			}
		};
		for (int i = 0; i < 6; i++) {
			pin(TRIG_INPUT + i)->describe = [=]() {
				std::string s;
				if (geode) {
					if (i == 0) return std::string("Geode is on: this is the clock. The six channels play rhythms locked to it.");
					return std::string("Geode is on: this jack is ignored, the clock at IDENTITY drives everything.");
				}
				if (i == 0) s = "Fires IDENTITY and, through the normals, every channel below it that has nothing patched. ";
				else s = string::f("Fires %s and every unpatched channel below it. Unpatched, it follows the jack above. ", CH_NAMES[i]);
				s += behaviourHint(e.behaviour);
				return s;
			};
		}
		pin(TIME_INPUT)->describe = [=]() {
			return polyPitch ? std::string("Poly pitch: channel 1 plays IDENTITY, channel 2 plays 2N, and so on. Plug a poly MIDI to CV here.")
				: std::string("1 V/oct. Adds to the TIME knob for all six channels.");
		};
		pin(RUN_INPUT)->describe = [=]() {
			return std::string("Patching anything here turns Run on. Its job depends on Range and Mode: SHIFT, STRATA, VOLLEY (shape) or SPILL, PLUME, FLOOM (sound).");
		};
		pin(POLY_TRIG_INPUT)->describe = [=]() {
			return std::string("Triggers or gates for all six channels in one cable. Combined with the single jacks (the higher voltage wins).");
		};
	}

	float JfRatio(int i) { return polyPitch ? 1.f : (float) SlopeEngine::intoneRatio(i + 1, e.c.intone); }

	void onReset() override {
		polyPitch = false; geode = false; velocity = true; geodeRealign = 1;
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "polyPitch", json_boolean(polyPitch));
		json_object_set_new(r, "geode", json_boolean(geode));
		json_object_set_new(r, "velocity", json_boolean(velocity));
		json_object_set_new(r, "geodeRealign", json_integer(geodeRealign));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* j;
		if ((j = json_object_get(r, "polyPitch"))) polyPitch = json_boolean_value(j);
		if ((j = json_object_get(r, "geode"))) geode = json_boolean_value(j);
		if ((j = json_object_get(r, "velocity"))) velocity = json_boolean_value(j);
		if ((j = json_object_get(r, "geodeRealign"))) geodeRealign = std::max(1, (int) json_integer_value(j));
	}

	void process(const ProcessArgs& args) override {
		Controls& c = e.c;
		e.sampleRate = args.sampleRate;
		c.range = params[RANGE_PARAM].getValue() > 0.5f ? SOUND : SHAPE;
		c.mode = (int) std::round(params[MODE_PARAM].getValue());
		c.time = params[TIME_PARAM].getValue();
		auto cv = [&](int knob, int input, int att) {
			return clamp(params[knob].getValue() + inputs[input].getVoltage() / 5.f * params[att].getValue(), -1.f, 1.f);
		};
		c.intone = cv(INTONE_PARAM, INTONE_INPUT, INTONE_ATT_PARAM);
		c.ramp = cv(RAMP_PARAM, RAMP_INPUT, RAMP_ATT_PARAM);
		c.curve = cv(CURVE_PARAM, CURVE_INPUT, CURVE_ATT_PARAM);
		c.fmOct = params[FM_PARAM].getValue() * inputs[FM_INPUT].getVoltage();
		c.run = runActive();
		c.runV = params[RUN_PARAM].getValue() + inputs[RUN_INPUT].getVoltage();
		c.polyPitch = polyPitch;
		c.geode = geode;
		c.geodeRealign = geodeRealign;
		c.velocity = velocity;
		for (int i = 0; i < 6; i++)
			c.voct[i] = polyPitch ? inputs[TIME_INPUT].getPolyVoltage(i) : inputs[TIME_INPUT].getVoltage(0);

		// triggers: each jack (or its button) normals down to the next unpatched one
		float prevV = 0.f;
		bool prevConn = false;
		int polyCh = inputs[POLY_TRIG_INPUT].getChannels();
		for (int i = 0; i < 6; i++) {
			bool pressed = params[TRIG_BUTTON + i].getValue() > 0.5f;
			bool patched = inputs[TRIG_INPUT + i].isConnected();
			float v = prevV;
			bool conn = prevConn;
			if (patched || pressed) {
				v = patched ? inputs[TRIG_INPUT + i].getVoltage() : 0.f;
				if (pressed) v = std::max(v, 5.f);
				conn = conn || patched;
				if (patched) conn = true;
			}
			prevV = v;
			prevConn = conn;
			float pv = v;
			if (i < polyCh) { pv = std::max(v, inputs[POLY_TRIG_INPUT].getVoltage(i)); conn = true; }
			e.gateV[i] = pv;
			e.gateConn[i] = conn;
		}

		e.process(args.sampleTime);

		for (int i = 0; i < 6; i++) outputs[OUT_OUTPUT + i].setVoltage(e.s[i].out);
		outputs[MIX_OUTPUT].setVoltage(e.mix);
		outputs[POLY_OUTPUT].setChannels(6);
		for (int i = 0; i < 6; i++) outputs[POLY_OUTPUT].setVoltage(e.s[i].out, i);

		recordScope(args.sampleTime);

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			lights[RUN_LIGHT].setBrightness(c.run ? 1.f : 0.f);
			for (int i = 0; i < 6; i++) {
				lights[TRIG_LIGHT + i].setBrightnessSmooth(e.s[i].gate ? 1.f : 0.f, dt);
				float b = c.range == SHAPE ? e.s[i].out / 8.f : std::fabs(e.s[i].out) / 5.f;
				lights[OUT_LIGHT + i].setBrightnessSmooth(clamp(b, 0.f, 1.f), dt);
			}
		}
	}

	// Shape range: a scrolling trace. Sound range: a scope locked to IDENTITY.
	void recordScope(float dt) {
		Controls& c = e.c;
		if (c.range != lastRange) { lastRange = c.range; scopePos = 0; capturing = false; }
		double f0 = std::max(1e-4, e.s[0].freq);
		auto write = [&]() {
			for (int i = 0; i < 6; i++) scope[i][scopePos] = e.s[i].out;
			scope[6][scopePos] = e.mix;
		};
		if (c.range == SHAPE) {
			double window = std::min(20.0, std::max(0.5, 3.0 / f0));
			if (c.mode != CYCLE || c.run) window = std::min(20.0, std::max(1.0, 2.5 / f0));
			scopeAcc += dt;
			if (scopeAcc >= window / N) {
				scopeAcc = 0.0;
				write();
				scopePos = (scopePos + 1) % N;
				// shown = the ring, oldest first
				for (int k = 0; k < 7; k++)
					for (int j = 0; j < N; j++) shown[k][j] = scope[k][(scopePos + j) % N];
			}
		}
		else {
			double window = 2.0 / f0;
			if (!capturing) {
				captureWait += dt;
				if ((e.s[0].wrapped && captureWait > 0.03f) || captureWait > 0.25f) {
					capturing = true;
					scopePos = 0;
					scopeAcc = 0.0;
				}
			}
			if (capturing) {
				scopeAcc += dt;
				if (scopeAcc >= window / N) {
					scopeAcc -= window / N;
					if (scopeAcc > window / N) scopeAcc = 0.0;
					write();
					scopePos++;
					if (scopePos >= N) {
						std::memcpy(shown, scope, sizeof(shown));
						capturing = false;
						captureWait = 0.f;
						scopePos = 0;
					}
				}
			}
		}
	}
};

// ---- display ---------------------------------------------------------------
static const NVGcolor C_BG = nvgRGB(0x11, 0x0E, 0x14);
static const NVGcolor C_LANE = nvgRGB(0x1B, 0x17, 0x20);
static const NVGcolor C_INK = nvgRGB(0xEE, 0xE8, 0xDC);
static const NVGcolor C_SOFT = nvgRGB(0x8E, 0x86, 0x96);
static const NVGcolor C_ACID = nvgRGB(0xB8, 0xE0, 0x4A);
static const NVGcolor CH_COL[7] = {
	nvgRGB(0xF2, 0xEC, 0xDE),  // identity: bone
	nvgRGB(0xF2, 0x6B, 0x5B),  // 2N coral
	nvgRGB(0xF2, 0xB1, 0x3A),  // 3N amber
	nvgRGB(0xB8, 0xE0, 0x4A),  // 4N acid
	nvgRGB(0x4C, 0xD6, 0xC8),  // 5N teal
	nvgRGB(0x9B, 0x8C, 0xF5),  // 6N violet
	nvgRGB(0xE0, 0x7B, 0xD0),  // mix
};

static std::shared_ptr<window::Font> displayFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

struct FiendDisplay : Widget {
	JustFiends* module = nullptr;

	void text(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, int align, float size) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}
	void rect(const DrawArgs& a, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(a.vg);
		nvgRect(a.vg, x, y, w, h);
		nvgFillColor(a.vg, c);
		nvgFill(a.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(1.2f));
		nvgFillColor(args.vg, C_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = displayFont();
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
		float pad = mm2px(1.5f);

		if (!module) {
			text(args, W / 2, H / 2, "JUST FIENDS", C_INK, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, mm2px(4.f));
			Widget::drawLayer(args, layer);
			return;
		}
		JustFiends* m = module;
		SlopeEngine& e = m->e;
		int b = e.behaviour;

		// header: range / mode, then what is happening
		float hy = pad + mm2px(1.4f);
		std::string rm = std::string(e.c.range == SHAPE ? "SHAPE" : "SOUND") + " / "
			+ (e.c.mode == TRANSIENT ? "TRANSIENT" : e.c.mode == SUSTAIN ? "SUSTAIN" : "CYCLE");
		text(args, pad, hy, rm, C_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.3f));
		std::string name = std::string(e.c.run ? "RUN: " : "") + behaviourName(b);
		text(args, mm2px(40.f), hy, name, e.c.run ? C_ACID : C_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(3.0f));
		std::string badges;
		if (m->geode) badges += "GEODE  ";
		if (m->polyPitch) badges += "POLY PITCH  ";
		if (e.c.run) badges += string::f("RUN %.2f V", e.c.runV);
		text(args, W - pad, hy, badges, C_ACID, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.1f));
		std::string hint = m->geode
			? "GEODE: the clock at IDENTITY drives six rhythms. Intone sets hits per clock for each channel."
			: behaviourHint(b);
		text(args, pad, hy + mm2px(3.3f), hint, C_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.0f));

		// lanes
		float top = pad + mm2px(6.6f);
		float laneH = (H - top - pad) / 7.f;
		float labelW = mm2px(26.f);
		float rightW = mm2px(28.f);
		float tx = pad + labelW, tw = W - tx - rightW - pad * 2;
		bool shape = e.c.range == SHAPE;
		for (int k = 0; k < 7; k++) {
			float y = top + k * laneH;
			rect(args, tx, y + 1, tw, laneH - 2, C_LANE);
			NVGcolor col = CH_COL[k];
			if (k < 6) {
				text(args, pad, y + laneH / 2, k == 0 ? "ID" : string::f("%dN", k + 1), col, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.4f));
				std::string info = m->polyPitch ? fmtHz(e.s[k].freq) : string::f("x%.2f %s", m->JfRatio(k), fmtHz(e.s[k].freq).c_str());
				text(args, pad + mm2px(5.5f), y + laneH / 2, info, C_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(1.85f));
			}
			else text(args, pad, y + laneH / 2, "MIX", col, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.4f));
			// trace
			nvgBeginPath(args.vg);
			const int N = JustFiends::N;
			for (int j = 0; j < N; j++) {
				float v = m->shown[k][j];
				float n;
				if (shape) n = clamp(v / 8.f, 0.f, 1.f);
				else n = clamp(v / (k == 6 ? 20.f : 10.f) + 0.5f, 0.f, 1.f);
				float px = tx + tw * j / (N - 1);
				float py = y + laneH - 1.5f - n * (laneH - 3.f);
				if (j == 0) nvgMoveTo(args.vg, px, py);
				else nvgLineTo(args.vg, px, py);
			}
			nvgStrokeColor(args.vg, col);
			nvgStrokeWidth(args.vg, 1.1f);
			nvgStroke(args.vg);
			// gate dot
			if (k < 6 && e.s[k].gate) {
				nvgBeginPath(args.vg);
				nvgCircle(args.vg, tx - mm2px(1.4f), y + laneH / 2, mm2px(0.6f));
				nvgFillColor(args.vg, col);
				nvgFill(args.vg);
			}
		}
		text(args, tx + tw, top - mm2px(0.4f), shape ? "scrolling, newest on the right" : "scope: two IDENTITY cycles", C_SOFT, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, mm2px(1.7f));

		// right panel: one cycle of the current shape + read-outs
		float rx = W - rightW - pad, ry = top + 1, rw = rightW, rh = mm2px(13.f);
		rect(args, rx, ry, rw, rh, C_LANE);
		nvgBeginPath(args.vg);
		float r = 0.5f + 0.5f * clamp(e.c.ramp, -1.f, 1.f) * 0.998f;
		for (int j = 0; j <= 80; j++) {
			float p = j / 80.f;
			bool rising = p < r;
			float lin = rising ? p / r : 1.f - (p - r) / (1.f - r);
			float v = shapeCurve(lin, e.c.curve, rising);
			float px = rx + mm2px(1.f) + (rw - mm2px(2.f)) * p;
			float py = ry + rh - mm2px(1.f) - v * (rh - mm2px(2.f));
			if (j == 0) nvgMoveTo(args.vg, px, py);
			else nvgLineTo(args.vg, px, py);
		}
		nvgStrokeColor(args.vg, C_ACID);
		nvgStrokeWidth(args.vg, 1.4f);
		nvgStroke(args.vg);
		text(args, rx + mm2px(0.8f), ry + mm2px(0.5f), "ONE CYCLE", C_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, mm2px(1.7f));

		float ly = ry + rh + mm2px(2.2f), lh = mm2px(2.6f);
		int rise = (int) std::round(r * 100.f);
		text(args, rx, ly, string::f("RAMP  %d/%d", rise, 100 - rise), C_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(1.9f));
		std::string cw = curveWords(e.c.curve);
		cw = cw.substr(0, cw.find(' '));
		text(args, rx, ly + lh, "CURVE " + cw, C_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(1.9f));
		text(args, rx, ly + 2 * lh, string::f("INTONE %+.2f", e.c.intone), C_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(1.9f));
		text(args, rx, ly + 3 * lh, "TIME  " + fmtHz(e.s[0].freq), C_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(1.9f));
		if (m->geode && e.geodeRunning)
			text(args, rx, ly + 4 * lh, string::f("CLOCK %.0f BPM", 60.0 / e.geodePeriod), C_ACID, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(1.9f));

		Widget::drawLayer(args, layer);
	}
};

// ---- panel -----------------------------------------------------------------
struct JustFiendsWidget : ModuleWidget {
	JustFiendsWidget(JustFiends* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/JustFiends.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		FiendDisplay* d = createWidget<FiendDisplay>(mm2px(Vec(5.0, 12.6)));
		d->box.size = mm2px(Vec(162.7, 33.0));
		d->module = module;
		addChild(d);

		const float rowA = 59.5, rowB = 75.5, rowC = 88.5;
		const float xTime = 18.0, xIntone = 46.0, xRamp = 72.0, xCurve = 98.0, xRange = 122.0, xMode = 146.0;
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(xTime, rowA)), module, JustFiends::TIME_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(xIntone, rowA)), module, JustFiends::INTONE_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(xRamp, rowA)), module, JustFiends::RAMP_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(xCurve, rowA)), module, JustFiends::CURVE_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(xRange, rowA)), module, JustFiends::RANGE_PARAM));
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(xMode, rowA)), module, JustFiends::MODE_PARAM));

		addParam(createParamCentered<Trimpot>(mm2px(Vec(xIntone, rowB)), module, JustFiends::INTONE_ATT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(xRamp, rowB)), module, JustFiends::RAMP_ATT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(xCurve, rowB)), module, JustFiends::CURVE_ATT_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xRange, rowB)), module, JustFiends::FM_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(xMode, rowB)), module, JustFiends::RUN_PARAM));
		addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<GreenLight>>>(mm2px(Vec(164.0, rowB)), module, JustFiends::RUN_BUTTON, JustFiends::RUN_LIGHT));

		const int cvIn[6] = {JustFiends::TIME_INPUT, JustFiends::INTONE_INPUT, JustFiends::RAMP_INPUT, JustFiends::CURVE_INPUT, JustFiends::FM_INPUT, JustFiends::RUN_INPUT};
		const float cvX[6] = {xTime, xIntone, xRamp, xCurve, xRange, xMode};
		for (int i = 0; i < 6; i++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(cvX[i], rowC)), module, cvIn[i]));

		const float trigY = 110.6, outY = 123.4;
		for (int i = 0; i < 6; i++) {
			float x = 16.0 + 21.0 * i;
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(x, trigY)), module, JustFiends::TRIG_INPUT + i));
			addParam(createLightParamCentered<VCVLightButton<SmallSimpleLight<WhiteLight>>>(mm2px(Vec(x + 7.5, trigY)), module, JustFiends::TRIG_BUTTON + i, JustFiends::TRIG_LIGHT + i));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(x, outY)), module, JustFiends::OUT_OUTPUT + i));
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(x + 6.6, outY)), module, JustFiends::OUT_LIGHT + i));
		}
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(150.0, trigY)), module, JustFiends::POLY_TRIG_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(140.0, outY)), module, JustFiends::MIX_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(158.0, outY)), module, JustFiends::POLY_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		JustFiends* m = getModule<JustFiends>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Extra modes (the hardware reaches these over i2c)"));
		menu->addChild(createBoolPtrMenuItem("Poly pitch (synth mode): each channel plays its own TIME input channel", "", &m->polyPitch));
		menu->addChild(createBoolPtrMenuItem("Geode: IDENTITY becomes a clock for six rhythms", "", &m->geode));
		menu->addChild(createIndexSubmenuItem("Geode: realign the rhythms every",
			{"1 clock", "2 clocks", "4 clocks", "8 clocks", "16 clocks"},
			[=]() { int r = m->geodeRealign, k = 0; while ((1 << k) < r && k < 4) k++; return k; },
			[=](int i) { m->geodeRealign = 1 << i; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Sound range: trigger voltage sets loudness (5 V = full)", "", &m->velocity));
	}
};

Model* modelJustFiends = createModel<JustFiends, JustFiendsWidget>("JustFiends");
