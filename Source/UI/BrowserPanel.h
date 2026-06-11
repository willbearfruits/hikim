#pragma once
#include "../Engine/AudioEngine.h"
#include "../Plugins/PluginHost.h"
#include "Look.h"

namespace dg
{

// FILES bin: browse a folder of audio, double-click to preview, drag into the
// timeline (drops land on the row/time under the cursor).
class FileBin : public juce::Component, private juce::FileBrowserListener
{
public:
    FileBin (AudioEngine&, juce::PropertiesFile*);
    ~FileBin() override;

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (col::panel); }

private:
    void selectionChanged() override {}
    void fileClicked (const File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const File&) override;
    void browserRootChanged (const File&) override;

    AudioEngine& engine;
    juce::PropertiesFile* props;
    juce::TimeSliceThread thread { "dg bin" };
    juce::WildcardFileFilter filter { "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.opus;*.aac;*.wv;*.wma;*.mp2;*.amr;*.mka", "*", "audio files" };
    juce::DirectoryContentsList contents { &filter, thread };
    std::unique_ptr<juce::FileTreeComponent> tree;
    juce::TextButton folderBtn { "FOLDER..." };
    juce::Label pathLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FileBin)
};

// FX explorer: TEETH, built-in instruments and every scanned plugin in one
// searchable list. Drag onto a track, or double-click to apply to the
// selected track.
class FxExplorer : public juce::Component, private juce::ListBoxModel
{
public:
    explicit FxExplorer (PluginHost&);

    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (col::panel); }
    void refresh();

    std::function<void (const String& fxId)> onApply;

private:
    int getNumRows() override { return (int) entries.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;

    PluginHost& host;
    struct Entry { String label, fxId; bool header = false; };
    std::vector<Entry> entries;
    juce::TextEditor search;
    juce::ListBox list { {}, this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FxExplorer)
};

} // namespace dg
