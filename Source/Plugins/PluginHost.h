#pragma once
#include "../Common.h"

namespace dg
{

// VST3 / AU / LV2 hosting: format registry, the known-plugin list (persisted in
// app settings), instantiation, and the dead-man's-pedal blacklist file used by
// the scanner UI. // EXTEND: out-of-process scanning for crashproof scans.
class PluginHost : private juce::ChangeListener
{
public:
    explicit PluginHost (juce::PropertiesFile* appProps);
    ~PluginHost() override;

    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription& desc,
                                                               double sampleRate, int blockSize,
                                                               String& error);
    std::unique_ptr<juce::PluginDescription> findByIdentifier (const String& identifierString) const;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownList;
    File deadMansPedal;
    juce::PropertiesFile* props = nullptr;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;   // persist list on change
};

} // namespace dg
