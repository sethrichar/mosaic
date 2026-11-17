#include "plugin/PluginEditor.h"
#include "engine/EngineInfo.h"

namespace mosaic
{

MosaicAudioProcessorEditor::MosaicAudioProcessorEditor (MosaicAudioProcessor& p)
    : AudioProcessorEditor (&p), mosaicProcessor (p)
{
    addCombo (role,        params::role,            "Role");
    addCombo (group,       params::sendGroup,       "Group");
    addCombo (sendNumber,  params::sendNumber,      "Send #");
    addCombo (probeSource, params::probeSource,     "Probe source");
    addCombo (lookBehind,  params::probeLookBehind, "Look-behind");

    addAndMakeVisible (muteOutput);
    muteAttachment = std::make_unique<ButtonAttachment> (mosaicProcessor.state(), params::sendMuteOutput, muteOutput);

    addAndMakeVisible (resetStats);
    resetStats.onClick = [this] { mosaicProcessor.telemetry().resetStatsRequest.store (true); };

    readout.setMultiLine (true);
    readout.setReadOnly (true);
    readout.setCaretVisible (false);
    readout.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    readout.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff151515));
    addAndMakeVisible (readout);

    setResizable (true, true);
    setResizeLimits (640, 480, 2000, 1400);
    setSize (760, 560);
    startTimerHz (30);
}

MosaicAudioProcessorEditor::~MosaicAudioProcessorEditor()
{
    stopTimer();
}

void MosaicAudioProcessorEditor::addCombo (ParamCombo& c, const char* paramId, const juce::String& text)
{
    c.label.setText (text, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (c.label);

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (mosaicProcessor.state().getParameter (paramId)))
        c.box.addItemList (choiceParam->choices, 1);

    addAndMakeVisible (c.box);
    c.attachment = std::make_unique<ComboAttachment> (mosaicProcessor.state(), paramId, c.box);
}

void MosaicAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e1e));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText (juce::String ("Mosaic routing probe  v") + MOSAIC_VERSION_STRING,
                getLocalBounds().removeFromTop (30).reduced (12, 0), juce::Justification::centredLeft);
    drawMeters (g, meterArea);
}

void MosaicAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (30);

    auto row = area.removeFromTop (26);
    auto place = [&row] (ParamCombo& c, int labelW, int boxW)
    {
        c.label.setBounds (row.removeFromLeft (labelW));
        c.box.setBounds (row.removeFromLeft (boxW).reduced (0, 2));
        row.removeFromLeft (8);
    };
    place (role, 40, 90);
    place (group, 48, 60);
    place (sendNumber, 52, 90);
    muteOutput.setBounds (row.removeFromLeft (170));

    area.removeFromTop (6);
    row = area.removeFromTop (26);
    place (probeSource, 90, 110);
    place (lookBehind, 84, 90);
    resetStats.setBounds (row.removeFromLeft (100).reduced (0, 2));

    area.removeFromTop (10);
    meterArea = area.removeFromTop (150);
    area.removeFromTop (10);
    readout.setBounds (area);
}

void MosaicAudioProcessorEditor::drawMeters (juce::Graphics& g, juce::Rectangle<int> r)
{
    auto drawBank = [&g] (juce::Rectangle<int> bank, const juce::String& title, const float* values,
                          const bool* enabled, int count)
    {
        g.setColour (juce::Colours::lightgrey);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (title, bank.removeFromTop (18), juce::Justification::centredLeft);
        const int w = bank.getWidth() / count;
        for (int i = 0; i < count; ++i)
        {
            auto cell = bank.removeFromLeft (w).reduced (3, 0);
            auto labelArea = cell.removeFromBottom (16);
            const bool on = enabled == nullptr || enabled[i];
            g.setColour (on ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff202020));
            g.fillRect (cell);
            if (on)
            {
                const float db = juce::Decibels::gainToDecibels (values[i], -60.0f);
                const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
                auto bar = cell.withTop (cell.getBottom() - juce::roundToInt (frac * (float) cell.getHeight()));
                g.setColour (db > -0.5f ? juce::Colours::red : juce::Colour (0xff4caf50));
                g.fillRect (bar);
            }
            g.setColour (on ? juce::Colours::lightgrey : juce::Colours::darkgrey);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (i + 1), labelArea, juce::Justification::centred);
        }
    };

    const auto& t = mosaicProcessor.telemetry();
    bool enabled[MosaicAudioProcessor::kNumInputBuses];
    for (int i = 0; i < MosaicAudioProcessor::kNumInputBuses; ++i)
        enabled[i] = t.inputEnabled[i].load();

    auto left = r.removeFromLeft (r.getWidth() / 2).reduced (0, 0);
    r.removeFromLeft (16);
    drawBank (left, "Native input buses (Input 1-8)", inputMeter, enabled, MosaicAudioProcessor::kNumInputBuses);

    const bool isSend = role.box.getSelectedItemIndex() == 1;
    if (isSend)
    {
        float v[1] = { sendRoleMeter };
        drawBank (r.removeFromLeft (60), "This Send", v, nullptr, 1);
    }
    else
        drawBank (r, "Sends in this group (Send 1-8)", sendMeter, nullptr, hub::kNumSends);
}

