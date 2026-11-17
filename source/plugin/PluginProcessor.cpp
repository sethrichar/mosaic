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

    constexpr const char* kFilesTreeType = "files";
    constexpr const char* kSlotTreeType  = "slot";
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
      pRate        (apvts.getRawParameterValue (params::rate)),
      pMode        (apvts.getRawParameterValue (params::mode)),
      pPosition    (apvts.getRawParameterValue (params::position)),
      pFade        (apvts.getRawParameterValue (params::fade)),
      pTileLength  (apvts.getRawParameterValue (params::tileLength)),
      pSeed        (apvts.getRawParameterValue (params::seed)),
      pPatternLength (apvts.getRawParameterValue (params::patternLength)),
      pAvoidRepeats  (apvts.getRawParameterValue (params::avoidRepeats)),
      pOutputGain    (apvts.getRawParameterValue (params::outputGain)),
      myInstanceId (hub::SendHub::instance().newInstanceId())
{
    for (int s = 0; s < kNumSlots; ++s)
    {
        pSlotGain[s]        = apvts.getRawParameterValue (params::slotParam (s, params::slotGainSuffix));
        pSlotMute[s]        = apvts.getRawParameterValue (params::slotParam (s, params::slotMuteSuffix));
        pSlotWeight[s]      = apvts.getRawParameterValue (params::slotParam (s, params::slotWeightSuffix));
        pSlotStartOffset[s] = apvts.getRawParameterValue (params::slotParam (s, params::slotStartOffsetSuffix));
        pSlotLoop[s]        = apvts.getRawParameterValue (params::slotParam (s, params::slotLoopSuffix));
        pSlotKeyMode[s]     = apvts.getRawParameterValue (params::slotParam (s, params::slotKeyModeSuffix));
        pSlotDirection[s]   = apvts.getRawParameterValue (params::slotParam (s, params::slotDirectionSuffix));
        pSlotTranspose[s]   = apvts.getRawParameterValue (params::slotParam (s, params::slotTransposeSuffix));
        pSlotFine[s]        = apvts.getRawParameterValue (params::slotParam (s, params::slotFineSuffix));
        slotPtrs[s] = &fileSources[s];
        tele.slotPlayPos[s].store (-1);

        for (const char* suffix : { params::slotKeyModeSuffix, params::slotDirectionSuffix, params::slotTransposeSuffix, params::slotFineSuffix })
            apvts.addParameterListener (params::slotParam (s, suffix), &keyListener);
    }
    pKeyMode  = apvts.getRawParameterValue (params::keyMode);
    pKeyRoot  = apvts.getRawParameterValue (params::keyRoot);
    pKeyScale = apvts.getRawParameterValue (params::keyScale);
    for (const char* id : { params::keyMode, params::keyRoot, params::keyScale })
        apvts.addParameterListener (id, &keyListener);

    // Default sequence (editable in M6): slots 1-8 in order, twice.
    chooserScratch.sequenceLength = 16;
    for (int i = 0; i < engine::ChooserConfig::kMaxSequenceSteps; ++i)
        chooserScratch.sequence[i] = (int8_t) (i % kNumSlots);

    apvts.state.setProperty ("stateVersion", kStateVersion, nullptr);

    loader.setCallback ([this] (int slot, int generation, sources::FileLoader::Result&& result)
    {
        if (slot >= 0 && slot < kNumSlots && slots[slot].generation == generation)
            applyLoadResult (slot, std::move (result));
    });
    updateEffectiveKey();

    renderer.prepare (512);
    startTimerHz (20);
}

MosaicAudioProcessor::~MosaicAudioProcessor()
{
    stopTimer();
    for (int s = 0; s < kNumSlots; ++s)
        for (const char* suffix : { params::slotKeyModeSuffix, params::slotDirectionSuffix, params::slotTransposeSuffix, params::slotFineSuffix })
            apvts.removeParameterListener (params::slotParam (s, suffix), &keyListener);
    for (const char* id : { params::keyMode, params::keyRoot, params::keyScale })
        apvts.removeParameterListener (id, &keyListener);
    loader.setCallback (nullptr);
    if (claimedChannel >= 0)
        hub::SendHub::instance().channelAt (claimedChannel).releaseOwnership (myInstanceId);
}

void MosaicAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    tele.sampleRate.store (sampleRate);
    tele.preparedBlockSize.store (samplesPerBlock);
    tele.maxBlockSize.store (0);
    renderer.prepare (samplesPerBlock);
    standalone.sampleRate.store (sampleRate);

    if (std::abs (sampleRate - preparedSampleRate) > 1e-9)
    {
        preparedSampleRate = sampleRate;
        reloadRequested.store (true);      // files are re-resampled on the message thread
    }
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
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

//==============================================================================
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

    for (auto& fs : fileSources)
        fs.collectGarbage();

    if (reloadRequested.exchange (false) && std::abs (preparedSampleRate - loadedForSampleRate) > 1e-9)
    {
        loadedForSampleRate = preparedSampleRate;
        for (int s = 0; s < kNumSlots; ++s)
            if (slots[s].hasFile() && ! slots[s].missing)
                beginLoad (s, slots[s].file, ! slots[s].keyDetected);   // keep the detected key (SPEC 7.5)
    }

    runDueRenders();
}

//==============================================================================
// Key

analysis::Key MosaicAudioProcessor::effectiveKey (bool& known, int* sourceSlot) const noexcept
{
    if (sourceSlot != nullptr) *sourceSlot = -1;
    if (choice (pKeyMode) == 1)
    {
        known = true;
        return { choice (pKeyRoot), choice (pKeyScale) == 1 ? analysis::Scale::Minor : analysis::Scale::Major };
    }
    for (int s = 0; s < kNumSlots; ++s)
        if (slots[s].hasFile() && ! slots[s].missing && slots[s].confidentSourceKey())
        {
            known = true;
            if (sourceSlot != nullptr) *sourceSlot = s;
            return slots[s].sourceKey();
        }
    known = false;
    return {};
}

void MosaicAudioProcessor::updateEffectiveKey()
{
    bool known = false;
    int src = -1;
    const auto key = effectiveKey (known, &src);
    tele.effectiveKeyIndex.store (known ? key.toIndex() : -1);
    tele.effectiveKeySourceSlot.store (src);
}

double MosaicAudioProcessor::desiredShiftFor (int slot) const noexcept
{
    const auto& info = slots[slot];
    const int transpose = choice (pSlotTranspose[slot]);
    const double fine = pSlotFine[slot]->load();
    const bool snap = choice (pSlotKeyMode[slot]) == 0;

    bool known = false;
    const auto target = effectiveKey (known);
    if (! snap || ! known || ! info.sourceKeyKnown())
        return analysis::KeySnap::manualShift (transpose, fine);

    const auto dir = (analysis::Direction) juce::jlimit (0, 2, choice (pSlotDirection[slot]));
    return analysis::KeySnap::totalShift (info.sourceKey(), target, dir, transpose, fine);
}

void MosaicAudioProcessor::setSlotKeyOverride (int slot, analysis::Key key)
{
    if (slot < 0 || slot >= kNumSlots) return;
    slots[slot].hasOverride = true;
    slots[slot].overrideKey = key;
    scheduleRenders();
}

void MosaicAudioProcessor::clearSlotKeyOverride (int slot)
{
    if (slot < 0 || slot >= kNumSlots) return;
    slots[slot].hasOverride = false;
    scheduleRenders();
}

