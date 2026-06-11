#include "PatcherEditor.h"

namespace dg
{

static const Identifier kNodeId ("NODE"), kCableId ("CABLE");
static const Identifier kArgs ("args"), kSrcPort ("srcPort"), kDstPort ("dstPort"), kDst ("dst");

static int portCount (const String& type, bool outs)
{
    for (const auto& s : PatcherProcessor::specs())
        if (type == s.name) return outs ? s.outs : s.ins;
    return 0;
}

// =========================================================================== NodeComp
class PatcherEditor::NodeComp : public juce::Component
{
public:
    NodeComp (PatcherEditor& ed, ValueTree n) : editor (ed), node (n)
    {
        setSize (110, 40);
    }

    String uid() const { return node[id::uid].toString(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (col::panelHi);
        g.fillRoundedRectangle (r, 3.0f);
        const String type = node[id::type];
        g.setColour (type.endsWith ("~") ? col::accent : col::accent2);
        g.drawRoundedRectangle (r, 3.0f, 1.2f);

        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain)));
        g.drawText ((type + " " + node[kArgs].toString()).trim(),
                    getLocalBounds().reduced (6, 0), juce::Justification::centredLeft);

        // inlets (top) / outlets (bottom)
        g.setColour (col::text);
        const int ins = portCount (type, false), outs = portCount (type, true);
        for (int i = 0; i < ins; ++i)
            g.fillRect (portX (i, ins) - 4, 0, 8, 4);
        for (int o = 0; o < outs; ++o)
            g.fillRect (portX (o, outs) - 4, getHeight() - 4, 8, 4);
    }

    int portX (int idx, int count) const
    {
        return (int) (((float) idx + 0.5f) / (float) juce::jmax (1, count) * (float) getWidth());
    }

    int inletAt (juce::Point<int> p) const
    {
        const int ins = portCount (node[id::type].toString(), false);
        if (p.y > 10 || ins == 0) return -1;
        for (int i = 0; i < ins; ++i)
            if (std::abs (p.x - portX (i, ins)) < 14) return i;
        return -1;
    }

    int outletAt (juce::Point<int> p) const
    {
        const int outs = portCount (node[id::type].toString(), true);
        if (p.y < getHeight() - 10 || outs == 0) return -1;
        for (int o = 0; o < outs; ++o)
            if (std::abs (p.x - portX (o, outs)) < 14) return o;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Delete object");
            auto* ed = &editor;
            ValueTree n = node;
            m.showMenuAsync ({}, [ed, n] (int r)
            {
                if (r == 1) { ed->patcher.removeNode (n); ed->rebuildNodes(); }
            });
            return;
        }
        const int o = outletAt (e.getPosition());
        if (o >= 0)
        {
            editor.beginCable (uid(), o, editor.getLocalPoint (this, e.position));
            return;
        }
        dragger.startDraggingComponent (this, e);
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
        {
            editor.dragCable (editor.getLocalPoint (this, e.position));
            return;
        }
        dragger.dragComponent (this, e, nullptr);
        node.setProperty (id::x, getX(), nullptr);
        node.setProperty (id::y, getY(), nullptr);
        editor.repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! dragging)
            editor.endCable (editor.getLocalPoint (this, e.position));
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        // retype the object in place
        auto* w = new juce::AlertWindow ("Edit object", "name + args", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("t", (node[id::type].toString() + " " + node[kArgs].toString()).trim());
        w->addButton ("OK", 1); w->addButton ("Cancel", 0);
        auto* ed = &editor;
        ValueTree n = node;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([ed, n, w] (int res) mutable
        {
            if (res != 1) return;
            auto tokens = juce::StringArray::fromTokens (w->getTextEditorContents ("t").trim(), " ", "");
            if (tokens.isEmpty() || PatcherProcessor::parseType (tokens[0]) == PatcherProcessor::oUnknown)
                return;
            n.setProperty (id::type, tokens[0], nullptr);
            tokens.remove (0);
            n.setProperty (kArgs, tokens.joinIntoString (" "), nullptr);
            ed->rebuildNodes();
        }), true);
    }

    ValueTree node;

