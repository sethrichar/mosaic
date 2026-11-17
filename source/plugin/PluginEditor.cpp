#include "plugin/PluginEditor.h"
#include "engine/EngineInfo.h"

namespace mosaic
{

MosaicAudioProcessorEditor::MosaicAudioProcessorEditor (MosaicAudioProcessor& p)
    : AudioProcessorEditor (&p)
{
    title.setText ("Mosaic", juce::dontSendNotification);
    title.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    subtitle.setText (juce::String ("M0 skeleton  ·  v") + MOSAIC_VERSION_STRING
                          + "  ·  engine " + engine::versionString(),
                      juce::dontSendNotification);
    subtitle.setJustificationType (juce::Justification::centred);
    subtitle.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (subtitle);

    setResizable (true, true);
    setResizeLimits (320, 160, 1600, 1000);
    setSize (480, 240);
}

void MosaicAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
}

void MosaicAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (16);
    title.setBounds (area.removeFromTop (area.getHeight() / 2));
    subtitle.setBounds (area);
}

} // namespace mosaic
