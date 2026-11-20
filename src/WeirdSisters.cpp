// WeirdSisters.cpp
// Three linked filters with a clear interface: a display draws the response of
// LOW, CENTRE and HIGH, names the mode and the three frequencies, and the
// tooltips explain each control. The engine is in SistersEngine.hpp. Positions
// are in mm and match res/make_panel_sisters.py.
#include "plugin.hpp"
#include "SistersEngine.hpp"
#include <functional>

using namespace ws;

static std::string hzStr(double f) {
	if (f >= 1000.0) return string::f("%.2f kHz", f / 1000.0);
	return string::f("%.0f Hz", f);
}

static std::string qualityWords(float q) {
	if (q < -0.6f) return "anti-resonant: CENTRE is a notch, the edges dip";
	if (q < -0.05f) return "a little anti-resonance (soft dips)";
	if (q < 0.05f) return "flat (no resonance)";
	if (q < 0.6f) return "resonant";
	if (q < 0.95f) return "very resonant (ringing)";
	return "self-oscillating: the filters sing on their own";
}

struct SisQuantity : ParamQuantity {
	std::function<std::string()> describe;
	std::string getDescription() override { return describe ? describe() : description; }
};

struct WeirdSisters : Module {
	enum ParamId { FREQ_PARAM, SPAN_PARAM, QUALITY_PARAM, FM_PARAM, SPAN_ATT_PARAM, QUALITY_ATT_PARAM, MODE_PARAM, PARAMS_LEN };
	enum InputId { VOCT_INPUT, FM_INPUT, SPAN_INPUT, QUALITY_INPUT, LOW_INPUT, CENTRE_INPUT, HIGH_INPUT, ALL_INPUT, INPUTS_LEN };
	enum OutputId { LOW_OUTPUT, CENTRE_OUTPUT, HIGH_OUTPUT, ALL_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENUMS(LEVEL_LIGHT, 4), LIGHTS_LEN };

	SistersEngine eng[16];
	Params shown;  // channel 1's settings, for the display
	int channels = 1;
	dsp::ClockDivider lightDiv;

	SisQuantity* q(int id) { return (SisQuantity*) paramQuantities[id]; }

	WeirdSisters() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam<SisQuantity>(FREQ_PARAM, 0.f, 1.f, 0.5f, "Freq", " Hz", std::pow(2.f, 10.f), 20.f);
		configParam<SisQuantity>(SPAN_PARAM, 0.f, 1.f, 0.3f, "Span", " octaves", 0.f, 4.f);
		configParam<SisQuantity>(QUALITY_PARAM, -1.f, 1.f, 0.f, "Quality");
		configParam(FM_PARAM, -1.f, 1.f, 0.f, "FM amount (octaves per volt)", "%", 0.f, 100.f);
		configParam(SPAN_ATT_PARAM, -1.f, 1.f, 1.f, "Span CV amount", "%", 0.f, 100.f);
		configParam(QUALITY_ATT_PARAM, -1.f, 1.f, 1.f, "Quality CV amount", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Crossover: low pass / band pass / high pass", "Formant: three band passes"});
		configInput(VOCT_INPUT, "Freq (1 V/oct)");
		configInput(FM_INPUT, "FM");
		configInput(SPAN_INPUT, "Span CV (1 V = 1 octave at full amount)");
		configInput(QUALITY_INPUT, "Quality CV (+-5 V)");
		configInput(LOW_INPUT, "LOW sister in (unpatched: from ALL)");
		configInput(CENTRE_INPUT, "CENTRE sister in (unpatched: from ALL)");
		configInput(HIGH_INPUT, "HIGH sister in (unpatched: from ALL)");
		configInput(ALL_INPUT, "ALL in: feeds every sister without its own input");
		configOutput(LOW_OUTPUT, "LOW");
		configOutput(CENTRE_OUTPUT, "CENTRE");
		configOutput(HIGH_OUTPUT, "HIGH");
		configOutput(ALL_OUTPUT, "ALL (the three sisters summed)");
		configBypass(ALL_INPUT, ALL_OUTPUT);
		lightDiv.setDivision(64);

		q(FREQ_PARAM)->describe = [=]() {
			return std::string("The centre frequency all three sisters share. 1 V/oct at the V/OCT jack, so with QUALITY up it plays like an oscillator.");
		};
		q(SPAN_PARAM)->describe = [=]() {
			double so = spanOctaves(shown.span, shown.spanCv);
			return string::f("How far LOW sits below and HIGH sits above the centre: now %.1f octaves each way. Fully anticlockwise all three meet at FREQ.", so);
		};
		q(QUALITY_PARAM)->describe = [=]() {
			return "Noon is flat. Clockwise adds resonance up to self-oscillation. Anticlockwise is anti-resonance: CENTRE becomes a notch. Now: " + qualityWords(shown.quality) + ".";
		};
	}

