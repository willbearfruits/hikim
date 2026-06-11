#pragma once
#include <JuceHeader.h>

// Naming slots — rename the whole app in one pass here.
namespace dg::names
{
    static constexpr const char* appName  = "RUIN";
    static constexpr const char* rackName = "TEETH";
    static constexpr const char* projectExtension = ".dgproj";
}

namespace dg
{
    using juce::ValueTree;
    using juce::Identifier;
    using juce::String;
    using juce::File;
    using juce::var;
}