void MosaicAudioProcessor::runDueRenders()
{
    updateEffectiveKey();
    for (int s = 0; s < kNumSlots; ++s)
        slots[s].desiredShift = (slots[s].hasFile() && ! slots[s].missing) ? desiredShiftFor (s) : 0.0;

    if (! rendersDirty.load())
        return;

    // Debounce: rapid changes (dragging Fine) coalesce into one render per slot.
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastRenderCheckMs == 0.0) lastRenderCheckMs = now;
    if (now - lastRenderCheckMs < kRenderDebounceMs)
        return;
    lastRenderCheckMs = 0.0;
    rendersDirty.store (false);

    for (int s = 0; s < kNumSlots; ++s)
    {
        auto& info = slots[s];
        if (! info.hasFile() || info.missing || info.analyzing || info.error.isNotEmpty())
            continue;
        if (std::abs (info.desiredShift - info.renderedShift) < 1e-6 && ! info.rendering)
            continue;
        // A render is in flight for a now-stale target, or the target changed: re-render
        // from disk (only the played buffer stays in RAM, SPEC 6).
        info.rendering = true;
        info.generation++;
        loader.requestRender (s, info.generation, info.file, preparedSampleRate, nullptr, info.desiredShift);
    }
}

void MosaicAudioProcessor::publishOrRender (int slot, std::unique_ptr<sources::LoadedBuffer> original)
{
    auto& info = slots[slot];
    updateEffectiveKey();
    info.desiredShift = desiredShiftFor (slot);

    if (std::abs (info.desiredShift) < 1e-6)
    {
        info.renderedShift = 0.0;
        info.rendering = false;
        info.bytes = original->bytes();
        fileSources[slot].publish (std::move (original));
        return;
    }
    // Shifted: the slot stays silent until the render is ready (SPEC 7.6).
    info.rendering = true;
    info.generation++;
    loader.requestRender (slot, info.generation, info.file, preparedSampleRate, std::move (original), info.desiredShift);
}

//==============================================================================
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

    // Playhead: the host's, or the Standalone transport when the host has none.
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* ph = getPlayHead())
        pos = ph->getPosition();
    if (! pos.hasValue() && usesStandaloneTransport())
        pos = standalone.getPosition();

    bool playing = false, haveTime = false;
    int64_t t = 0;
    tele.hasPlayhead.store (pos.hasValue());
    if (pos.hasValue())
    {
        playing = pos->getIsPlaying();
        if (const auto ts = pos->getTimeInSamples()) { haveTime = true; t = *ts; }
        tele.hasTimeInSamples.store (haveTime);
        tele.timeInSamples.store (t);
        if (const auto p = pos->getPpqPosition()) { tele.hasPpq.store (true); tele.ppq.store (*p); } else tele.hasPpq.store (false);
        if (const auto b = pos->getBpm())         { tele.hasBpm.store (true); tele.bpm.store (*b); } else tele.hasBpm.store (false);
    }
    tele.isPlaying.store (playing);

    if (isNonRealtime())
        waitForPendingWorkOffline();                    // SPEC 7.6 / CLAUDE.md: the one sanctioned wait
    pollSourcesAtTileBoundaries (n, playing, getSampleRate());

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

    const int src = choice (pProbeSource);
    if (choice (pRole) == 1)
    {
        tele.engineActive.store (false);
        processSendRole (buffer, n, playing, haveTime, t);
    }
    else if (src != params::probeSourceNone)
    {
        tele.engineActive.store (false);
        processProbe (buffer, n, playing, haveTime, t, src);
    }
    else
    {
        tele.desiredLatency.store (0);
        processEngine (buffer, n, pos);
    }

    if (usesStandaloneTransport())
        standalone.advance (n);

    auto out = getBusBuffer (buffer, false, 0);
    tele.outputPeak.store (out.getNumChannels() > 0 ? out.getMagnitude (0, n) : 0.0f);
}

