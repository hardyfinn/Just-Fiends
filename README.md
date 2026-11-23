# Just Fiends

Three VCV Rack modules in one plugin: **Just Fiends** (six linked slopes),
**Weird Sisters** (three linked filters) and **Mangrave** (a formant
oscillator). The guide is in docs/JustFiends-Guide.html.

## Just Fiends

A VCV Rack module of six linked slope generators: envelopes, LFOs and
oscillators that share one Time control, spread across six channels by Intone.
It is built for learning: every mode is labelled on the panel, the display draws
all six channels and names what the module is doing, and the tooltips explain
each control in the current mode.

Brand: **Haer**. Builds for Apple Silicon Macs run on every push (see the
Actions tab, or the "latest" release).

## Controls

| Control | What it does |
|---|---|
| TIME | Shape range: rate (1 Hz at noon). Sound range: pitch (middle C at noon). The input is 1 V/oct. |
| INTONE | Spreads the channels. Noon: all the same. Clockwise: channel N runs N times faster. Anticlockwise: N times slower. |
| RAMP | Rise/fall balance: pluck (fast rise) to swell (slow rise). |
| CURVE | Rectangle, log, linear (noon), exp, sine. |
| FM | Linear through-zero FM depth. Clockwise: all six (TIME). Anticlockwise: INTONE style, IDENTITY untouched. |
| RANGE | Shape (0-8 V envelopes and LFOs) or Sound (+-5 V oscillators). |
| MODE | Transient (one shot), Sustain (follows the gate), Cycle (loops). |
| RUN + ON | Turns on the Run modes (also on when the RUN jack is patched). The knob adds to the jack. |

| | Transient | Sustain | Cycle |
|---|---|---|---|
| Shape | Envelopes / **Shift** | ASR / **Strata** | LFOs / **Volley** |
| Sound | Impulses / **Spill** | Trapezoids / **Plume** | Oscillators / **Floom** |

Bold = with Run on. Right-click for **Poly pitch** (each channel plays its own
channel of the TIME input, like a six-voice synth), **Geode** (IDENTITY becomes
a clock and the six channels play locked rhythms) and trigger velocity.

Since 2.5.0 the engine follows the Just Friends Technical Map: triggers
normal right to left (one cable into 6N fires all six), Transient ignores
triggers until the slope ends, MIX divides each channel by its number in Shape
range, and the six Run modes use the hardware's RUN voltage maps.

## Weird Sisters

Three filters around one FREQ (1 V/oct). SPAN moves LOW down and HIGH up,
QUALITY goes from dry/wet (anticlockwise) through flat to self-oscillation
(from about 3 o'clock), MODE is crossover (LP / the band between / HP) or
formant (three band passes). Below about 10 o'clock SPAN crosses over and
closes the crossover CENTRE band like a VCA. Inputs for each sister, with ALL
in added to each. A display draws all three responses.

## Mangrave

A formant oscillator after the Mangrove technical map, with the PPM ("Patch
Programmable Mangrove") section built into the panel. A triangle core fires an
impulse (an AR envelope) on each cycle; FORMANT sets its length (long impulses
skip cycles: undertones; a retrigger while falling gives split tones), BARREL its rise/fall, AIR a VCA plus overdrive.
CONSTANT WAVE / FORMANT, SYNC, linear through-zero FM (AC-coupled, DC mod in the menu) with FM INDEX. PPM adds an
IMPULSE / SYNC / CYCLE switch, TRIGGER and TO AIR inputs, and SINE, SAW, PWM and
HOLLOW outputs.

## Building

```
make RACK_DIR=/path/to/Rack-SDK dist
for t in engine sisters grove; do c++ -std=c++11 -O2 -o test/${t}_test test/${t}_test.cpp && test/${t}_test; done
pip install fonttools && python3 res/make_panel.py && python3 res/make_panel_sisters.py && python3 res/make_panel_mangrave.py
```

License: GPL-3.0-or-later.
