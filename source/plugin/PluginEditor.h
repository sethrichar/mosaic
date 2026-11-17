#pragma once

#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PluginProcessor.h"
#include "ui/SlotRow.h"

namespace mosaic
{

/// M3 placeholder editor: global controls, 8 slot rows with drop targets, memory readout,
/// the M1 probe readout, and a transport for the Standalone build. Functional, not pretty
/// (the real UI is M7).
class MosaicAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                         public juce::FileDragAndDropTarget,
                                         private juce::Timer
{
public:
    explicit MosaicAudioProcessorEditor (MosaicAudioProcessor&);
    ~MosaicAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int x, int y) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct ParamCombo { juce::Label label; juce::ComboBox box; std::unique_ptr<ComboAttachment> attachment; };
    struct ParamSlider { juce::Label label; juce::Slider slider; std::unique_ptr<SliderAttachment> attachment; };

    void addCombo (ParamCombo&, const char* paramId, const juce::String& text);
    void addSlider (ParamSlider&, const char* paramId, const juce::String& text);
    int  slotAt (int y) const;
    void highlightSlot (int slot);
    void openFileChooser (int slot);
    void timerCallback() override;

    MosaicAudioProcessor& mosaicProcessor;

    // Global controls (SPEC 4.7)
    ParamCombo rate, mode, position, patternLength;
    ParamSlider fade, tileLength, seed, outputGain;
    juce::ToggleButton avoidRepeats { "Avoid repeats" };
    std::unique_ptr<ButtonAttachment> avoidRepeatsAtt;
    juce::TextButton reroll { "Reroll" };

    // M4 global key (SPEC 7.2)
    ParamCombo keyMode, keyRoot, keyScale;
    juce::Label keyReadout;

    // Slots
    std::unique_ptr<ui::SlotRow> rows[MosaicAudioProcessor::kNumSlots];
    juce::Rectangle<int> slotArea;
    int highlighted = -1;
    std::unique_ptr<juce::FileChooser> chooser;

    // Bottom strip
    juce::Label memoryLabel, statusLabel;
    ParamCombo role, probeSource, lookBehind;
    juce::TextButton resetStats { "Reset stats" };

    // Standalone transport
    juce::TextButton playButton { "Play" }, rewindButton { "Rewind" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MosaicAudioProcessorEditor)
};

} // namespace mosaic
