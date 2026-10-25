#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace corda
{

// Main physically-modelled grand piano engine. It is deliberately independent
// of JUCE so that it can be exercised by the offline renderer and unit tests.
class PianoEngine
{
public:
    static constexpr int kFirstNote = 21;   // A0
    static constexpr int kLastNote  = 108;  // C8
    static constexpr int kNumNotes  = kLastNote - kFirstNote + 1;
    static constexpr int kMaxVoices = 48;

    struct Params
    {
        float masterGainDb   = -2.0f;
        float tone           = 0.55f;  // 0..1, hammer hardness / high partials
        float dynamics       = 0.65f;  // 0..1, velocity sensitivity
        float detuneCents    = 1.4f;   // unison spread
        float sympathetic    = 0.45f;  // 0..1, pedal/sympathetic resonance
        float body           = 0.45f;  // 0..1, soundboard colour
        float mechanical     = 0.30f;  // 0..1, hammer/damper noise
        float stereoWidth    = 0.65f;  // 0..1
        float releaseTime    = 0.35f;  // damper time, seconds
        float pedalNoise     = 0.25f;  // 0..1
        float velocityCurve  = 0.5f;   // 0..1, 0=soft touch, 1=hard
        float tuning         = 440.0f; // A4
        float room           = 0.16f;  // 0..1 built-in soundboard space
        float timbre         = 0.0f;   // -1 dark .. +1 bright, set by voicing
        int   voicing        = 0;      // 0 concert, 1 intimate, 2 bright, 3 vintage
    };

    PianoEngine();
    ~PianoEngine();

    void prepare(double sampleRate, int maximumBlockSize, int numChannels);
    void reset();

    void setParams(const Params& p) noexcept;
    const Params& getParams() const noexcept { return params; }

    void noteOn(int midiNote, float velocity, int sampleOffset = 0);
    void noteOff(int midiNote, float velocity = 0.5f, int sampleOffset = 0);
    void sustainPedal(float value, int sampleOffset = 0);
    void allNotesOff(bool immediately = false);

    // Adds stereo output. The engine is stereo; mono output is downmixed.
    void render(float* left, float* right, int numSamples);

    static constexpr int getLatencySamples() noexcept { return 128; }

    float getMeter() const noexcept { return meter.load(std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept { return activeVoices.load(std::memory_order_relaxed); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Params params {};
    std::atomic<float> meter { 0.0f };
    std::atomic<int> activeVoices { 0 };
};

} // namespace corda
