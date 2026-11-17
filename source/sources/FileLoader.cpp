#include "sources/FileLoader.h"

#include <algorithm>
#include <cmath>

namespace mosaic::sources
{

FileLoader::FileLoader() : juce::Thread ("Mosaic File Loader")
{
    startThread (juce::Thread::Priority::normal);
}

FileLoader::~FileLoader()
{
    for (auto& f : cancelFlags) f.store (true);
    signalThreadShouldExit();
    wake.signal();
    stopThread (10000);
}

void FileLoader::requestLoad (int slot, int generation, const juce::File& file, double targetSampleRate, bool detectKey)
{
    if (slot < 0 || slot >= engine::kNumSlots) return;
    latestGeneration[slot].store (generation);
    cancelFlags[slot].store (true);   // whatever is running for this slot is stale now
    {
        const juce::ScopedLock sl (lock);
        jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [slot] (const Job& j) { return j.slot == slot; }), jobs.end());
        jobs.push_back ({ Job::Kind::Load, slot, generation, file, targetSampleRate, detectKey, nullptr, 0.0 });
    }
    pending.fetch_add (1);
    wake.signal();
}

void FileLoader::requestRender (int slot, int generation, const juce::File& file, double targetSampleRate,
                                std::unique_ptr<LoadedBuffer> original, double semitones)
{
    if (slot < 0 || slot >= engine::kNumSlots) return;
    latestGeneration[slot].store (generation);
    cancelFlags[slot].store (true);
    {
        const juce::ScopedLock sl (lock);
        jobs.erase (std::remove_if (jobs.begin(), jobs.end(), [slot] (const Job& j) { return j.slot == slot; }), jobs.end());
        jobs.push_back ({ Job::Kind::Render, slot, generation, file, targetSampleRate, false,
                          std::shared_ptr<LoadedBuffer> (std::move (original)), semitones });
    }
    pending.fetch_add (1);
    wake.signal();
}

void FileLoader::post (int slot, int gen, std::shared_ptr<Result> result)
{
    juce::WeakReference<FileLoader> weak (this);
    juce::MessageManager::callAsync ([weak, slot, gen, result]
    {
        if (auto* self = weak.get())
        {
            self->pending.fetch_sub (1);
            if (self->callback != nullptr && self->latestGeneration[slot].load() == gen)
                self->callback (slot, gen, std::move (*result));
        }
    });
}

void FileLoader::run()
{
    while (! threadShouldExit())
    {
        Job job;
        bool have = false;
        {
            const juce::ScopedLock sl (lock);
            if (! jobs.empty())
            {
                job = jobs.front();
                jobs.erase (jobs.begin());
                have = true;
            }
        }

        if (! have)
        {
            wake.wait (200);
            continue;
        }

        if (latestGeneration[job.slot].load() != job.generation)
        {
            pending.fetch_sub (1);
            continue;   // superseded before it started
        }

        cancelFlags[job.slot].store (false);
        progress[job.slot].store (0.0f);
        std::shared_ptr<Result> result;

        if (job.kind == Job::Kind::Load)
        {
            result = std::make_shared<Result> (loadNow (job.file, job.rate, job.detectKey, &cancelFlags[job.slot]));
        }
        else
        {
            std::shared_ptr<LoadedBuffer> original = job.original;
            if (original == nullptr)
            {
                auto reload = loadNow (job.file, job.rate, false, &cancelFlags[job.slot]);
                if (! reload.ok)
                {
                    result = std::make_shared<Result> (std::move (reload));
                }
                else
                    original = std::shared_ptr<LoadedBuffer> (std::move (reload.buffer));
            }
            if (result == nullptr)
                result = std::make_shared<Result> (renderNow (*original, job.semitones, &cancelFlags[job.slot], &progress[job.slot]));
        }
        progress[job.slot].store (1.0f);

        if (threadShouldExit() || latestGeneration[job.slot].load() != job.generation || cancelFlags[job.slot].load())
        {
            pending.fetch_sub (1);
            continue;   // superseded while working: drop the result
        }

        post (job.slot, job.generation, std::move (result));
    }
}

