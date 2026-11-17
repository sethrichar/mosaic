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

    // --- key row ---
    keyBadge.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    keyBadge.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (keyBadge);

    sourceKeyBox.addItem ("Detected", 1);
    for (int i = 0; i < 24; ++i)
        sourceKeyBox.addItem (analysis::Key::fromIndex (i).name(), i + 2);
    sourceKeyBox.setSelectedId (1, juce::dontSendNotification);
    sourceKeyBox.setTooltip ("Source key: detected, or set it yourself");
    sourceKeyBox.onChange = [this]
    {
        if (updatingSourceKeyBox) return;
        const int id = sourceKeyBox.getSelectedId();
        if (id <= 1) processor.clearSlotKeyOverride (slot);
        else         processor.setSlotKeyOverride (slot, analysis::Key::fromIndex (id - 2));
    };
    addAndMakeVisible (sourceKeyBox);

    auto fillCombo = [this] (juce::ComboBox& box, const char* suffix, std::unique_ptr<ComboAttachment>& att)
    {
        const auto paramId = params::slotParam (slot, suffix);
        if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processor.state().getParameter (paramId)))
            box.addItemList (choiceParam->choices, 1);
        addAndMakeVisible (box);
        att = std::make_unique<ComboAttachment> (processor.state(), paramId, box);
    };
    fillCombo (keyModeBox, params::slotKeyModeSuffix, keyModeAtt);
    fillCombo (directionBox, params::slotDirectionSuffix, directionAtt);

    setupSlider (transpose, transposeLabel, "Transp", params::slotTransposeSuffix);
    setupSlider (fine, fineLabel, "Fine", params::slotFineSuffix);
    transposeAtt = std::make_unique<SliderAttachment> (processor.state(), params::slotParam (slot, params::slotTransposeSuffix), transpose);
    fineAtt      = std::make_unique<SliderAttachment> (processor.state(), params::slotParam (slot, params::slotFineSuffix), fine);

    shiftReadout.setFont (juce::FontOptions (12.0f));
    shiftReadout.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (shiftReadout);
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
    const int h3 = ctl.getHeight() / 3;
    auto row1 = ctl.removeFromTop (h3);
    auto row2 = ctl.removeFromTop (h3);
    auto row3 = ctl;
    gainLabel.setBounds (row1.removeFromLeft (44));   gain.setBounds (row1.removeFromLeft (170));
    row1.removeFromLeft (6);
    mute.setBounds (row1.removeFromLeft (60));
    startOffsetLabel.setBounds (row1.removeFromLeft (48)); startOffset.setBounds (row1.removeFromLeft (150));
    weightLabel.setBounds (row2.removeFromLeft (44)); weight.setBounds (row2.removeFromLeft (170));
    row2.removeFromLeft (6);
    loop.setBounds (row2.removeFromLeft (60));
    keyBadge.setBounds (row2.removeFromLeft (120));
    sourceKeyBox.setBounds (row2.removeFromLeft (90).reduced (0, 2));
    keyModeBox.setBounds (row3.removeFromLeft (84).reduced (0, 2));
    row3.removeFromLeft (4);
    directionBox.setBounds (row3.removeFromLeft (84).reduced (0, 2));
    transposeLabel.setBounds (row3.removeFromLeft (46)); transpose.setBounds (row3.removeFromLeft (120));
    fineLabel.setBounds (row3.removeFromLeft (34));      fine.setBounds (row3.removeFromLeft (120));
    shiftReadout.setBounds (row3);
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
    else if (info.analyzing)     status = "loading + detecting key...";
    else if (info.loading)       status = "loading...";
    else if (info.rendering)     status = "rendering " + juce::String (juce::roundToInt (processor.renderProgress (slot) * 100.0f)) + "%";
    else if (info.error.isNotEmpty()) status = info.error;
    else if (! info.hasFile())   status = "drop a file here, or Load";
    waveform.setStatus (status);

    // Key badge: detected key with confidence, dimmed when low; override shown as such.
    juce::String badge;
    juce::Colour badgeColour = juce::Colours::lightgrey;
    if (info.hasOverride)
    {
        badge = info.overrideKey.name() + " (set)";
        badgeColour = accentFor (slot);
    }
    else if (info.keyDetected)
    {
        if (! info.key.found) { badge = "no key"; badgeColour = juce::Colours::grey; }
        else
        {
            badge = info.key.key.name() + (info.key.level == analysis::Confidence::High ? "" : " ?");
            badgeColour = info.key.level == analysis::Confidence::High ? juce::Colours::white : juce::Colours::grey;
        }
    }
    else if (info.hasFile() && ! info.missing)
        badge = "...";
    if (badge != lastBadge)
    {
        lastBadge = badge;
        keyBadge.setText (badge, juce::dontSendNotification);
        keyBadge.setColour (juce::Label::textColourId, badgeColour);
        updatingSourceKeyBox = true;
        sourceKeyBox.setSelectedId (info.hasOverride ? info.overrideKey.toIndex() + 2 : 1, juce::dontSendNotification);
        updatingSourceKeyBox = false;
    }

    juce::String shift;
    if (info.hasFile() && ! info.missing)
    {
        shift = juce::String (info.desiredShift, 2) + " st";
        if (std::abs (info.desiredShift - info.renderedShift) > 1e-6) shift += "  (pending)";
    }
    if (shift != lastShift) { lastShift = shift; shiftReadout.setText (shift, juce::dontSendNotification); }

    const int64_t pos = t.slotPlayPos[slot].load();
    waveform.setPlayhead (pos >= 0 && info.lengthSamples > 0 ? (double) pos / (double) info.lengthSamples : -1.0);

    const bool nowActive = t.activeSlot.load() == slot && t.isPlaying.load();
    if (nowActive != active) { active = nowActive; repaint(); }
}

} // namespace mosaic::ui