	void process(const ProcessArgs& args) override {
		int ch = 1;
		for (int i : {VOCT_INPUT, LOW_INPUT, CENTRE_INPUT, HIGH_INPUT, ALL_INPUT}) ch = std::max(ch, inputs[i].getChannels());
		channels = ch;
		Params p;
		p.freqKnob = params[FREQ_PARAM].getValue();
		p.mode = params[MODE_PARAM].getValue() > 0.5f ? FORMANT : CROSSOVER;
		float spanK = params[SPAN_PARAM].getValue(), qK = params[QUALITY_PARAM].getValue();
		float fmAmt = params[FM_PARAM].getValue(), spanAtt = params[SPAN_ATT_PARAM].getValue(), qAtt = params[QUALITY_ATT_PARAM].getValue();
		for (int c = 0; c < ch; c++) {
			p.voct = inputs[VOCT_INPUT].getPolyVoltage(c);
			p.fmOct = inputs[FM_INPUT].getPolyVoltage(c) * fmAmt;
			p.span = spanK;
			p.spanCv = inputs[SPAN_INPUT].getPolyVoltage(c) * spanAtt;
			p.quality = clamp(qK + inputs[QUALITY_INPUT].getPolyVoltage(c) / 5.f * qAtt, -1.f, 1.f);
			float all = inputs[ALL_INPUT].getPolyVoltage(c);
			float lo = inputs[LOW_INPUT].isConnected() ? inputs[LOW_INPUT].getPolyVoltage(c) : all;
			float ce = inputs[CENTRE_INPUT].isConnected() ? inputs[CENTRE_INPUT].getPolyVoltage(c) : all;
			float hi = inputs[HIGH_INPUT].isConnected() ? inputs[HIGH_INPUT].getPolyVoltage(c) : all;
			eng[c].process(p, lo, ce, hi, args.sampleRate);
			for (int k = 0; k < 3; k++) outputs[LOW_OUTPUT + k].setVoltage(eng[c].out[k], c);
			outputs[ALL_OUTPUT].setVoltage(eng[c].all, c);
			if (c == 0) shown = p;
		}
		for (int k = 0; k < 4; k++) outputs[LOW_OUTPUT + k].setChannels(ch);

		if (lightDiv.process()) {
			float dt = args.sampleTime * lightDiv.getDivision();
			for (int k = 0; k < 3; k++) lights[LEVEL_LIGHT + k].setBrightnessSmooth(std::fabs(eng[0].out[k]) / 5.f, dt);
			lights[LEVEL_LIGHT + 3].setBrightnessSmooth(std::fabs(eng[0].all) / 5.f, dt);
		}
	}
};

// ---- display ---------------------------------------------------------------
static const NVGcolor S_BG = nvgRGB(0x0E, 0x10, 0x12);
static const NVGcolor S_GRID = nvgRGB(0x22, 0x26, 0x2A);
static const NVGcolor S_INK = nvgRGB(0xEE, 0xE8, 0xDC);
static const NVGcolor S_SOFT = nvgRGB(0x86, 0x8E, 0x94);
static const NVGcolor S_COL[4] = {
	nvgRGB(0xF2, 0x8C, 0x3A),  // LOW: ember
	nvgRGB(0xB8, 0xE0, 0x4A),  // CENTRE: acid
	nvgRGB(0x5C, 0xC8, 0xF0),  // HIGH: ice
	nvgRGB(0xEE, 0xE8, 0xDC),  // ALL
};

struct SistersDisplay : Widget {
	WeirdSisters* module = nullptr;