private:
    PatcherEditor& editor;
    juce::ComponentDragger dragger;
    bool dragging = false;
};

// =========================================================================== PatcherEditor

PatcherEditor::PatcherEditor (PatcherProcessor& p)
    : juce::AudioProcessorEditor (p), patcher (p)
{
    for (int i = 0; i < 8; ++i)
    {
        pKnobs[i].setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pKnobs[i].setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible (pKnobs[i]);
        pAtts[i] = std::make_unique<juce::SliderParameterAttachment> (*patcher.hostParams[i], pKnobs[i]);
        pLabels[i].setText ("P" + String (i + 1), juce::dontSendNotification);
        pLabels[i].setFont (juce::Font (juce::FontOptions (9.0f)));
        pLabels[i].setJustificationType (juce::Justification::centred);
        addAndMakeVisible (pLabels[i]);
    }

    objEntry.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain)));
    objEntry.onReturnKey = [this]
    {
        const auto pos = objEntry.getBounds().getPosition();
        patcher.addNode (objEntry.getText(), pos.x, pos.y);
        objEntry.setVisible (false);
        rebuildNodes();
    };
    objEntry.onEscapeKey = [this] { objEntry.setVisible (false); };
    objEntry.onFocusLost = [this] { objEntry.setVisible (false); };
    addChildComponent (objEntry);

    String names;
    for (const auto& s : PatcherProcessor::specs())
        names += String (s.name) + "  ";
    hint.setText ("double-click: new object  |  " + names, juce::dontSendNotification);
    hint.setFont (juce::Font (juce::FontOptions (10.0f)));
    hint.setColour (juce::Label::textColourId, col::dim);
    addAndMakeVisible (hint);

    setResizable (true, true);
    setResizeLimits (520, 360, 1600, 1200);
    setSize (760, 520);
    startTimerHz (10);
    rebuildNodes();
}

PatcherEditor::~PatcherEditor() = default;

void PatcherEditor::timerCallback()
{
    // editor follows external patch changes (state recall, second editor)
    int h = 0;
    for (const auto& c : patcher.patch)
        h = h * 31 + c.getType().toString().hashCode()
              + c[id::uid].toString().hashCode() + c[id::type].toString().hashCode();
    if (h != lastPatchHash)
        rebuildNodes();
    repaint();
}

void PatcherEditor::rebuildNodes()
{
    nodeComps.clear();
    int h = 0;
    for (const auto& n : patcher.patch)
    {
        h = h * 31 + n.getType().toString().hashCode()
              + n[id::uid].toString().hashCode() + n[id::type].toString().hashCode();
        if (! n.hasType (kNodeId)) continue;
        auto* nc = nodeComps.add (new NodeComp (*this, n));
        nc->setTopLeftPosition ((int) n.getProperty (id::x, 40), (int) n.getProperty (id::y, 40));
        addAndMakeVisible (nc);
    }
    lastPatchHash = h;
    repaint();
}

juce::Point<float> PatcherEditor::outletPos (const String& uid, int port) const
{
    for (auto* nc : nodeComps)
        if (nc->uid() == uid)
        {
            const int outs = portCount (nc->node[id::type].toString(), true);
            return nc->getBounds().toFloat().getTopLeft()
                   + juce::Point<float> ((float) nc->portX (port, outs), (float) nc->getHeight());
        }
    return {};
}

juce::Point<float> PatcherEditor::inletPos (const String& uid, int port) const
{
    for (auto* nc : nodeComps)
        if (nc->uid() == uid)
        {
            const int ins = portCount (nc->node[id::type].toString(), false);
            return nc->getBounds().toFloat().getTopLeft()
                   + juce::Point<float> ((float) nc->portX (port, ins), 0.0f);
        }
    return {};
}

