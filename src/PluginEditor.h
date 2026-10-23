#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <array>
#include <memory>

namespace corda
{

class CordaAudioProcessor;

class CordaLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CordaLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getLabelFont(juce::Label&) override;
};

class PianoKeyboard final : public juce::Component
{
public:
    explicit PianoKeyboard(CordaAudioProcessor&);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

private:
    int noteAt(juce::Point<float> p) const;
    CordaAudioProcessor& processor;
    int mouseNote = -1;
};

class CordaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                        private juce::Timer
{
public:
    explicit CordaAudioProcessorEditor(CordaAudioProcessor&);
    ~CordaAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void configureKnob(int index, const juce::String& paramId, const juce::String& label,
                       const juce::String& suffix, const juce::String& tooltip,
                       bool percent = true);
    void drawHeader(juce::Graphics&);
    void drawPanels(juce::Graphics&);

    CordaAudioProcessor& processor;
    CordaLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 700 };

    juce::Label title, subtitle, edition, modelLabel, statusLeft, statusRight;
    juce::ComboBox modelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modelAttachment;

    static constexpr int numKnobs = 13;
    std::array<std::unique_ptr<juce::Slider>, numKnobs> knobs;
    std::array<std::unique_ptr<juce::Label>, numKnobs> knobLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, numKnobs> attachments;

    PianoKeyboard keyboard;

    float displayedMeter = 0.0f;
    int displayedVoices = 0;
    int displayedNote = -1;
    bool uiReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CordaAudioProcessorEditor)
};

} // namespace corda
