#include "plugin/Parameters.h"
#include "engine/Rate.h"
#include "engine/StepChooser.h"
#include "analysis/Key.h"

namespace mosaic::params
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    const auto nonAuto     = AudioParameterChoiceAttributes().withAutomatable (false);
    const auto nonAutoBool = AudioParameterBoolAttributes().withAutomatable (false);

    // --- M1 ---
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
        ParameterID { probeSource, 1 }, "Probe source", sources, probeSourceNone, nonAuto));
    StringArray lookBehinds;
    for (int v : kLookBehindSamples) lookBehinds.add (String (v));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { probeLookBehind, 1 }, "Probe look-behind (samples)", lookBehinds,
        kDefaultLookBehindIndex, nonAuto));

    // --- M3 global ---
    StringArray rates;
    for (int i = 0; i < engine::Rate::kNumChoices; ++i)
    {
        const auto r = engine::Rate::fromIndex (i);
        rates.add (String (engine::Rate::baseName (r.base)) + engine::Rate::modSuffix (r.mod));
    }
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { rate, 1 }, "Rate", rates, engine::Rate { engine::RateBase::Eighth, engine::RateMod::Straight }.toIndex()));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { mode, 1 }, "Mode", StringArray { "Random", "Sequence" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { position, 1 }, "Position", StringArray { "Locked", "Free" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { fade, 1 }, "Fade", NormalisableRange<float> (0.0f, 20.0f, 0.1f), 5.0f,
        AudioParameterFloatAttributes().withLabel ("ms")));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { tileLength, 1 }, "Tile length", NormalisableRange<float> (10.0f, 100.0f, 1.0f), 100.0f,
        AudioParameterFloatAttributes().withLabel ("%")));
    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { seed, 1 }, "Seed", 0, kMaxSeed, 1));
    StringArray patterns { "Off" };
    for (int i = 1; i < kNumPatternLengthChoices; ++i)
        patterns.add (String (kPatternLengthBars[i]) + (kPatternLengthBars[i] == 1 ? " bar" : " bars"));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { patternLength, 1 }, "Pattern length", patterns, 0));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { avoidRepeats, 1 }, "Avoid repeats", false));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { outputGain, 1 }, "Output gain", NormalisableRange<float> (kMinGainDb, kMaxGainDb, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));

    // --- M4 key: global ---
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { keyMode, 1 }, "Key mode", StringArray { "Auto", "Manual" }, 0, nonAuto));
    StringArray roots;
    for (int i = 0; i < 12; ++i) roots.add (analysis::Key::rootName (i));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { keyRoot, 1 }, "Key root", roots, 0, nonAuto));
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { keyScale, 1 }, "Key scale", StringArray { "Major", "Minor" }, 0, nonAuto));

    // --- M3 per slot ---
    for (int s = 0; s < engine::kNumSlots; ++s)
    {
        const String name = "Slot " + String (s + 1) + " ";
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { slotParam (s, slotGainSuffix), 1 }, name + "gain",
            NormalisableRange<float> (kMinGainDb, kMaxGainDb, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { slotParam (s, slotMuteSuffix), 1 }, name + "mute", false));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { slotParam (s, slotWeightSuffix), 1 }, name + "weight",
            NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, AudioParameterFloatAttributes().withLabel ("%")));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { slotParam (s, slotStartOffsetSuffix), 1 }, name + "start offset",
            NormalisableRange<float> (-16.0f, 16.0f, 0.25f), 0.0f, AudioParameterFloatAttributes().withLabel ("bars")));
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { slotParam (s, slotLoopSuffix), 1 }, name + "loop", true));

        // M4 key, per slot (not automatable)
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { slotParam (s, slotKeyModeSuffix), 1 }, name + "key mode", StringArray { "Snap", "Original" }, 0, nonAuto));
        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { slotParam (s, slotDirectionSuffix), 1 }, name + "direction", StringArray { "Nearest", "Up", "Down" }, 0, nonAuto));
        layout.add (std::make_unique<AudioParameterInt> (
            ParameterID { slotParam (s, slotTransposeSuffix), 1 }, name + "transpose", -12, 12, 0,
            AudioParameterIntAttributes().withAutomatable (false).withLabel ("st")));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { slotParam (s, slotFineSuffix), 1 }, name + "fine",
            NormalisableRange<float> (-50.0f, 50.0f, 1.0f), 0.0f,
            AudioParameterFloatAttributes().withAutomatable (false).withLabel ("ct")));
    }

    return layout;
}

} // namespace mosaic::params
