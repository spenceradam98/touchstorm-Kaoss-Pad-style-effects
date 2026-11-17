#include "Engine.h"
#include <cstring>

namespace ts
{
namespace
{
// Pad X -> one of N steps (left = first)
inline int stepOf (float x, int n) { return std::min (n - 1, (int) (clampf (x, 0.0f, 1.0f) * (float) n)); }

// Musical lengths in beats (quarter notes)
const float kDelayBeats[] = { 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f };          // 1/16 .. 1/2
const float kRepeatBeats[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f }; // 1 bar .. 1/64 (right = shorter)
const float kLfoBeats[] = { 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f };       // LFO period
const float kSliceBeats[] = { 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };              // 1/4 .. 1/64

const int kScales[(int) Scale::Count][12] = {
    { 0, 3, 5, 7, 10, -1 },
    { 0, 2, 4, 5, 7, 9, 11, -1 },
    { 0, 2, 3, 5, 7, 8, 10, -1 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
};
int scaleSize (Scale s)
{
    int n = 0;
    while (n < 12 && kScales[(int) s][n] >= 0) ++n;
    if (s == Scale::Chromatic) n = 12;
    return n;
}
// degree index (may exceed the scale size) -> semitones above the root
int degreeToSemis (Scale s, int degree)
{
    const int n = scaleSize (s);
    const int oct = degree / n, d = degree % n;
    return oct * 12 + kScales[(int) s][d];
}
inline float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

// Speed control with a dead zone at the centre: 0.5x .. 1x .. 2x
inline float speedFromY (float y)
{
    float t = (y - 0.5f) * 2.0f;
    if (std::fabs (t) < 0.12f) return 1.0f;
    t = (t > 0 ? t - 0.12f : t + 0.12f) / 0.88f;
    return std::pow (2.0f, t);
}

// Stereo capture buffer with absolute (64-bit) sample positions.
struct Capture
{
    std::vector<float> l, r;
    int64_t write = 0;
    int size = 1;
    void allocate (int n) { size = n; l.assign ((size_t) n, 0.0f); r.assign ((size_t) n, 0.0f); write = 0; }
    void clear() { std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f); }
    void push (float a, float b)
    {
        const size_t i = (size_t) (write % size);
        l[i] = a; r[i] = b; ++write;
    }
    void read (double pos, float& a, float& b) const
    {
        const double fl = std::floor (pos);
        const int64_t i0 = (int64_t) fl;
        const float f = (float) (pos - fl);
        auto idx = [this] (int64_t i) { int64_t m = i % size; if (m < 0) m += size; return (size_t) m; };
        const size_t a0 = idx (i0), a1 = idx (i0 + 1);
        a = l[a0] + (l[a1] - l[a0]) * f;
        b = r[a0] + (r[a1] - r[a0]) * f;
    }
};

struct Grain
{
    double pos = 0;
    float inc = 1, pan = 0;
    int len = 0, age = 0;
    bool on = false;
};

struct Bank
{
    std::vector<float> l, r;
    int len = 0, pos = 0, target = 0, beats = 4;
    double startBeat = 0;   // host beat where the recording started (bar-locked playback)
    std::atomic<int> state { (int) BankState::Empty };
    std::atomic<bool> reversed { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<int> pressCount { 0 }, clearCount { 0 }, revCount { 0 };
    int pressSeen = 0, clearSeen = 0, revSeen = 0;
};
} // namespace

struct Engine::Impl
{
    double sr = 48000;
    Params p;
    int maxBlock = 512;

    // controls
    Smoother sx, sy, activeEnv, synthEnv, depthS, inGain, outGain, delayTime, tapeTime;
    float progFade = 1.0f;
    int cur = 0;
    bool padActive = false, prevPadActive = false;
    float lastX = 0.5f, lastY = 0.5f;
    double beatPos = 0;
    float bpm = 120;

    // motion
    std::vector<float> motionX, motionY;
    int motionLen = 0, motionIdx = 0, ctrlCounter = 0;
    bool motionRecording = false, motionPlaying = false, prevTouch = false;

    // dsp state
    Svf f[2], f2[2], f3[2], isoLo[2], isoHi[2], tone[2];
    DelayLine dl[2], mod[2];
    OnePole lp1[2], hp1[2], lp2[2];
    float apZ[2][6] {};
    float phaserFb[2] {};
    Reverb rev;
    PitchShifter ps[2], ps2[2];
    Capture cap;
    bool frozen = false;
    int64_t freezeAt = 0;
    double loopPos = 0, readHead = 0, scratchPos = 0, scratchVel = 0;
    float tapeSpeed = 1.0f;
    Grain grains[6];
    int grainTimer = 0;
    float lfoPhase = 0, lfo2 = 0;
    Rng rng;
    EnvFollower env, gateEnv;
    int gateTimer = 0;
    Smoother gate;
    float shimmerPrev[2] {};
    BlepOsc osc[4];

    // vocoder: 16 analysis/synthesis band pairs
    static constexpr int kBands = 16;
    Svf vocMod[kBands], vocCar[kBands];
    EnvFollower vocEnv[kBands];
    EnvFollower scLevel;
    OnePole vocHp;

    // drum voices for Drum Roll / Beat Loop
    struct Drums
    {
        float kAmp = 0, kPitch = 0, kPh = 0, sAmp = 0, sTone = 0, sPh = 0, hAmp = 0;
        float kDec = 0, kPDec = 0, sDec = 0, stDec = 0, hDec = 0, tune = 1.0f;
        OnePole hp1, hp2, sHp;
    } drums;
    int64_t lastStep = -1;
    Svf synthF[2];
    float synthPhase = 0, holdL = 0, holdR = 0, holdCount = 0;
    float crackle = 0;
    float prevWetL = 0, prevWetR = 0;

    Bank banks[4];
    static constexpr float kBankSeconds = 16.0f;

