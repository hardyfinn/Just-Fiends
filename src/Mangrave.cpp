// Mangrave.cpp
// A formant oscillator with a clear interface: a scope shows the square core
// and the formant bursts with their dying envelope, and read-outs name the
// pitch, the formant and the ratio between them. The engine is in
// GroveEngine.hpp. Positions are in mm and match res/make_panel_mangrave.py.
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

struct GroveQuantity : ParamQuantity {
	std::function<std::string()> describe;
	std::string getDescription() override { return describe ? describe() : description; }
};

struct Mangrave : Module {
	enum ParamId { PITCH_PARAM, FINE_PARAM, FM_PARAM, FORMANT_PARAM, BARREL_PARAM, AIR_PARAM,
		FORMANT_ATT_PARAM, BARREL_ATT_PARAM, AIR_ATT_PARAM, CONSTANT_PARAM, STRIKE_BUTTON, PARAMS_LEN };
	enum InputId { VOCT_INPUT, FM_INPUT, STRIKE_INPUT, FORMANT_INPUT, BARREL_INPUT, AIR_INPUT, INPUTS_LEN };
	enum OutputId { SQUARE_OUTPUT, FORMANT_OUTPUT, OUTPUTS_LEN };
	enum LightId { STRIKE_LIGHT, AIR_LIGHT, LIGHTS_LEN };

	GroveEngine eng[16];
	Params shown;
	int channels = 1;
	bool strikePlucks = true;
	dsp::ClockDivider lightDiv;

	// scope: two core cycles of channel 1
	static const int N = 256;
	float scopeOut[N] = {}, scopeEnv[N] = {}, scopeSq[N] = {};
	float shownOut[N] = {}, shownEnv[N] = {}, shownSq[N] = {};
	int pos = 0;
	double acc = 0.0;
	bool capturing = false;
	float wait = 0.f;
	int cyclesSeen = 0;

	GroveQuantity* q(int id) { return (GroveQuantity*) paramQuantities[id]; }

	Mangrave() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<GroveQuantity>(PITCH_PARAM, 0.f, 1.f, 0.5f, "Pitch", " Hz", std::pow(2.f, 10.f), 261.6256f / 32.f);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine", " semitones");
		configParam<GroveQuantity>(FM_PARAM, -1.f, 1.f, 0.f, "FM index", "%", 0.f, 100.f);
		configParam<GroveQuantity>(FORMANT_PARAM, 0.f, 1.f, 0.5f, "Formant");
		configParam<GroveQuantity>(BARREL_PARAM, -1.f, 1.f, 0.f, "Barrel");
		configParam<GroveQuantity>(AIR_PARAM, 0.f, 1.f, 0.7f, "Air", "%", 0.f, 100.f);
		configParam(FORMANT_ATT_PARAM, -1.f, 1.f, 1.f, "Formant CV amount", "%", 0.f, 100.f);
		configParam(BARREL_ATT_PARAM, -1.f, 1.f, 1.f, "Barrel CV amount", "%", 0.f, 100.f);
		configParam(AIR_ATT_PARAM, -1.f, 1.f, 1.f, "Air CV amount", "%", 0.f, 100.f);
		configSwitch(CONSTANT_PARAM, 0.f, 1.f, 0.f, "Constant", {"Wave: the formant follows the pitch", "Formant: the formant stays put"});
		configButton(STRIKE_BUTTON, "Strike by hand");
		configInput(VOCT_INPUT, "Pitch (1 V/oct)");
		configInput(FM_INPUT, "FM (linear, through zero; scaled by FM index)");
		configInput(STRIKE_INPUT, "Strike: a trigger restarts the cycle and puffs air through");
		configInput(FORMANT_INPUT, "Formant CV (1 V/oct at full amount)");
		configInput(BARREL_INPUT, "Barrel CV (+-5 V)");
		configInput(AIR_INPUT, "Air CV (0-5 V opens it fully)");
		configOutput(SQUARE_OUTPUT, "Square (the core)");
		configOutput(FORMANT_OUTPUT, "Formant");
		configLight(AIR_LIGHT, "Air");
		lightDiv.setDivision(64);

