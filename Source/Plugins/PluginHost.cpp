#include "PluginHost.h"

namespace dg
{

PluginHost::PluginHost (juce::PropertiesFile* appProps) : props (appProps)
{
    juce::addDefaultFormatsToManager (formatManager);   // VST3 everywhere, AU on macOS, LV2

    if (props != nullptr)
    {
        deadMansPedal = props->getFile().getSiblingFile ("plugin-scan-crash.txt");
        if (auto xml = props->getXmlValue ("knownPlugins"))
            knownList.recreateFromXml (*xml);
    }
    knownList.addChangeListener (this);
}

PluginHost::~PluginHost()
{
    knownList.removeChangeListener (this);
}

void PluginHost::changeListenerCallback (juce::ChangeBroadcaster*)
{
    if (props != nullptr)
        if (auto xml = knownList.createXml())
            props->setValue ("knownPlugins", xml.get());
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::createInstance (const juce::PluginDescription& desc,
                                                                       double sampleRate, int blockSize,
                                                                       String& error)
{
    return formatManager.createPluginInstance (desc, sampleRate, blockSize, error);
}

std::unique_ptr<juce::PluginDescription> PluginHost::findByIdentifier (const String& ident) const
{
    if (ident.isEmpty()) return nullptr;
    return knownList.getTypeForIdentifierString (ident);
}

} // namespace dg