    void prepare (double sampleRate, int block)
    {
        sr = sampleRate;
        maxBlock = block;
        sx.setTime (sr, 0.015f); sy.setTime (sr, 0.015f);
        activeEnv.setTime (sr, 0.006f);
        synthEnv.setTime (sr, 0.02f);
        depthS.setTime (sr, 0.02f); inGain.setTime (sr, 0.02f); outGain.setTime (sr, 0.02f);
        delayTime.setTime (sr, 0.06f); tapeTime.setTime (sr, 0.35f);
        gate.setTime (sr, 0.004f);
        for (int c = 0; c < 2; ++c)
        {
            dl[c].allocate ((int) (4.2 * sr));
            mod[c].allocate ((int) (0.12 * sr));
            ps[c].allocate ((int) (0.15 * sr));
            ps2[c].allocate ((int) (0.15 * sr));
        }
        rev.prepare (sr);
        scLevel.set (sr, 5.0f, 300.0f);
        vocHp.set (sr, 3000.0f);
        for (int i = 0; i < kBands; ++i) vocEnv[i].set (sr, 4.0f, 40.0f);
        auto dec = [this] (float seconds) { return std::exp (-6.9f / (seconds * (float) sr)); };   // -60 dB in `seconds`
        drums.kDec = dec (0.45f); drums.kPDec = dec (0.05f); drums.sDec = dec (0.22f); drums.stDec = dec (0.10f); drums.hDec = dec (0.06f);
        drums.hp1.set (sr, 7000.0f); drums.hp2.set (sr, 7000.0f); drums.sHp.set (sr, 1200.0f);
        cap.allocate ((int) (10.0 * sr));
        env.set (sr, 3.0f, 120.0f);
        gateEnv.set (sr, 1.0f, 60.0f);
        const int motionCap = (int) (16.0 * sr / 32.0) + 1;
        motionX.assign ((size_t) motionCap, 0.5f); motionY.assign ((size_t) motionCap, 0.5f);
        for (auto& b : banks)
        {
            b.l.assign ((size_t) (kBankSeconds * sr), 0.0f);
            b.r.assign ((size_t) (kBankSeconds * sr), 0.0f);
            b.len = 0; b.pos = 0;
            b.state.store ((int) BankState::Empty);
        }
        sx.snap (p.x); sy.snap (p.y);
        depthS.snap (p.depth); inGain.snap (dbToGain (p.inputDb)); outGain.snap (dbToGain (p.outputDb));
        activeEnv.snap (0); synthEnv.snap (0);
        cur = p.program;
        resetFx();
    }

    void resetFx()
    {
        for (int c = 0; c < 2; ++c)
        {
            f[c].reset(); f2[c].reset(); f3[c].reset(); isoLo[c].reset(); isoHi[c].reset(); tone[c].reset();
            dl[c].clear(); mod[c].clear(); ps[c].clear(); ps2[c].clear();
            lp1[c].reset(); hp1[c].reset(); lp2[c].reset(); synthF[c].reset();
            for (auto& z : apZ[c]) z = 0;
            phaserFb[c] = 0; shimmerPrev[c] = 0;
        }
        rev.clear();
        for (int i = 0; i < kBands; ++i) { vocMod[i].reset(); vocCar[i].reset(); vocEnv[i].env = 0; }
        drums.kAmp = drums.sAmp = drums.hAmp = 0;
        lastStep = -1;
        for (auto& g : grains) g.on = false;
        frozen = false;
        tapeSpeed = 1.0f;
        gateTimer = 0;
        delayTime.snap (0); tapeTime.snap (0);
    }

    float beatsToSamples (float beats) const { return beats * 60.0f / std::max (20.0f, bpm) * (float) sr; }

    // ---------------------------------------------------------------- banks
    void serviceBankEvents()
    {
        for (auto& b : banks)
        {
            const int cl = b.clearCount.load();
            if (cl != b.clearSeen) { b.clearSeen = cl; b.len = 0; b.pos = 0; b.state.store ((int) BankState::Empty); }
            const int rv = b.revCount.load();
            if (rv != b.revSeen) { b.revSeen = rv; b.reversed.store (! b.reversed.load()); }
            const int pr = b.pressCount.load();
            if (pr != b.pressSeen)
            {
                b.pressSeen = pr;
                switch ((BankState) b.state.load())
                {
                    case BankState::Empty:
                    case BankState::Armed:
                        if ((BankState) b.state.load() == BankState::Armed) { b.state.store ((int) BankState::Empty); break; }
                        b.target = std::min ((int) b.l.size(), (int) beatsToSamples ((float) std::max (1, p.bankBeats)));
                        b.len = 0;
                        b.beats = std::max (1, p.bankBeats);
                        b.startBeat = 0;
                        b.state.store ((int) (p.hostPlaying ? BankState::Armed : BankState::Recording));
                        break;
                    case BankState::Recording: finishRecording (b); break;
                    case BankState::Playing: b.state.store ((int) BankState::Stopped); break;
                    case BankState::Stopped: b.pos = 0; b.state.store ((int) BankState::Playing); break;
                }
            }
        }
    }

    void finishRecording (Bank& b)
    {
        if (b.len < 64) { b.len = 0; b.state.store ((int) BankState::Empty); return; }
        const int fade = std::min (b.len / 4, (int) (0.003 * sr));
        for (int i = 0; i < fade; ++i)
        {
            const float g = (float) i / (float) fade;
            b.l[(size_t) i] *= g; b.r[(size_t) i] *= g;
            b.l[(size_t) (b.len - 1 - i)] *= g; b.r[(size_t) (b.len - 1 - i)] *= g;
        }
        b.pos = 0;
        b.state.store ((int) BankState::Playing);
    }

    void bankPlayback (float& l, float& r)
    {
        const float lvl = p.sampleLevel;
        for (auto& b : banks)
        {
            if ((BankState) b.state.load() != BankState::Playing || b.len <= 0) continue;
            if (p.hostPlaying)
            {
                // locked to the host: the loop position follows the song position
                double ph = std::fmod (beatPos - b.startBeat, (double) b.beats);
                if (ph < 0) ph += b.beats;
                b.pos = std::min (b.len - 1, (int) (ph / b.beats * b.len));
            }
            const int i = b.reversed.load() ? b.len - 1 - b.pos : b.pos;
            l += b.l[(size_t) i] * lvl;
            r += b.r[(size_t) i] * lvl;
            if (++b.pos >= b.len) b.pos = 0;
        }
    }

    void bankRecord (float l, float r, bool beatTick)
    {
        for (auto& b : banks)
        {
            auto st = (BankState) b.state.load();
            if (st == BankState::Armed && beatTick)
            {
                // loops of a bar or more start on a downbeat (4/4), shorter ones on the next beat
                const double beat = std::floor (beatPos + 1e-9);
                const bool onBar = std::fmod (beat, 4.0) < 0.5;
                if (b.beats < 4 || onBar)
                {
                    st = BankState::Recording; b.state.store ((int) st);
                    b.startBeat = beat;
                }
            }
            if (st != BankState::Recording) continue;
            b.l[(size_t) b.len] = l; b.r[(size_t) b.len] = r;
            if (++b.len >= b.target) finishRecording (b);
        }
    }

    // ---------------------------------------------------------------- pad / motion (control rate)
    void controlTick()
    {
        const bool touch = p.touch;
        if (p.motion)
        {
            if (touch && ! prevTouch) { motionRecording = true; motionLen = 0; motionPlaying = false; }
            if (touch && motionRecording && motionLen < (int) motionX.size())
            {
                motionX[(size_t) motionLen] = p.x; motionY[(size_t) motionLen] = p.y; ++motionLen;
            }
            if (! touch && prevTouch)
            {
                motionRecording = false;
                motionPlaying = motionLen > 4;
                motionIdx = 0;
            }
        }
        else
        {
            motionRecording = motionPlaying = false;
        }
        prevTouch = touch;

        float x = p.x, y = p.y;
        if (touch) { lastX = x; lastY = y; }
        bool active = touch;
        if (! touch && motionPlaying)
        {
            x = motionX[(size_t) motionIdx]; y = motionY[(size_t) motionIdx];
            motionIdx = (motionIdx + 1) % motionLen;
            active = true;
        }
        else if (! touch && p.hold)
        {
            x = lastX; y = lastY; active = true;
        }
        else if (! touch)
        {
            x = lastX; y = lastY;
        }
        sx.target = x; sy.target = y;
        padActive = active;
    }

