# Parameters

Parameters are arranged in a grid. The **Param 1** and **Param 2** shift buttons (bottom-left pair, blue LEDs) select which layer is active. Each row corresponds to a functional area; each column corresponds to a pad position.

## Parameter Grid

### CV / LFO — A & B

|             | Col 1   | Col 2    | Col 3   | Col 4  | Col 5   | Col 6  |
| ----------- | ------- | -------- | ------- | ------ | ------- | ------ |
| **Param 1** | Scale A | Offset A | Depth A | Rate A | Shape A | Warp A |
| **Param 2** | Scale B | Offset B | Depth B | Rate B | Shape B | Warp B |

### CV / LFO — X & Y

|             | Col 1   | Col 2    | Col 3   | Col 4  | Col 5   | Col 6  |
| ----------- | ------- | -------- | ------- | ------ | ------- | ------ |
| **Param 1** | Scale X | Offset X | Depth X | Rate X | Shape X | Warp X |
| **Param 2** | Scale Y | Offset Y | Depth Y | Rate Y | Shape Y | Warp Y |

### Delay

|             | Col 1      | Col 2      | Col 3      | Col 4      | Col 5      | Col 6      |
| ----------- | ---------- | ---------- | ---------- | ---------- | ---------- | ---------- |
| **Param 1** | Send       | Time       | Ratio      | Wobble     | Feedback   | Env2 Level |
| **Param 2** | *(Reverb)* | *(Reverb)* | *(Reverb)* | *(Reverb)* | *(Reverb)* | *(Reverb)* |

### Reverb

|             | Col 1 | Col 2 | Col 3   | Col 4  | Col 5 | Col 6       |
| ----------- | ----- | ----- | ------- | ------ | ----- | ----------- |
| **Param 1** | Send  | Time  | Shimmer | Wobble | —     | Env2 Repeat |

### Pitch

|             | Col 1  | Col 2 | Col 3     | Col 4    | Col 5       | Col 6     |
| ----------- | ------ | ----- | --------- | -------- | ----------- | --------- |
| **Param 1** | Octave | Pitch | Glide     | Interval | Gate Length | Env2 Rate |
| **Param 2** | Rotate | Scale | Microtone | Stride   | PWM         | Env2 Warp |

### Amp / Mix

|                   | Col 1       | Col 2       | Col 3       | Col 4   | Col 5   | Col 6     |
| ----------------- | ----------- | ----------- | ----------- | ------- | ------- | --------- |
| **Param 1 (Amp)** | Sensitivity | Drive       | Attack      | Decay   | Sustain | Release   |
| **Param 2 (Mix)** | Synth Level | Input Level | In→FX Level | Wet/Dry | HPF     | Resonance |

### Arp / Sequencer

|                   | Col 1    | Col 2  | Col 3       | Col 4         | Col 5      | Col 6 |
| ----------------- | -------- | ------ | ----------- | ------------- | ---------- | ----- |
| **Param 1 (Arp)** | Arp Mode | Divide | Probability | Euclid Length | Arp Octave | Tempo |
| **Param 2 (Seq)** | Seq Mode | Divide | Probability | Euclid Length | Pattern    | Step  |

### Sampler

|             | Col 1  | Col 2        | Col 3       | Col 4             | Col 5       | Col 6         |
| ----------- | ------ | ------------ | ----------- | ----------------- | ----------- | ------------- |
| **Param 1** | Sample | Scrub        | Rate        | Grain Size        | Timestretch | Headphone Vol |
| **Param 2** | Noise  | Scrub Jitter | Rate Jitter | Grain Size Jitter | —           | CV Quantise   |

______________________________________________________________________

## Modulation Sources

The **rightmost column** selects the modulation source when Param 1 or 2 is held:

| Position | Source                    |
| -------- | ------------------------- |
| Top      | Base Value                |
| 2        | Random                    |
| 3        | Pressure                  |
| 4        | Env2                      |
| 5        | A (CV A + Knob A + LFO A) |
| 6        | B (CV B + Knob B + LFO B) |
| 7        | X (CV X + LFO X)          |
| Bottom   | Y (CV Y + LFO Y)          |
