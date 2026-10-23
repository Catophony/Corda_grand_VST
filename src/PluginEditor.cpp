#include "PluginEditor.h"
#include "PluginProcessor.h"

namespace corda
{

namespace
{
constexpr juce::uint32 background = 0xff100d0b;
constexpr juce::uint32 panel      = 0xff181310;
constexpr juce::uint32 panelLine  = 0xff34291f;
constexpr juce::uint32 gold       = 0xffd6b16b;
constexpr juce::uint32 goldBright = 0xfff0d49a;
constexpr juce::uint32 cream      = 0xffe7d8bc;
constexpr juce::uint32 dim        = 0xff9d8c75;

juce::Colour c(juce::uint32 v) { return juce::Colour(v); }

int whiteIndexForNote(int note)
{
    int count = 0;
    for (int n = 21; n < note; ++n)
    {
        const int pc = n % 12;
        if (pc != 1 && pc != 3 && pc != 6 && pc != 8 && pc != 10)
            ++count;
    }
    return count;
}

bool isBlackNote(int note)
{
    const int pc = note % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}
} // namespace

//==============================================================================
CordaLookAndFeel::CordaLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, c(cream));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, c(gold).withAlpha(0.35f));
    setColour(juce::Label::textColourId, c(cream));
    setColour(juce::ComboBox::backgroundColourId, c(0xff211a15));
    setColour(juce::ComboBox::textColourId, c(cream));
    setColour(juce::ComboBox::outlineColourId, c(panelLine));
    setColour(juce::ComboBox::arrowColourId, c(gold));
    setColour(juce::PopupMenu::backgroundColourId, c(0xff1d1713));
    setColour(juce::PopupMenu::textColourId, c(cream));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c(0xff5a452b));
    setColour(juce::PopupMenu::highlightedTextColourId, c(goldBright));
    setColour(juce::TooltipWindow::backgroundColourId, c(0xff211a15));
    setColour(juce::TooltipWindow::textColourId, c(cream));
    setColour(juce::TooltipWindow::outlineColourId, c(panelLine));
}

void CordaLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    const float size = (float) juce::jmin(width, height);
    const float cx = (float) x + (float) width * 0.5f;
    const float cy = (float) y + (float) height * 0.5f;
    const float radius = size * 0.5f - 5.0f;
    const float angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Soft halo behind the knob.
    juce::ColourGradient halo(c(gold).withAlpha(0.09f), cx, cy,
                              c(gold).withAlpha(0.0f), cx + radius * 1.35f, cy + radius * 1.35f, true);
    g.setGradientFill(halo);
    g.fillEllipse(cx - radius * 1.2f, cy - radius * 1.2f, radius * 2.4f, radius * 2.4f);

    // Tick ring.
    for (int i = 0; i <= 10; ++i)
    {
        const float a = rotaryStartAngle + (rotaryEndAngle - rotaryStartAngle) * (float) i / 10.0f;
        const float r1 = radius * 1.02f;
        const float r2 = radius * 1.08f;
        const float t = std::sin(a), co = std::cos(a);
        g.setColour(c(0xff4d4032));
        g.drawLine(cx + r1 * t, cy - r1 * co, cx + r2 * t, cy - r2 * co, 1.0f);
    }

    // Track and value arc.
    const float arcR = radius * 0.94f;
    {
        juce::Path track;
        track.addCentredArc(cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(c(0xff3a2f25));
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }
    {
        juce::Path value;
        value.addCentredArc(cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(c(gold));
        g.strokePath(value, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Knob body.
    const float br = radius * 0.70f;
    juce::ColourGradient body(c(0xff3b3128), cx - br * 0.5f, cy - br * 0.65f,
                              c(0xff0d0b09), cx + br * 0.5f, cy + br * 0.7f, false);
    body.addColour(0.55, c(0xff211b16));
    g.setGradientFill(body);
    g.fillEllipse(cx - br, cy - br, br * 2.0f, br * 2.0f);
    g.setColour(c(0xff544433));
    g.drawEllipse(cx - br, cy - br, br * 2.0f, br * 2.0f, 1.1f);

    // Inner cap.
    g.setColour(c(0xff17120e));
    g.fillEllipse(cx - br * 0.76f, cy - br * 0.76f, br * 1.52f, br * 1.52f);
    g.setColour(c(0xff2e261e));
    g.drawEllipse(cx - br * 0.76f, cy - br * 0.76f, br * 1.52f, br * 1.52f, 1.0f);

    // Pointer.
    const float ptr = br * 0.88f;
    const float px = cx + ptr * std::sin(angle);
    const float py = cy - ptr * std::cos(angle);
    g.setColour(c(goldBright).withAlpha(slider.isEnabled() ? 0.95f : 0.35f));
    g.drawLine(cx, cy, px, py, 2.2f);
    g.fillEllipse(px - 2.0f, py - 2.0f, 4.0f, 4.0f);

    g.setColour(c(0xff6d5537));
    g.fillEllipse(cx - 1.6f, cy - 1.6f, 3.2f, 3.2f);
}

void CordaLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox&)
{
    const juce::Rectangle<float> r(0.0f, 0.0f, (float) width, (float) height);
    g.setColour(c(0xff211a15));
    g.fillRoundedRectangle(r, 4.0f);
    g.setColour(c(panelLine));
    g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);

    juce::Path arrow;
    const float ax = (float) width - 16.0f;
    const float ay = (float) height * 0.5f;
    arrow.startNewSubPath(ax - 4.0f, ay - 2.0f);
    arrow.lineTo(ax, ay + 2.5f);
    arrow.lineTo(ax + 4.0f, ay - 2.0f);
    g.setColour(c(gold));
    g.strokePath(arrow, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void CordaLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(10, 1, box.getWidth() - 30, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

juce::Font CordaLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(juce::FontOptions(12.5f));
}

juce::Font CordaLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(11.0f));
}

//==============================================================================
PianoKeyboard::PianoKeyboard(CordaAudioProcessor& p) : processor(p)
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

int PianoKeyboard::noteAt(juce::Point<float> p) const
{
    const float whiteW = (float) getWidth() / 52.0f;
    const float blackW = whiteW * 0.62f;
    const float blackH = (float) getHeight() * 0.62f;

    if (p.y < blackH)
    {
        for (int n = 21; n <= 108; ++n)
        {
            if (!isBlackNote(n))
                continue;
            const float x = (float) whiteIndexForNote(n) * whiteW - blackW * 0.5f;
            if (p.x >= x && p.x <= x + blackW)
                return n;
        }
    }

    int idx = (int) std::floor(p.x / whiteW);
    idx = juce::jlimit(0, 51, idx);
    for (int n = 21; n <= 108; ++n)
    {
        if (isBlackNote(n))
            continue;
        if (whiteIndexForNote(n) == idx)
            return n;
    }
    return -1;
}

void PianoKeyboard::mouseDown(const juce::MouseEvent& e)
{
    const int n = noteAt(e.position);
    if (n < 0)
        return;
    mouseNote = n;
    const float velocity = juce::jlimit(0.25f, 0.98f, 0.35f + e.position.y / (float) getHeight() * 0.6f);
    processor.keyboardState.noteOn(1, n, velocity);
    repaint();
}

void PianoKeyboard::mouseUp(const juce::MouseEvent&)
{
    if (mouseNote >= 0)
        processor.keyboardState.noteOff(1, mouseNote, 0.5f);
    mouseNote = -1;
    repaint();
}

void PianoKeyboard::paint(juce::Graphics& g)
{
    const float whiteW = (float) getWidth() / 52.0f;
    const float blackW = whiteW * 0.62f;
    const float h = (float) getHeight();
    const float blackH = h * 0.62f;

    // Felt strip.
    juce::ColourGradient felt(c(0xff4a1714), 0.0f, 0.0f, c(0xff210b0a), 0.0f, 8.0f, false);
    g.setGradientFill(felt);
    g.fillRect(0.0f, 0.0f, (float) getWidth(), 7.0f);
    g.setColour(c(0xff070605));
    g.fillRect(0.0f, 7.0f, (float) getWidth(), (float) getHeight() - 7.0f);

    for (int n = 21; n <= 108; ++n)
    {
        if (isBlackNote(n))
            continue;
        const int idx = whiteIndexForNote(n);
        const float x = (float) idx * whiteW;
        const juce::Rectangle<float> key(x + 0.7f, 8.0f, whiteW - 1.4f, h - 8.0f);
        const bool active = processor.isNoteActive(n) || n == mouseNote;

        juce::ColourGradient grad(active ? c(0xffe7c47f) : c(0xffe8e0d0), x, 8.0f,
                                  active ? c(0xffa87a37) : c(0xffaaa08d), x, h, false);
        if (!active)
            grad.addColour(0.72f, c(0xfff4eee2));
        g.setGradientFill(grad);
        g.fillRoundedRectangle(key, 1.4f);

        if (!active)
        {
            g.setColour(c(0x33000000));
            g.drawLine(key.getRight() - 0.8f, key.getY(), key.getRight() - 0.8f, key.getBottom(), 1.0f);
        }

        if (n % 12 == 0 && whiteW > 10.0f)
        {
            g.setColour(c(0xff3c342a));
            g.setFont(juce::Font(juce::FontOptions(7.5f)));
            auto labelArea = key;
            g.drawText(juce::String("C") + juce::String(n / 12 - 1),
                       labelArea.removeFromBottom(12.0f), juce::Justification::centred, false);
        }
    }

    for (int n = 21; n <= 108; ++n)
    {
        if (!isBlackNote(n))
            continue;
        const int idx = whiteIndexForNote(n);
        const float x = (float) idx * whiteW - blackW * 0.5f;
        const juce::Rectangle<float> key(x, 8.0f, blackW, blackH);
        const bool active = processor.isNoteActive(n) || n == mouseNote;

        juce::ColourGradient grad(active ? c(0xffc79445) : c(0xff2a2520), x, 8.0f,
                                  active ? c(0xff5b3b12) : c(0xff080706), x, blackH, false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(key, 1.6f);
        g.setColour(c(active ? 0xffe2b46b : 0xff4b3b2a).withAlpha(0.75f));
        g.drawRoundedRectangle(key.reduced(0.5f), 1.6f, 0.8f);
    }

    // A subtle lid shadow across the top of the keyboard.
    juce::ColourGradient shadow(juce::Colours::black.withAlpha(0.55f), 0.0f, 0.0f,
                                juce::Colours::transparentBlack, 0.0f, 26.0f, false);
    g.setGradientFill(shadow);
    g.fillRect(0.0f, 7.0f, (float) getWidth(), 26.0f);
}

//==============================================================================
CordaAudioProcessorEditor::CordaAudioProcessorEditor(CordaAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), keyboard(p)
{
    setLookAndFeel(&lnf);

    title.setText("CORDA", juce::dontSendNotification);
    title.setFont(juce::Font(juce::FontOptions("DejaVu Serif", 38.0f, juce::Font::bold)));
    title.setColour(juce::Label::textColourId, c(goldBright));
    title.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title);

    subtitle.setText("PHYSICAL GRAND PIANO", juce::dontSendNotification);
    subtitle.setFont(juce::Font(juce::FontOptions(10.5f)));
    subtitle.setColour(juce::Label::textColourId, c(dim));
    subtitle.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(subtitle);

    edition.setText("88 KEYS  |  MODELLED STRINGS  |  NO SAMPLES", juce::dontSendNotification);
    edition.setFont(juce::Font(juce::FontOptions(9.5f)));
    edition.setColour(juce::Label::textColourId, c(0xff776a58));
    edition.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(edition);

    modelLabel.setText("INSTRUMENT", juce::dontSendNotification);
    modelLabel.setFont(juce::Font(juce::FontOptions(9.0f)));
    modelLabel.setColour(juce::Label::textColourId, c(dim));
    modelLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(modelLabel);

    modelBox.addItemList({ "Concert Grand", "Intimate Grand", "Bright Studio", "Vintage Grand" }, 1);
    modelBox.setTooltip("Overall voicing of the instrument.");
    addAndMakeVisible(modelBox);
    modelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.apvts, "model", modelBox);

    configureKnob(0, "tone", "HAMMER", "", "Hammer felt hardness: soft and round to bright and incisive.", true);
    configureKnob(1, "dynamics", "DYNAMICS", "", "Velocity sensitivity of the action.", true);
    configureKnob(2, "detune", "UNISON", " ct", "Spacing between the unison strings. Real pianos use 1–2 cents.", false);
    configureKnob(3, "sympathy", "SYMPATHY", "", "Pedal-down sympathetic resonance and duplex-scale colour.", true);
    configureKnob(4, "body", "SOUNDBOARD", "", "Amount of bridge and soundboard body resonance.", true);
    configureKnob(5, "mechanics", "MECHANICS", "", "Hammer contact, key and damper noise.", true);
    configureKnob(6, "stereo", "WIDTH", "", "Stereo spread of the keyboard and unison strings.", true);
    configureKnob(7, "release", "DAMPERS", " s", "Damper fall time after key release.", false);
    configureKnob(8, "pedalNoise", "PEDAL NOISE", "", "Mechanical noise of the sustain pedal.", true);
    configureKnob(9, "velocityCurve", "VELOCITY", "", "Soft to hard velocity curve.", true);
    configureKnob(10, "tuning", "TUNING", " Hz", "Concert pitch of A4.", false);
    configureKnob(11, "room", "ROOM", "", "Built-in soundboard space. Set to zero to use Reaper's reverb.", true);
    configureKnob(12, "master", "MASTER", " dB", "Output level.", false);

    statusLeft.setFont(juce::Font(juce::FontOptions(9.5f)));
    statusLeft.setColour(juce::Label::textColourId, c(dim));
    statusLeft.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(statusLeft);

    statusRight.setFont(juce::Font(juce::FontOptions(9.5f)));
    statusRight.setColour(juce::Label::textColourId, c(dim));
    statusRight.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(statusRight);

    addAndMakeVisible(keyboard);
    statusLeft.setText("VOICES  00 / 48    OUTPUT  -inf dB", juce::dontSendNotification);
    statusRight.setText("A4  440.0 Hz    |    MIDI 21-108    |    CLICK KEYS TO PLAY",
                        juce::dontSendNotification);
    uiReady = true;
    setSize(920, 590);
    setResizable(false, false);
    startTimerHz(20);
}

