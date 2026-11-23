// Mangrave.cpp
// A formant oscillator after the Mangrove technical map, with the PPM
// ("Patch Programmable Mangrove") section built into the same panel. A scope
// shows the core and the impulses, and read-outs name the pitch, the impulse
// length and any undertone. The engine is in GroveEngine.hpp. Positions are in
// mm and match res/make_panel_mangrave.py.
#include "plugin.hpp"
#include "GroveEngine.hpp"
#include <functional>
#include <cstring>

using namespace mg;

static std::string noteName(double hz) {
	static const char* N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	if (hz < 1.0) return "";
	double m = 69.0 + 12.0 * std::log2(hz / 440.0);
	int mi = (int) std::lround(m);
	int cents = (int) std::lround((m - mi) * 100.0);
	int oct = (int) std::floor(mi / 12.0) - 1;
	return string::f("%s%d%s", N[((mi % 12) + 12) % 12], oct, cents == 0 ? "" : string::f(" %+dc", cents).c_str());
}

static std::string hzS(double f) {
	f = std::fabs(f);
	if (f >= 1000.0) return string::f("%.2f kHz", f / 1000.0);
	if (f >= 10.0) return string::f("%.1f Hz", f);
	return string::f("%.2f Hz", f);
}

static std::string msS(double s) {
	if (s >= 0.1) return string::f("%.0f ms", s * 1000.0);
	if (s >= 0.001) return string::f("%.1f ms", s * 1000.0);
	return string::f("%.2f ms", s * 1000.0);
}

static const char* PPM_NAMES[3] = {"IMPULSE", "SYNC", "CYCLE"};

struct GroveQuantity : ParamQuantity {
	std::function<std::string()> describe;
	std::string getDescription() override { return describe ? describe() : description; }
};

struct GroveSwitch : SwitchQuantity {
	std::function<std::string()> describe;
	std::string getDescription() override { return describe ? describe() : description; }
};

struct Mangrave : Module {
	enum ParamId { PITCH_PARAM, FINE_PARAM, FM_PARAM, FORMANT_PARAM, BARREL_PARAM, AIR_PARAM,
		FORMANT_ATT_PARAM, BARREL_ATT_PARAM, AIR_ATT_PARAM, CONSTANT_PARAM, PPM_PARAM, TRIG_BUTTON, PARAMS_LEN };
	enum InputId { VOCT_INPUT, FM_INPUT, SYNC_INPUT, FORMANT_INPUT, BARREL_INPUT, AIR_INPUT,
		INDEX_INPUT, TRIGGER_INPUT, TOAIR_INPUT, INPUTS_LEN };
	enum OutputId { SQUARE_OUTPUT, FORMANT_OUTPUT, SINE_OUTPUT, SAW_OUTPUT, PWM_OUTPUT, HOLLOW_OUTPUT, OUTPUTS_LEN };
	enum LightId { TRIG_LIGHT, AIR_LIGHT, LIGHTS_LEN };

	GroveEngine eng[16];
	Params shown;
	int channels = 1;
	bool fmDcCoupled = false;  // the hardware mod: FM input passes DC

