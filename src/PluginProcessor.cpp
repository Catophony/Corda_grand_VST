#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace corda
{

juce::AudioProcessorValueTreeState::ParameterLayout CordaAudioProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout] (std::unique_ptr<RangedAudioParameter> p)
    {
        layout.add(std::move(p));
    };

    add(std::make_unique<AudioParameterFloat>(ParameterID { masterId, 1 }, "Master",
        NormalisableRange<float>(-30.0f, 6.0f, 0.1f), -1.5f,
        AudioParameterFloatAttributes().withLabel("dB")));

    add(std::make_unique<AudioParameterFloat>(ParameterID { toneId, 1 }, "Hammer Tone",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.55f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { dynamicsId, 1 }, "Dynamics",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.65f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { detuneId, 1 }, "Unison Drift",
        NormalisableRange<float>(0.0f, 4.0f, 0.01f), 1.4f,
        AudioParameterFloatAttributes().withLabel("ct")));

    add(std::make_unique<AudioParameterFloat>(ParameterID { sympathyId, 1 }, "Sympathetic",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.40f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { bodyId, 1 }, "Soundboard",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.45f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { mechanicsId, 1 }, "Mechanics",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { stereoId, 1 }, "Stereo Width",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.65f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { releaseId, 1 }, "Damper Time",
        NormalisableRange<float>(0.05f, 1.2f, 0.001f, 0.45f), 0.35f,
        AudioParameterFloatAttributes().withLabel("s")));

    add(std::make_unique<AudioParameterFloat>(ParameterID { pedalNoiseId, 1 }, "Pedal Noise",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.25f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { velocityId, 1 }, "Velocity Curve",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.5f));

    add(std::make_unique<AudioParameterFloat>(ParameterID { tuningId, 1 }, "Tuning",
        NormalisableRange<float>(415.0f, 450.0f, 0.1f), 440.0f,
        AudioParameterFloatAttributes().withLabel("Hz")));

    add(std::make_unique<AudioParameterFloat>(ParameterID { roomId, 1 }, "Room",
        NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.18f));

    add(std::make_unique<AudioParameterChoice>(ParameterID { modelId, 1 }, "Voicing",
        StringArray { "Concert", "Intimate", "Bright", "Vintage" }, 0));

    return layout;
}

CordaAudioProcessor::CordaAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "CORDA", createParameterLayout())
{
    setLatencySamples(PianoEngine::getLatencySamples());
    for (auto* id : { masterId, toneId, dynamicsId, detuneId, sympathyId, bodyId,
                      mechanicsId, stereoId, releaseId, pedalNoiseId, velocityId,
                      tuningId, roomId, modelId })
        apvts.addParameterListener(id, this);
}

void CordaAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    piano.prepare(sampleRate, samplesPerBlock, 2);
    setLatencySamples(PianoEngine::getLatencySamples());
    renderScratch.setSize(2, juce::jmax(samplesPerBlock, 8192), false, true, false);
    masterGain.reset(sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue(masterId)->load()));
    updateEngineParams();
}

void CordaAudioProcessor::releaseResources()
{
    piano.reset();
}

bool CordaAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void CordaAudioProcessor::parameterChanged(const juce::String&, float)
{
    paramsDirty.store(true, std::memory_order_release);
}

void CordaAudioProcessor::updateEngineParams()
{
    auto value = [this] (const char* id, float fallback)
    {
        if (auto* p = apvts.getRawParameterValue(id))
            return p->load();
        return fallback;
    };

    PianoEngine::Params p;
    p.masterGainDb  = value(masterId, -1.5f);
    p.tone          = value(toneId, 0.55f);
    p.dynamics      = value(dynamicsId, 0.65f);
    p.detuneCents   = value(detuneId, 1.4f);
    p.sympathetic   = value(sympathyId, 0.40f);
    p.body          = value(bodyId, 0.45f);
    p.mechanical    = value(mechanicsId, 0.25f);
    p.stereoWidth   = value(stereoId, 0.65f);
    p.releaseTime   = value(releaseId, 0.35f);
    p.pedalNoise    = value(pedalNoiseId, 0.25f);
    p.velocityCurve = value(velocityId, 0.5f);
    p.tuning        = value(tuningId, 440.0f);
    p.room          = value(roomId, 0.18f);

    // Voicing presets gently re-voice the instrument without touching the user
    // controls. 0 = concert, 1 = intimate, 2 = bright, 3 = vintage.
    const int voicing = (int) value(modelId, 0.0f);
    switch (voicing)
    {
        case 1: p.tone *= 0.82f; p.sympathetic *= 1.2f; p.body *= 1.25f; p.stereoWidth *= 0.9f; break;
        case 2: p.tone = juce::jmin(1.0f, p.tone * 1.25f + 0.08f); p.dynamics = juce::jmin(1.0f, p.dynamics + 0.08f); break;
        case 3: p.tone *= 0.9f; p.detuneCents = juce::jmin(4.0f, p.detuneCents * 1.5f + 0.25f); p.body *= 1.1f; break;
        default: break;
    }

    piano.setParams(p);
    paramsDirty.store(false, std::memory_order_release);
}

