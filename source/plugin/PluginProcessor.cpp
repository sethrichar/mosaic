#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

namespace mosaic
{

namespace
{
    inline int choice (const std::atomic<float>* p) noexcept
    {
        return (int) std::lround (p->load (std::memory_order_relaxed));
    }

    inline void storeMinMax (std::atomic<int64_t>& mn, std::atomic<int64_t>& mx, int64_t v) noexcept
    {
        auto cur = mn.load (std::memory_order_relaxed);
        while (v < cur && ! mn.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
        cur = mx.load (std::memory_order_relaxed);
        while (v > cur && ! mx.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
}

juce::AudioProcessor::BusesProperties MosaicAudioProcessor::makeBuses()
{
    BusesProperties props;
    props.addBus (true, "Input 1", juce::AudioChannelSet::stereo(), true);
    for (int i = 2; i <= kNumInputBuses; ++i)
        props.addBus (true, "Input " + juce::String (i), juce::AudioChannelSet::stereo(), false);
    props.addBus (false, "Output", juce::AudioChannelSet::stereo(), true);
    return props;
}

MosaicAudioProcessor::MosaicAudioProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "MosaicState", params::createLayout()),
      pRole        (apvts.getRawParameterValue (params::role)),
      pGroup       (apvts.getRawParameterValue (params::sendGroup)),
      pSend        (apvts.getRawParameterValue (params::sendNumber)),
      pMute        (apvts.getRawParameterValue (params::sendMuteOutput)),
      pProbeSource (apvts.getRawParameterValue (params::probeSource)),
      pLookBehind  (apvts.getRawParameterValue (params::probeLookBehind)),
      myInstanceId (hub::SendHub::instance().newInstanceId())
{
    apvts.state.setProperty ("stateVersion", kStateVersion, nullptr);
    startTimerHz (20);
}

MosaicAudioProcessor::~MosaicAudioProcessor()
{
    stopTimer();
    if (claimedChannel >= 0)
        hub::SendHub::instance().channelAt (claimedChannel).releaseOwnership (myInstanceId);
}

void MosaicAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    tele.sampleRate.store (sampleRate);
    tele.preparedBlockSize.store (samplesPerBlock);
    tele.maxBlockSize.store (0);
    applyMessageThreadUpdates();
}

void MosaicAudioProcessor::releaseResources() {}

bool MosaicAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    for (const auto& in : layouts.inputBuses)
        if (! (in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()))
            return false;

    return true;
}

void MosaicAudioProcessor::applyMessageThreadUpdates()
{
    auto& hubRef = hub::SendHub::instance();
    const bool isSend = choice (pRole) == 1;
    const int idx = hub::SendHub::index (choice (pGroup), choice (pSend));

    if (isSend)
    {
        auto& ch = hubRef.channelAt (idx);
        if (! ch.isAllocated())
            ch.allocate();

        if (claimedChannel != idx)
        {
            if (claimedChannel >= 0)
                hubRef.channelAt (claimedChannel).releaseOwnership (myInstanceId);
            claimedChannel = idx;
        }
    }
    else if (claimedChannel >= 0)
    {
        hubRef.channelAt (claimedChannel).releaseOwnership (myInstanceId);
        claimedChannel = -1;
    }

    const int desired = tele.desiredLatency.load();
    if (desired != getLatencySamples())
        setLatencySamples (desired);
}

void MosaicAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    tele.blockCounter.fetch_add (1, std::memory_order_relaxed);
    tele.blockSize.store (n);
    if (n > tele.maxBlockSize.load()) tele.maxBlockSize.store (n);
    tele.isOffline.store (isNonRealtime());

    if (tele.resetStatsRequest.exchange (false))
    {
        tele.skewValid.store (false);
        tele.skewMin.store (INT64_MAX); tele.skewMax.store (INT64_MIN);
        tele.neededLookBehind.store (0);
        tele.sendReads.store (0); tele.sendUnderruns.store (0);
        tele.maxBlockSize.store (n);
    }

    // Playhead — never assumed valid.
    bool playing = false, haveTime = false;
    int64_t t = 0;
    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            tele.hasPlayhead.store (true);
            playing = pos->getIsPlaying();
            if (const auto ts = pos->getTimeInSamples()) { haveTime = true; t = *ts; }
            tele.hasTimeInSamples.store (haveTime);
            tele.timeInSamples.store (t);
            if (const auto p = pos->getPpqPosition()) { tele.hasPpq.store (true); tele.ppq.store (*p); } else tele.hasPpq.store (false);
            if (const auto b = pos->getBpm())         { tele.hasBpm.store (true); tele.bpm.store (*b); } else tele.hasBpm.store (false);
        }
        else tele.hasPlayhead.store (false);
    }
    else tele.hasPlayhead.store (false);
    tele.isPlaying.store (playing);

    // Input meters, measured before anything overwrites the buffer.
    const int inBuses = getBusCount (true);
    for (int b = 0; b < kNumInputBuses; ++b)
    {
        float peak = 0.0f;
        bool enabled = false;
        if (b < inBuses && getBus (true, b)->isEnabled())
        {
            auto bb = getBusBuffer (buffer, true, b);
            if (bb.getNumChannels() > 0 && n > 0)
            {
                enabled = true;
                peak = bb.getMagnitude (0, n);
            }
        }
        tele.inputEnabled[b].store (enabled);
        tele.inputPeak[b].store (peak);
    }

    if (n == 0)
        return;

    if (choice (pRole) == 1)
        processSendRole (buffer, n, playing, haveTime, t);
    else
        processMosaicRole (buffer, n, playing, haveTime, t);
}