FileLoader::Result FileLoader::loadNow (const juce::File& file, double targetSampleRate, bool detectKey, std::atomic<bool>* cancel)
{
    Result r;

    if (! file.existsAsFile())
    {
        r.error = "File not found";
        return r;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        r.error = "Unsupported or unreadable file";
        return r;
    }

    const double sourceRate = reader->sampleRate;
    const int sourceChannels = (int) reader->numChannels;
    const int64_t sourceLength = reader->lengthInSamples;
    if (sourceRate <= 0.0 || sourceChannels <= 0 || sourceLength <= 0)
    {
        r.error = "Empty file";
        return r;
    }
    if (targetSampleRate <= 0.0)
        targetSampleRate = sourceRate;

    const int keepChannels = std::min (2, sourceChannels);
    juce::AudioBuffer<float> source (keepChannels, (int) sourceLength);
    {
        const int chunk = 1 << 16;
        for (int64_t pos = 0; pos < sourceLength; pos += chunk)
        {
            if (cancel != nullptr && cancel->load()) { r.error = "Cancelled"; return r; }
            const int n = (int) std::min<int64_t> (chunk, sourceLength - pos);
            if (! reader->read (&source, (int) pos, n, pos, true, keepChannels > 1))
            {
                r.error = "Read failed";
                return r;
            }
        }
    }

    auto out = std::make_unique<LoadedBuffer>();
    out->sourceRate = sourceRate;
    out->sampleRate = targetSampleRate;

    if (std::abs (sourceRate - targetSampleRate) < 1e-9)
    {
        out->numSamples = sourceLength;
        out->left.assign (source.getReadPointer (0), source.getReadPointer (0) + sourceLength);
        if (keepChannels > 1)
            out->right.assign (source.getReadPointer (1), source.getReadPointer (1) + sourceLength);
        else
            out->right = out->left;
    }
    else
    {
        const double ratio = sourceRate / targetSampleRate;
        const int64_t outLength = (int64_t) std::llround ((double) sourceLength / ratio);
        out->numSamples = outLength;

        const int pad = 512;
        std::vector<float> padded ((size_t) (sourceLength + pad), 0.0f);

        for (int ch = 0; ch < 2; ++ch)
        {
            if (cancel != nullptr && cancel->load()) { r.error = "Cancelled"; return r; }
            const int srcCh = std::min (ch, keepChannels - 1);
            std::copy (source.getReadPointer (srcCh), source.getReadPointer (srcCh) + sourceLength, padded.begin());
            std::vector<float>& dst = ch == 0 ? out->left : out->right;
            if (ch == 1 && keepChannels == 1)
            {
                dst = out->left;
                break;
            }
            dst.assign ((size_t) outLength, 0.0f);
            juce::WindowedSincInterpolator interp;
            interp.reset();
            interp.process (ratio, padded.data(), dst.data(), (int) outLength);
        }
    }

    if (detectKey)
    {
        r.key = analysis::KeyDetector::analyze (out->left.data(), out->right.data(), out->numSamples, targetSampleRate, {}, cancel);
        if (cancel != nullptr && cancel->load()) { r.error = "Cancelled"; return r; }
        r.keyDetected = true;
    }

    r.overview = makeOverview (*out);
    r.buffer = std::move (out);
    r.sourceSampleRate = sourceRate;
    r.sourceChannels = sourceChannels;
    r.ok = true;
    return r;
}

FileLoader::Result FileLoader::renderNow (const LoadedBuffer& original, double semitones, std::atomic<bool>* cancel,
                                          std::atomic<float>* progressOut)
{
    Result r;
    if (original.numSamples <= 0)
    {
        r.error = "Nothing to render";
        return r;
    }

    auto out = std::make_unique<LoadedBuffer>();
    out->sampleRate = original.sampleRate;
    out->sourceRate = original.sourceRate;
    out->numSamples = original.numSamples;
    out->left.assign ((size_t) original.numSamples, 0.0f);
    out->right.assign ((size_t) original.numSamples, 0.0f);

    analysis::SignalsmithPitchShifter shifter;
    if (! shifter.shift (original.left.data(), original.right.data(), original.numSamples, original.sampleRate,
                         semitones, out->left.data(), out->right.data(), cancel))
    {
        r.error = "Cancelled";
        return r;
    }
    if (progressOut != nullptr) progressOut->store (1.0f);

    r.overview = makeOverview (*out);
    r.buffer = std::move (out);
    r.sourceSampleRate = original.sourceRate;
    r.rendered = true;
    r.renderedSemitones = semitones;
    r.ok = true;
    return r;
}

std::shared_ptr<const std::vector<float>> FileLoader::makeOverview (const LoadedBuffer& b, int bins)
{
    auto ov = std::make_shared<std::vector<float>> ((size_t) bins * 2, 0.0f);
    if (b.numSamples <= 0 || bins <= 0)
        return ov;

    for (int bin = 0; bin < bins; ++bin)
    {
        const int64_t start = b.numSamples * bin / bins;
        const int64_t end   = std::max (start + 1, b.numSamples * (bin + 1) / bins);
        float mn = 1.0e9f, mx = -1.0e9f;
        for (int64_t i = start; i < end && i < b.numSamples; ++i)
        {
            const float v = 0.5f * (b.left[(size_t) i] + b.right[(size_t) i]);
            mn = std::min (mn, v);
            mx = std::max (mx, v);
        }
        (*ov)[(size_t) bin * 2]     = mn > mx ? 0.0f : mn;
        (*ov)[(size_t) bin * 2 + 1] = mn > mx ? 0.0f : mx;
    }
    return ov;
}

} // namespace mosaic::sources
