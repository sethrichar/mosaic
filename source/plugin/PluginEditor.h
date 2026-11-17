#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "plugin/PluginProcessor.h"

namespace mosaic
{

/// M0 placeholder editor. Real UI is M7; do not polish before then.
class MosaicAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit MosaicAudioProcessorEditor (MosaicAudioProcessor&);
    ~MosaicAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    juce::Label title, subtitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicAudioProcessorEditor)
};

} // namespace mosaic
