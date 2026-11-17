#include "plugin/PluginProcessor.h"
#include "plugin/PluginEditor.h"

namespace mosaic
{

MosaicAudioProcessor::MosaicAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

void MosaicAudioProcessor::prepareToPlay (double /*sampleRate*/, int /*samplesPerBlock*/)
{
    // Nothing to prepare yet. Later milestones allocate everything here, never in processBlock.
}

void MosaicAudioProcessor::releaseResources()
{
}

bool MosaicAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Output is always stereo. Input may be mono (Logic mono tracks) or stereo.
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in  = layouts.getMainInputChannelSet();

    if (out != juce::AudioChannelSet::stereo())
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void MosaicAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    if (numSamples == 0)
        return;

    // Pass-through. A mono input is copied to both outputs; extra outputs are cleared.
    if (numIn == 1 && numOut >= 2)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    for (int ch = juce::jmax (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);
}

juce::AudioProcessorEditor* MosaicAudioProcessor::createEditor()
{
    return new MosaicAudioProcessorEditor (*this);
}

void MosaicAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state ("MosaicState");
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

    // Nothing to restore yet. Future versions migrate from state["stateVersion"] here.
}

} // namespace mosaic

// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new mosaic::MosaicAudioProcessor();
}