	void onReset() override { fmDcCoupled = false; }
	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "fmDcCoupled", json_boolean(fmDcCoupled));
		return r;
	}
	void dataFromJson(json_t* r) override {
		if (json_t* j = json_object_get(r, "fmDcCoupled")) fmDcCoupled = json_boolean_value(j);
	}
	dsp::ClockDivider lightDiv;

	// scope: two core cycles of channel 1
	static const int N = 256;
	float scopeOut[N] = {}, scopeTri[N] = {}, scopePwm[N] = {};
	float shownOut[N] = {}, shownTri[N] = {}, shownPwm[N] = {};
	int pos = 0;
	double acc = 0.0;
	bool capturing = false;
	float wait = 0.f;

	GroveQuantity* q(int id) { return (GroveQuantity*) paramQuantities[id]; }

	Mangrave() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<GroveQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch", " Hz", std::pow(2.f, 10.f), 261.6256f / 32.f);
		configParam(FINE_PARAM, -6.f, 6.f, 0.f, "Fine (one octave in total)", " semitones");
		configParam<GroveQuantity>(FM_PARAM, -1.f, 1.f, 1.f, "FM index", "%", 0.f, 100.f);
		configParam<GroveQuantity>(FORMANT_PARAM, 0.f, 1.f, 0.5f, "Formant");
		configParam<GroveQuantity>(BARREL_PARAM, -1.f, 1.f, 0.f, "Barrel");
		configParam<GroveQuantity>(AIR_PARAM, 0.f, 1.f, 0.4f, "Air", "%", 0.f, 100.f);
		configParam(FORMANT_ATT_PARAM, -1.f, 1.f, 1.f, "Formant CV amount (+-5 V = half the knob)", "%", 0.f, 100.f);
		configParam(BARREL_ATT_PARAM, -1.f, 1.f, 1.f, "Barrel CV amount (+-5 V = the whole knob)", "%", 0.f, 100.f);
		configParam(AIR_ATT_PARAM, -1.f, 1.f, 1.f, "Air CV amount", "%", 0.f, 100.f);
		configSwitch<GroveSwitch>(CONSTANT_PARAM, 0.f, 1.f, 0.f, "Constant", {"Wave", "Formant"});
		configSwitch<GroveSwitch>(PPM_PARAM, 0.f, 2.f, 0.f, "PPM mode", {"Impulse (classic Mangrove)", "Sync", "Cycle"});
		configButton(TRIG_BUTTON, "Fire the impulse by hand (acts like a patched TRIGGER while held)");
		configInput(VOCT_INPUT, "Pitch (1 V/oct)");
		configInput(FM_INPUT, "FM (linear, through zero)");
		configInput(SYNC_INPUT, "Sync: resets the core");
		configInput(FORMANT_INPUT, "Formant CV (+-5 V sweeps half the knob)");
		configInput(BARREL_INPUT, "Barrel CV (+-5 V sweeps the whole knob)");
		configInput(AIR_INPUT, "Air CV (through the amount trimmer)");
		configInput(INDEX_INPUT, "FM index CV (0-5 V, unpatched = 5 V = full)");
		configInput(TRIGGER_INPUT, "PPM trigger: replaces the core as what fires the impulse");
		configInput(TOAIR_INPUT, "PPM to air: added to the impulse before the AIR stage");
		configOutput(SQUARE_OUTPUT, "Square (the core)");
		configOutput(FORMANT_OUTPUT, "Formant (the impulses through AIR)");
		configOutput(SINE_OUTPUT, "PPM sine (the core)");
		configOutput(SAW_OUTPUT, "PPM saw (the core)");
		configOutput(PWM_OUTPUT, "PPM PWM: high while the impulse rises (BARREL sweeps the width)");
		configOutput(HOLLOW_OUTPUT, "PPM hollow: flips on every impulse, half its rate (odd harmonics)");
		configLight(AIR_LIGHT, "Air");
		lightDiv.setDivision(64);

		q(PITCH_PARAM)->describe = [=]() {
			return "The core pitch, middle C at noon. Now " + hzS(eng[0].f0) + " (" + noteName(std::fabs(eng[0].f0)) + ").";
		};
		q(FORMANT_PARAM)->describe = [=]() {
			std::string s = "The impulse length: clockwise = shorter and brighter (a bit like a high pass). ";
			if (shown.constant == CONST_WAVE) {
				double r = durationRatio(shown.formant);
				s += string::f("CONSTANT WAVE: each impulse lasts x%.2f of a core cycle", r);
				if (r > 1.0) s += string::f(", so it skips cycles and you hear about 1/%d of the pitch (an undertone)", (int) std::ceil(r));
				return s + ".";
			}
			return s + "CONSTANT FORMANT: each impulse lasts " + msS(eng[0].dur) + " whatever the pitch, like a vowel.";
		};
		q(BARREL_PARAM)->describe = [=]() {
			return std::string("The impulse's rise against its fall. Anticlockwise: all rise, a ramp (bright). Noon: a triangle (softest). Clockwise: all fall, a saw (bright again).");
		};
		q(AIR_PARAM)->describe = [=]() {
			return std::string("The VCA and overdrive. Closed at 0, clean and quiet low down, 10 V peak to peak at noon, then the sine-shaping overdrive squares it off. Turn it up to hear anything.");
		};
		q(FM_PARAM)->describe = [=]() {
			return std::string("Linear FM depth (the FM INDEX CV multiplies it). Anticlockwise inverts it. Deep FM pushes the core through zero for clangy, bell-like tones. The FM jack blocks DC like the hardware (right-click for the DC-coupled mod).");
		};
		((GroveSwitch*) paramQuantities[CONSTANT_PARAM])->describe = [=]() {
			return std::string("WAVE: the impulse length follows the pitch, so the waveform keeps its shape as you play. FORMANT: the impulse length stays put, so a fixed region of the spectrum is emphasised, like a voice or a wind instrument.");
		};
		((GroveSwitch*) paramQuantities[PPM_PARAM])->describe = [=]() {
			return std::string("IMPULSE: classic, the core fires each impulse. SYNC: the impulse cycles on its own and the core soft-syncs it (an inverted Mangrove sound). CYCLE: the impulse is a second free oscillator; FORMANT sets its pitch and BARREL its shape.");
		};
	}

	void process(const ProcessArgs& args) override {
		int ch = 1;
		for (int i : {VOCT_INPUT, SYNC_INPUT, FM_INPUT, TRIGGER_INPUT, FORMANT_INPUT, BARREL_INPUT, AIR_INPUT, INDEX_INPUT, TOAIR_INPUT}) ch = std::max(ch, inputs[i].getChannels());
		channels = ch;
		Params p;
		p.pitch = params[PITCH_PARAM].getValue();
		p.fine = params[FINE_PARAM].getValue();
		p.constant = params[CONSTANT_PARAM].getValue() > 0.5f ? CONST_FORMANT : CONST_WAVE;
		p.ppm = (int) std::round(params[PPM_PARAM].getValue());
		p.fmDcCoupled = fmDcCoupled;
		bool button = params[TRIG_BUTTON].getValue() > 0.5f;
		bool trigPatched = inputs[TRIGGER_INPUT].isConnected();  // FIRE adds a trigger without silencing the core
		float fmIdx = params[FM_PARAM].getValue();
		for (int c = 0; c < ch; c++) {
			float index = inputs[INDEX_INPUT].isConnected() ? clamp(inputs[INDEX_INPUT].getPolyVoltage(c) / 5.f, 0.f, 1.f) : 1.f;
			p.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			p.fm = inputs[FM_INPUT].getPolyVoltage(c) * fmIdx * index;
			p.formant = params[FORMANT_PARAM].getValue() + inputs[FORMANT_INPUT].getPolyVoltage(c) / 10.f * params[FORMANT_ATT_PARAM].getValue();
			p.barrel = clamp(params[BARREL_PARAM].getValue() + inputs[BARREL_INPUT].getPolyVoltage(c) / 5.f * params[BARREL_ATT_PARAM].getValue(), -1.f, 1.f);
			p.air = clamp(params[AIR_PARAM].getValue() + inputs[AIR_INPUT].getPolyVoltage(c) / 10.f * params[AIR_ATT_PARAM].getValue(), 0.f, 1.f);
			p.toAir = inputs[TOAIR_INPUT].getPolyVoltage(c);
			float trig = std::max(inputs[TRIGGER_INPUT].getPolyVoltage(c), button ? 5.f : 0.f);
			eng[c].process(p, inputs[SYNC_INPUT].getPolyVoltage(c), trig, trigPatched, args.sampleRate);
			GroveEngine& e = eng[c];
			outputs[SQUARE_OUTPUT].setVoltage(e.square, c);
			outputs[FORMANT_OUTPUT].setVoltage(e.out, c);
			outputs[SINE_OUTPUT].setVoltage(e.sine, c);
			outputs[SAW_OUTPUT].setVoltage(e.saw, c);
			outputs[PWM_OUTPUT].setVoltage(e.pwm, c);
			outputs[HOLLOW_OUTPUT].setVoltage(e.hol, c);
			if (c == 0) shown = p;
		}
		for (int o = 0; o < OUTPUTS_LEN; o++) outputs[o].setChannels(ch);
		recordScope(args.sampleTime);

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			lights[TRIG_LIGHT].setBrightnessSmooth(eng[0].trigHigh ? 1.f : 0.f, dt);
			lights[AIR_LIGHT].setBrightnessSmooth(clamp(shown.air * 2.f, 0.f, 1.f), dt);
		}
	}

	void recordScope(float dt) {
		GroveEngine& e = eng[0];
		double f = std::max(1.0, std::fabs(e.f0));
		double window = std::min(0.5, 2.0 / f);
		if (shown.ppm == PPM_CYCLE || (shown.ppm == PPM_IMPULSE && inputs[TRIGGER_INPUT].isConnected()))
			window = std::min(0.5, std::max(window, 2.0 * e.dur));
		if (!capturing) {
			wait += dt;
			bool edge = shown.ppm == PPM_CYCLE ? e.started : e.coreEdge;
			if ((edge && wait > 0.03f) || wait > 0.3f) { capturing = true; pos = 0; acc = 0.0; }
		}
		if (!capturing) return;
		acc += dt;
		double step = window / N;
		while (acc >= step && pos < N) {
			acc -= step;
			scopeOut[pos] = e.out;
			scopeTri[pos] = e.tri;
			scopePwm[pos] = e.pwm;
			pos++;
		}
		if (pos >= N) {
			std::memcpy(shownOut, scopeOut, sizeof(shownOut));
			std::memcpy(shownTri, scopeTri, sizeof(shownTri));
			std::memcpy(shownPwm, scopePwm, sizeof(shownPwm));
			capturing = false;
			wait = 0.f;
		}
	}
};

