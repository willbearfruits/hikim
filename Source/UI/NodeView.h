#pragma once
#include "../Engine/AudioEngine.h"
#include "../Patcher/PatcherProcessor.h"
#include "../Patcher/PatcherEditor.h"
#include "Look.h"

namespace dg
{

// The PATCHER view as one navigable canvas (NODES.md "three altitudes"): a
// breadcrumb bar over a swappable node editor. Root = the session graph;
// EDIT on a WIRES device dives into that device's patch, and the breadcrumb
// climbs back. Non-node devices (plugins, TEETH) still open their own window.
class NodeView : public juce::Component
{
public:
    NodeView (AudioEngine& e, SessionModel& s) : engine (e), session (s) { showRoot(); }

    void showRoot()
    {
        divedInsert.clear();
        deviceName.clear();
        setEditorFor (engine.getSessionGraph());
    }

    // dive into a WIRES device's patch; returns false (and does nothing) if the
    // insert isn't a node device
    bool dive (const String& insertUid, const String& name)
    {
        auto* pp = dynamic_cast<PatcherProcessor*> (engine.getInsertProcessor (insertUid));
        if (pp == nullptr) return false;
        divedInsert = insertUid;
        deviceName = name.isEmpty() ? String (names::patcherName) : name;
        setEditorFor (pp);
        return true;
    }

    // engine fires before a device processor is destroyed: climb out first so
    // the editor dies before its processor
    void insertRemoved (const String& insertUid) { if (insertUid == divedInsert) showRoot(); }

    void paint (juce::Graphics& g) override
    {
        auto bar = getLocalBounds().removeFromTop (kBar);
        g.setColour (col::panel);
        g.fillRect (bar);
        g.setColour (col::line);
        g.drawHorizontalLine (kBar - 1, 0.0f, (float) getWidth());

        auto row = bar.reduced (8, 0);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        sessionRect = row.removeFromLeft (74);
        g.setColour (divedInsert.isEmpty() ? col::accent2 : col::accent);
        g.drawText ("SESSION", sessionRect, juce::Justification::centredLeft);
        if (! divedInsert.isEmpty())
        {
            g.setColour (col::dim);
            g.drawText (">", row.removeFromLeft (16), juce::Justification::centred);
            g.setColour (col::accent2);
            g.drawText (deviceName, row, juce::Justification::centredLeft);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! divedInsert.isEmpty() && sessionRect.contains (e.getPosition()))
            showRoot();                          // click SESSION to climb out
    }

    void resized() override
    {
        auto b = getLocalBounds();
        b.removeFromTop (kBar);
        if (editor != nullptr) editor->setBounds (b);
    }

private:
    static constexpr int kBar = 22;

    void setEditorFor (PatcherProcessor* pp)
    {
        editor.reset();                          // old editor dies before the new one
        if (pp != nullptr)
        {
            editor.reset (pp->createEditor());
            addAndMakeVisible (*editor);
            // pset target-picker only at the session altitude (only the session
            // graph's psets are gathered by the engine; device psets wouldn't apply)
            if (auto* pe = dynamic_cast<PatcherEditor*> (editor.get()); pe != nullptr
                    && pp == engine.getSessionGraph())
                pe->onPickTarget = [this] (ValueTree n) { pickTarget (n); };
        }
        resized();
        repaint();
    }

    // build a session-parameter menu and write "<track#> <target>" into the pset
    void pickTarget (ValueTree psetNode)
    {
        juce::PopupMenu menu;
        juce::StringArray entries;               // "<track#>\t<target>"
        int nextId = 1, idx = 0;
        for (const auto& track : session.tracks())
        {
            const String type = track[id::type];
            if (type == "video") continue;
            ++idx;                               // 1-based, matches resolveTrackRef
            const int trackNum = idx;
            juce::PopupMenu sub;
            auto add = [&] (const String& label, const String& target)
            {
                sub.addItem (nextId++, label);
                entries.add (String (trackNum) + "\t" + target);
            };
            add ("Volume", "strip:gain");
            add ("Pan", "strip:pan");
            add ("Mute", "strip:mute");
            if (type == "audio" || type == "midi") { add ("Send A", "send:A"); add ("Send B", "send:B"); }
            for (const auto& ins : SessionModel::insertsOf (track))
                if (auto* proc = engine.getInsertProcessor (ins[id::uid].toString()))
                {
                    juce::PopupMenu insSub;
                    const auto& params = proc->getParameters();
                    const int count = juce::jmin (64, params.size());
                    for (int i = 0; i < count; ++i)
                    {
                        insSub.addItem (nextId++, params[i]->getName (40));
                        entries.add (String (trackNum) + "\tins:" + ins[id::uid].toString() + ":" + String (i));
                    }
                    if (count > 0) sub.addSubMenu (ins[id::name].toString(), insSub);
                }
            menu.addSubMenu (track[id::name].toString(), sub);
        }

        ValueTree n = psetNode;
        menu.showMenuAsync ({}, [n, entries] (int r) mutable
        {
            if (r <= 0 || r > entries.size()) return;
            auto parts = juce::StringArray::fromTokens (entries[r - 1], "\t", "");
            if (parts.size() == 2)
                n.setProperty (juce::Identifier ("args"), parts[0] + " " + parts[1], nullptr);
        });
    }

    AudioEngine& engine;
    SessionModel& session;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    String divedInsert, deviceName;
    juce::Rectangle<int> sessionRect;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeView)
};

} // namespace dg