CordaAudioProcessorEditor::~CordaAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void CordaAudioProcessorEditor::configureKnob(int index, const juce::String& paramId,
                                              const juce::String& label, const juce::String& suffix,
                                              const juce::String& tooltip, bool percent)
{
    auto s = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                            juce::Slider::TextBoxBelow);
    s->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 17);
    s->setColour(juce::Slider::textBoxTextColourId, c(cream));
    s->setTooltip(tooltip);
    s->setTextValueSuffix(suffix);
    s->setPopupDisplayEnabled(true, false, this);
    s->setMouseDragSensitivity(160);

    auto l = std::make_unique<juce::Label>();
    l->setText(label, juce::dontSendNotification);
    l->setFont(juce::Font(juce::FontOptions(9.5f)));
    l->setColour(juce::Label::textColourId, c(dim));
    l->setJustificationType(juce::Justification::centred);

    addAndMakeVisible(*s);
    addAndMakeVisible(*l);
    attachments[(size_t) index] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.apvts, paramId, *s);

    // The attachment installs its own text callbacks, so install ours after it.
    if (percent)
    {
        s->setTextValueSuffix("");
        s->textFromValueFunction = [] (double v)
        {
            return juce::String(juce::roundToInt(v * 100.0)) + "%";
        };
        s->valueFromTextFunction = [] (const juce::String& t)
        {
            return t.retainCharacters("0123456789.-").getDoubleValue() / 100.0;
        };
    }
    else if (paramId == "detune")
    {
        s->textFromValueFunction = [] (double v) { return juce::String(v, 2); };
    }
    else if (paramId == "release")
    {
        s->textFromValueFunction = [] (double v) { return juce::String(v, 2); };
    }
    else if (paramId == "master")
    {
        s->textFromValueFunction = [] (double v) { return juce::String(v, 1); };
    }
    else if (paramId == "tuning")
    {
        s->setTextValueSuffix("");
        s->textFromValueFunction = [] (double v) { return juce::String(juce::roundToInt(v)) + " Hz"; };
        s->valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    }

    s->updateText();
    knobs[(size_t) index] = std::move(s);
    knobLabels[(size_t) index] = std::move(l);
}

void CordaAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(c(background));

    // Warm light from the "lid" and a quiet falloff.
    juce::ColourGradient glow(c(0xff4b2d17).withAlpha(0.45f), getWidth() * 0.5f, 0.0f,
                              juce::Colours::transparentBlack, getWidth() * 0.5f, getHeight() * 0.8f, true);
    g.setGradientFill(glow);
    g.fillRect(getLocalBounds());

    // Strings behind the panel.
    g.setColour(c(gold).withAlpha(0.035f));
    for (int i = 0; i < 28; ++i)
    {
        const float x = 30.0f + (float) i * (getWidth() - 60.0f) / 27.0f;
        g.drawLine(x, 90.0f, x + 30.0f, (float) getHeight(), 0.7f);
    }

    // Vignette.
    juce::ColourGradient vig(juce::Colours::transparentBlack, getWidth() * 0.5f, getHeight() * 0.45f,
                             juce::Colours::black.withAlpha(0.5f), 0.0f, (float) getHeight(), true);
    g.setGradientFill(vig);
    g.fillRect(getLocalBounds());

    drawHeader(g);
    drawPanels(g);

    // Output meter.
    const auto meterRect = juce::Rectangle<float>(34.0f, 414.0f, 168.0f, 5.0f);
    g.setColour(c(0xff2a2119));
    g.fillRoundedRectangle(meterRect, 2.0f);
    const float m = juce::jlimit(0.0f, 1.0f, displayedMeter);
    if (m > 0.001f)
    {
        juce::ColourGradient mg(c(0xff7fae73), meterRect.getX(), 0.0f,
                                c(0xffd6b16b), meterRect.getRight(), 0.0f, false);
        mg.addColour(0.72, c(0xffd98045));
        mg.addColour(1.0, c(0xffcf5b3e));
        g.setGradientFill(mg);
        g.fillRoundedRectangle(meterRect.withWidth(meterRect.getWidth() * m), 2.0f);
    }

    // A tiny gold rule under the header.
    g.setColour(c(gold).withAlpha(0.35f));
    g.drawLine(30.0f, 88.0f, (float) getWidth() - 30.0f, 88.0f, 0.8f);
}

