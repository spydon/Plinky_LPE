# Plinky LPE

Plinky is an 8-voice polyphonic touch synthesiser that specialises in fragile, melancholic sounds.

<div class="grid cards" markdown>

- **[Manual](manual.md)**

  Complete user guide covering all features, parameters, and modulation

- **[Build Guide](build-guide.md)**

  Instructions for building and flashing Plinky from scratch

- **[Parameters](parameters.md)**

  Quick-reference grid for every parameter

- **[MIDI CC Map](midi-cc.md)**

  MIDI CC number to parameter mapping table

</div>

## Overview

Think of Plinky as 8 vertical monophonic strings played by touching the 64 main pads.
It supports 4 external CV modulation sources (A, B, X, Y), each with a dedicated LFO.
A & B also have physical offset knobs.

Each string has:

- **Either** 4 detuned sawtooth oscillators or 4 sample grains from one of 8 recordable samples
- A white noise oscillator
- An ADSR envelope driving a resonant 2-pole low-pass gate
- A secondary Attack-Decay envelope with repeat

Global effects include delay, reverb, high-pass filter, saturation, an arpeggiator, and a sequencer.

______________________________________________________________________

[Source code](https://github.com/ember-labs-io/Plinky_LPE) · [Community](https://www.plinkysynth.com)
