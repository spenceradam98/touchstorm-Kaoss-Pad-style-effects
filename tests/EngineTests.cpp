// Engine tests: plain asserts, no framework. Run: ctest --test-dir build
#include "Engine.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace ts;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static const double SR = 48000.0;
static const int BLOCK = 256;

struct Run
{
    Engine e;
    Params p;
    Rng rng;
    std::vector<float> l, r;
    Run() { l.resize (BLOCK); r.resize (BLOCK); e.setParams (p); e.prepare (SR, BLOCK); }
    // feeds noise (or silence), returns peak and rms of the output
    void go (int blocks, bool noise, float& peak, double& rms, float amp = 0.3f)
    {
        peak = 0; rms = 0;
        double acc = 0; long n = 0;
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < BLOCK; ++i) { l[(size_t) i] = noise ? rng.next() * amp : 0.0f; r[(size_t) i] = noise ? rng.next() * amp : 0.0f; }
            e.setParams (p);
            e.process (l.data(), r.data(), BLOCK);
            for (int i = 0; i < BLOCK; ++i)
            {
                const float a = std::fabs (l[(size_t) i]), c = std::fabs (r[(size_t) i]);
                if (! std::isfinite (a) || ! std::isfinite (c)) { peak = 1e9f; return; }
                peak = std::max (peak, std::max (a, c));
                acc += (double) a * a; ++n;
            }
        }
        rms = std::sqrt (acc / (double) std::max (1L, n));
    }
};

static void testAllProgramsStable()
{
    int count = 0;
    auto* progs = programs (count);
    CHECK (count >= 50, "expected at least 50 programs, got %d", count);
    for (int i = 0; i < count; ++i)
    {
        // corners of the pad, worst case feedback
        const float xs[] = { 0.0f, 1.0f, 0.5f, 1.0f }, ys[] = { 0.0f, 1.0f, 0.5f, 0.0f };
        for (int k = 0; k < 4; ++k)
        {
            Run run;
            run.p.program = i; run.p.touch = true; run.p.x = xs[k]; run.p.y = ys[k];
            float pk; double rms;
            run.go (400, true, pk, rms);   // ~2 s
            CHECK (pk < 8.0f, "program %d (%s) at x=%.1f y=%.1f peaked at %.2f", i, progs[i].name, xs[k], ys[k], pk);
            if (progs[i].category != Category::GrainLoop)
                CHECK (rms > 1e-4, "program %d (%s) at x=%.1f y=%.1f is silent", i, progs[i].name, xs[k], ys[k]);
        }
    }
}

static void testReleaseBypassIsDry()
{
    Run run;
    run.p.program = 0;   // low pass
    run.p.touch = false; run.p.x = 0.0f; run.p.y = 1.0f;
    run.e.setParams (run.p);
    // with the pad untouched, the output must equal the input exactly
    for (int i = 0; i < BLOCK; ++i) { run.l[(size_t) i] = 0.1f * std::sin ((float) i * 0.05f); run.r[(size_t) i] = run.l[(size_t) i]; }
    std::vector<float> ref = run.l;
    run.e.process (run.l.data(), run.r.data(), BLOCK);
    float err = 0;
    for (int i = 0; i < BLOCK; ++i) err = std::max (err, std::fabs (run.l[(size_t) i] - ref[(size_t) i]));
    CHECK (err < 1e-5f, "untouched pad should pass dry, max error %g", err);
}

static void testLowPassCutsHighs()
{
    Run run;
    run.p.program = 0; run.p.touch = true; run.p.x = 0.05f; run.p.y = 0.0f;
    float pk; double wet, dry;
    run.go (100, true, pk, wet);
    run.p.touch = false;
    run.go (100, true, pk, dry);
    CHECK (wet < dry * 0.5, "closed low pass should cut white noise (wet %g dry %g)", wet, dry);
}

static void testDelayTailAfterRelease()
{
    Run run;
    int count = 0; programs (count);
    run.p.program = firstProgramInCategory (Category::Delay);
    run.p.touch = true; run.p.x = 0.3f; run.p.y = 0.7f;
    float pk; double rms;
    run.go (50, true, pk, rms);
    run.p.touch = false;
    run.go (40, false, pk, rms);   // silence in, tail should still ring
    CHECK (rms > 1e-3, "delay should keep ringing after release (rms %g)", rms);
}

static void testHoldKeepsEffect()
{
    Run run;
    run.p.program = 0; run.p.touch = true; run.p.x = 0.05f; run.p.y = 0.0f;
    float pk; double held, ref;
    run.go (20, true, pk, ref);
    run.p.touch = false; run.p.hold = true;
    run.go (100, true, pk, held);
    run.p.hold = false;
    run.go (100, true, pk, ref);
    CHECK (held < ref * 0.5, "hold should keep the filter on after release (held %g, released %g)", held, ref);
}

static void testBanks()
{
    Run run;
    run.p.bpm = 120; run.p.bankBeats = 1;   // 0.5 s
    run.e.bankPress (0);
    float pk; double rms;
    run.go (2, true, pk, rms);
    CHECK (run.e.bankState (0) == BankState::Recording, "bank should be recording");
    run.go (100, true, pk, rms);   // > 0.5 s
    CHECK (run.e.bankState (0) == BankState::Playing, "bank should play after its length");
    run.go (50, false, pk, rms);   // silence in, loop playing
    CHECK (rms > 1e-3, "playing bank should be audible (rms %g)", rms);
    run.e.bankPress (0);
    run.go (2, false, pk, rms);
    CHECK (run.e.bankState (0) == BankState::Stopped, "second press stops the bank");
    run.go (10, false, pk, rms);
    CHECK (pk < 1e-6f, "stopped bank should be silent (peak %g)", pk);
    run.e.bankClear (0);
    run.go (1, false, pk, rms);
    CHECK (run.e.bankState (0) == BankState::Empty, "clear empties the bank");
}

static void testMotionReplay()
{
    Run run;
    run.p.motion = true; run.p.touch = true;
    float pk; double rms;
    for (int k = 0; k < 40; ++k) { run.p.x = (float) k / 40.0f; run.go (1, false, pk, rms); }
    run.p.touch = false;
    run.go (5, false, pk, rms);
    CHECK (run.e.motionPlaying(), "motion should replay after release");
    CHECK (run.e.displayActive(), "pad should stay active while motion replays");
    float minX = 1, maxX = 0;
    for (int k = 0; k < 60; ++k) { run.go (1, false, pk, rms); minX = std::min (minX, run.e.displayX()); maxX = std::max (maxX, run.e.displayX()); }
    CHECK (maxX - minX > 0.4f, "replayed X should sweep (range %g)", maxX - minX);
}

static void testBeatRepeatLoops()
{
    Run run;
    run.p.program = firstProgramInCategory (Category::GrainLoop);   // Beat Repeat
    run.p.x = 0.5f; run.p.y = 0.5f;
    float pk; double rms;
    run.go (100, true, pk, rms);   // fill the capture buffer
    run.p.touch = true;
    run.go (2, true, pk, rms);
    run.go (100, false, pk, rms);  // input goes silent, the repeat keeps playing
    CHECK (rms > 1e-2, "beat repeat should loop captured audio (rms %g)", rms);
}

int main()
{
    testAllProgramsStable();
    testReleaseBypassIsDry();
    testLowPassCutsHighs();
    testDelayTailAfterRelease();
    testHoldKeepsEffect();
    testBanks();
    testMotionReplay();
    testBeatRepeatLoops();
    if (failures == 0) std::printf ("all engine tests passed\n");
    return failures == 0 ? 0 : 1;
}
