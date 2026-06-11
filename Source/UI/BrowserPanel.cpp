#include "BrowserPanel.h"

namespace dg
{

// =========================================================================== FileBin

FileBin::FileBin (AudioEngine& e, juce::PropertiesFile* p) : engine (e), props (p)
{
    thread.startThread (juce::Thread::Priority::low);

    File root = File::getSpecialLocation (File::userMusicDirectory);
    if (props != nullptr)
    {
        const File saved (props->getValue ("binFolder"));
        if (saved.isDirectory()) root = saved;
    }
    contents.setDirectory (root, true, true);

    tree = std::make_unique<juce::FileTreeComponent> (contents);
    tree->setDragAndDropDescription ("binfiles");
    tree->addListener (this);
    tree->setColour (juce::TreeView::backgroundColourId, col::panel);
    addAndMakeVisible (*tree);

    folderBtn.onClick = [this]
    {
        juce::FileChooser fc ("Bin folder", contents.getDirectory());
        if (fc.browseForDirectory())
        {
            contents.setDirectory (fc.getResult(), true, true);
            browserRootChanged (fc.getResult());
        }
    };
    addAndMakeVisible (folderBtn);

    pathLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    pathLabel.setColour (juce::Label::textColourId, col::dim);
    pathLabel.setText (root.getFullPathName(), juce::dontSendNotification);
    addAndMakeVisible (pathLabel);
}

FileBin::~FileBin()
{
    tree->removeListener (this);
    tree.reset();           // before contents/thread go down
}

void FileBin::browserRootChanged (const File& newRoot)
{
    pathLabel.setText (newRoot.getFullPathName(), juce::dontSendNotification);
    if (props != nullptr)
        props->setValue ("binFolder", newRoot.getFullPathName());
}

void FileBin::fileDoubleClicked (const File& f)
{
    if (! f.existsAsFile()) return;
    if (engine.getPreviewFile() == f)
        engine.stopPreview();
    else
        engine.startPreview (f);        // double-click again to stop
}

void FileBin::resized()
{
    auto b = getLocalBounds().reduced (4);
    auto top = b.removeFromTop (24);
    folderBtn.setBounds (top.removeFromLeft (90));
    top.removeFromLeft (6);
    pathLabel.setBounds (top);
    b.removeFromTop (2);
    tree->setBounds (b);
}

// =========================================================================== FxExplorer

FxExplorer::FxExplorer (PluginHost& h) : host (h)
{
    search.setTextToShowWhenEmpty ("search...", col::dim);
    search.onTextChange = [this] { refresh(); };
    addAndMakeVisible (search);

    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, col::panel);
    addAndMakeVisible (list);

    refresh();
}

void FxExplorer::refresh()
{
    entries.clear();
    const String q = search.getText().trim().toLowerCase();
    auto matches = [&q] (const String& s) { return q.isEmpty() || s.toLowerCase().contains (q); };

    if (matches (names::rackName))
        entries.push_back ({ String (names::rackName) + "  - corruption rack", "fx:rack" });
    if (matches (names::patcherName))
        entries.push_back ({ String (names::patcherName) + "  - patcher (max-style objects)", "fx:patcher" });

    entries.push_back ({ "BUILT-IN INSTRUMENTS", {}, true });
    if (matches ("glitchtone")) entries.push_back ({ "GlitchTone  - default saw", "fx:builtin:glitchtone" });
    if (matches ("rust"))       entries.push_back ({ "RUST  - FM bell/metal", "fx:builtin:rust" });
    if (matches ("gravel"))     entries.push_back ({ "GRAVEL  - noise percussion", "fx:builtin:gravel" });
    if (matches ("hymn"))       entries.push_back ({ "HYMN  - detuned pad", "fx:builtin:hymn" });
    if (matches ("rubble"))     entries.push_back ({ "RUBBLE  - drum kit", "fx:builtin:rubble" });

    auto types = host.knownList.getTypes();
    if (types.size() > 0)
        entries.push_back ({ "PLUGINS", {}, true });
    for (const auto& d : types)
        if (matches (d.name + " " + d.manufacturerName))
            entries.push_back ({ d.name + (d.isInstrument ? "  [inst]" : "") + "  (" + d.pluginFormatName + ")",
                                 "fx:plug:" + d.createIdentifierString() });

    list.updateContent();
    list.repaint();
}

void FxExplorer::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= (int) entries.size()) return;
    const auto& e = entries[(size_t) row];
    if (e.header)
    {
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
        g.drawText (e.label, 6, 0, w - 12, h, juce::Justification::centredLeft);
        return;
    }
    if (selected)
    {
        g.setColour (col::accent.withAlpha (0.25f));
        g.fillRect (0, 0, w, h);
    }
    g.setColour (e.fxId == "fx:rack" ? col::accent : col::text);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (e.label, 8, 0, w - 16, h, juce::Justification::centredLeft);
}

juce::var FxExplorer::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.size() > 0)
    {
        const int r = rows[0];
        if (r >= 0 && r < (int) entries.size() && ! entries[(size_t) r].header)
            return entries[(size_t) r].fxId;
    }
    return {};
}

void FxExplorer::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < (int) entries.size() && ! entries[(size_t) row].header && onApply)
        onApply (entries[(size_t) row].fxId);
}

void FxExplorer::resized()
{
    auto b = getLocalBounds().reduced (4);
    search.setBounds (b.removeFromTop (24));
    b.removeFromTop (2);
    list.setBounds (b);
}

} // namespace dg
