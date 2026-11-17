#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "plugin/PluginProcessor.h"

namespace mosaic
{

/// M1 probe UI. Deliberately plain: parameter combos, 16 meters, a text readout.
/// The real UI is M7.
class MosaicAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit MosaicAudioProcessorEditor (MosaicAudioProcessor&);
    ~MosaicAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct ParamCombo
    {
        juce::Label label;
        juce::ComboBox box;
        std::unique_ptr<ComboAttachment> attachment;
    };

    void addCombo (ParamCombo&, const char* paramId, const juce::String& text);
    void timerCallback() override;
    void drawMeters (juce::Graphics&, juce::Rectangle<int>);

    MosaicAudioProcessor& mosaicProcessor;

    ParamCombo role, group, sendNumber, probeSource, lookBehind;
    juce::ToggleButton muteOutput { "Send: mute output" };
    std::unique_ptr<ButtonAttachment> muteAttachment;
    juce::TextButton resetStats { "Reset stats" };
    juce::TextEditor readout;

    juce::Rectangle<int> meterArea;
    float inputMeter[MosaicAudioProcessor::kNumInputBuses] {};
    float sendMeter[hub::kNumSends] {};
    float sendRoleMeter = 0.0f;

    uint64_t lastBlockCounter = 0;
    double lastBlockTimeMs = 0.0;
    double blocksPerSecond = 0.0;
    double lastBlockSeenMs = 0.0;
    uint64_t lastCollisionCount = 0;
    double lastCollisionMs = -1e9;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicAudioProcessorEditor)
};

} // namespace mosaic
