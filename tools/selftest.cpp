#include "dsp/PianoEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
struct Buffer
{
    std::vector<float> l, r;
    explicit Buffer(int n) : l((size_t) n), r((size_t) n) {}
};

Buffer renderBlock(corda::PianoEngine& engine, int n)
{
    Buffer b(n);
    engine.render(b.l.data(), b.r.data(), n);
    return b;
}

double rms(const Buffer& b)
{
    double s = 0.0;
    for (size_t i = 0; i < b.l.size(); ++i)
        s += (double) b.l[i] * b.l[i] + (double) b.r[i] * b.r[i];
    return std::sqrt(s / (double) std::max<size_t>(1, b.l.size() * 2));
}

float peak(const Buffer& b)
{
    float p = 0.0f;
    for (size_t i = 0; i < b.l.size(); ++i)
        p = std::max(p, std::max(std::abs(b.l[i]), std::abs(b.r[i])));
    return p;
}

bool finite(const Buffer& b)
{
    for (size_t i = 0; i < b.l.size(); ++i)
        if (! std::isfinite(b.l[i]) || ! std::isfinite(b.r[i]))
            return false;
    return true;
}

int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s  %s\n", condition ? "PASS" : "FAIL", what);
    if (! condition)
        ++failures;
}
} // namespace

int main()
{
    using namespace corda;

    for (int sr : { 44100, 48000, 96000 })
    {
        PianoEngine engine;
        PianoEngine::Params p;
        p.tuning = 442.0f;
        p.room = 0.22f;
        p.sympathetic = 0.5f;
        engine.setParams(p);
        engine.prepare((double) sr, 512, 2);

        engine.noteOn(60, 0.8f);
        Buffer b = renderBlock(engine, sr / 2);
        check(finite(b), "finite audio after note-on");
        check(peak(b) > 0.002f, "audible note at all sample rates");
        check(peak(b) <= 1.0001f, "output stays bounded");

        engine.noteOff(60, 0.6f);
        Buffer tail = renderBlock(engine, sr / 4);
        check(finite(tail), "finite audio after note-off");
        check(rms(tail) < rms(b), "damper reduces level");

        // Sustain-pedal behaviour: a released note must still ring.
        engine.reset();
        engine.noteOn(48, 0.75f);
        engine.sustainPedal(1.0f);
        engine.noteOff(48, 0.5f);
        Buffer pedalled = renderBlock(engine, sr);
        check(rms(pedalled) > 0.0005, "sustain pedal holds the note");

        // All 88 keys must produce sound and remain finite.
        bool allSound = true;
        for (int n = 21; n <= 108; ++n)
        {
            engine.reset();
            engine.noteOn(n, 0.7f);
            Buffer note = renderBlock(engine, 4096);
            if (! finite(note) || peak(note) < 0.0005f)
            {
                std::printf("  note %d failed (peak %.6f)\n", n, (double) peak(note));
                allSound = false;
            }
        }
        check(allSound, "all 88 keys sound");

        // Extreme parameters must stay finite.
        engine.reset();
        p.tone = 1.0f; p.dynamics = 1.0f; p.detuneCents = 4.0f;
        p.sympathetic = 1.0f; p.body = 1.0f; p.mechanical = 1.0f;
        p.room = 1.0f; p.releaseTime = 0.05f; p.velocityCurve = 1.0f;
        engine.setParams(p);
        for (int n : { 21, 33, 45, 57, 69, 81, 93, 108 })
            engine.noteOn(n, 1.0f);
        Buffer hot = renderBlock(engine, sr / 2);
        check(finite(hot), "extreme parameters remain finite");
        check(peak(hot) <= 1.0001f, "extreme parameters stay bounded");
    }

    // Polyphony / real-time headroom.
    PianoEngine engine;
    engine.prepare(48000.0, 512, 2);
    for (int i = 0; i < 48; ++i)
        engine.noteOn(21 + (i * 17) % 88, 0.55f);
    engine.sustainPedal(1.0f);
    const int seconds = 5;
    const int total = 48000 * seconds;
    Buffer block(512);
    const auto t0 = std::chrono::steady_clock::now();
    for (int done = 0; done < total; done += 512)
    {
        const int n = std::min(512, total - done);
        engine.render(block.l.data(), block.r.data(), n);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(t1 - t0).count();
    std::printf("polyphony: 48 voices, %.2f s audio in %.3f s (%.1fx realtime)\n",
                (double) seconds, elapsed, (double) seconds / std::max(0.001, elapsed));
    check(elapsed < (double) seconds, "48-voice render is faster than realtime");

    std::printf("\n%s (%d failure%s)\n", failures == 0 ? "SELFTEST PASSED" : "SELFTEST FAILED",
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