		q(PITCH_PARAM)->describe = [=]() {
			return "The core pitch, middle C at noon. Now " + hzS(eng[0].f0) + " (" + noteName(std::fabs(eng[0].f0)) + ").";
		};
		q(FORMANT_PARAM)->describe = [=]() {
			if (shown.constant == CONST_WAVE)
				return string::f("CONSTANT WAVE: the formant is x%.2f the pitch, so the tone colour stays the same as you play. Now %s.", formantRatio(shown), hzS(eng[0].ff).c_str());
			return "CONSTANT FORMANT: the formant stays at one frequency whatever the pitch, like a vowel. Now " + hzS(eng[0].ff) + ".";
		};
		q(BARREL_PARAM)->describe = [=]() {
			return std::string("Skews each burst. Anticlockwise: a ramp up (bright, buzzy). Noon: a triangle (round). Clockwise: a ramp down.");
		};
		q(AIR_PARAM)->describe = [=]() {
			return std::string("How much air blows through: louder and longer bursts. At 0 it is silent; low settings are thin and reedy, full is a steady, rich tone. It is also the VCA: patch an envelope into AIR CV.");
		};
		q(FM_PARAM)->describe = [=]() {
			return std::string("Linear FM depth. Anticlockwise inverts it. At high depth the pitch passes through zero and runs backwards, which gives clangy, bell-like FM.");
		};
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_object_set_new(r, "strikePlucks", json_boolean(strikePlucks));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* j = json_object_get(r, "strikePlucks");
		if (j) strikePlucks = json_boolean_value(j);
	}
	void onReset() override { strikePlucks = true; }

	void process(const ProcessArgs& args) override {
		int ch = 1;
		for (int i : {VOCT_INPUT, STRIKE_INPUT, FM_INPUT}) ch = std::max(ch, inputs[i].getChannels());
		channels = ch;
		Params p;
		p.pitch = params[PITCH_PARAM].getValue();
		p.fine = params[FINE_PARAM].getValue();
		p.constant = params[CONSTANT_PARAM].getValue() > 0.5f ? CONST_FORMANT : CONST_WAVE;
		p.strikePlucks = strikePlucks;
		bool button = params[STRIKE_BUTTON].getValue() > 0.5f;
		float fmIdx = params[FM_PARAM].getValue();
		for (int c = 0; c < ch; c++) {
			p.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			p.fm = inputs[FM_INPUT].getPolyVoltage(c) * fmIdx;
			p.formant = params[FORMANT_PARAM].getValue();
			p.formantCv = inputs[FORMANT_INPUT].getPolyVoltage(c) * params[FORMANT_ATT_PARAM].getValue();
			p.barrel = clamp(params[BARREL_PARAM].getValue() + inputs[BARREL_INPUT].getPolyVoltage(c) / 5.f * params[BARREL_ATT_PARAM].getValue(), -1.f, 1.f);
			p.air = clamp(params[AIR_PARAM].getValue() + inputs[AIR_INPUT].getPolyVoltage(c) / 5.f * params[AIR_ATT_PARAM].getValue(), 0.f, 1.f);
			float strike = std::max(inputs[STRIKE_INPUT].getPolyVoltage(c), button ? 5.f : 0.f);
			eng[c].process(p, strike, args.sampleRate);
			outputs[SQUARE_OUTPUT].setVoltage(eng[c].square, c);
			outputs[FORMANT_OUTPUT].setVoltage(eng[c].out, c);
			if (c == 0) shown = p;
		}
		outputs[SQUARE_OUTPUT].setChannels(ch);
		outputs[FORMANT_OUTPUT].setChannels(ch);
		recordScope(args.sampleTime);

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			lights[STRIKE_LIGHT].setBrightnessSmooth(eng[0].strikeHigh ? 1.f : 0.f, dt);
			lights[AIR_LIGHT].setBrightnessSmooth(eng[0].airSmooth, dt);
		}
	}

	void recordScope(float dt) {
		GroveEngine& e = eng[0];
		double f = std::max(1.0, std::fabs(e.f0));
		double window = 2.0 / f;
		if (window > 0.5) window = 0.5;
		if (!capturing) {
			wait += dt;
			if ((e.wrapped && wait > 0.03f) || wait > 0.3f) { capturing = true; pos = 0; acc = 0.0; }
		}
		if (!capturing) return;
		acc += dt;
		double step = window / N;
		while (acc >= step && pos < N) {
			acc -= step;
			scopeOut[pos] = e.out;
			scopeEnv[pos] = e.env;
			scopeSq[pos] = e.square;
			pos++;
		}
		if (pos >= N) {
			std::memcpy(shownOut, scopeOut, sizeof(shownOut));
			std::memcpy(shownEnv, scopeEnv, sizeof(shownEnv));
			std::memcpy(shownSq, scopeSq, sizeof(shownSq));
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
		text(args, W - pad, hy, string::f("AIR %d%%", (int) std::round(e.airSmooth * 100.f)), G_LEAF, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.4f));

		float sx = pad, sy = pad + mm2px(4.0f), sw = W - 2 * pad, sh = H - sy - pad - mm2px(4.6f);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, sx, sy + sh / 2); nvgLineTo(args.vg, sx + sw, sy + sh / 2);
		nvgMoveTo(args.vg, sx + sw / 2, sy); nvgLineTo(args.vg, sx + sw / 2, sy + sh);
		nvgStrokeColor(args.vg, G_GRID);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		trace(args, m->shownSq, 5.f * 1.1f, sx, sy, sw, sh, nvgTransRGBA(G_SOFT, 120), 0.8f);
		trace(args, m->shownEnv, 1.1f, sx, sy, sw, sh, nvgTransRGBA(G_MUD, 200), 1.0f);
		trace(args, m->shownOut, 5.f * 1.1f, sx, sy, sw, sh, G_LEAF, 1.5f);
		text(args, sx + sw - 2, sy + 1, "green: formant  amber: burst envelope  grey: square", G_SOFT, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, mm2px(1.8f));

		float fy = H - pad - mm2px(1.6f);
		text(args, pad, fy, "PITCH " + hzS(e.f0) + " " + noteName(std::fabs(e.f0)), G_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.2f));
		double ratio = e.ff / std::max(1e-3, std::fabs(e.f0));
		text(args, W - pad, fy, string::f("FORMANT %s  x%.2f", hzS(e.ff).c_str(), ratio), G_LEAF, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.2f));
		if (m->channels > 1)
			text(args, W / 2, hy, string::f("%d ch", m->channels), G_SOFT, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, mm2px(2.0f));
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
		d->box.size = mm2px(Vec(111.92, 32.0));
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
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(62.0, rowC)), module, Mangrave::STRIKE_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(76.0, rowC)), module, Mangrave::BARREL_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(102.0, rowC)), module, Mangrave::AIR_INPUT));

		addParam(createParamCentered<CKSS>(mm2px(Vec(16.0, 116.0)), module, Mangrave::CONSTANT_PARAM));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(50.0, 116.0)), module, Mangrave::STRIKE_BUTTON, Mangrave::STRIKE_LIGHT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(76.0, 118.0)), module, Mangrave::SQUARE_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(100.0, 118.0)), module, Mangrave::FORMANT_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Mangrave* m = getModule<Mangrave>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Strike also puffs air through (a pluck)", "", &m->strikePlucks));
	}
};

Model* modelMangrave = createModel<Mangrave, MangraveWidget>("Mangrave");
