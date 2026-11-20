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
| FM | How much the FM input bends TIME, in octaves per volt. |
| RANGE | Shape (0-8 V envelopes and LFOs) or Sound (+-5 V oscillators). |
| MODE | Transient (one shot), Sustain (follows the gate), Cycle (loops). |
| RUN + ON | Turns on the Run modes (also on when the RUN jack is patched). The knob adds to the jack. |

| | Transient | Sustain | Cycle |
|---|---|---|---|
| Shape | Envelopes / **Shift** | ASR / **Strata** | LFOs / **Volley** |
| Sound | Impulses / **Spill** | Gated oscillators / **Plume** | Oscillators / **Floom** |

Bold = with Run on. Right-click for **Poly pitch** (each channel plays its own
channel of the TIME input, like a six-voice synth), **Geode** (IDENTITY becomes
a clock and the six channels play locked rhythms) and trigger velocity.

## Weird Sisters

Three filters around one FREQ (1 V/oct). SPAN moves LOW down and HIGH up,
QUALITY goes from anti-resonance (notch) through flat to self-oscillation,
MODE is crossover (LP / BP / HP) or formant (three band passes). Inputs for
each sister, normalled from ALL. A display draws all three responses.

## Mangrave

A formant oscillator: a square core fires a burst of a second wave every
cycle. FORMANT sets where the bursts sit (a ratio of the pitch in CONSTANT
WAVE, a fixed frequency in CONSTANT FORMANT), BARREL skews them, AIR sets their
length and level (it is the VCA). STRIKE restarts the cycle and puffs air
through. Linear through-zero FM. A scope shows the core, the bursts and their
envelope.

## Building

```
make RACK_DIR=/path/to/Rack-SDK dist
for t in engine sisters grove; do c++ -std=c++11 -O2 -o test/${t}_test test/${t}_test.cpp && test/${t}_test; done
pip install fonttools && python3 res/make_panel.py && python3 res/make_panel_sisters.py && python3 res/make_panel_mangrave.py
```

License: GPL-3.0-or-later.
