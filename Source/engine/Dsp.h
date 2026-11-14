#pragma once
// Small scalar DSP building blocks. Plain C++17, no SIMD intrinsics, no -march flags:
// runs on any x86-64 (macOS 11 Intel included) and on arm64.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ts
{
constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;

inline float clampf (float v, float lo, float hi) { return std::min (hi, std::max (lo, v)); }
inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
inline float softClip (float x) { return std::tanh (x); }
// 20 Hz .. 20 kHz on a log scale from 0..1
inline float expMap (float t, float lo, float hi) { return lo * std::pow (hi / lo, clampf (t, 0.0f, 1.0f)); }

struct Smoother
{
    float value = 0, target = 0, coeff = 0.001f;
    void setTime (double sr, float seconds) { coeff = 1.0f - std::exp (-1.0f / (float) (std::max (1e-4f, seconds) * sr)); }
    void snap (float v) { value = target = v; }
    float next() { value += (target - value) * coeff; return value; }
};

struct Rng
{
    uint32_t s = 0x1234567u;
    float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) * (1.0f / 8388608.0f) - 1.0f; }   // -1..1
    float uni() { return 0.5f * (next() + 1.0f); }
};

// Topology-preserving state variable filter (Simper / Zavalishin). One channel.
struct Svf
{
    float ic1 = 0, ic2 = 0, g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0;
    float lp = 0, bp = 0, hp = 0;
    void reset() { ic1 = ic2 = 0; }
    void set (double sr, float hz, float q)
    {
        hz = clampf (hz, 10.0f, (float) sr * 0.45f);
        g = std::tan (kPi * hz / (float) sr);
        k = 1.0f / std::max (0.05f, q);
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    void tick (float x)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2; bp = v1; hp = x - k * v1 - v2;
    }
    float notch() const { return lp + hp; }
    void copyCoeffs (const Svf& o) { g = o.g; k = o.k; a1 = o.a1; a2 = o.a2; a3 = o.a3; }
};

struct OnePole
{
    float z = 0, a = 0;
    void set (double sr, float hz) { a = std::exp (-kTwoPi * clampf (hz, 1.0f, (float) sr * 0.49f) / (float) sr); }
    float lpf (float x) { z = x + a * (z - x); return z; }
    float hpf (float x) { return x - lpf (x); }
    void reset() { z = 0; }
};

// Mono circular delay line with fractional (cubic) read.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;
    void allocate (int maxSamples)
    {
        int n = 1;
        while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f);
        mask = n - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float tap (int d) const { return buf[(size_t) ((w - 1 - d) & mask)]; }
    // d = delay in samples (>= 0), measured from the most recently written sample
    float read (float d) const
    {
        d = clampf (d, 0.0f, (float) (mask - 4));
        const int i = (int) d;
        const float f = d - (float) i;
        const float y0 = tap (i - 1 < 0 ? 0 : i - 1), y1 = tap (i), y2 = tap (i + 1), y3 = tap (i + 2);
        const float c0 = y1, c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3,
                    c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    int size() const { return mask + 1; }
};

struct Allpass
{
    DelayLine d;
    int len = 1;
    float g = 0.5f;
    void allocate (int n) { d.allocate (n); len = n; }
    float tick (float x)
    {
        const float delayed = d.tap (len - 1);
        const float v = x + g * delayed;
        d.push (v);
        return delayed - g * v;
    }
    void clear() { d.clear(); }
};

// Delay-line pitch shifter: two read taps sweep across a window and crossfade (sin^2), so
// the output never clicks. Ratio 0.25..4. Window ~ 30..120 ms.
struct PitchShifter
{
    DelayLine d;
    float phase = 0, window = 2048;
    void allocate (int maxWindow) { d.allocate (maxWindow * 2 + 8); }
    void clear() { d.clear(); phase = 0; }
    float tick (float x, float ratio, float windowSamples)
    {
        window = windowSamples;
        d.push (x);
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);
        float p2 = phase + 0.5f;
        p2 -= std::floor (p2);
        const float a = std::sin (kPi * phase), b = std::sin (kPi * p2);
        return d.read (phase * window + 1.0f) * a * a + d.read (p2 * window + 1.0f) * b * b;
    }
};