void MosaicAudioProcessor::pollSourcesAtTileBoundaries (int n, bool playing, double sampleRate)
{
    // A new buffer (load, clear, or finished render) swaps in only when its slot is not
    // sounding: not the active slot and past any crossfade tail. If the slot never goes
    // quiet (single-slot mosaic) the swap happens anyway after ~2 s. Stopped: immediately.
    const int active = tele.activeSlot.load (std::memory_order_relaxed);
    const int fadeSamples = (int) std::lround (pFade->load() * (sampleRate > 0.0 ? sampleRate : 48000.0) / 1000.0);
    const int maxWaitBlocks = (int) std::max<int64_t> (1, (int64_t) (2.0 * (sampleRate > 0.0 ? sampleRate : 48000.0)) / std::max (1, n));

    for (int s = 0; s < kNumSlots; ++s)
    {
        const bool sounding = playing && (s == active || s == lastActiveSlot);
        blocksSinceActive[s] = sounding ? 0 : std::min (blocksSinceActive[s] + 1, 1 << 20);
        if (! fileSources[s].hasIncoming())
            continue;
        const bool pastFade = (int64_t) blocksSinceActive[s] * n > fadeSamples + n;
        if (! playing || (! sounding && pastFade) || blocksSinceActive[s] > maxWaitBlocks || isNonRealtime())
            fileSources[s].pollIncoming();
    }
    lastActiveSlot = active;
}

void MosaicAudioProcessor::waitForPendingWorkOffline()
{
    // Offline bounce: wait (bounded) for loads and renders so the bounce never contains
    // silence or stale pitch. Results are published by the message thread, so this only
    // helps when the host keeps its message loop running during the bounce (they do).
    int waited = 0;
    while (loader.pendingJobs() > 0 && waited < kOfflineWaitLimitMs)
    {
        juce::Thread::sleep (2);
        waited += 2;
    }
    if (waited > 0)
        tele.offlineWaitsMs.fetch_add (waited);
}

void MosaicAudioProcessor::processEngine (juce::AudioBuffer<float>& buffer, int n,
                                          const juce::Optional<juce::AudioPlayHead::PositionInfo>& pos)
{
    auto out = getBusBuffer (buffer, false, 0);
    tele.engineActive.store (true);

    engine::HostBlock hb;
    bool valid = pos.hasValue() && out.getNumChannels() >= 2;
    double qnPerBar = 4.0;
    if (valid)
    {
        const auto ppq = pos->getPpqPosition();
        const auto bpm = pos->getBpm();
        const auto ts  = pos->getTimeInSamples();
        valid = ppq.hasValue() && bpm.hasValue() && ts.hasValue() && *bpm > 0.0;
        if (valid)
        {
            hb.ppqAtStart = *ppq;
            hb.bpm = *bpm;
            hb.timeInSamples = *ts;
            hb.playing = pos->getIsPlaying();
            hb.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : preparedSampleRate;
            if (const auto sig = pos->getTimeSignature())
                qnPerBar = engine::GridClock::quarterNotesPerBar (sig->numerator, sig->denominator);
        }
    }

    if (! valid || hb.sampleRate <= 0.0)
    {
        out.clear();
        tele.activeSlot.store (-1);
        for (auto& p : tele.slotPlayPos) p.store (-1);
        return;
    }

    // Parameters -> engine configuration (cheap: a few dozen atomics)
    engine::RenderParams rp;
    rp.rate = engine::Rate::fromIndex (choice (pRate));
    rp.freePosition = choice (pPosition) == 1;
    rp.fadeMs = pFade->load();
    rp.tileLengthPercent = pTileLength->load();
    rp.seed = (uint32_t) choice (pSeed);

    const double spq = engine::GridClock::samplesPerQuarter (hb);
    const double stepQN = rp.rate.lengthInQuarterNotes();

    engine::ChooserConfig& cc = chooserScratch;
    cc.mode = choice (pMode) == 1 ? engine::ChooseMode::Sequence : engine::ChooseMode::Random;
    cc.seed = rp.seed;
    const int patIdx = juce::jlimit (0, params::kNumPatternLengthChoices - 1, choice (pPatternLength));
    cc.patternLengthSteps = (int) engine::GridClock::patternLengthSteps (params::kPatternLengthBars[patIdx], qnPerBar, stepQN);
    cc.avoidRepeats = pAvoidRepeats->load() > 0.5f;

    for (int s = 0; s < kNumSlots; ++s)
    {
        rp.slotGain[s] = params::dbToGain (pSlotGain[s]->load());
        rp.placement[s].startOffsetSamples = engine::GridClock::barsToSamples (pSlotStartOffset[s]->load(), qnPerBar, spq);
        rp.placement[s].loop = pSlotLoop[s]->load() > 0.5f;
        cc.weight[s] = pSlotWeight[s]->load();
        cc.available[s] = ! fileSources[s].isEmpty() && pSlotMute[s]->load() < 0.5f;
    }

    renderer.render (hb, rp, cc, slotPtrs, out.getWritePointer (0), out.getWritePointer (1), n);

    const float gain = params::dbToGain (pOutputGain->load());
    if (gain != 1.0f)
        out.applyGain (0, n, gain);

    // Telemetry for the UI: what plays at the start of this block and where it reads from.
    const int64_t step = engine::GridClock::stepAtStart (hb, stepQN);
    const int active = hb.playing ? engine::StepChooser::choose (cc, step) : -1;
    tele.stepIndex.store (step);
    tele.activeSlot.store (active);
    for (int s = 0; s < kNumSlots; ++s)
    {
        int64_t p = -1;
        if (s == active)
        {
            const int64_t tileStart = engine::GridClock::boundaryTimeline (hb, stepQN, step);
            const int64_t stepLen   = engine::GridClock::stepLengthSamples (hb, stepQN, step);
            const auto run = rp.freePosition
                ? engine::SlicePosition::free (fileSources[s], rp.placement[s], hb.timeInSamples, tileStart, step, s, rp.seed, stepLen, 1)
                : engine::SlicePosition::locked (fileSources[s], rp.placement[s], hb.timeInSamples, 1);
            p = run.silent ? -1 : run.sourcePos;
        }
        tele.slotPlayPos[s].store (p);
    }
}

