#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugin/PluginProcessor.h"
#include "ui/WaveformView.h"

namespace mosaic::ui
{

/// One slot: file, waveform, active light and the per-slot controls (SPEC 4.6).
/// Placeholder layout; the designed UI is M7.
class SlotRow final : public juce::Component
{
public:
    SlotRow (MosaicAudioProcessor&, int slotIndex, std::function<void (int)> onLoad, std::function<void (int)> onClear);

    void resized() override;
    void paint (juce::Graphics&) override;

    /// Timer-driven refresh from the processor's message-thread info and telemetry.
    void refresh();
    void setDragHighlight (bool on) { if (highlight != on) { highlight = on; repaint(); } }

    static juce::Colour accentFor (int slot);

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void setupSlider (juce::Slider&, juce::Label&, const juce::String& text, const char* suffix);

    MosaicAudioProcessor& processor;
    const int slot;

    juce::Label number, name;
    juce::TextButton loadButton { "Load" }, clearButton { "Clear" };
    WaveformView waveform;
    juce::Slider gain, weight, startOffset;
    juce::Label gainLabel, weightLabel, startOffsetLabel;
    juce::ToggleButton mute { "Mute" }, loop { "Loop" };
    std::unique_ptr<SliderAttachment> gainAtt, weightAtt, startOffsetAtt;
    std::unique_ptr<ButtonAttachment> muteAtt, loopAtt;

    bool active = false, highlight = false;
    juce::String lastName;
};

} // namespace mosaic::ui
