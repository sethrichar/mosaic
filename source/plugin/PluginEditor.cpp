#include "plugin/PluginEditor.h"
#include "engine/EngineInfo.h"

namespace mosaic
{

MosaicAudioProcessorEditor::MosaicAudioProcessorEditor (MosaicAudioProcessor& p)
    : AudioProcessorEditor (&p), mosaicProcessor (p)
{
    addCombo (rate, params::rate, "Rate");
    addCombo (mode, params::mode, "Mode");
    addCombo (position, params::position, "Position");
    addCombo (patternLength, params::patternLength, "Pattern");
    addSlider (fade, params::fade, "Fade");
    addSlider (tileLength, params::tileLength, "Tile len");
    addSlider (seed, params::seed, "Seed");
    addSlider (outputGain, params::outputGain, "Output");

    addAndMakeVisible (avoidRepeats);
    avoidRepeatsAtt = std::make_unique<ButtonAttachment> (mosaicProcessor.state(), params::avoidRepeats, avoidRepeats);
    reroll.onClick = [this] { mosaicProcessor.reroll(); };
    addAndMakeVisible (reroll);

    for (int s = 0; s < MosaicAudioProcessor::kNumSlots; ++s)
    {
        rows[s] = std::make_unique<ui::SlotRow> (mosaicProcessor, s,
            [this] (int slot) { openFileChooser (slot); },
            [this] (int slot) { mosaicProcessor.clearSlot (slot); });
        addAndMakeVisible (*rows[s]);
    }

    memoryLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (memoryLabel);
    statusLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (statusLabel);

    addCombo (role, params::role, "Role");
    addCombo (probeSource, params::probeSource, "Probe");
    addCombo (lookBehind, params::probeLookBehind, "Look-behind");
    resetStats.onClick = [this] { mosaicProcessor.telemetry().resetStatsRequest.store (true); };
    addAndMakeVisible (resetStats);

    if (mosaicProcessor.usesStandaloneTransport())
    {
        auto& tr = mosaicProcessor.standaloneTransport();
        playButton.setClickingTogglesState (true);
        playButton.onClick = [this, &tr] { tr.playing.store (playButton.getToggleState()); playButton.setButtonText (playButton.getToggleState() ? "Stop" : "Play"); };
        rewindButton.onClick = [&tr] { tr.rewind(); };
        bpmSlider.setRange (40.0, 240.0, 0.1);
        bpmSlider.setValue (tr.bpm.load(), juce::dontSendNotification);
        bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 60, 18);
        bpmSlider.onValueChange = [this, &tr] { tr.bpm.store (bpmSlider.getValue()); };
        bpmLabel.setText ("BPM", juce::dontSendNotification);
        addAndMakeVisible (playButton);
        addAndMakeVisible (rewindButton);
        addAndMakeVisible (bpmSlider);
        addAndMakeVisible (bpmLabel);
    }

    setResizable (true, true);
    setResizeLimits (900, 640, 2400, 1600);
    setSize (1040, 760);
    startTimerHz (30);
}

MosaicAudioProcessorEditor::~MosaicAudioProcessorEditor()
{
    stopTimer();
}

void MosaicAudioProcessorEditor::addCombo (ParamCombo& c, const char* paramId, const juce::String& text)
{
    c.label.setText (text, juce::dontSendNotification);
    c.label.setFont (juce::FontOptions (12.0f));
    c.label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (c.label);
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (mosaicProcessor.state().getParameter (paramId)))
        c.box.addItemList (choiceParam->choices, 1);
    addAndMakeVisible (c.box);
    c.attachment = std::make_unique<ComboAttachment> (mosaicProcessor.state(), paramId, c.box);
}

void MosaicAudioProcessorEditor::addSlider (ParamSlider& s, const char* paramId, const juce::String& text)
{
    s.label.setText (text, juce::dontSendNotification);
    s.label.setFont (juce::FontOptions (12.0f));
    s.label.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (s.label);
    s.slider.setSliderStyle (juce::Slider::LinearHorizontal);
    s.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    addAndMakeVisible (s.slider);
    s.attachment = std::make_unique<SliderAttachment> (mosaicProcessor.state(), paramId, s.slider);
}

void MosaicAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1a1a1a));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (juce::String ("Mosaic  v") + MOSAIC_VERSION_STRING + "  (M3 placeholder UI)",
                getLocalBounds().removeFromTop (28).reduced (12, 0), juce::Justification::centredLeft);
}

void MosaicAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (28);

    auto placeCombo = [] (juce::Rectangle<int>& row, ParamCombo& c, int labelW, int boxW)
    {
        c.label.setBounds (row.removeFromLeft (labelW));
        c.box.setBounds (row.removeFromLeft (boxW).reduced (0, 2));
        row.removeFromLeft (8);
    };
    auto placeSlider = [] (juce::Rectangle<int>& row, ParamSlider& s, int labelW, int w)
    {
        s.label.setBounds (row.removeFromLeft (labelW));
        s.slider.setBounds (row.removeFromLeft (w));
        row.removeFromLeft (8);
    };

    auto row = area.removeFromTop (26);
    placeCombo (row, rate, 40, 80);
    placeCombo (row, mode, 44, 100);
    placeCombo (row, position, 60, 90);
    placeCombo (row, patternLength, 56, 90);
    avoidRepeats.setBounds (row.removeFromLeft (120));
    placeSlider (row, seed, 40, 170);
    reroll.setBounds (row.removeFromLeft (70).reduced (0, 2));

    area.removeFromTop (4);
    row = area.removeFromTop (26);
    placeSlider (row, fade, 40, 180);
    placeSlider (row, tileLength, 56, 180);
    placeSlider (row, outputGain, 50, 180);
    if (mosaicProcessor.usesStandaloneTransport())
    {
        row.removeFromLeft (10);
        playButton.setBounds (row.removeFromLeft (60).reduced (0, 2));
        row.removeFromLeft (4);
        rewindButton.setBounds (row.removeFromLeft (64).reduced (0, 2));
        bpmLabel.setBounds (row.removeFromLeft (36));
        bpmSlider.setBounds (row.removeFromLeft (160));
    }

    area.removeFromTop (8);
    auto bottom = area.removeFromBottom (48);
    slotArea = area;

    const int rowH = slotArea.getHeight() / MosaicAudioProcessor::kNumSlots;
    auto slotsRect = slotArea;
    for (auto& r : rows)
        r->setBounds (slotsRect.removeFromTop (rowH).reduced (0, 2));

    auto b1 = bottom.removeFromTop (24);
    memoryLabel.setBounds (b1.removeFromLeft (300));
    placeCombo (b1, role, 36, 80);
    placeCombo (b1, probeSource, 44, 90);
    placeCombo (b1, lookBehind, 76, 80);
    resetStats.setBounds (b1.removeFromLeft (90).reduced (0, 2));
    statusLabel.setBounds (bottom);
}

int MosaicAudioProcessorEditor::slotAt (int y) const
{
    if (! slotArea.contains (slotArea.getX(), y)) return -1;
    const int rowH = juce::jmax (1, slotArea.getHeight() / MosaicAudioProcessor::kNumSlots);
    return juce::jlimit (0, MosaicAudioProcessor::kNumSlots - 1, (y - slotArea.getY()) / rowH);
}

void MosaicAudioProcessorEditor::highlightSlot (int slot)
{
    if (slot == highlighted) return;
    highlighted = slot;
    for (int s = 0; s < MosaicAudioProcessor::kNumSlots; ++s)
        rows[s]->setDragHighlight (s == slot);
}

bool MosaicAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() > 0;
}

void MosaicAudioProcessorEditor::fileDragEnter (const juce::StringArray&, int, int y) { highlightSlot (slotAt (y)); }
void MosaicAudioProcessorEditor::fileDragMove  (const juce::StringArray&, int, int y) { highlightSlot (slotAt (y)); }
void MosaicAudioProcessorEditor::fileDragExit  (const juce::StringArray&)             { highlightSlot (-1); }

void MosaicAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int y)
{
    highlightSlot (-1);
    int slot = slotAt (y);
    if (slot < 0) return;
    // Several files dropped at once fill consecutive slots.
    for (const auto& path : files)
    {
        if (slot >= MosaicAudioProcessor::kNumSlots) break;
        mosaicProcessor.loadFile (slot++, juce::File (path));
    }
}

void MosaicAudioProcessorEditor::openFileChooser (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load audio into slot " + juce::String (slot + 1),
                                                   juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, slot] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f.existsAsFile())
                mosaicProcessor.loadFile (slot, f);
        });
}

void MosaicAudioProcessorEditor::timerCallback()
{
    for (auto& r : rows) r->refresh();

    const auto& t = mosaicProcessor.telemetry();
    const double mb = (double) mosaicProcessor.totalFileBytes() / (1024.0 * 1024.0);
    juce::String mem = "Files in RAM: " + juce::String (mb, 1) + " MB";
    if (mosaicProcessor.totalFileBytes() > MosaicAudioProcessor::kMemoryWarningBytes)
        mem += "   *** over 1 GB ***";
    memoryLabel.setText (mem, juce::dontSendNotification);
    memoryLabel.setColour (juce::Label::textColourId,
        mosaicProcessor.totalFileBytes() > MosaicAudioProcessor::kMemoryWarningBytes ? juce::Colours::orange : juce::Colours::lightgrey);

    juce::String s;
    s << (t.hasPlayhead.load() ? "" : "no playhead  ")
      << (t.isPlaying.load() ? "PLAYING" : "stopped")
      << "  step " << t.stepIndex.load()
      << "  slot " << (t.activeSlot.load() >= 0 ? juce::String (t.activeSlot.load() + 1) : "-")
      << "  ppq " << (t.hasPpq.load() ? juce::String (t.ppq.load(), 2) : "n/a")
      << "  bpm " << (t.hasBpm.load() ? juce::String (t.bpm.load(), 1) : "n/a")
      << "  sr " << t.sampleRate.load() << "  block " << t.blockSize.load()
      << "  " << (t.engineActive.load() ? "engine" : (mosaicProcessor.state().getRawParameterValue (params::role)->load() > 0.5f ? "SEND role" : "probe"))
      << "  latency " << mosaicProcessor.getLatencySamples();
    if (! t.engineActive.load() && t.sendReads.load() > 0)
        s << "  underruns " << t.sendUnderruns.load() << "  need " << t.neededLookBehind.load();
    statusLabel.setText (s, juce::dontSendNotification);
}

} // namespace mosaic