void MosaicAudioProcessorEditor::timerCallback()
{
    const auto& t = mosaicProcessor.telemetry();
    const double nowMs = juce::Time::getMillisecondCounterHiRes();

    auto smooth = [] (float& m, float v) { m = v > m ? v : m * 0.85f; };
    for (int i = 0; i < MosaicAudioProcessor::kNumInputBuses; ++i) smooth (inputMeter[i], t.inputPeak[i].load());
    for (int i = 0; i < hub::kNumSends; ++i)                       smooth (sendMeter[i], t.sendPeak[i].load());
    smooth (sendRoleMeter, t.sendRolePeak.load());

    // processBlock call rate — the "is the host still calling us?" readout (M1 task 6).
    const uint64_t blocks = t.blockCounter.load();
    if (blocks != lastBlockCounter)
        lastBlockSeenMs = nowMs;
    if (nowMs - lastBlockTimeMs >= 500.0)
    {
        blocksPerSecond = (double) (blocks - lastBlockCounter) * 1000.0 / juce::jmax (1.0, nowMs - lastBlockTimeMs);
        lastBlockCounter = blocks;
        lastBlockTimeMs = nowMs;
    }
    const bool beingCalled = (nowMs - lastBlockSeenMs) < 500.0;

    const uint64_t collisions = t.collisionCount.load();
    if (collisions != lastCollisionCount) { lastCollisionCount = collisions; lastCollisionMs = nowMs; }
    const bool collidingNow = (nowMs - lastCollisionMs) < 2000.0;

    const bool isSend = role.box.getSelectedItemIndex() == 1;
    const auto groupName = juce::String::charToString ((juce::juce_wchar) ('A' + group.box.getSelectedItemIndex()));
    const auto sendName  = juce::String (sendNumber.box.getSelectedItemIndex() + 1);

    juce::String s;
    s << "Role: " << (isSend ? "SEND" : "MOSAIC") << "   Group " << groupName << "   Send " << sendName
      << "   instance id " << mosaicProcessor.instanceId() << "   engine " << engine::versionString() << "\n\n";

    s << "Host:      sampleRate " << t.sampleRate.load()
      << "   block " << t.blockSize.load() << " (prepared " << t.preparedBlockSize.load()
      << ", max seen " << t.maxBlockSize.load() << ")"
      << "   offline " << (t.isOffline.load() ? "YES" : "no") << "\n";
    s << "processBlock: " << (beingCalled ? "BEING CALLED" : "*** NOT BEING CALLED ***")
      << "   " << juce::String (blocksPerSecond, 1) << " calls/s   total " << blocks << "\n\n";

    s << "Playhead:  " << (t.hasPlayhead.load() ? "present" : "*** MISSING ***")
      << "   playing " << (t.isPlaying.load() ? "YES" : "no")
      << "   timeInSamples " << (t.hasTimeInSamples.load() ? juce::String (t.timeInSamples.load()) : "n/a")
      << "   ppq " << (t.hasPpq.load() ? juce::String (t.ppq.load(), 3) : "n/a")
      << "   bpm " << (t.hasBpm.load() ? juce::String (t.bpm.load(), 2) : "n/a") << "\n\n";

    if (isSend)
    {
        const auto& ch = hub::SendHub::instance().channel (group.box.getSelectedItemIndex(), sendNumber.box.getSelectedItemIndex());
        s << "Send:      writing to Group " << groupName << " / Send " << sendName
          << "   head " << (ch.head() == hub::kNoHead ? juce::String ("none") : juce::String (ch.head()))
          << "   owner id " << ch.owner() << "\n";
        s << "Collisions on this channel: " << collisions
          << (collidingNow ? "   *** ANOTHER SEND IS WRITING TO THIS GROUP/NUMBER ***" : "") << "\n";
    }
    else
    {
        const int src = probeSource.box.getSelectedItemIndex();
        s << "Probe:     source " << probeSource.box.getText()
          << "   look-behind " << lookBehind.box.getText() << " samples"
          << "   reported latency " << mosaicProcessor.getLatencySamples() << " samples\n";
        if (src >= params::probeSourceSend)
        {
            s << "Send skew (head - T):  last " << (t.skewValid.load() ? juce::String (t.skewLast.load()) : "n/a")
              << "   min " << (t.skewValid.load() ? juce::String (t.skewMin.load()) : "n/a")
              << "   max " << (t.skewValid.load() ? juce::String (t.skewMax.load()) : "n/a") << "\n";
            s << "Smallest look-behind with no underruns so far: " << t.neededLookBehind.load()
              << " samples   (block " << t.blockSize.load() << ")\n";
            s << "Send reads " << t.sendReads.load() << "   underruns " << t.sendUnderruns.load()
              << (t.sendUnderruns.load() > 0 ? "   *** UNDERRUNS: look-behind too small or no Send ***" : "") << "\n";
            s << "Collisions on that channel: " << collisions
              << (collidingNow ? "   *** TWO SENDS ON THE SAME GROUP/NUMBER ***" : "") << "\n";
        }
    }

    s << "\nInput buses enabled: ";
    for (int i = 0; i < MosaicAudioProcessor::kNumInputBuses; ++i)
        s << (t.inputEnabled[i].load() ? juce::String (i + 1) : juce::String ("-")) << " ";

    readout.setText (s, false);
    repaint (meterArea);
}

} // namespace mosaic