    // ---------------------------------------------------------------- grains
    void spawnGrain (int len, double start, float inc, float pan)
    {
        for (auto& g : grains)
            if (! g.on) { g.on = true; g.pos = start; g.inc = inc; g.len = std::max (16, len); g.age = 0; g.pan = pan; return; }
    }
    void grainsOut (float& l, float& r)
    {
        l = r = 0;
        for (auto& g : grains)
        {
            if (! g.on) continue;
            float a, b;
            cap.read (g.pos, a, b);
            const float w = std::sin (kPi * (float) g.age / (float) g.len);
            const float win = w * w;
            l += a * win * (1.0f - std::max (0.0f, g.pan));
            r += b * win * (1.0f + std::min (0.0f, g.pan));
            g.pos += g.inc;
            if (++g.age >= g.len) g.on = false;
        }
    }

    // ---------------------------------------------------------------- one sample of the current program
    // dry is the input (after gain + bank playback). Returns wet in wl/wr.
    void runProgram (Algo a, float x, float y, float dl0, float dr0, float act, float& wl, float& wr)
    {
        const float in[2] = { dl0, dr0 };
        float out[2] = { dl0, dr0 };
        const float fs = (float) sr;
        auto lfoSync = [this] (float beats) { const double ph = beatPos / beats; return (float) (ph - std::floor (ph)); };

        switch (a)
        {
            // ---------------- Filter
            case Algo::LowPass:
            case Algo::HighPass:
            case Algo::BandPass:
            case Algo::Notch:
            {
                float hz, q;
                if (a == Algo::LowPass) { hz = expMap (x, 40.0f, 18000.0f); q = lerpf (0.6f, 14.0f, y * y); }
                else if (a == Algo::HighPass) { hz = expMap (x, 20.0f, 12000.0f); q = lerpf (0.6f, 14.0f, y * y); }
                else if (a == Algo::BandPass) { hz = expMap (x, 80.0f, 8000.0f); q = lerpf (0.7f, 16.0f, y); }
                else { hz = expMap (x, 80.0f, 10000.0f); q = lerpf (4.0f, 0.3f, y); }
                f[0].set (sr, hz, q); f[1].copyCoeffs (f[0]);
                for (int c = 0; c < 2; ++c)
                {
                    f[c].tick (in[c]);
                    if (a == Algo::LowPass) out[c] = f[c].lp;
                    else if (a == Algo::HighPass) out[c] = f[c].hp;
                    else if (a == Algo::BandPass) out[c] = f[c].bp * std::sqrt (q) * 0.9f;
                    else out[c] = f[c].notch();
                    out[c] = softClip (out[c] * 0.8f) * 1.25f;
                }
                break;
            }
            case Algo::Isolator:
            {
                isoLo[0].set (sr, 250.0f, 0.707f); isoLo[1].copyCoeffs (isoLo[0]);
                isoHi[0].set (sr, 2500.0f, 0.707f); isoHi[1].copyCoeffs (isoHi[0]);
                const float b = x * 2.0f;
                float g[3];
                for (int i = 0; i < 3; ++i) g[i] = 1.0f - y * std::max (0.0f, 1.0f - std::fabs (b - (float) i));
                for (int c = 0; c < 2; ++c)
                {
                    isoLo[c].tick (in[c]); isoHi[c].tick (in[c]);
                    const float lo = isoLo[c].lp, hi = isoHi[c].hp, mid = in[c] - lo - hi;
                    out[c] = lo * g[0] + mid * g[1] + hi * g[2];
                }
                break;
            }
            case Algo::TalkFilter:
            {
                static const float F[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
                const float pos = x * 4.0f;
                const int i0 = std::min (3, (int) pos);
                const float t = pos - (float) i0;
                const float q = lerpf (5.0f, 18.0f, y);
                f[0].set (sr, lerpf (F[i0][0], F[i0 + 1][0], t), q);
                f2[0].set (sr, lerpf (F[i0][1], F[i0 + 1][1], t), q);
                f3[0].set (sr, lerpf (F[i0][2], F[i0 + 1][2], t), q);
                f[1].copyCoeffs (f[0]); f2[1].copyCoeffs (f2[0]); f3[1].copyCoeffs (f3[0]);
                for (int c = 0; c < 2; ++c)
                {
                    const float s = softClip (in[c] * 1.5f);
                    f[c].tick (s); f2[c].tick (s); f3[c].tick (s);
                    out[c] = (f[c].bp + 0.6f * f2[c].bp + 0.35f * f3[c].bp) * std::sqrt (q) * 0.6f;
                }
                break;
            }
            case Algo::DriveFilter:
            {
                const float drive = lerpf (1.0f, 18.0f, y * y);
                f[0].set (sr, expMap (x, 60.0f, 16000.0f), 3.5f); f[1].copyCoeffs (f[0]);
                for (int c = 0; c < 2; ++c)
                {
                    f[c].tick (softClip (in[c] * drive));
                    out[c] = softClip (f[c].lp) * 0.8f;
                }
                break;
            }
            case Algo::AutoWah:
            {
                const float e = env.tick (0.5f * (in[0] + in[1]));
                const float hz = 200.0f * std::pow (2.0f, clampf (e * lerpf (4.0f, 40.0f, x), 0.0f, 5.5f));
                f[0].set (sr, hz, lerpf (1.5f, 10.0f, y)); f[1].copyCoeffs (f[0]);
                for (int c = 0; c < 2; ++c) { f[c].tick (in[c]); out[c] = f[c].lp * 0.6f + f[c].bp * 0.8f; }
                break;
            }
            case Algo::LfoFilter:
            {
                const float ph = lfoSync (kLfoBeats[stepOf (x, 7)]);
                const float l = std::sin (kTwoPi * ph);
                const float hz = 900.0f * std::pow (2.0f, l * y * 3.5f);
                f[0].set (sr, hz, 3.0f); f[1].copyCoeffs (f[0]);
                for (int c = 0; c < 2; ++c) { f[c].tick (in[c]); out[c] = f[c].lp; }
                break;
            }
            case Algo::Comb:
            {
                const float hz = expMap (x, 40.0f, 2000.0f);
                const float d = fs / hz;
                const float fb = y * 0.97f;
                for (int c = 0; c < 2; ++c)
                {
                    const float o = mod[c].read (d - 1.0f);
                    mod[c].push (in[c] + o * fb);
                    out[c] = (in[c] + o) * (1.0f - fb * 0.6f);
                }
                break;
            }

            // ---------------- Modulation
            case Algo::Flanger:
            case Algo::JetFlanger:
            {
                float d0, d1;
                if (a == Algo::Flanger)
                {
                    const float ph = lfoSync (kLfoBeats[stepOf (x, 7)]);
                    const float tri = 1.0f - std::fabs (2.0f * ph - 1.0f);
                    d0 = (0.4f + 5.5f * tri) * 0.001f * fs;
                    const float ph2 = ph + 0.25f - std::floor (ph + 0.25f);
                    d1 = (0.4f + 5.5f * (1.0f - std::fabs (2.0f * ph2 - 1.0f))) * 0.001f * fs;
                }
                else
                {
                    lfo2 += 0.15f / fs; if (lfo2 >= 1.0f) lfo2 -= 1.0f;
                    d0 = expMap (x, 0.25f, 9.0f) * 0.001f * fs * (1.0f + 0.03f * std::sin (kTwoPi * lfo2));
                    d1 = d0 * 1.01f;
                }
                const float fb = y * 0.92f;
                const float d[2] = { d0, d1 };
                for (int c = 0; c < 2; ++c)
                {
                    const float o = mod[c].read (d[c]);
                    mod[c].push (softClip (in[c] + o * fb));
                    out[c] = 0.6f * (in[c] + o);
                }
                break;
            }
            case Algo::Phaser:
            {
                const float rate = expMap (x, 0.05f, 8.0f);
                lfoPhase += rate / fs; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
                const float fb = y * 0.85f;
                for (int c = 0; c < 2; ++c)
                {
                    const float ph = lfoPhase + (c ? 0.25f : 0.0f);
                    const float hz = 200.0f * std::pow (20.0f, 0.5f + 0.5f * std::sin (kTwoPi * ph));
                    const float t = std::tan (kPi * std::min (hz, fs * 0.45f) / fs);
                    const float coef = (t - 1.0f) / (t + 1.0f);
                    float s = in[c] + phaserFb[c] * fb;
                    for (int k = 0; k < 6; ++k)
                    {
                        const float v = coef * s + apZ[c][k];
                        apZ[c][k] = s - coef * v;
                        s = v;
                    }
                    phaserFb[c] = s;
                    out[c] = 0.6f * (in[c] + s);
                }
                break;
            }
            case Algo::Chorus:
            {
                const float rate = expMap (x, 0.1f, 6.0f);
                lfoPhase += rate / fs; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
                const float depthMs = 0.3f + y * 6.0f;
                for (int c = 0; c < 2; ++c)
                {
                    mod[c].push (in[c]);
                    float acc = 0;
                    for (int v = 0; v < 3; ++v)
                    {
                        const float ph = lfoPhase + (float) v / 3.0f + (c ? 0.5f : 0.0f);
                        const float dms = 12.0f + 4.0f * (float) v + depthMs * std::sin (kTwoPi * ph);
                        acc += mod[c].read (dms * 0.001f * fs);
                    }
                    out[c] = 0.6f * in[c] + 0.45f * acc;
                }
                break;
            }
            case Algo::Tremolo:
            {
                const float ph = lfoSync (kLfoBeats[stepOf (x, 7)] * 0.5f);
                const float g = 1.0f - y * (0.5f + 0.5f * std::cos (kTwoPi * ph));
                out[0] = in[0] * g; out[1] = in[1] * g;
                break;
            }
            case Algo::AutoPan:
            {
                const float ph = lfoSync (kLfoBeats[stepOf (x, 7)] * 0.5f);
                const float pan = std::sin (kTwoPi * ph) * y;   // -1..1
                const float gl = std::sqrt (0.5f * (1.0f - pan)) * 1.414f, gr = std::sqrt (0.5f * (1.0f + pan)) * 1.414f;
                const float m = 0.5f * (in[0] + in[1]);
                out[0] = lerpf (in[0], m * gl, y); out[1] = lerpf (in[1], m * gr, y);
                break;
            }
            case Algo::RingMod:
            {
                const float hz = expMap (x, 20.0f, 3000.0f);
                lfoPhase += hz / fs; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
                const float m = std::sin (kTwoPi * lfoPhase);
                for (int c = 0; c < 2; ++c) out[c] = lerpf (in[c], in[c] * m * 1.4f, 0.3f + 0.7f * y);
                break;
            }
            case Algo::Vibrato:
            {
                const float rate = expMap (x, 0.5f, 12.0f);
                lfoPhase += rate / fs; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
                const float dms = 5.0f + y * 4.5f * std::sin (kTwoPi * lfoPhase);
                for (int c = 0; c < 2; ++c) { mod[c].push (in[c]); out[c] = mod[c].read (dms * 0.001f * fs); }
                break;
            }
            case Algo::Slicer:
            {
                const float ph = lfoSync (kSliceBeats[stepOf (x, 5)]);
                gate.target = ph < lerpf (0.1f, 0.9f, 1.0f - y) ? 1.0f : 0.0f;
                const float g = gate.next();
                out[0] = in[0] * g; out[1] = in[1] * g;
                break;
            }

            // ---------------- Delay (input already gated by the pad)
            case Algo::TempoDelay:
            case Algo::PingPong:
            case Algo::DubEcho:
            case Algo::FilterDelay:
            {
                delayTime.target = beatsToSamples (kDelayBeats[stepOf (x, 6)]);
                if (delayTime.value <= 1.0f) delayTime.snap (delayTime.target);
                const float d = std::min (delayTime.next(), (float) dl[0].size() - 8.0f);
                float fb = a == Algo::FilterDelay ? 0.62f : (a == Algo::DubEcho ? y * 1.08f : y * 0.95f);
                if (a == Algo::DubEcho) { hp1[0].set (sr, 220.0f); hp1[1].set (sr, 220.0f); lp1[0].set (sr, 2600.0f); lp1[1].set (sr, 2600.0f); }
                if (a == Algo::FilterDelay)
                {
                    const float lpHz = y < 0.5f ? expMap (y * 2.0f, 300.0f, 18000.0f) : 18000.0f;
                    const float hpHz = y > 0.5f ? expMap ((y - 0.5f) * 2.0f, 20.0f, 3000.0f) : 20.0f;
                    lp1[0].set (sr, lpHz); lp1[1].set (sr, lpHz); hp1[0].set (sr, hpHz); hp1[1].set (sr, hpHz);
                }
                const float o0 = dl[0].read (d), o1 = dl[1].read (d);
                if (a == Algo::PingPong)
                {
                    const float m = 0.5f * (in[0] + in[1]);
                    dl[0].push (m + o1 * fb);
                    dl[1].push (o0 * fb);
                }
                else
                {
                    const float o[2] = { o0, o1 };
                    for (int c = 0; c < 2; ++c)
                    {
                        float s = o[c] * fb;
                        if (a == Algo::DubEcho || a == Algo::FilterDelay) s = lp1[c].lpf (hp1[c].hpf (s));
                        if (a == Algo::DubEcho) s = softClip (s);
                        dl[c].push (in[c] + s);
                    }
                }
                out[0] = o0; out[1] = o1;
                break;
            }
            case Algo::TapeEcho:
            {
                tapeTime.target = expMap (x, 0.04f, 1.0f) * fs;
                if (tapeTime.value <= 1.0f) tapeTime.snap (tapeTime.target);
                lfo2 += 0.7f / fs; if (lfo2 >= 1.0f) lfo2 -= 1.0f;
                const float d = tapeTime.next() * (1.0f + 0.0025f * std::sin (kTwoPi * lfo2));
                const float fb = y * 1.05f;
                lp1[0].set (sr, 3200.0f); lp1[1].set (sr, 3200.0f); hp1[0].set (sr, 90.0f); hp1[1].set (sr, 90.0f);
                for (int c = 0; c < 2; ++c)
                {
                    const float o = lp1[c].lpf (hp1[c].hpf (dl[c].read (d + (c ? 0.003f * fs : 0.0f))));
                    dl[c].push (softClip (in[c] + o * fb));
                    out[c] = o;
                }
                break;
            }
            case Algo::MultiTap:
            {
                const float spread = beatsToSamples (lerpf (0.25f, 2.0f, x));
                const float fb = y * 0.85f;
                static const float taps[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
                static const float pans[4] = { -0.8f, 0.6f, -0.3f, 0.9f };
                const float m = 0.5f * (in[0] + in[1]);
                const float last = dl[0].read (std::min (spread, (float) dl[0].size() - 8.0f));
                dl[0].push (m + last * fb);
                out[0] = out[1] = 0;
                for (int t = 0; t < 4; ++t)
                {
                    const float s = dl[0].read (std::min (spread * taps[t], (float) dl[0].size() - 8.0f)) * (1.0f - 0.15f * (float) t);
                    out[0] += s * (1.0f - pans[t]) * 0.5f;
                    out[1] += s * (1.0f + pans[t]) * 0.5f;
                }
                break;
            }

            // ---------------- Reverb
            case Algo::Hall:
            case Algo::Room:
            {
                if (a == Algo::Hall) rev.set (expMap (x, 0.8f, 14.0f), 1.6f);
                else rev.set (lerpf (0.25f, 1.6f, x), lerpf (0.35f, 0.9f, x));
                rev.setDamping (expMap (y, 1500.0f, 15000.0f));
                rev.tick (in[0], in[1], out[0], out[1]);
                break;
            }
            case Algo::Shimmer:
            {
                rev.set (expMap (x, 2.0f, 16.0f), 1.8f);
                rev.setDamping (9000.0f);
                const float sh0 = ps[0].tick (shimmerPrev[0], 2.0f, 0.08f * fs);
                const float sh1 = ps[1].tick (shimmerPrev[1], 2.0f, 0.09f * fs);
                const float amt = y * 0.55f;
                rev.tick (in[0] + softClip (sh0 * amt), in[1] + softClip (sh1 * amt), out[0], out[1]);
                hp1[0].set (sr, 200.0f); hp1[1].set (sr, 200.0f);
                shimmerPrev[0] = hp1[0].hpf (out[0]); shimmerPrev[1] = hp1[1].hpf (out[1]);
                break;
            }
            case Algo::GatedVerb:
            {
                rev.set (4.0f, lerpf (0.5f, 1.8f, y));
                rev.setDamping (8000.0f);
                const float e = gateEnv.tick (0.5f * (in[0] + in[1]));
                if (e > 0.05f) gateTimer = (int) (expMap (x, 0.05f, 0.8f) * fs);
                gate.target = gateTimer > 0 ? 1.0f : 0.0f;
                if (gateTimer > 0) --gateTimer;
                rev.tick (in[0], in[1], out[0], out[1]);
                const float g = gate.next();
                out[0] *= g * 1.4f; out[1] *= g * 1.4f;
                break;
            }
            case Algo::DelayVerb:
            {
                delayTime.target = beatsToSamples (kDelayBeats[stepOf (x, 6)]);
                if (delayTime.value <= 1.0f) delayTime.snap (delayTime.target);
                const float d = std::min (delayTime.next(), (float) dl[0].size() - 8.0f);
                const float o0 = dl[0].read (d), o1 = dl[1].read (d);
                dl[0].push (in[0] + o1 * 0.5f);   // cross-fed: wide repeats
                dl[1].push (in[1] + o0 * 0.5f);
                rev.set (3.5f, 1.3f); rev.setDamping (7000.0f);
                float rl, rr;
                rev.tick (in[0] * 0.5f + o0, in[1] * 0.5f + o1, rl, rr);
                out[0] = o0 + rl * y * 1.2f; out[1] = o1 + rr * y * 1.2f;
                break;
            }

            // ---------------- Pitch
            case Algo::PitchShift:
            {
                const float semis = std::round (lerpf (-12.0f, 12.0f, x));
                const float ratio = std::pow (2.0f, semis / 12.0f);
                const float win = lerpf (0.02f, 0.12f, y) * fs;
                for (int c = 0; c < 2; ++c) out[c] = ps[c].tick (in[c], ratio, win);
                break;
            }
            case Algo::Harmonizer:
            {
                static const float iv[8] = { -12, -7, -5, 3, 4, 5, 7, 12 };
                const float ratio = std::pow (2.0f, iv[stepOf (x, 8)] / 12.0f);
                for (int c = 0; c < 2; ++c) out[c] = in[c] * (1.0f - 0.4f * y) + ps[c].tick (in[c], ratio, 0.06f * fs) * (0.2f + 0.8f * y);
                break;
            }
            case Algo::Doubler:
            {
                const float cents = x * 30.0f;
                const float spreadMs = lerpf (6.0f, 40.0f, y);
                const float up = std::pow (2.0f, cents / 1200.0f), dn = 1.0f / up;
                const float m = 0.5f * (in[0] + in[1]);
                mod[0].push (m);
                const float dly = mod[0].read (spreadMs * 0.001f * fs);
                out[0] = 0.7f * in[0] + 0.6f * ps[0].tick (m, up, 0.04f * fs);
                out[1] = 0.7f * in[1] + 0.6f * ps[1].tick (dly, dn, 0.045f * fs);
                break;
            }
            case Algo::PitchDelay:
            {
                const float semis = std::round (lerpf (-12.0f, 12.0f, x));
                const float ratio = std::pow (2.0f, semis / 12.0f);
                const float d = beatsToSamples (0.5f);
                const float fb = y * 0.9f;
                for (int c = 0; c < 2; ++c)
                {
                    const float o = dl[c].read (std::min (d + (c ? 0.011f * fs : 0.0f), (float) dl[c].size() - 8.0f));
                    dl[c].push (in[c] + softClip (ps[c].tick (o, ratio, 0.06f * fs) * fb));
                    out[c] = o;
                }
                break;
            }

            // ---------------- Grain / Loop: the capture buffer is frozen while the pad is active
            case Algo::BeatRepeat:
            case Algo::ReverseLoop:
            {
                const double len = std::max (64.0, (double) beatsToSamples (kRepeatBeats[stepOf (x, 7)]));
                if (! frozen) { out[0] = in[0]; out[1] = in[1]; break; }
                if (loopPos >= len) loopPos = std::fmod (loopPos, len);
                const double rp = a == Algo::ReverseLoop ? len - 1.0 - loopPos : loopPos;
                const double start = (double) freezeAt - len;
                cap.read (start + rp, out[0], out[1]);
                const double fadeLen = std::min (len * 0.25, 0.002 * sr);
                const float g = (float) std::min (1.0, std::min (loopPos / fadeLen, (len - loopPos) / fadeLen));
                out[0] *= g; out[1] *= g;
                loopPos += speedFromY (y);
                break;
            }
            case Algo::GrainShift:
            case Algo::Freeze:
            {
                const int len = (int) (expMap (x, 0.012f, 0.3f) * fs);
                if (--grainTimer <= 0)
                {
                    grainTimer = std::max (8, len / 3);
                    if (a == Algo::GrainShift)
                    {
                        const double jitter = (double) (rng.uni() * y * 0.4f * fs);
                        spawnGrain (len, (double) cap.write - len - 64 - jitter, 1.0f, rng.next() * y * 0.8f);
                    }
                    else if (frozen)
                    {
                        const float ratio = std::pow (2.0f, std::round (lerpf (-12.0f, 12.0f, y)) / 12.0f);
                        const double region = 0.6 * sr;
                        const double span = std::max (1.0, region - len * ratio);
                        spawnGrain (len, (double) freezeAt - region + rng.uni() * span, ratio, rng.next() * 0.6f);
                    }
                }
                grainsOut (out[0], out[1]);
                out[0] *= 0.8f; out[1] *= 0.8f;
                if (a == Algo::Freeze && ! frozen) { out[0] = in[0]; out[1] = in[1]; }
                break;
            }
            case Algo::TapeStop:
            {
                // read head runs at tapeSpeed; when the pad is let go it snaps back to real time
                if (act > 0.5f)
                {
                    const float stop = expMap (x, 0.08f, 3.0f);
                    tapeSpeed = std::max (0.0f, tapeSpeed - 1.0f / (stop * fs));
                }
                else
                {
                    tapeSpeed = 1.0f;
                    readHead = (double) cap.write - 1.0;
                }
                readHead += tapeSpeed;
                readHead = std::min (readHead, (double) cap.write - 1.0);
                cap.read (readHead, out[0], out[1]);
                lp2[0].set (sr, expMap (lerpf (1.0f, tapeSpeed, y), 200.0f, 20000.0f));
                lp2[1].a = lp2[0].a;
                const float g = std::min (1.0f, tapeSpeed * 4.0f);
                for (int c = 0; c < 2; ++c) out[c] = lp2[c].lpf (out[c]) * g;
                break;
            }
            case Algo::Scratch:
            {
                if (! frozen) { out[0] = in[0]; out[1] = in[1]; break; }
                const double region = beatsToSamples (2.0f);
                const double target = x * region;
                // second-order follower: the hand drags a record with some inertia
                scratchVel += (target - scratchPos) * 0.0008 - scratchVel * 0.03;
                scratchPos += scratchVel;
                scratchPos = std::max (0.0, std::min (region - 2.0, scratchPos));
                cap.read ((double) freezeAt - region + scratchPos, out[0], out[1]);
                const float speed = (float) std::fabs (scratchVel);
                lp2[0].set (sr, expMap (y, 800.0f, 18000.0f)); lp2[1].a = lp2[0].a;
                const float g = std::min (1.0f, speed * 3.0f);
                for (int c = 0; c < 2; ++c) out[c] = lp2[c].lpf (out[c]) * g;
                break;
            }

            // ---------------- Lo-Fi
            case Algo::BitCrush:
            case Algo::Decimator:
            {
                float hold;
                if (a == Algo::BitCrush) hold = 1.0f + y * y * 40.0f;
                else hold = fs / expMap (1.0f - x, 400.0f, 22000.0f);
                if (++holdCount >= hold) { holdCount -= hold; holdL = in[0]; holdR = in[1]; }
                out[0] = holdL; out[1] = holdR;
                if (a == Algo::BitCrush)
                {
                    const float bits = lerpf (16.0f, 1.5f, x);
                    const float steps = std::pow (2.0f, bits - 1.0f);
                    for (int c = 0; c < 2; ++c) out[c] = std::round (clampf (out[c], -1.0f, 1.0f) * steps) / steps;
                }
                else
                {
                    f[0].set (sr, expMap (y, 800.0f, 18000.0f), 0.707f); f[1].copyCoeffs (f[0]);
                    for (int c = 0; c < 2; ++c) { f[c].tick (out[c]); out[c] = f[c].lp; }
                }
                break;
            }
            case Algo::Overdrive:
            case Algo::Fuzz:
            {
                const float drive = a == Algo::Overdrive ? lerpf (1.0f, 30.0f, x * x) : lerpf (8.0f, 120.0f, x);
                f[0].set (sr, a == Algo::Overdrive ? expMap (y, 800.0f, 12000.0f) : expMap (y, 300.0f, 9000.0f), 0.8f);
                f[1].copyCoeffs (f[0]);
                for (int c = 0; c < 2; ++c)
                {
                    float s = in[c] * drive;
                    if (a == Algo::Overdrive) s = softClip (s + 0.2f) - softClip (0.2f);
                    else s = clampf (s, -1.0f, 1.0f) * 0.7f + std::fabs (clampf (s, -1.0f, 1.0f)) * 0.3f - 0.15f;
                    f[c].tick (s);
                    out[c] = f[c].lp * (a == Algo::Overdrive ? 0.7f : 0.45f);
                }
                break;
            }
            case Algo::Radio:
            {
                f[0].set (sr, expMap (x, 300.0f, 3000.0f), 2.0f); f[1].copyCoeffs (f[0]);
                const float drive = lerpf (1.0f, 12.0f, y);
                const float hiss = rng.next() * 0.004f;
                const float m = 0.5f * (in[0] + in[1]);
                f[0].tick (softClip (m * drive) + hiss);
                out[0] = out[1] = f[0].bp * 1.6f;
                break;
            }
            case Algo::Vinyl:
            {
                // crackle: sparse random impulses through a short decay
                const float rate = lerpf (2.0f, 60.0f, x);
                if (rng.uni() < rate / fs) crackle = rng.next() * lerpf (0.1f, 0.5f, x);
                crackle *= 0.85f;
                const float hiss = rng.next() * 0.003f * x;
                lfo2 += 0.55f / fs; if (lfo2 >= 1.0f) lfo2 -= 1.0f;   // 33 rpm wow
                const float dms = 3.0f + 0.25f * std::sin (kTwoPi * lfo2);
                lp1[0].set (sr, expMap (y, 2000.0f, 16000.0f)); lp1[1].a = lp1[0].a;
                hp1[0].set (sr, 80.0f); hp1[1].a = hp1[0].a;
                for (int c = 0; c < 2; ++c)
                {
                    mod[c].push (in[c]);
                    out[c] = lp1[c].lpf (hp1[c].hpf (mod[c].read (dms * 0.001f * fs))) + crackle + hiss;
                }
                break;
            }

            // ---------------- Synth (output only; the engine adds it on top of the input)
            case Algo::LeadSynth:
            case Algo::BassSynth:
            case Algo::Drone:
            {
                const int n = scaleSize (p.scale);
                float note;
                if (a == Algo::Drone)
                    note = 48.0f + (float) p.key + (float) degreeToSemis (p.scale, stepOf (x, n + 1));
                else
                {
                    const int deg = stepOf (x, 2 * n + 1);
                    note = (a == Algo::LeadSynth ? 60.0f : 36.0f) + (float) p.key + (float) degreeToSemis (p.scale, deg);
                }
                const float hz = midiToHz (note);
                float s0, s1;
                if (a == Algo::LeadSynth)
                {
                    const float v = osc[0].saw (hz / fs) + osc[1].saw (hz * 1.006f / fs) + 0.5f * osc[2].square (hz * 0.5f / fs);
                    s0 = s1 = v * 0.3f;
                    f[0].set (sr, expMap (y, 300.0f, 12000.0f), 2.0f);
                }
                else if (a == Algo::BassSynth)
                {
                    const float v = osc[0].saw (hz / fs) + 0.8f * osc[1].square (hz * 0.5f / fs);
                    s0 = s1 = v * 0.4f;
                    f[0].set (sr, 300.0f + 2500.0f * synthEnv.value * synthEnv.value, lerpf (0.7f, 12.0f, y));
                }
                else
                {
                    const float v0 = osc[0].saw (hz / fs) + osc[1].saw (hz * 1.4983f / fs);
                    const float v1 = osc[2].saw (hz * 1.0035f / fs) + osc[3].saw (hz * 2.002f / fs);
                    s0 = v0 * 0.22f; s1 = v1 * 0.22f;
                    f[0].set (sr, expMap (y, 150.0f, 8000.0f), 1.2f);
                }
                f[1].copyCoeffs (f[0]);
                f[0].tick (s0); f[1].tick (s1);
                out[0] = f[0].lp; out[1] = f[1].lp;
                break;
            }
            case Algo::Siren:
            {
                const float rate = expMap (x, 0.2f, 12.0f);
                lfoPhase += rate / fs; if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
                const float hz = expMap (y, 150.0f, 1600.0f) * std::pow (2.0f, std::sin (kTwoPi * lfoPhase));
                synthPhase += hz / fs; if (synthPhase >= 1.0f) synthPhase -= 1.0f;
                const float s = std::sin (kTwoPi * synthPhase) * 0.35f;
                out[0] = out[1] = s;
                break;
            }
            case Algo::NoiseRiser:
            {
                f[0].set (sr, expMap (x, 100.0f, 14000.0f), lerpf (0.7f, 12.0f, y)); f[1].copyCoeffs (f[0]);
                f[0].tick (rng.next() * 0.5f); f[1].tick (rng.next() * 0.5f);
                out[0] = f[0].bp; out[1] = f[1].bp;
                break;
            }
            case Algo::Vocoder:
            {
                // carrier: the sidechain when something is playing into it, otherwise a built-in
                // saw chord on the pad note (X, quantised to the key/scale)
                const bool external = scLevel.tick (scSample) > 0.002f;
                float car;
                if (external) car = scSample;
                else
                {
                    const int n = scaleSize (p.scale);
                    const float note = 48.0f + (float) p.key + (float) degreeToSemis (p.scale, stepOf (x, 2 * n + 1));
                    const float hz = midiToHz (note);
                    car = 0.35f * (osc[0].saw (hz / fs) + osc[1].saw (hz * 1.005f / fs) + 0.6f * osc[2].saw (hz * 2.0f / fs));
                }
                car += rng.next() * 0.02f;   // a little noise so consonants come through
                const float mod = 0.5f * (in[0] + in[1]);
                const float shift = std::pow (2.0f, (y - 0.5f) * 2.0f);   // formant shift 0.5x .. 2x
                float acc = 0;
                for (int b = 0; b < kBands; ++b)
                {
                    const float hz = 100.0f * std::pow (80.0f, (float) b / (float) (kBands - 1));   // 100 Hz .. 8 kHz
                    vocMod[b].set (sr, hz, 6.0f);
                    vocCar[b].set (sr, hz * shift, 6.0f);
                    vocMod[b].tick (mod);
                    vocCar[b].tick (car);
                    acc += vocCar[b].bp * vocEnv[b].tick (vocMod[b].bp);
                }
                const float sib = vocHp.hpf (mod) * 0.6f;   // pass the input's sibilance
                out[0] = out[1] = softClip (acc * 1.2f + sib);
                break;
            }
            case Algo::DrumRoll:
            case Algo::BeatLoop:
            {
                auto& d = drums;
                if (act > 0.5f)
                {
                    const float stepBeats = a == Algo::DrumRoll ? kSliceBeats[stepOf (x, 5)] : 0.25f;
                    const int64_t step = (int64_t) std::floor (beatPos / stepBeats);
                    if (step != lastStep)
                    {
                        const bool first = lastStep < 0;
                        lastStep = step;
                        bool k = false, sn = false, h = false;
                        if (a == Algo::DrumRoll)
                        {
                            const int zone = stepOf (y, 3);   // bottom kick, middle snare, top hat
                            k = zone == 0; sn = zone == 1; h = zone == 2;
                            d.tune = 0.8f + 0.5f * (y * 3.0f - (float) zone);
                        }
                        else
                        {
                            // 16-step patterns: kick / snare / hat
                            static const char* pats[8][3] = {
                                { "x...x...x...x...", "....x.......x...", "..x...x...x...x." },   // four on the floor
                                { "x.........x.....", "....x.......x...", "x.x.x.x.x.x.x.x." },   // break
                                { "x......x..x.....", "....x.......x...", "x.x.x.x.x.x.x.x." },   // hip hop
                                { "x..........x....", "........x.......", "x.x.x.x.x.x.x.x." },   // half time
                                { "x.........x.....", "....x..x....x...", "xxxxxxxxxxxxxxxx" },   // drum and bass
                                { "x...x...x...x...", "................", "xxxxxxxxxxxxxxxx" },   // techno
                                { "x.....x...x.....", "....x.......x...", "..x...x...x...x." },   // electro
                                { "x.......x.......", "....x.......x...", "x..xx..xx..xx..x" },   // shuffle
                            };
                            const int pat = stepOf (x, 8), st = (int) (((step % 16) + 16) % 16);
                            k = pats[pat][0][st] == 'x'; sn = pats[pat][1][st] == 'x'; h = pats[pat][2][st] == 'x';
                            if (first && ! (k || sn || h)) k = true;   // always answer the touch
                            d.tune = 0.75f + 0.6f * y;
                        }
                        if (k) { d.kAmp = 1.0f; d.kPitch = 1.0f; d.kPh = 0; }
                        if (sn) { d.sAmp = 0.8f; d.sTone = 0.7f; }
                        if (h) d.hAmp = 0.5f;
                    }
                }
                else lastStep = -1;
                const float kHz = (48.0f + 160.0f * d.kPitch) * d.tune;
                d.kPh += kHz / fs; if (d.kPh >= 1.0f) d.kPh -= 1.0f;
                const float kick = std::sin (kTwoPi * d.kPh) * d.kAmp;
                d.sPh += 190.0f * d.tune / fs; if (d.sPh >= 1.0f) d.sPh -= 1.0f;
                const float noise = rng.next();
                const float snare = d.sHp.hpf (noise) * d.sAmp * 0.7f + std::sin (kTwoPi * d.sPh) * d.sTone * 0.5f;
                const float hat = d.hp2.hpf (d.hp1.hpf (noise)) * d.hAmp * 1.2f;
                d.kAmp *= d.kDec; d.kPitch *= d.kPDec; d.sAmp *= d.sDec; d.sTone *= d.stDec; d.hAmp *= d.hDec;
                const float bright = a == Algo::BeatLoop ? lerpf (0.4f, 1.0f, y) : 1.0f;
                out[0] = softClip (kick * 0.9f + snare * bright + hat * bright * 0.8f);
                out[1] = softClip (kick * 0.9f + snare * bright + hat * bright * 1.0f);
                break;
            }
            case Algo::Count: break;
        }
        wl = out[0]; wr = out[1];
    }

    static bool isSynth (Algo a)
    {
        return a == Algo::LeadSynth || a == Algo::BassSynth || a == Algo::Drone || a == Algo::Siren || a == Algo::NoiseRiser
            || a == Algo::DrumRoll || a == Algo::BeatLoop;
    }
    static bool usesFreeze (Algo a) { return a == Algo::BeatRepeat || a == Algo::ReverseLoop || a == Algo::Freeze || a == Algo::Scratch; }

    const float* scL = nullptr;
    const float* scR = nullptr;
    float scSample = 0;

    void process (float* L, float* R, int n, Engine& owner, const float* sideL, const float* sideR)
    {
        scL = sideL; scR = sideR != nullptr ? sideR : sideL;
        int count = 0;
        auto* progs = programs (count);
        serviceBankEvents();

        bpm = clampf (p.bpm, 20.0f, 300.0f);
        if (p.hostPlaying) beatPos = p.ppq;
        const double beatInc = bpm / 60.0 / sr;
        depthS.target = p.depth;
        inGain.target = dbToGain (p.inputDb);
        outGain.target = dbToGain (p.outputDb);
        const int wanted = std::max (0, std::min (count - 1, p.program));

        float inPk = 0, outPk = 0;
        for (int i = 0; i < n; ++i)
        {
            if ((ctrlCounter++ & 31) == 0) controlTick();

            // program change: fade the wet path out, swap, fade back in
            if (wanted != cur)
            {
                progFade -= 1.0f / (0.006f * (float) sr);
                if (progFade <= 0.0f) { progFade = 0.0f; cur = wanted; resetFx(); }
            }
            else if (progFade < 1.0f) progFade = std::min (1.0f, progFade + 1.0f / (0.006f * (float) sr));

            const Algo algo = progs[cur].algo;
            const bool active = padActive;
            if (active && ! prevPadActive)
            {
                // touch-down events
                if (usesFreeze (algo)) { frozen = true; freezeAt = cap.write; loopPos = 0; scratchPos = beatsToSamples (2.0f) * sx.target; scratchVel = 0; }
                if (algo == Algo::TapeStop) { tapeSpeed = 1.0f; readHead = (double) cap.write - 1.0; }
            }
            if (! active && prevPadActive) frozen = false;
            prevPadActive = active;

            activeEnv.target = active ? 1.0f : 0.0f;
            synthEnv.target = active ? 1.0f : 0.0f;
            synthEnv.coeff = active ? 1.0f - std::exp (-1.0f / (0.005f * (float) sr)) : 1.0f - std::exp (-1.0f / (0.25f * (float) sr));
            const float act = activeEnv.next();
            const float senv = synthEnv.next();
            const float x = sx.next(), y = sy.next();
            const float depth = depthS.next(), ig = inGain.next(), og = outGain.next();

            scSample = scL != nullptr ? 0.5f * (scL[i] + scR[i]) : 0.0f;
            float dryL = L[i] * ig, dryR = R[i] * ig;
            inPk = std::max (inPk, std::max (std::fabs (dryL), std::fabs (dryR)));
            if (! p.resample) bankRecordPending (dryL, dryR);
            bankPlayback (dryL, dryR);

            if (! frozen) cap.push (dryL, dryR);

            float wl = 0, wr = 0, oL, oR;
            const ReleaseKind rk = releaseKind (algo);
            if (isSynth (algo))
            {
                if (senv > 1e-4f) runProgram (algo, x, y, dryL, dryR, act, wl, wr);
                const float g = senv * depth * progFade;
                oL = dryL + wl * g; oR = dryR + wr * g;
            }
            else if (rk == ReleaseKind::Tail)
            {
                runProgram (algo, x, y, dryL * act, dryR * act, act, wl, wr);
                const float g = depth * progFade;
                oL = dryL + wl * g; oR = dryR + wr * g;
            }
            else
            {
                runProgram (algo, x, y, dryL, dryR, act, wl, wr);
                const float mix = depth * act * progFade;
                oL = lerpf (dryL, wl, mix); oR = lerpf (dryR, wr, mix);
            }

            if (! std::isfinite (oL) || ! std::isfinite (oR)) { oL = oR = 0; resetFx(); }
            if (p.resample) bankRecordPending (oL, oR);
            oL *= og; oR *= og;
            outPk = std::max (outPk, std::max (std::fabs (oL), std::fabs (oR)));
            L[i] = oL; R[i] = oR;

            const double prevBeat = beatPos;
            beatPos += beatInc;
            beatTick = std::floor (beatPos) != std::floor (prevBeat);
        }

        for (auto& b : banks)
        {
            const auto st = (BankState) b.state.load();
            float pr = 0;
            if (st == BankState::Recording && b.target > 0) pr = (float) b.len / (float) b.target;
            else if (st == BankState::Playing && b.len > 0) pr = (float) b.pos / (float) b.len;
            b.progress.store (pr);
        }
        owner.dispX.store (sx.value);
        owner.dispY.store (sy.value);
        owner.dispActive.store (padActive);
        owner.motionReplay.store (motionPlaying && ! p.touch);
        owner.inPeak.store (inPk);
        owner.outPeak.store (outPk);
    }

    bool beatTick = false;
    void bankRecordPending (float l, float r) { bankRecord (l, r, beatTick); }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int maxBlock) { sr = sampleRate; impl->prepare (sampleRate, maxBlock); }
void Engine::setParams (const Params& p) { impl->p = p; }
void Engine::process (float* left, float* right, int numSamples, const float* sideL, const float* sideR)
{
    impl->process (left, right, numSamples, *this, sideL, sideR);
}

void Engine::bankPress (int b) { if (b >= 0 && b < 4) impl->banks[b].pressCount.fetch_add (1); }
void Engine::bankClear (int b) { if (b >= 0 && b < 4) impl->banks[b].clearCount.fetch_add (1); }
void Engine::bankToggleReverse (int b) { if (b >= 0 && b < 4) impl->banks[b].revCount.fetch_add (1); }
BankState Engine::bankState (int b) const { return (b >= 0 && b < 4) ? (BankState) impl->banks[b].state.load() : BankState::Empty; }
bool Engine::bankReversed (int b) const { return b >= 0 && b < 4 && impl->banks[b].reversed.load(); }
float Engine::bankProgress (int b) const { return (b >= 0 && b < 4) ? impl->banks[b].progress.load() : 0.0f; }
} // namespace ts