// Stereo reverb: 4 input diffusers + 8-line feedback delay network (Hadamard mix), damping,
// slow modulation of two lines to avoid metallic ringing.
struct Reverb
{
    static constexpr int N = 8;
    DelayLine lines[N];
    Allpass diff[4];
    OnePole damp[N];
    float lens[N] {};
    float fb = 0.8f, size = 1.0f;
    float modPhase = 0;
    double sr = 48000;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static const float baseMs[N] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.3f, 59.9f, 67.1f, 73.3f };
        for (int i = 0; i < N; ++i)
        {
            lens[i] = baseMs[i] * 0.001f * (float) sr;
            lines[i].allocate ((int) (lens[i] * 2.6f) + 64);
        }
        static const float apMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
        for (int i = 0; i < 4; ++i) { diff[i].allocate ((int) (apMs[i] * 0.001f * (float) sr) + 1); diff[i].g = 0.62f; }
        setDamping (6000.0f);
    }
    void clear() { for (auto& l : lines) l.clear(); for (auto& a : diff) a.clear(); for (auto& d : damp) d.reset(); }
    void setDamping (float hz) { for (auto& d : damp) d.set (sr, hz); }
    // decaySeconds = RT60, sizeScale 0.3..2.5
    void set (float decaySeconds, float sizeScale)
    {
        size = clampf (sizeScale, 0.3f, 2.5f);
        const float avgLen = 0.050f * size;   // seconds
        fb = std::pow (10.0f, -3.0f * avgLen / std::max (0.1f, decaySeconds));
        fb = clampf (fb, 0.0f, 0.985f);
    }
    void tick (float inL, float inR, float& outL, float& outR)
    {
        float x = 0.5f * (inL + inR);
        for (auto& a : diff) x = a.tick (x);
        modPhase += 0.6f / (float) sr;
        if (modPhase > 1.0f) modPhase -= 1.0f;
        const float m = std::sin (kTwoPi * modPhase) * 0.0012f * (float) sr;
        float o[N];
        for (int i = 0; i < N; ++i)
        {
            float d = lens[i] * size;
            if (i == 2) d += m;
            if (i == 5) d -= m;
            o[i] = damp[i].lpf (lines[i].read (d));
        }
        // fast Walsh-Hadamard transform (unnormalised), then scale 1/sqrt(8)
        float h[N];
        for (int i = 0; i < N; ++i) h[i] = o[i];
        for (int len = 1; len < N; len <<= 1)
            for (int i = 0; i < N; i += len << 1)
                for (int j = i; j < i + len; ++j)
                {
                    const float a = h[j], b = h[j + len];
                    h[j] = a + b; h[j + len] = a - b;
                }
        const float s = 0.35355339f * fb;
        const float inject = x;
        for (int i = 0; i < N; ++i) lines[i].push (h[i] * s + ((i & 1) ? inject : -inject) * 0.5f + (i < 2 ? inL - inR : 0.0f) * 0.15f);
        outL = (o[0] + o[2] + o[4] + o[6]) * 0.5f;
        outR = (o[1] + o[3] + o[5] + o[7]) * 0.5f;
    }
};

struct EnvFollower
{
    float env = 0, att = 0, rel = 0;
    void set (double sr, float attackMs, float releaseMs)
    {
        att = 1.0f - std::exp (-1.0f / (attackMs * 0.001f * (float) sr));
        rel = 1.0f - std::exp (-1.0f / (releaseMs * 0.001f * (float) sr));
    }
    float tick (float x)
    {
        const float a = std::fabs (x);
        env += (a - env) * (a > env ? att : rel);
        return env;
    }
};

// PolyBLEP sawtooth / square oscillator for the synth programs.
struct BlepOsc
{
    float phase = 0;
    static float blep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    float saw (float dt)
    {
        float v = 2.0f * phase - 1.0f - blep (phase, dt);
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }
    float square (float dt)
    {
        float v = phase < 0.5f ? 1.0f : -1.0f;
        v += blep (phase, dt);
        float p2 = phase + 0.5f;
        if (p2 >= 1.0f) p2 -= 1.0f;
        v -= blep (p2, dt);
        phase += dt;
        if (phase >= 1.0f) phase -= 1.0f;
        return v;
    }
};
} // namespace ts
