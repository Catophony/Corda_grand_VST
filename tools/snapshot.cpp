#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_gui_extra/juce_gui_extra.h>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String out = argc > 1 ? juce::String (argv[1]) : juce::String ("build/ui.png");

    corda::CordaAudioProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
        return 1;

    editor->setBounds (0, 0, 920, 590);
    editor->resized();
    juce::Image image (juce::Image::ARGB, 920, 590, true);
    {
        juce::Graphics g (image);
        editor->paintEntireComponent (g, true);
    }

    juce::File file (juce::File::getCurrentWorkingDirectory().getChildFile (out));
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (! stream.openedOk())
        return 2;

    juce::PNGImageFormat png;
    if (! png.writeImageToStream (image, stream))
        return 3;

    std::printf ("wrote %s\n", file.getFullPathName().toRawUTF8());
    return 0;
}
