#pragma once
#include "Session.h"
#include "../Engine/TempoMap.h"
#include <set>

namespace dg::clipops
{
    // All clip editing operations live here, UI-free, so the test suite can
    // drive them exactly as the timeline does. Every op opens its own undo
    // transaction. Returned arrays are the uids of newly created clips.

    juce::StringArray splitAt (SessionModel&, const TempoMap&,
                               const std::set<String>& selectedUids, double timeSec);

    void deleteClips (SessionModel&, const std::set<String>& uids);
    void rippleDelete (SessionModel&, const std::set<String>& uids);

    struct ClipboardItem { String trackUid; ValueTree clip; };
    std::vector<ClipboardItem> copyClips (SessionModel&, const std::set<String>& uids);
    juce::StringArray paste (SessionModel&, const std::vector<ClipboardItem>&, double atSec);
    juce::StringArray duplicate (SessionModel&, const std::set<String>& uids);
}