static juce::Path patchCable (juce::Point<float> a, juce::Point<float> b)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dy = juce::jmax (30.0f, std::abs (b.y - a.y) * 0.5f);
    p.cubicTo (a.translated (0, dy), b.translated (0, -dy), b);
    return p;
}

void PatcherEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

void PatcherEditor::paintOverChildren (juce::Graphics& g)
{
    for (const auto& c : patcher.patch)
    {
        if (! c.hasType (kCableId)) continue;
        const auto a = outletPos (c[id::src].toString(), (int) c[kSrcPort]);
        const auto b = inletPos (c[kDst].toString(), (int) c[kDstPort]);
        if (a.isOrigin() || b.isOrigin()) continue;
        g.setColour (col::accent.withAlpha (0.8f));
        g.strokePath (patchCable (a, b), juce::PathStrokeType (1.8f));
    }
    if (draggingCable)
    {
        g.setColour (col::text.withAlpha (0.8f));
        g.strokePath (patchCable (cableFrom, cableTo), juce::PathStrokeType (1.6f));
    }
}

void PatcherEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    objEntry.setBounds (e.x, e.y, 180, 22);
    objEntry.setText ({});
    objEntry.setVisible (true);
    objEntry.grabKeyboardFocus();
}

ValueTree PatcherEditor::cableAt (juce::Point<float> p) const
{
    for (const auto& c : patcher.patch)
    {
        if (! c.hasType (kCableId)) continue;
        const auto a = outletPos (c[id::src].toString(), (int) c[kSrcPort]);
        const auto b = inletPos (c[kDst].toString(), (int) c[kDstPort]);
        if (a.isOrigin() || b.isOrigin()) continue;
        auto path = patchCable (a, b);
        juce::Point<float> nearest;
        path.getNearestPoint (p, nearest);
        if (nearest.getDistanceFrom (p) < 7.0f)
            return c;
    }
    return {};
}

void PatcherEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        auto c = cableAt (e.position);
        if (c.isValid())
            patcher.patch.removeChild (c, nullptr);
        repaint();
    }
}

void PatcherEditor::mouseUp (const juce::MouseEvent& e)
{
    if (draggingCable)
        endCable (e.position);
}

void PatcherEditor::beginCable (const String& srcUid, int port, juce::Point<float> from)
{
    cableSrc = srcUid;
    cableSrcPort = port;
    cableFrom = outletPos (srcUid, port);
    cableTo = from;
    draggingCable = true;
    repaint();
}

void PatcherEditor::dragCable (juce::Point<float> p)
{
    if (! draggingCable) return;
    cableTo = p;
    repaint();
}

void PatcherEditor::endCable (juce::Point<float> p)
{
    if (! draggingCable) return;
    draggingCable = false;
    for (auto* nc : nodeComps)
    {
        if (! nc->getBounds().toFloat().expanded (4.0f).contains (p)) continue;
        const int inlet = nc->inletAt (nc->getLocalPoint (this, p.toInt()));
        const int fallback = portCount (nc->node[id::type].toString(), false) > 0 ? 0 : -1;
        const int use = inlet >= 0 ? inlet : fallback;
        if (use >= 0 && nc->uid() != cableSrc)
            patcher.addCable (cableSrc, cableSrcPort, nc->uid(), use);
        break;
    }
    repaint();
}

void PatcherEditor::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (54).reduced (6, 2);
    const int kw = juce::jmin (64, top.getWidth() / 8);
    for (int i = 0; i < 8; ++i)
    {
        auto cell = top.removeFromLeft (kw);
        pLabels[i].setBounds (cell.removeFromBottom (12));
        pKnobs[i].setBounds (cell);
    }
    hint.setBounds (getLocalBounds().removeFromBottom (18).reduced (6, 0));
}

} // namespace dg
