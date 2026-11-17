#include "sources/FileSlotSource.h"

#include <algorithm>
#include <cstring>

namespace mosaic::sources
{

FileSlotSource::~FileSlotSource()
{
    // Nothing is running any more: everything still in flight is ours to free.
    LoadedBuffer* p = nullptr;
    while (incoming.pop (p)) {}
    while (retired.pop (p)) {}
    // `current` is owned by inFlight (or pendingPublish never reached it); the vectors free it.
}

void FileSlotSource::pollIncoming() noexcept
{
    LoadedBuffer* next = nullptr;
    bool got = false;
    LoadedBuffer* candidate = nullptr;
    while (incoming.pop (candidate))   // take the newest; retire anything it superseded
    {
        if (got)
            retired.push (next);       // never fails: retired has the same capacity as incoming
        next = candidate;
        got = true;
    }
    if (! got)
        return;

    if (current != nullptr)
        retired.push (current);
    current = next;
}

void FileSlotSource::read (int64_t pos, float* left, float* right, int n) noexcept
{
    if (n <= 0) return;
    const LoadedBuffer* b = current;

    if (b == nullptr || b->numSamples <= 0)
    {
        std::memset (left,  0, (size_t) n * sizeof (float));
        std::memset (right, 0, (size_t) n * sizeof (float));
        return;
    }

    const int64_t len = b->numSamples;
    int i = 0;

    // Leading silence
    if (pos < 0)
    {
        const int lead = (int) std::min<int64_t> (n, -pos);
        std::memset (left,  0, (size_t) lead * sizeof (float));
        std::memset (right, 0, (size_t) lead * sizeof (float));
        i = lead;
    }

    // In range
    if (i < n && pos + i < len)
    {
        const int64_t start = pos + i;
        const int count = (int) std::min<int64_t> (n - i, len - start);
        std::memcpy (left + i,  b->left.data()  + start, (size_t) count * sizeof (float));
        std::memcpy (right + i, b->right.data() + start, (size_t) count * sizeof (float));
        i += count;
    }

    // Trailing silence
    if (i < n)
    {
        std::memset (left + i,  0, (size_t) (n - i) * sizeof (float));
        std::memset (right + i, 0, (size_t) (n - i) * sizeof (float));
    }
}

void FileSlotSource::publish (std::unique_ptr<LoadedBuffer> buffer)
{
    if (buffer == nullptr)
        buffer = std::make_unique<LoadedBuffer>();   // an empty buffer clears the slot

    // Anything still waiting to be published is superseded: free it now (it never
    // reached the audio thread) so only the newest buffer is ever handed over.
    pendingPublish.clear();
    pendingPublish.push_back (std::move (buffer));
    collectGarbage();
}

void FileSlotSource::collectGarbage()
{
    LoadedBuffer* p = nullptr;
    while (retired.pop (p))
    {
        auto it = std::find_if (inFlight.begin(), inFlight.end(), [p] (const auto& u) { return u.get() == p; });
        if (it != inFlight.end())
            inFlight.erase (it);
    }

    while (! pendingPublish.empty())
    {
        LoadedBuffer* raw = pendingPublish.front().get();
        if (! incoming.push (raw))
            break;                                       // audio thread hasn't drained yet; retry later
        inFlight.push_back (std::move (pendingPublish.front()));
        pendingPublish.erase (pendingPublish.begin());
    }
}

size_t FileSlotSource::bytesInUse() const noexcept
{
    size_t total = 0;
    for (const auto& b : inFlight)       total += b->bytes();
    for (const auto& b : pendingPublish) total += b->bytes();
    return total;
}

} // namespace mosaic::sources
