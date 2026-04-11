# MIDI CC Map

Plinky responds to standard MIDI CC messages for parameter control. Parameters marked `!!!` are reserved and not mapped to Plinky parameters.

| CC     | Standard Name        | Plinky Parameter                      |
| ------ | -------------------- | ------------------------------------- |
| 0      | Bank Select          | *reserved*                            |
| 1      | Modulation Wheel     | *reserved*                            |
| 2      | Breath               | Noise Level (`P_NOISE`)               |
| 3      | —                    | Sensitivity (`P_SENS`)                |
| 4      | Foot Controller      | Drive (`P_DRIVE`)                     |
| 5      | Portamento Time      | Glide (`P_GLIDE`)                     |
| 6      | Data Entry           | *reserved*                            |
| 7      | Volume               | Synth Level (`P_MIXSYNTH`)            |
| 8      | Balance              | Wet/Dry (`P_MIXWETDRY`)               |
| 9      | —                    | Pitch (`P_PITCH`)                     |
| 10     | Pan                  | —                                     |
| 11     | Expression           | Gate Length (`P_GATE_LENGTH`)         |
| 12     | FX Control 1         | Delay Time (`P_DLTIME`)               |
| 13     | FX Control 2         | PWM (`P_PWM`)                         |
| 14     | —                    | Interval (`P_INTERVAL`)               |
| 15     | —                    | Scrub Position (`P_SMP_POS`)          |
| 16     | —                    | Grain Size (`P_SMP_GRAINSIZE`)        |
| 17     | —                    | Sample Rate (`P_SMP_RATE`)            |
| 18     | —                    | Timestretch (`P_SMP_TIME`)            |
| 19     | —                    | Env2 Level (`P_ENVLVL2`)              |
| 20     | —                    | Env2 Attack (`P_A2`)                  |
| 21     | —                    | Env2 Decay (`P_D2`)                   |
| 22     | —                    | Env2 Sustain (`P_S2`)                 |
| 23     | —                    | Env2 Release (`P_R2`)                 |
| 24     | —                    | A Offset (`P_AOFFSET`)                |
| 25     | —                    | A Depth (`P_ADEPTH`)                  |
| 26     | —                    | A Rate (`P_AFREQ`)                    |
| 27     | —                    | B Offset (`P_BOFFSET`)                |
| 28     | —                    | B Depth (`P_BDEPTH`)                  |
| 29     | —                    | B Rate (`P_BFREQ`)                    |
| 30     | —                    | —                                     |
| 31     | —                    | High Pass Filter (`P_MIXHPF`)         |
| 32–63  | LSB (high-res pairs) | *reserved*                            |
| 64     | Sustain Pedal        | *reserved*                            |
| 65     | Portamento Switch    | *reserved*                            |
| 66     | Sostenuto Switch     | *reserved*                            |
| 67     | Soft Pedal           | *reserved*                            |
| 68     | Legato Switch        | *reserved*                            |
| 69     | Hold 2               | *reserved*                            |
| 70     | Sound Variation      | —                                     |
| 71     | Harmonic Content     | Resonance (`P_MIXRESO`)               |
| 72     | Release Time         | Release (`P_R`)                       |
| 73     | Attack Time          | Attack (`P_A`)                        |
| 74     | Brightness           | Sustain (`P_S`)                       |
| 75     | Decay Time           | Decay (`P_D`)                         |
| 76     | Vibrato Rate         | X Rate (`P_XFREQ`)                    |
| 77     | Vibrato Depth        | X Depth (`P_XDEPTH`)                  |
| 78     | Vibrato Delay        | X Offset (`P_XOFFSET`)                |
| 79     | Sound Controller 10  | Y Rate (`P_YFREQ`)                    |
| 80     | —                    | Y Depth (`P_YDEPTH`)                  |
| 81     | —                    | Y Offset (`P_YOFFSET`)                |
| 82     | —                    | Sample Select (`P_SAMPLE`)            |
| 83     | —                    | Seq Pattern (`P_SEQPAT`)              |
| 84     | Portamento Control   | *reserved*                            |
| 85     | —                    | Seq Step (`P_SEQSTEP`)                |
| 86     | —                    | —                                     |
| 87     | —                    | —                                     |
| 88     | High Velocity        | *reserved*                            |
| 89     | —                    | Input Level (`P_MIXINPUT`)            |
| 90     | —                    | Input → FX Level (`P_MIXINWETDRY`)    |
| 91     | Reverb Send Level    | Reverb Send (`P_RVSEND`)              |
| 92     | FX Depth             | Reverb Time (`P_RVTIME`)              |
| 93     | Chorus Level         | Shimmer (`P_RVSHIM`)                  |
| 94     | FX Depth             | Delay Send (`P_DLSEND`)               |
| 95     | FX Depth             | Delay Feedback (`P_DLFB`)             |
| 96–101 | —                    | *reserved*                            |
| 102    | —                    | Arp On/Off (`P_ARPONOFF`)             |
| 103    | —                    | Arp Mode (`P_ARPMODE`)                |
| 104    | —                    | Arp Divide (`P_ARPDIV`)               |
| 105    | —                    | Arp Probability (`P_ARPPROB`)         |
| 106    | —                    | Arp Euclid Length (`P_ARPLEN`)        |
| 107    | —                    | Arp Octave (`P_ARPOCT`)               |
| 108    | —                    | Seq Mode (`P_SEQMODE`)                |
| 109    | —                    | Seq Divide (`P_SEQDIV`)               |
| 110    | —                    | Seq Probability (`P_SEQPROB`)         |
| 111    | —                    | Seq Euclid Length (`P_SEQLEN`)        |
| 112    | —                    | Delay Ratio (`P_DLRATIO`)             |
| 113    | —                    | Delay Wobble (`P_DLWOB`)              |
| 114    | —                    | Reverb Wobble (`P_RVWOB`)             |
| 115    | —                    | —                                     |
| 116    | —                    | Scrub Jitter (`P_JIT_POS`)            |
| 117    | —                    | Grain Size Jitter (`P_JIT_GRAINSIZE`) |
| 118    | —                    | Rate Jitter (`P_JIT_RATE`)            |
| 119    | —                    | Pulse Jitter (`P_JIT_PULSE`)          |