void MosaicAudioProcessor::processSendRole (juce::AudioBuffer<float>& buffer, int n,
                                            bool playing, bool haveTime, int64_t t)
{
    auto& ch = hub::SendHub::instance().channel (choice (pGroup), choice (pSend));
    tele.collisionCount.store (ch.collisionCount());
    tele.desiredLatency.store (0);

    auto in  = getBusBuffer (buffer, true, 0);
    auto out = getBusBuffer (buffer, false, 0);

    if (in.getNumChannels() == 0)
    {
        tele.sendRolePeak.store (0.0f);
        out.clear();
        return;
    }

    const float* L = in.getReadPointer (0);
    const float* R = in.getNumChannels() > 1 ? in.getReadPointer (1) : L;
    tele.sendRolePeak.store (in.getMagnitude (0, n));

    if (playing && haveTime)
        ch.write (myInstanceId, t, L, R, n);

    if (pMute->load() > 0.5f)
    {
        out.clear();
        return;
    }

    // Pass-through. Main input channels alias the output channels, so only a mono
    // input needs copying (to the second output).
    if (in.getNumChannels() == 1 && out.getNumChannels() > 1)
        out.copyFrom (1, 0, L, n);
}

void MosaicAudioProcessor::copyInputBusToOutput (juce::AudioBuffer<float>& buffer, int bus, int n)
{
    auto out = getBusBuffer (buffer, false, 0);

    if (bus >= getBusCount (true) || ! getBus (true, bus)->isEnabled())
    {
        out.clear();
        return;
    }

    auto in = getBusBuffer (buffer, true, bus);
    if (in.getNumChannels() == 0)
    {
        out.clear();
        return;
    }

    for (int c = 0; c < out.getNumChannels(); ++c)
    {
        const int srcCh = juce::jmin (c, in.getNumChannels() - 1);
        const float* src = in.getReadPointer (srcCh);
        float* dst = out.getWritePointer (c);
        if (src != dst)
            juce::FloatVectorOperations::copy (dst, src, n);
    }
}

void MosaicAudioProcessor::processMosaicRole (juce::AudioBuffer<float>& buffer, int n,
                                              bool playing, bool haveTime, int64_t t)
{
    auto& hubRef = hub::SendHub::instance();
    const int group = choice (pGroup);

    // Send meters for the current group: newest samples, regardless of alignment.
    for (int s = 0; s < hub::kNumSends; ++s)
        tele.sendPeak[s].store (hubRef.channel (group, s).peakOfLatest (n));

    const int src = choice (pProbeSource);
    auto out = getBusBuffer (buffer, false, 0);

    if (src >= params::probeSourceSend)
    {
        const int s = src - params::probeSourceSend;
        const int L = params::kLookBehindSamples[juce::jlimit (0, params::kNumLookBehindChoices - 1, choice (pLookBehind))];
        tele.desiredLatency.store (L);

        auto& ch = hubRef.channel (group, s);
        tele.collisionCount.store (ch.collisionCount());

        if (! (playing && haveTime) || out.getNumChannels() < 1)
        {
            out.clear();
            return;
        }

        hub::SendChannel::ReadResult res;
        if (out.getNumChannels() > 1)
            res = ch.read (t - L, out.getWritePointer (0), out.getWritePointer (1), n);
        else
        {
            // Mono output is not a supported layout, but stay safe.
            out.clear();
            res = { false, ch.head() };
        }

        tele.sendReads.fetch_add (1, std::memory_order_relaxed);
        if (! res.valid) tele.sendUnderruns.fetch_add (1, std::memory_order_relaxed);
        if (res.head != hub::kNoHead)
        {
            const int64_t skew = res.head - t;
            tele.skewLast.store (skew);
            if (! tele.skewValid.exchange (true)) { tele.skewMin.store (skew); tele.skewMax.store (skew); }
            else storeMinMax (tele.skewMin, tele.skewMax, skew);

            const int64_t needed = std::max<int64_t> (0, (int64_t) n - skew);
            auto cur = tele.neededLookBehind.load (std::memory_order_relaxed);
            while (needed > cur && ! tele.neededLookBehind.compare_exchange_weak (cur, needed, std::memory_order_relaxed)) {}
        }
        return;
    }

    tele.desiredLatency.store (0);

    if (src == params::probeSourceNone)
    {
        out.clear();
        return;
    }

    copyInputBusToOutput (buffer, src - params::probeSourceInput, n);
}

juce::AudioProcessorEditor* MosaicAudioProcessor::createEditor()
{
    return new MosaicAudioProcessorEditor (*this);
}

void MosaicAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);
    state.setProperty ("pluginVersion", MOSAIC_VERSION_STRING, nullptr);

    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void MosaicAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);

    if (! state.isValid() || ! state.hasType ("MosaicState"))
        return;

    // stateVersion 1 is the only version so far; nothing to migrate.
    apvts.replaceState (state);
}

} // namespace mosaic

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mosaic::MosaicAudioProcessor();
}