void MosaicAudioProcessor::processSendRole (juce::AudioBuffer<float>& buffer, int n,
                                            bool playing, bool haveTime, int64_t t)
{
    auto& ch = hub::SendHub::instance().channel (choice (pGroup), choice (pSend));
    tele.collisionCount.store (ch.collisionCount());
    tele.desiredLatency.store (0);
    tele.activeSlot.store (-1);

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

void MosaicAudioProcessor::processProbe (juce::AudioBuffer<float>& buffer, int n,
                                         bool playing, bool haveTime, int64_t t, int src)
{
    auto& hubRef = hub::SendHub::instance();
    const int group = choice (pGroup);
    tele.activeSlot.store (-1);

    for (int s = 0; s < hub::kNumSends; ++s)
        tele.sendPeak[s].store (hubRef.channel (group, s).peakOfLatest (n));

    auto out = getBusBuffer (buffer, false, 0);

    if (src >= params::probeSourceSend)
    {
        const int s = src - params::probeSourceSend;
        const int L = params::kLookBehindSamples[juce::jlimit (0, params::kNumLookBehindChoices - 1, choice (pLookBehind))];
        tele.desiredLatency.store (L);

        auto& ch = hubRef.channel (group, s);
        tele.collisionCount.store (ch.collisionCount());

        if (! (playing && haveTime) || out.getNumChannels() < 2)
        {
            out.clear();
            return;
        }

        const auto res = ch.read (t - L, out.getWritePointer (0), out.getWritePointer (1), n);
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
    copyInputBusToOutput (buffer, src - params::probeSourceInput, n);
}

//==============================================================================
// Files

void MosaicAudioProcessor::beginLoad (int slot, const juce::File& file, bool detectKey)
{
    auto& info = slots[slot];
    info.file = file;
    info.error.clear();
    info.generation++;
    info.missing = ! file.existsAsFile();
    info.loading = ! info.missing;
    info.analyzing = ! info.missing && detectKey;
    info.rendering = false;
    info.renderedShift = 0.0;

    if (info.missing)
    {
        info.lengthSamples = 0; info.bytes = 0; info.overview.reset();
        fileSources[slot].publish (nullptr);
        updateEffectiveKey();
        return;
    }

    loadedForSampleRate = preparedSampleRate;
    loader.requestLoad (slot, info.generation, file, preparedSampleRate, detectKey);
}

void MosaicAudioProcessor::loadFile (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= kNumSlots) return;
    // A fresh import: forget any override and detection, detect again (SPEC 7.1).
    slots[slot].hasOverride = false;
    slots[slot].keyDetected = false;
    slots[slot].key = {};
    beginLoad (slot, file, true);
}

void MosaicAudioProcessor::clearSlot (int slot)
{
    if (slot < 0 || slot >= kNumSlots) return;
    const int gen = slots[slot].generation + 1;
    slots[slot] = SlotInfo {};
    slots[slot].generation = gen;
    loader.requestLoad (slot, gen, juce::File(), preparedSampleRate, false);   // cancels any running job; result is "not found" and ignored
    slots[slot].generation = gen + 1;                                           // ...because we bump past it here
    fileSources[slot].publish (nullptr);
    scheduleRenders();
    updateEffectiveKey();
}

bool MosaicAudioProcessor::loadFileNow (int slot, const juce::File& file)
{
    if (slot < 0 || slot >= kNumSlots) return false;
    auto& info = slots[slot];
    info.file = file;
    info.error.clear();
    info.generation++;
    info.missing = ! file.existsAsFile();
    info.loading = false;
    info.hasOverride = false;
    info.keyDetected = false;
    if (info.missing)
    {
        fileSources[slot].publish (nullptr);
        return false;
    }
    auto result = sources::FileLoader::loadNow (file, preparedSampleRate, true);
    const bool ok = result.ok;
    applyLoadResult (slot, std::move (result));
    // Synchronous path: render now too, so tests and tools see the final buffer.
    if (ok && info.rendering)
    {
        auto original = sources::FileLoader::loadNow (file, preparedSampleRate, false);
        if (original.ok)
        {
            auto rendered = sources::FileLoader::renderNow (*original.buffer, info.desiredShift);
            applyLoadResult (slot, std::move (rendered));
        }
    }
    return ok;
}

void MosaicAudioProcessor::applyLoadResult (int slot, sources::FileLoader::Result&& result)
{
    auto& info = slots[slot];
    info.loading = false;
    info.analyzing = false;

    if (! result.ok)
    {
        info.rendering = false;
        info.error = result.error;
        info.lengthSamples = 0; info.bytes = 0; info.overview.reset();
        fileSources[slot].publish (nullptr);
        updateEffectiveKey();
        return;
    }

    info.lengthSamples    = result.buffer->numSamples;
    info.sourceSampleRate = result.sourceSampleRate > 0.0 ? result.sourceSampleRate : info.sourceSampleRate;
    info.sourceChannels   = result.sourceChannels > 0 ? result.sourceChannels : info.sourceChannels;
    info.overview         = result.overview;

    if (result.rendered)
    {
        info.rendering = false;
        info.renderedShift = result.renderedSemitones;
        info.bytes = result.buffer->bytes();
        fileSources[slot].publish (std::move (result.buffer));
        return;
    }

    if (result.keyDetected)
    {
        info.keyDetected = true;
        info.key = result.key;
        // Import default (SPEC 7.3): Snap if confident, Original otherwise.
        if (auto* p = apvts.getParameter (params::slotParam (slot, params::slotKeyModeSuffix)))
        {
            const float v = (result.key.found && result.key.level == analysis::Confidence::High) ? 0.0f : 1.0f;
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
        rendersDirty.store (false);   // the listener above marked us dirty; this slot is handled right here
    }

    publishOrRender (slot, std::move (result.buffer));
    scheduleRenders();                // the Auto key may have changed for the other slots
}

size_t MosaicAudioProcessor::totalFileBytes() const noexcept
{
    size_t total = 0;
    for (const auto& fs : fileSources) total += fs.bytesInUse();
    return total;
}

void MosaicAudioProcessor::reroll()
{
    if (auto* p = apvts.getParameter (params::seed))
    {
        const int value = juce::Random::getSystemRandom().nextInt (params::kMaxSeed + 1);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) value));
    }
}

