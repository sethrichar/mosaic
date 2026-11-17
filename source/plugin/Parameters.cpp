#include "plugin/Parameters.h"

namespace mosaic::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    // None of these are musical controls; keep them out of host automation lanes.
    const auto nonAuto = AudioParameterChoiceAttributes().withAutomatable (false);
    const auto nonAutoBool = AudioParameterBoolAttributes().withAutomatable (false);

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { role, 1 }, "Role", StringArray { "Mosaic", "Send" }, 0, nonAuto));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { sendGroup, 1 }, "Group", StringArray { "A", "B", "C", "D" }, 0, nonAuto));

    StringArray sends;
    for (int i = 1; i <= 8; ++i) sends.add ("Send " + String (i));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { sendNumber, 1 }, "Send number", sends, 0, nonAuto));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { sendMuteOutput, 1 }, "Send: mute output", false, nonAutoBool));

    StringArray sources { "None" };
    for (int i = 1; i <= 8; ++i) sources.add ("Input " + String (i));
    for (int i = 1; i <= 8; ++i) sources.add ("Send " + String (i));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { probeSource, 1 }, "Probe source", sources, probeSourceInput, nonAuto));

    StringArray lookBehinds;
    for (int v : kLookBehindSamples) lookBehinds.add (String (v));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { probeLookBehind, 1 }, "Probe look-behind (samples)", lookBehinds,
        kDefaultLookBehindIndex, nonAuto));

    return layout;
}

} // namespace mosaic::params
