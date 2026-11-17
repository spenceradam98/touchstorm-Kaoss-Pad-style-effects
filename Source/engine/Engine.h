#pragma once
// Touchstorm engine: an XY touch-pad effects processor with a 4-bank loop sampler and
// pad-motion recording. No JUCE dependency, so it can be unit tested on its own.

#include "Dsp.h"
#include <array>
#include <atomic>
#include <memory>

namespace ts
{
enum class Algo
{
    // Filter
    LowPass, HighPass, BandPass, Notch, Isolator, TalkFilter, DriveFilter, AutoWah, LfoFilter, Comb,
    // Modulation
    Flanger, JetFlanger, Phaser, Chorus, Tremolo, AutoPan, RingMod, Vibrato, Slicer,
    // Delay (tails)
    TempoDelay, PingPong, TapeEcho, MultiTap, DubEcho, FilterDelay,
    // Reverb (tails)
    Hall, Room, Shimmer, GatedVerb, DelayVerb,
    // Pitch
    PitchShift, Harmonizer, Doubler, PitchDelay, Vocoder,
    // Grain / Loop (captures the input when the pad is touched)
    BeatRepeat, ReverseLoop, GrainShift, TapeStop, Scratch, Freeze,
    // Lo-Fi
    BitCrush, Decimator, Overdrive, Fuzz, Radio, Vinyl,
    // Synth (plays on top of the input while the pad is touched)
    LeadSynth, BassSynth, Drone, Siren, NoiseRiser, DrumRoll, BeatLoop,
    Count
};

enum class Category { Filter, Modulation, Delay, Reverb, Pitch, GrainLoop, LoFi, Synth, Count };

struct Program
{
    const char* name;
    Category category;
    Algo algo;
    const char* xLabel;
    const char* yLabel;
};

const Program* programs (int& count);
const char* categoryName (Category c);
int firstProgramInCategory (Category c);

// How a program reacts when the finger leaves the pad (and Hold is off).
enum class ReleaseKind { Bypass, Tail };
ReleaseKind releaseKind (Algo a);

enum class Scale { MinorPentatonic, Major, Minor, Chromatic, Count };

struct Params
{
    int program = 0;
    float x = 0.5f, y = 0.5f;   // 0..1, y = 1 is the top of the pad
    bool touch = false;
    bool hold = false;
    float depth = 1.0f;         // FX depth (wet amount), 0..1
    float bpm = 120.0f;
    float inputDb = 0.0f, outputDb = 0.0f;
    int key = 0;                // 0 = C
    Scale scale = Scale::MinorPentatonic;
    bool motion = false;        // pad motion record/replay
    float sampleLevel = 0.8f;   // loop bank playback level
    int bankBeats = 4;          // length of a new loop recording in beats (1..16)
    bool resample = false;      // banks record the output instead of the input
    // transport (host)
    bool hostPlaying = false;
    double ppq = 0.0;           // host beat position at the start of the block
};

enum class BankState { Empty, Armed, Recording, Playing, Stopped };

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlock);
    void setParams (const Params& p);
    // stereo, in place. The optional sidechain is the vocoder's external carrier.
    void process (float* left, float* right, int numSamples, const float* sideL = nullptr, const float* sideR = nullptr);

    // Loop banks (A..D). Press: Empty -> record (starts on the next beat when the host plays),
    // Recording -> stop early and play, Playing <-> Stopped. clear() empties a bank.
    void bankPress (int bank);
    void bankClear (int bank);
    void bankToggleReverse (int bank);
    BankState bankState (int bank) const;
    bool bankReversed (int bank) const;
    float bankProgress (int bank) const;   // 0..1 play/record head position

    // What the UI should draw: the pad position actually in use (motion replay included)
    float displayX() const { return dispX.load(); }
    float displayY() const { return dispY.load(); }
    bool displayActive() const { return dispActive.load(); }
    bool motionPlaying() const { return motionReplay.load(); }
    float inputPeak() const { return inPeak.load(); }
    float outputPeak() const { return outPeak.load(); }

    double sampleRate() const { return sr; }

    struct Impl;

private:
    std::unique_ptr<Impl> impl;
    double sr = 48000.0;
    std::atomic<float> dispX { 0.5f }, dispY { 0.5f }, inPeak { 0 }, outPeak { 0 };
    std::atomic<bool> dispActive { false }, motionReplay { false };
};
} // namespace ts