	void text(const DrawArgs& a, float x, float y, const std::string& s, NVGcolor c, int align, float size) {
		nvgFontSize(a.vg, size);
		nvgTextAlign(a.vg, align);
		nvgFillColor(a.vg, c);
		nvgText(a.vg, x, y, s.c_str(), NULL);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1) { Widget::drawLayer(args, layer); return; }
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, mm2px(1.2f));
		nvgFillColor(args.vg, S_BG);
		nvgFill(args.vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
		float pad = mm2px(1.5f);
		Params p;
		if (module) p = module->shown;

		// header
		float hy = pad + mm2px(1.5f);
		text(args, pad, hy, p.mode == FORMANT ? "FORMANT" : "CROSSOVER", S_INK, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(3.0f));
		text(args, W - pad, hy, qualityWords(p.quality).substr(0, qualityWords(p.quality).find(':')), p.quality > 0.95f ? S_COL[1] : S_SOFT,
			NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.2f));

		// plot area: 20 Hz .. 20 kHz, -36 .. +18 dB
		float px = pad, py = pad + mm2px(4.2f), pw = W - 2 * pad, ph = H - py - pad - mm2px(4.4f);
		const double dbTop = 18.0, dbBot = -36.0;
		auto xOf = [&](double hz) { return px + pw * (float) (std::log2(hz / 20.0) / std::log2(1000.0)); };
		auto yOf = [&](double db) { db = std::max(dbBot, std::min(dbTop, db)); return py + ph * (float) ((dbTop - db) / (dbTop - dbBot)); };
		nvgBeginPath(args.vg);
		for (double hz : {100.0, 1000.0, 10000.0}) { nvgMoveTo(args.vg, xOf(hz), py); nvgLineTo(args.vg, xOf(hz), py + ph); }
		nvgMoveTo(args.vg, px, yOf(0.0)); nvgLineTo(args.vg, px + pw, yOf(0.0));
		nvgStrokeColor(args.vg, S_GRID);
		nvgStrokeWidth(args.vg, 1.f);
		nvgStroke(args.vg);
		text(args, xOf(100.0) + 2, py + ph - 2, "100", S_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, mm2px(1.9f));
		text(args, xOf(1000.0) + 2, py + ph - 2, "1k", S_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, mm2px(1.9f));
		text(args, xOf(10000.0) + 2, py + ph - 2, "10k", S_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, mm2px(1.9f));
		text(args, px + 2, yOf(0.0) - 1, "0 dB", S_SOFT, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, mm2px(1.9f));

		double f = knobHz(p.freqKnob, p.voct + p.fmOct);
		double so = spanOctaves(p.span, p.spanCv);
		double fc[3] = {f * std::pow(2.0, -so), f, f * std::pow(2.0, so)};
		double k = std::max(0.02, dampK(p.quality));
		double anti = p.quality < 0.f ? -p.quality : 0.0;
		const int N = 160;
		for (int s = 3; s >= 0; s--) {
			nvgBeginPath(args.vg);
			for (int j = 0; j <= N; j++) {
				double hz = 20.0 * std::pow(1000.0, j / (double) N);
				double mag;
				if (s < 3) mag = response(hz, std::min(fc[s], 20000.0), k, anti, p.mode == FORMANT ? 1 : s);
				else {
					mag = 0.0;  // ALL: rough sum of magnitudes, enough to show the shape
					for (int t = 0; t < 3; t++) mag += response(hz, std::min(fc[t], 20000.0), k, anti, p.mode == FORMANT ? 1 : t);
				}
				double db = 20.0 * std::log10(std::max(1e-6, mag));
				float x = xOf(hz), y = yOf(db);
				if (j == 0) nvgMoveTo(args.vg, x, y);
				else nvgLineTo(args.vg, x, y);
			}
			nvgStrokeColor(args.vg, s == 3 ? nvgTransRGBA(S_COL[3], 90) : S_COL[s]);
			nvgStrokeWidth(args.vg, s == 3 ? 1.0f : 1.6f);
			nvgStroke(args.vg);
		}
		// footer: the three frequencies
		static const char* NAMES[3] = {"LOW", "CENTRE", "HIGH"};
		float fy = H - pad - mm2px(1.6f);
		for (int s = 0; s < 3; s++)
			text(args, pad + (W - 2 * pad) * (s / 3.f), fy, string::f("%s %s", NAMES[s], hzStr(std::min(fc[s], 20000.0)).c_str()), S_COL[s],
				NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mm2px(2.3f));
		if (module && module->channels > 1)
			text(args, W - pad, fy, string::f("%d ch", module->channels), S_SOFT, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, mm2px(2.0f));
		Widget::drawLayer(args, layer);
	}
};

struct WeirdSistersWidget : ModuleWidget {
	WeirdSistersWidget(WeirdSisters* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/WeirdSisters.svg")));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		SistersDisplay* d = createWidget<SistersDisplay>(mm2px(Vec(5.0, 12.6)));
		d->box.size = mm2px(Vec(111.92, 32.0));
		d->module = module;
		addChild(d);

		const float rowA = 59.0, rowB = 76.0, rowC = 88.5;
		addParam(createParamCentered<RoundBigBlackKnob>(mm2px(Vec(20.0, rowA)), module, WeirdSisters::FREQ_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(52.0, rowA)), module, WeirdSisters::SPAN_PARAM));
		addParam(createParamCentered<RoundLargeBlackKnob>(mm2px(Vec(80.0, rowA)), module, WeirdSisters::QUALITY_PARAM));
		addParam(createParamCentered<CKSS>(mm2px(Vec(100.0, rowA)), module, WeirdSisters::MODE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(28.0, rowB)), module, WeirdSisters::FM_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(52.0, rowB)), module, WeirdSisters::SPAN_ATT_PARAM));
		addParam(createParamCentered<Trimpot>(mm2px(Vec(80.0, rowB)), module, WeirdSisters::QUALITY_ATT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(12.0, rowC)), module, WeirdSisters::VOCT_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(30.0, rowC)), module, WeirdSisters::FM_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(52.0, rowC)), module, WeirdSisters::SPAN_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(80.0, rowC)), module, WeirdSisters::QUALITY_INPUT));

		const float colX[4] = {20.0, 44.0, 68.0, 96.0};
		const int ins[4] = {WeirdSisters::LOW_INPUT, WeirdSisters::CENTRE_INPUT, WeirdSisters::HIGH_INPUT, WeirdSisters::ALL_INPUT};
		for (int i = 0; i < 4; i++) {
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(colX[i], 106.0)), module, ins[i]));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(colX[i], 121.6)), module, WeirdSisters::LOW_OUTPUT + i));
			addChild(createLightCentered<SmallLight<YellowLight>>(mm2px(Vec(colX[i] + 6.0, 121.6)), module, WeirdSisters::LEVEL_LIGHT + i));
		}
	}
};

Model* modelWeirdSisters = createModel<WeirdSisters, WeirdSistersWidget>("WeirdSisters");