//==============================================================================
// State

juce::ValueTree MosaicAudioProcessor::filesToTree() const
{
    juce::ValueTree files (kFilesTreeType);
    for (int s = 0; s < kNumSlots; ++s)
    {
        if (! slots[s].hasFile()) continue;
        juce::ValueTree slot (kSlotTreeType);
        const auto& info = slots[s];
        slot.setProperty ("index", s, nullptr);
        slot.setProperty ("path", info.file.getFullPathName(), nullptr);
        slot.setProperty ("keyDetected", info.keyDetected, nullptr);
        slot.setProperty ("keyFound", info.keyDetected && info.key.found, nullptr);
        slot.setProperty ("detectedKey", info.keyDetected ? info.key.key.toIndex() : -1, nullptr);
        slot.setProperty ("confidence", info.key.confidence, nullptr);
        slot.setProperty ("confidenceLevel", (int) info.key.level, nullptr);
        slot.setProperty ("tuningCents", info.key.tuningCents, nullptr);
        slot.setProperty ("overrideKey", info.hasOverride ? info.overrideKey.toIndex() : -1, nullptr);
        files.appendChild (slot, nullptr);
    }
    return files;
}

void MosaicAudioProcessor::filesFromTree (const juce::ValueTree& files)
{
    bool present[kNumSlots] = {};
    for (const auto& slot : files)
    {
        if (! slot.hasType (kSlotTreeType)) continue;
        const int s = (int) slot.getProperty ("index", -1);
        const juce::String path = slot.getProperty ("path").toString();
        if (s < 0 || s >= kNumSlots || path.isEmpty()) continue;
        present[s] = true;

        // Restore the saved key data; never re-detect on restore (SPEC 7.5).
        auto& info = slots[s];
        info.keyDetected = (bool) slot.getProperty ("keyDetected", false);
        info.key = {};
        if (info.keyDetected)
        {
            info.key.found = (bool) slot.getProperty ("keyFound", false);
            info.key.key = analysis::Key::fromIndex ((int) slot.getProperty ("detectedKey", 0));
            info.key.confidence = (float) (double) slot.getProperty ("confidence", 0.0);
            info.key.level = (analysis::Confidence) juce::jlimit (0, 2, (int) slot.getProperty ("confidenceLevel", 0));
            info.key.tuningCents = (float) (double) slot.getProperty ("tuningCents", 0.0);
        }
        const int ov = (int) slot.getProperty ("overrideKey", -1);
        info.hasOverride = ov >= 0;
        if (info.hasOverride) info.overrideKey = analysis::Key::fromIndex (ov);

        beginLoad (s, juce::File (path), ! info.keyDetected);   // missing files set the "missing" state, nothing else breaks
    }
    for (int s = 0; s < kNumSlots; ++s)
        if (! present[s] && slots[s].hasFile())
            clearSlot (s);
}

void MosaicAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName (kFilesTreeType), nullptr);
    state.appendChild (filesToTree(), nullptr);
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

    // Versions 1–3 differ only by added parameters and files-tree properties; the APVTS
    // fills in defaults for anything missing.
    apvts.replaceState (state);

    const auto files = state.getChildWithName (kFilesTreeType).createCopy();
    auto apply = [weak = juce::WeakReference<MosaicAudioProcessor> (this), files]
    {
        if (auto* self = weak.get())
            self->filesFromTree (files);
    };
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        juce::MessageManager::callAsync (apply);
}

juce::AudioProcessorEditor* MosaicAudioProcessor::createEditor()
{
    return new MosaicAudioProcessorEditor (*this);
}

} // namespace mosaic

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mosaic::MosaicAudioProcessor();
}