void CordaAudioProcessor::pushParamsToEngineIfNeeded()
{
    if (paramsDirty.exchange(false, std::memory_order_acq_rel))
        updateEngineParams();
}

void CordaAudioProcessor::handleMidi(const juce::MidiMessage& m)
{
    if (m.isNoteOn())
    {
        lastNote.store(m.getNoteNumber());
        lastVelocity.store(m.getFloatVelocity());
        const int n = m.getNoteNumber();
        activeNoteMask[n >> 6].fetch_or(1ull << (n & 63), std::memory_order_relaxed);
        piano.noteOn(n, m.getFloatVelocity());
    }
    else if (m.isNoteOff())
    {
        const int n = m.getNoteNumber();
        activeNoteMask[n >> 6].fetch_and(~(1ull << (n & 63)), std::memory_order_relaxed);
        piano.noteOff(n, m.getFloatVelocity());
    }
    else if (m.isController() && m.getControllerNumber() == 64)
    {
        // Keep the continuous 0..127 pedal value so half-pedalling works.
        piano.sustainPedal((float) m.getControllerValue() / 127.0f);
    }
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        for (auto& msk : activeNoteMask) msk.store(0, std::memory_order_relaxed);
        piano.allNotesOff(false);
    }
}

void CordaAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int channels = buffer.getNumChannels();
    buffer.clear();
    if (n <= 0)
        return;

    pushParamsToEngineIfNeeded();

    // Merge notes played on the editor's on-screen keyboard.
    keyboardState.processNextMidiBuffer(midi, 0, n, true);

    if (renderScratch.getNumSamples() < n)
        renderScratch.setSize(2, n, false, true, false);

    renderScratch.clear();

    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventPos = juce::jlimit(0, n, meta.samplePosition);
        if (eventPos > pos)
        {
            piano.render(renderScratch.getWritePointer(0, pos),
                         renderScratch.getWritePointer(1, pos), eventPos - pos);
            pos = eventPos;
        }
        handleMidi(meta.getMessage());
    }
    if (pos < n)
        piano.render(renderScratch.getWritePointer(0, pos),
                     renderScratch.getWritePointer(1, pos), n - pos);

    masterGain.setTargetValue(juce::Decibels::decibelsToGain(
        apvts.getRawParameterValue(masterId)->load()));

    if (channels == 1)
    {
        auto* dst = buffer.getWritePointer(0);
        const float* l = renderScratch.getReadPointer(0);
        const float* r = renderScratch.getReadPointer(1);
        for (int i = 0; i < n; ++i)
            dst[i] = (l[i] + r[i]) * 0.7071f * masterGain.getNextValue();
    }
    else
    {
        for (int i = 0; i < n; ++i)
        {
            const float g = masterGain.getNextValue();
            for (int ch = 0; ch < channels; ++ch)
            {
                const float* src = renderScratch.getReadPointer(ch == 0 ? 0 : 1);
                buffer.getWritePointer(ch)[i] = src[i] * g;
            }
        }
    }

    outputMeter.store(piano.getMeter(), std::memory_order_relaxed);
    activeVoiceCount.store(piano.getActiveVoiceCount(), std::memory_order_relaxed);
}

void CordaAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void CordaAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* CordaAudioProcessor::createEditor()
{
    return new CordaAudioProcessorEditor(*this);
}

} // namespace corda

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new corda::CordaAudioProcessor();
}
