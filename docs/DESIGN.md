# Touchstorm v0.1: design plan

Working name. An XY touch-pad effects processor and loop sampler (VST3 + standalone), in the
spirit of the classic DJ/performance touch-pad units. No trademarked names, artwork or factory
program names are used; programs are our own.

## What the original class of unit does (research summary)

- One large XY touch pad controls two parameters of the selected effect program at once.
- Around 100 to 150 programs grouped by colour-coded category: filters, modulation, LFO effects,
  delays, reverbs, grain/looper/shifter effects, pitch and vocoder, and synth/drum sounds that play
  from the pad.
- FX Depth knob (wet amount). Hold button keeps the effect at the last position after the finger
  lifts. Delays and reverbs keep their tails on release; filters snap back to dry.
- Tap tempo and auto BPM; tempo-synced effects.
- Four sample banks (A to D) that record loops in time with the BPM, play back through the effects,
  can be reversed, and can resample the output.
- Pad Motion: record a finger movement and loop it.
- Eight program memory buttons that store program, depth and pad state.
- MIDI control of X, Y and touch (fixed CCs), program change, MIDI clock sync.

## What v0.1 has

| Area | Implementation |
|---|---|
| Pad | Large XY pad with glow, trail and crosshair. X, Y and Touch are host parameters, so DAW automation records and plays back finger moves. |
| Programs | 50 programs in 8 categories: Filter (10), Modulation (9), Delay (6), Reverb (5), Pitch (4), Grain/Loop (6), Lo-Fi (6), Synth (5). The full list is in `Source/engine/Programs.cpp`. |
| Release | Filters, modulation, pitch, lo-fi and grain programs return to dry when the finger lifts (6 ms fade). Delays and reverbs stop taking input but ring out. Synth programs fade with a 250 ms release. |
| Hold | Keeps the effect on at the last position. |
| Pad Motion | While Motion is on, touching records the movement; on release it loops. Touching again re-records. |
| Tempo | Host tempo and transport by default; Tap tempo or the BPM parameter when Sync is off. Synced LFOs follow the host beat position. |
| Loop banks | 4 banks, 1 to 16 beats (max 16 s). Click: record (starts on the next beat when the host is playing), click again: stop/play. Right-click: clear, reverse. Banks play into the effect, like the hardware. Resample records the output instead of the input. |
| Memories | 8 slots (program, depth, X/Y, hold, key, scale). WRITE then a slot to store; click to recall. Saved with the session. |
| Synth key | Key and scale (minor pentatonic, major, minor, chromatic) for the synth programs; the pad X axis is quantised to the scale. |
| MIDI | CC12 X, CC13 Y, CC92 touch, CC93 depth, CC94 hold, CC95 motion, program change selects the program. |
| Themes | Midnight, Magma, Mint, Daylight, Vapor. Saved per instance. |
| Code layout | `Source/engine`: plain C++17, no JUCE, scalar DSP (no AVX2 or `-march`), unit tested in `tests/`. `Source/plugin`: JUCE 8.0.4 wrapper and UI. macOS 11+ universal, Windows static runtime. |

## Not yet done (candidates for v0.2)

- Vocoder program (needs a carrier: built-in synth or sidechain input).
- Drum/percussion programs played from the pad.
- More programs to reach the 100+ of the hardware, and variations per category.
- Loop banks locked to the host bar position (now they are tempo-length but free-running).
- MIDI learn for any parameter; MIDI clock in the standalone.
- User preset files (save/load memories to disk) and a factory memory bank.
- Resizable UI, oversampling of the drive stages, installers and CI.