// ---- display ---------------------------------------------------------------
static const NVGcolor G_BG = nvgRGB(0x0D, 0x11, 0x0E);
static const NVGcolor G_GRID = nvgRGB(0x1E, 0x26, 0x20);
static const NVGcolor G_INK = nvgRGB(0xEE, 0xE8, 0xDC);
static const NVGcolor G_SOFT = nvgRGB(0x84, 0x92, 0x86);
static const NVGcolor G_LEAF = nvgRGB(0x7C, 0xE0, 0x8A);
static const NVGcolor G_MUD = nvgRGB(0xC8, 0x9B, 0x5A);
static const NVGcolor G_PPM = nvgRGB(0xE0, 0x9C, 0xD8);

struct GroveDisplay : Widget {
	Mangrave* module = nullptr;

	void text(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, int align, float size) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}

	void trace(const DrawArgs& a, const float* v, float scale, float x, float y, float w, float h, NVGcolor c, float width) {
		nvgBeginPath(a.vg);
		for (int j = 0; j < Mangrave::N; j++) {
			float px = x + w * j / (Mangrave::N - 1);
			float py = y + h / 2 - clamp(v[j] / scale, -1.f, 1.f) * (h / 2 - 1);
			if (j == 0) nvgMoveTo(a.vg, px, py);
			else nvgLineTo(a.vg, px, py);
		}
		nvgStrokeColor(a.vg, c);
		nvgStrokeWidth(a.vg, width);
		nvgStroke(a.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(1.2f));
		nvgFillColor(args.vg, G_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
		float pad = mm2px(1.5f);
		if (!module) {
			text(args, W / 2, H / 2, "MANGRAVE", G_INK, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, mm2px(4.f));
			Widget::drawLayer(args, layer);
			return;
		}
		Mangrave* m = module;
		GroveEngine& e = m->eng[0];
		const Params& p = m->shown;

		float hy = pad + mm2px(1.5f);
		text(args, pad, hy, p.constant == CONST_WAVE ? "CONSTANT WAVE" : "CONSTANT FORMANT", G_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.7f));
		text(args, W / 2, hy, std::string("PPM ") + PPM_NAMES[clamp(p.ppm, 0, 2)], G_PPM, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, mm2px(2.6f));
		text(args, W - pad, hy, string::f("AIR %d%%", (int) std::round(p.air * 100.f)), G_LEAF, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.4f));

		float sx = pad, sy = pad + mm2px(4.0f), sw = W - 2 * pad, sh = H - sy - pad - mm2px(4.6f);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, sx, sy + sh / 2); nvgLineTo(args.vg, sx + sw, sy + sh / 2);
		nvgStrokeColor(args.vg, G_GRID);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		trace(args, m->shownTri, 5.f * 1.1f, sx, sy, sw, sh, nvgTransRGBA(G_SOFT, 110), 0.8f);
		trace(args, m->shownPwm, 5.f * 1.25f, sx, sy, sw, sh, nvgTransRGBA(G_MUD, 150), 0.8f);
		trace(args, m->shownOut, 5.7f * 1.05f, sx, sy, sw, sh, G_LEAF, 1.5f);
		text(args, sx + sw - 2, sy + 1, "green: FORMANT out  amber: impulse rising  grey: core", G_SOFT, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, mm2px(1.8f));

		float fy = H - pad - mm2px(1.6f);
		text(args, pad, fy, "PITCH " + hzS(e.f0) + " " + noteName(std::fabs(e.f0)), G_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.2f));
		std::string imp;
		if (p.ppm == PPM_CYCLE) imp = "IMPULSE OSC " + hzS(1.0 / e.dur);
		else if (p.constant == CONST_WAVE) {
			double r = durationRatio(p.formant);
			imp = string::f("IMPULSE x%.2f of a cycle", r);
			double rr = r * riseShare(p.barrel);  // impulses are ignored only while rising
			if (p.ppm == PPM_IMPULSE) {
				if (rr > 1.0) imp += string::f("  UNDERTONE 1/%d", (int) std::ceil(rr));
				else if (r > 1.0) imp += "  SPLIT TONE";
			}
		}
		else imp = "IMPULSE " + msS(e.dur) + " (" + hzS(1.0 / e.dur) + ")";
		text(args, W - pad, fy, imp, G_LEAF, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.2f));
		if (m->channels > 1)
			text(args, W * 0.72f, hy, string::f("%d ch", m->channels), G_SOFT, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, mm2px(2.0f));
		Widget::drawLayer(args, layer);
	}
};

