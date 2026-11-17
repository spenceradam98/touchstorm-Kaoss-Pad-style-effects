#include "Engine.h"

namespace ts
{
namespace
{
const Program kPrograms[] = {
    // Filter
    { "Low Pass Sweep",   Category::Filter, Algo::LowPass,     "Cutoff",       "Resonance" },
    { "High Pass Sweep",  Category::Filter, Algo::HighPass,    "Cutoff",       "Resonance" },
    { "Band Pass",        Category::Filter, Algo::BandPass,    "Frequency",    "Narrowness" },
    { "Notch Sweep",      Category::Filter, Algo::Notch,       "Frequency",    "Width" },
    { "Isolator",         Category::Filter, Algo::Isolator,    "Low/Mid/High", "Kill" },
    { "Talk Filter",      Category::Filter, Algo::TalkFilter,  "Vowel",        "Resonance" },
    { "Drive Filter",     Category::Filter, Algo::DriveFilter, "Cutoff",       "Drive" },
    { "Auto Wah",         Category::Filter, Algo::AutoWah,     "Range",        "Resonance" },
    { "Sync LFO Filter",  Category::Filter, Algo::LfoFilter,   "LFO Rate",     "Depth" },
    { "Comb Filter",      Category::Filter, Algo::Comb,        "Pitch",        "Feedback" },
    // Modulation
    { "Sync Flanger",     Category::Modulation, Algo::Flanger,    "Rate",      "Feedback" },
    { "Jet Flanger",      Category::Modulation, Algo::JetFlanger, "Manual",    "Feedback" },
    { "Phaser",           Category::Modulation, Algo::Phaser,     "Rate",      "Resonance" },
    { "Chorus",           Category::Modulation, Algo::Chorus,     "Rate",      "Depth" },
    { "Tremolo",          Category::Modulation, Algo::Tremolo,    "Rate",      "Depth" },
    { "Auto Pan",         Category::Modulation, Algo::AutoPan,    "Rate",      "Width" },
    { "Ring Modulator",   Category::Modulation, Algo::RingMod,    "Frequency", "Mix" },
    { "Vibrato",          Category::Modulation, Algo::Vibrato,    "Rate",      "Depth" },
    { "Slicer",           Category::Modulation, Algo::Slicer,     "Rate",      "Gate Length" },
    // Delay
    { "Tempo Delay",      Category::Delay, Algo::TempoDelay,  "Time",   "Feedback" },
    { "Ping Pong Delay",  Category::Delay, Algo::PingPong,    "Time",   "Feedback" },
    { "Tape Echo",        Category::Delay, Algo::TapeEcho,    "Time",   "Feedback" },
    { "Multi Tap",        Category::Delay, Algo::MultiTap,    "Spread", "Feedback" },
    { "Dub Echo",         Category::Delay, Algo::DubEcho,     "Time",   "Feedback" },
    { "Filter Delay",     Category::Delay, Algo::FilterDelay, "Time",   "Tone" },
    // Reverb
    { "Hall",             Category::Reverb, Algo::Hall,      "Decay",     "Brightness" },
    { "Room",             Category::Reverb, Algo::Room,      "Size",      "Brightness" },
    { "Shimmer",          Category::Reverb, Algo::Shimmer,   "Decay",     "Shimmer" },
    { "Gated Reverb",     Category::Reverb, Algo::GatedVerb, "Gate Time", "Size" },
    { "Delay + Reverb",   Category::Reverb, Algo::DelayVerb, "Time",      "Reverb" },
    // Pitch
    { "Pitch Shifter",    Category::Pitch, Algo::PitchShift, "Pitch",    "Grain" },
    { "Harmonizer",       Category::Pitch, Algo::Harmonizer, "Interval", "Blend" },
    { "Doubler",          Category::Pitch, Algo::Doubler,    "Detune",   "Spread" },
    { "Pitch Delay",      Category::Pitch, Algo::PitchDelay, "Shift",    "Feedback" },
    { "Vocoder",          Category::Pitch, Algo::Vocoder,    "Note",     "Formant" },
    // Grain / Loop
    { "Beat Repeat",      Category::GrainLoop, Algo::BeatRepeat,  "Length",     "Speed" },
    { "Reverse Loop",     Category::GrainLoop, Algo::ReverseLoop, "Length",     "Speed" },
    { "Grain Shifter",    Category::GrainLoop, Algo::GrainShift,  "Grain Size", "Spray" },
    { "Tape Stop",        Category::GrainLoop, Algo::TapeStop,    "Stop Time",  "Darken" },
    { "Scratch",          Category::GrainLoop, Algo::Scratch,     "Position",   "Tone" },
    { "Freeze",           Category::GrainLoop, Algo::Freeze,      "Grain Size", "Pitch" },
    // Lo-Fi
    { "Bit Crusher",      Category::LoFi, Algo::BitCrush,  "Bits",      "Sample Rate" },
    { "Decimator",        Category::LoFi, Algo::Decimator, "Rate",      "Tone" },
    { "Overdrive",        Category::LoFi, Algo::Overdrive, "Drive",     "Tone" },
    { "Fuzz",             Category::LoFi, Algo::Fuzz,      "Fuzz",      "Cutoff" },
    { "Old Radio",        Category::LoFi, Algo::Radio,     "Tuning",    "Crunch" },
    { "Worn Vinyl",       Category::LoFi, Algo::Vinyl,     "Wear",      "Bandwidth" },
    // Synth
    { "Lead Synth",       Category::Synth, Algo::LeadSynth,  "Note",  "Filter" },
    { "Bass Synth",       Category::Synth, Algo::BassSynth,  "Note",  "Resonance" },
    { "Drone",            Category::Synth, Algo::Drone,      "Root",  "Brightness" },
    { "Siren",            Category::Synth, Algo::Siren,      "Speed", "Pitch" },
    { "Noise Riser",      Category::Synth, Algo::NoiseRiser, "Sweep", "Resonance" },
    { "Drum Roll",        Category::Synth, Algo::DrumRoll,   "Roll Rate", "Kick/Snare/Hat" },
    { "Beat Loop",        Category::Synth, Algo::BeatLoop,   "Pattern",   "Tone" },
};
} // namespace

const Program* programs (int& count)
{
    count = (int) (sizeof (kPrograms) / sizeof (kPrograms[0]));
    return kPrograms;
}

const char* categoryName (Category c)
{
    switch (c)
    {
        case Category::Filter:     return "Filter";
        case Category::Modulation: return "Modulation";
        case Category::Delay:      return "Delay";
        case Category::Reverb:     return "Reverb";
        case Category::Pitch:      return "Pitch";
        case Category::GrainLoop:  return "Grain/Loop";
        case Category::LoFi:       return "Lo-Fi";
        case Category::Synth:      return "Synth";
        default:                   return "";
    }
}

int firstProgramInCategory (Category c)
{
    int n = 0;
    auto* p = programs (n);
    for (int i = 0; i < n; ++i)
        if (p[i].category == c) return i;
    return 0;
}

ReleaseKind releaseKind (Algo a)
{
    switch (a)
    {
        case Algo::TempoDelay: case Algo::PingPong: case Algo::TapeEcho: case Algo::MultiTap:
        case Algo::DubEcho: case Algo::FilterDelay: case Algo::Hall: case Algo::Room: case Algo::Shimmer:
        case Algo::GatedVerb: case Algo::DelayVerb: case Algo::PitchDelay:
            return ReleaseKind::Tail;
        default:
            return ReleaseKind::Bypass;
    }
}
} // namespace ts
