#include "ui/SlotRow.h"

namespace mosaic::ui
{

juce::Colour SlotRow::accentFor (int slot)
{
    static const juce::Colour colours[] = {
        juce::Colour (0xffe57373), juce::Colour (0xffffb74d), juce::Colour (0xfffff176), juce::Colour (0xff81c784),
        juce::Colour (0xff4dd0e1), juce::Colour (0xff64b5f6), juce::Colour (0xffba68c8), juce::Colour (0xfff06292)
    };
    return colours[juce::jlimit (0, 7, slot)];
}

SlotRow::SlotRow (MosaicAudioProcessor& p, int slotIndex, std::function<void (int)> onLoad, std::function<void (int)> onClear)
    : processor (p), slot (slotIndex)
{
    number.setText (juce::String (slot + 1), juce::dontSendNotification);
    number.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    number.setColour (juce::Label::textColourId, accentFor (slot));
    number.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (number);

    name.setText ("(empty)", juce::dontSendNotification);
    name.setFont (juce::FontOptions (12.0f));
    name.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (name);

    loadButton.onClick  = [this, onLoad]  { onLoad (slot); };
    clearButton.onClick = [this, onClear] { onClear (slot); };
    addAndMakeVisible (loadButton);
    addAndMakeVisible (clearButton);

    waveform.setAccent (accentFor (slot));
    addAndMakeVisible (waveform);

    setupSlider (gain, gainLabel, "Gain", params::slotGainSuffix);
    setupSlider (weight, weightLabel, "Weight", params::slotWeightSuffix);
    setupSlider (startOffset, startOffsetLabel, "Offset", params::slotStartOffsetSuffix);
    gainAtt        = std::make_unique<SliderAttachment> (processor.state(), params::slotParam (slot, params::slotGainSuffix), gain);
    weightAtt      = std::make_unique<SliderAttachment> (processor.state(), params::slotParam (slot, params::slotWeightSuffix), weight);
    startOffsetAtt = std::make_unique<SliderAttachment> (processor.state(), params::slotParam (slot, params::slotStartOffsetSuffix), startOffset);

    addAndMakeVisible (mute);
    addAndMakeVisible (loop);
    muteAtt = std::make_unique<ButtonAttachment> (processor.state(), params::slotParam (slot, params::slotMuteSuffix), mute);
    loopAtt = std::make_unique<ButtonAttachment> (processor.state(), params::slotParam (slot, params::slotLoopSuffix), loop);
}

void SlotRow::setupSlider (juce::Slider& s, juce::Label& l, const juce::String& text, const char*)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    addAndMakeVisible (s);
    l.setText (text, juce::dontSendNotification);
    l.setFont (juce::FontOptions (11.0f));
    l.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (l);
}

void SlotRow::resized()
{
    auto r = getLocalBounds().reduced (4, 2);
    number.setBounds (r.removeFromLeft (26));
    r.removeFromLeft (4);

    auto fileCol = r.removeFromLeft (150);
    name.setBounds (fileCol.removeFromTop (18));
    auto btns = fileCol.removeFromTop (22);
    loadButton.setBounds (btns.removeFromLeft (70));
    btns.removeFromLeft (4);
    clearButton.setBounds (btns.removeFromLeft (60));
    r.removeFromLeft (6);

    waveform.setBounds (r.removeFromLeft (juce::jmax (120, r.getWidth() - 470)));
    r.removeFromLeft (8);

    auto ctl = r;
    auto row1 = ctl.removeFromTop (ctl.getHeight() / 2);
    auto row2 = ctl;
    gainLabel.setBounds (row1.removeFromLeft (44));   gain.setBounds (row1.removeFromLeft (170));
    row1.removeFromLeft (6);
    mute.setBounds (row1.removeFromLeft (60));
    weightLabel.setBounds (row2.removeFromLeft (44)); weight.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (6);
    loop.setBounds (row2.removeFromLeft (60));
    startOffsetLabel.setBounds (row1.removeFromLeft (48)); startOffset.setBounds (row1.removeFromLeft (150));
}

void SlotRow::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (highlight ? accentFor (slot).withAlpha (0.25f) : juce::Colour (0xff232323));
    g.fillRoundedRectangle (r, 4.0f);
    if (active)
    {
        g.setColour (accentFor (slot));
        g.fillRoundedRectangle (r.withWidth (4.0f), 2.0f);
    }
}

void SlotRow::refresh()
{
    const auto& info = processor.slotInfo (slot);
    const auto& t = processor.telemetry();

    juce::String label = info.hasFile() ? info.file.getFileName() : "(empty)";
    if (label != lastName)
    {
        lastName = label;
        name.setText (label, juce::dontSendNotification);
        name.setTooltip (info.hasFile() ? info.file.getFullPathName() : juce::String());
    }

    waveform.setOverview (info.overview);
    juce::String status;
    if (info.missing)            status = "MISSING - relink: " + info.file.getFileName();
    else if (info.loading)       status = "loading...";
    else if (info.error.isNotEmpty()) status = info.error;
    else if (! info.hasFile())   status = "drop a file here, or Load";
    waveform.setStatus (status);

    const int64_t pos = t.slotPlayPos[slot].load();
    waveform.setPlayhead (pos >= 0 && info.lengthSamples > 0 ? (double) pos / (double) info.lengthSamples : -1.0);

    const bool nowActive = t.activeSlot.load() == slot && t.isPlaying.load();
    if (nowActive != active) { active = nowActive; repaint(); }
}

} // namespace mosaic::ui