struct MangraveWidget : ModuleWidget {
	MangraveWidget(Mangrave* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Mangrave.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		GroveDisplay* d = createWidget<GroveDisplay>(mm2px(Vec(5.0, 12.6)));
		d->box.size = mm2px(Vec(152.56, 32.0));
		d->module = module;
		addChild(d);

		const float rowA = 59.0, rowB = 76.0, rowC = 88.5;
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(19.0, rowA)), module, Mangrave::PITCH_PARAM));
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(48.0, rowA)), module, Mangrave::FORMANT_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(76.0, rowA)), module, Mangrave::BARREL_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(102.0, rowA)), module, Mangrave::AIR_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(11.0, rowB)), module, Mangrave::FINE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(27.0, rowB)), module, Mangrave::FM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(48.0, rowB)), module, Mangrave::FORMANT_ATT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(76.0, rowB)), module, Mangrave::BARREL_ATT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(102.0, rowB)), module, Mangrave::AIR_ATT_PARAM));
		addChild(createLightCentered<MediumLight<GreenLight>>(mm2px(Vec(110.0, rowA - 7.0)), module, Mangrave::AIR_LIGHT));

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(11.0, rowC)), module, Mangrave::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(27.0, rowC)), module, Mangrave::FM_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(48.0, rowC)), module, Mangrave::FORMANT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(62.0, rowC)), module, Mangrave::SYNC_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(76.0, rowC)), module, Mangrave::BARREL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.0, rowC)), module, Mangrave::AIR_INPUT));

		addParam(createParamCentered<CKSS>(mm2px(Vec(16.0, 116.0)), module, Mangrave::CONSTANT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(50.0, 117.0)), module, Mangrave::INDEX_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(76.0, 118.0)), module, Mangrave::SQUARE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(100.0, 118.0)), module, Mangrave::FORMANT_OUTPUT));

		// PPM section
		addParam(createParamCentered<CKSSThree>(mm2px(Vec(129.0, rowA)), module, Mangrave::PPM_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(131.0, rowB)), module, Mangrave::TRIGGER_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(149.0, rowB)), module, Mangrave::TRIG_BUTTON, Mangrave::TRIG_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(131.0, rowC + 2.0)), module, Mangrave::TOAIR_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(129.5, 109.8)), module, Mangrave::SINE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(145.5, 109.8)), module, Mangrave::SAW_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(129.5, 122.0)), module, Mangrave::PWM_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(145.5, 122.0)), module, Mangrave::HOLLOW_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Mangrave* m = getModule<Mangrave>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("DC-coupled FM (the hardware mod: a steady FM voltage shifts the pitch)", "", &m->fmDcCoupled));
	}
};

Model* modelMangrave = createModel<Mangrave, MangraveWidget>("Mangrave");
