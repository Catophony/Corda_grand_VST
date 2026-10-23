#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <juce_audio_utils/juce_audio_utils.h>

#include "dsp/PianoEngine.h"

namespace corda
{

class CordaAudioProcessor final : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    CordaAudioProcessor();
    ~CordaAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Corda Grand"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 30.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Concert Grand"; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::MidiKeyboardState keyboardState;

    PianoEngine& engine() noexcept { return piano; }
    const PianoEngine& engine() const noexcept { return piano; }

    using AudioProcessor::processBlock;

    std::atomic<float> inputActivity { 0.0f };
    std::atomic<int> lastNote { -1 };
    std::atomic<float> lastVelocity { 0.0f };
    std::atomic<float> outputMeter { 0.0f };
    std::atomic<int> activeVoiceCount { 0 };
    std::atomic<uint64_t> activeNoteMask[2] { { 0 }, { 0 } };
    bool isNoteActive(int note) const noexcept
    {
        if (note < 0 || note > 127) return false;
        return (activeNoteMask[note >> 6].load(std::memory_order_relaxed) >> (note & 63)) & 1ull;
    }

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void updateEngineParams();
    void handleMidi(const juce::MidiMessage& message);
    void pushParamsToEngineIfNeeded();

    PianoEngine piano;
    juce::AudioBuffer<float> renderScratch;
    std::atomic<bool> paramsDirty { true };
    double currentSampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> masterGain;

    static constexpr const char* masterId      = "master";
    static constexpr const char* toneId        = "tone";
    static constexpr const char* dynamicsId    = "dynamics";
    static constexpr const char* detuneId      = "detune";
    static constexpr const char* sympathyId    = "sympathy";
    static constexpr const char* bodyId        = "body";
    static constexpr const char* mechanicsId   = "mechanics";
    static constexpr const char* stereoId      = "stereo";
    static constexpr const char* releaseId     = "release";
    static constexpr const char* pedalNoiseId  = "pedalNoise";
    static constexpr const char* velocityId    = "velocityCurve";
    static constexpr const char* tuningId      = "tuning";
    static constexpr const char* roomId        = "room";
    static constexpr const char* modelId       = "model";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CordaAudioProcessor)
};

} // namespace corda
