#pragma once

#include <memory>
#include <vector>
#include <juce_gui_basics/juce_gui_basics.h>

namespace mosaic::ui
{

/// Placeholder waveform overview with a playhead (M3). Not the designed UI (M7).
class WaveformView final : public juce::Component
{
public:
    void setOverview (std::shared_ptr<const std::vector<float>> ov)
    {
        if (ov != overview) { overview = std::move (ov); repaint(); }
    }
    /// 0..1 across the file, or < 0 for none.
    void setPlayhead (double fraction)
    {
        if (std::abs (fraction - playhead) > 1e-4) { playhead = fraction; repaint(); }
    }
    void setStatus (const juce::String& s)
    {
        if (s != status) { status = s; repaint(); }
    }
    void setAccent (juce::Colour c) { accent = c; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colour (0xff151515));
        g.fillRoundedRectangle (r, 3.0f);

        if (overview != nullptr && ! overview->empty())
        {
            const int bins = (int) overview->size() / 2;
            const float mid = r.getCentreY();
            const float half = r.getHeight() * 0.48f;
            g.setColour (accent.withAlpha (0.85f));
            for (int x = 0; x < (int) r.getWidth(); ++x)
            {
                const int b0 = bins * x / juce::jmax (1, (int) r.getWidth());
                const int b1 = juce::jmax (b0 + 1, bins * (x + 1) / juce::jmax (1, (int) r.getWidth()));
                float mn = 1.0f, mx = -1.0f;
                for (int b = b0; b < b1 && b < bins; ++b)
                {
                    mn = juce::jmin (mn, (*overview)[(size_t) b * 2]);
                    mx = juce::jmax (mx, (*overview)[(size_t) b * 2 + 1]);
                }
                if (mn <= mx)
                    g.drawVerticalLine (x, mid - mx * half, mid - mn * half + 1.0f);
            }
        }

        if (playhead >= 0.0)
        {
            g.setColour (juce::Colours::white);
            const float x = r.getX() + (float) playhead * r.getWidth();
            g.fillRect (x - 0.5f, r.getY(), 1.5f, r.getHeight());
        }

        if (status.isNotEmpty())
        {
            g.setColour (juce::Colours::lightgrey);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (status, getLocalBounds(), juce::Justification::centred);
        }
    }

private:
    std::shared_ptr<const std::vector<float>> overview;
    double playhead = -1.0;
    juce::String status;
    juce::Colour accent { 0xff4caf50 };
};

} // namespace mosaic::ui