void CordaAudioProcessorEditor::drawHeader(juce::Graphics& g)
{
    g.setColour(c(gold).withAlpha(0.7f));
    g.fillRect(30.0f, 18.0f, 3.0f, 46.0f);

}

void CordaAudioProcessorEditor::drawPanels(juce::Graphics& g)
{
    auto panelRect = juce::Rectangle<float>(24.0f, 98.0f, (float) getWidth() - 48.0f, 300.0f);
    g.setColour(c(panel).withAlpha(0.72f));
    g.fillRoundedRectangle(panelRect, 7.0f);
    g.setColour(c(panelLine).withAlpha(0.8f));
    g.drawRoundedRectangle(panelRect.reduced(0.5f), 7.0f, 1.0f);

    auto kbRect = juce::Rectangle<float>(24.0f, 440.0f, (float) getWidth() - 48.0f, 126.0f);
    g.setColour(c(0xff0d0b09).withAlpha(0.85f));
    g.fillRoundedRectangle(kbRect, 7.0f);
    g.setColour(c(panelLine).withAlpha(0.7f));
    g.drawRoundedRectangle(kbRect.reduced(0.5f), 7.0f, 1.0f);

    g.setColour(c(gold).withAlpha(0.18f));
    g.drawLine(44.0f, 112.0f, (float) getWidth() - 44.0f, 112.0f, 0.6f);
}

void CordaAudioProcessorEditor::resized()
{
    if (! uiReady)
        return;

    title.setBounds(44, 12, 320, 50);
    subtitle.setBounds(46, 58, 320, 16);
    edition.setBounds(470, 62, (float) getWidth() - 500, 14);
    modelLabel.setBounds(714, 18, 170, 14);
    modelBox.setBounds(714, 34, 170, 26);

    const int cellW = 122;
    const int topY = 114;
    const int bottomY = 252;

    for (int i = 0; i < 7; ++i)
    {
        const int cx = 33 + i * cellW;
        knobLabels[(size_t) i]->setBounds(cx, topY, cellW, 16);
        knobs[(size_t) i]->setBounds(cx + 18, topY + 20, 86, 92);
    }

    const int startBottom = 94;
    for (int i = 0; i < 6; ++i)
    {
        const int idx = 7 + i;
        const int cx = startBottom + i * cellW;
        knobLabels[(size_t) idx]->setBounds(cx, bottomY, cellW, 16);
        knobs[(size_t) idx]->setBounds(cx + 18, bottomY + 20, 86, 92);
    }

    statusLeft.setBounds(34, 400, 380, 16);
    statusRight.setBounds(getWidth() - 434, 400, 400, 16);
    keyboard.setBounds(30, 450, getWidth() - 60, 108);
}

void CordaAudioProcessorEditor::timerCallback()
{
    const float target = juce::jlimit(0.0f, 1.0f, processor.outputMeter.load(std::memory_order_relaxed));
    displayedMeter = std::max(target, displayedMeter * 0.86f);
    const int voices = processor.activeVoiceCount.load(std::memory_order_relaxed);
    const int note = processor.lastNote.load(std::memory_order_relaxed);

    if (std::abs(displayedMeter - target) > 0.004f || voices != displayedVoices || note != displayedNote)
    {
        displayedVoices = voices;
        displayedNote = note;
        statusLeft.setText("VOICES  " + juce::String(voices).paddedLeft('0', 2)
                           + " / 48    OUTPUT  " + (target > 0.0001f
                               ? juce::String(juce::Decibels::gainToDecibels(target), 1) + " dB"
                               : juce::String("-inf dB")),
                           juce::dontSendNotification);
        statusRight.setText("A4  " + juce::String(processor.apvts.getRawParameterValue("tuning")->load(), 1)
                            + " Hz    ·    MIDI 21-108    |    CLICK KEYS TO PLAY",
                            juce::dontSendNotification);
        keyboard.repaint();
        repaint(28, 396, getWidth() - 56, 26);
    }
}

} // namespace corda
