#include "PatcherEditor.h"

namespace dg
{

static const Identifier kNodeId ("NODE"), kCableId ("CABLE");
static const Identifier kArgs ("args"), kSrcPort ("srcPort"), kDstPort ("dstPort"), kDst ("dst");

static int portCount (const String& type, bool outs)
{
    if (auto* s = PatcherProcessor::specFor (type))
        return outs ? s->outs : s->ins;
    return 0;
}

static juce::Colour famColour (PatcherProcessor::Family f)
{
    switch (f)
    {
        case PatcherProcessor::famSource:  return col::nodeSource;
        case PatcherProcessor::famEffect:  return col::nodeEffect;
        case PatcherProcessor::famMath:    return col::nodeMath;
        case PatcherProcessor::famTime:    return col::nodeTime;
        case PatcherProcessor::famRouting: return col::nodeRouting;
    }
    return col::accent;
}

static juce::Colour famColour (const String& objName)
{
    if (auto* s = PatcherProcessor::specFor (objName))
        return famColour (s->fam);
    return col::accent;
}

static char portType (const String& objName, bool out, int idx)   // 's' / 'n' / 'e'
{
    if (auto* s = PatcherProcessor::specFor (objName))
    {
        const char* t = out ? s->outTypes : s->inTypes;
        for (int i = 0; t[i] != 0; ++i)
            if (i == idx) return t[i];
    }
    return 's';
}

static juce::Colour cableColour (char type)
{
    return type == 'n' ? col::nodeMath
         : type == 'e' ? col::nodeTime
         : col::accent;
}

// =========================================================================== NodeComp
class PatcherEditor::NodeComp : public juce::Component
{
public:
    NodeComp (PatcherEditor& ed, ValueTree n) : editor (ed), node (n)
    {
        isNumber = node[id::type].toString() == "number";
        setSize (isNumber ? 86 : 110, 40);
    }

    String uid() const { return node[id::uid].toString(); }

    void paint (juce::Graphics& g) override
    {
        // LOD rides the canvas zoom: far = chip (the name is the face),
        // mid = name + ports, near = full face with the object's help line
        const float zoom = editor.canvasZoom();
        const bool chip = zoom < 0.6f && ! isNumber;            // (far/near are windef.h macros)
        const bool full = zoom >= 1.4f && ! isNumber;

        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const String type = node[id::type];
        const auto fam = isNumber ? col::play : famColour (type);

        g.setColour (chip ? fam.withAlpha (0.18f) : col::panelHi);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (fam);
        g.drawRoundedRectangle (r, 3.0f, isNumber ? 1.6f : chip ? 1.8f : 1.2f);

        // chan~ face: a live meter from the tapped ring (alive at every LOD)
        auto drawMeter = [&]
        {
            if (type != "chan~") return;
            auto tapPtr = editor.patcher.chanTapForNode (uid());
            if (tapPtr == nullptr) return;
            auto m = getLocalBounds().toFloat().reduced (7.0f, 0.0f)
                         .removeFromBottom (10.0f).removeFromTop (3.0f);
            g.setColour (col::line.withAlpha (0.8f));
            g.fillRect (m);
            g.setColour (col::play);
            g.fillRect (m.removeFromLeft (m.getWidth()
                            * juce::jlimit (0.0f, 1.0f, tapPtr->peak())));
        };

        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                                  isNumber ? 15.0f : chip ? 14.0f : 12.0f,
                                                  chip ? juce::Font::bold : juce::Font::plain)));
        if (isNumber)
        {
            const float v = editor.patcher.numberValueFor (uid(),
                                node[kArgs].toString().getFloatValue())->load();
            g.drawText (String (v, 3), getLocalBounds().reduced (8, 0),
                        juce::Justification::centredRight);
            g.setColour (col::dim);
            g.setFont (juce::Font (juce::FontOptions (8.5f)));
            g.drawText ("#", 5, 0, 12, getHeight(), juce::Justification::centredLeft);
        }
        else if (chip)
        {
            g.drawText (type, getLocalBounds(), juce::Justification::centred);
            drawMeter();
            return;                                 // chip: no ports, no args
        }
        else if (full)
        {
            g.drawText ((type + " " + node[kArgs].toString()).trim(),
                        getLocalBounds().reduced (6, 0).removeFromTop (24),
                        juce::Justification::centredLeft);
            if (auto* spec = PatcherProcessor::specFor (type); spec != nullptr && type != "chan~")
            {
                g.setColour (col::dim);
                g.setFont (juce::Font (juce::FontOptions (8.0f)));
                g.drawText (spec->desc, getLocalBounds().reduced (6, 0).removeFromBottom (16),
                            juce::Justification::centredLeft);
            }
        }
        else
            g.drawText ((type + " " + node[kArgs].toString()).trim(),
                        getLocalBounds().reduced (6, 0), juce::Justification::centredLeft);
        drawMeter();

        // typed ports (NODES.md shapes: signal dome, number square, event triangle);
        // inlets on top glow while a cable is dragging
        const int ins = portCount (type, false), outs = portCount (type, true);
        const bool glow = editor.isCableDragging() && ins > 0;
        for (int i = 0; i < ins; ++i)
            drawPort (g, (float) portX (i, ins), true, portType (type, false, i), glow);
        for (int o = 0; o < outs; ++o)
            drawPort (g, (float) portX (o, outs), false, portType (type, true, o), false);
    }

    void drawPort (juce::Graphics& g, float cx, bool inlet, char t, bool glow) const
    {
        const float h = (float) getHeight();
        g.setColour (glow ? col::play : cableColour (t).interpolatedWith (col::text, t == 's' ? 1.0f : 0.25f));
        const float s = glow ? 1.5f : 1.0f;
        if (t == 'n')
            g.fillRect (cx - 3.5f * s, inlet ? 0.0f : h - 5.0f * s, 7.0f * s, 5.0f * s);
        else if (t == 'e')
        {
            juce::Path p;
            if (inlet) { p.addTriangle (cx - 4.5f * s, 0.0f, cx + 4.5f * s, 0.0f, cx, 5.5f * s); }
            else       { p.addTriangle (cx - 4.5f * s, h - 5.5f * s, cx + 4.5f * s, h - 5.5f * s, cx, h); }
            g.fillPath (p);
        }
        else    // signal: a dome on the edge (clipped half-ellipse)
            g.fillEllipse (cx - 4.5f * s, inlet ? -4.5f * s : h - 4.5f * s, 9.0f * s, 9.0f * s);
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

    juce::Point<float> toCanvas (juce::Point<float> p) const
    {
        return getParentComponent()->getLocalPoint (this, p);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        valueDragging = false;
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
            editor.beginCable (uid(), o, toCanvas (e.position));
            return;
        }
        if (isNumber && ! e.mods.isCtrlDown())
        {
            // the number box's primary gesture: drag the value (Ctrl-drag moves)
            valueDragging = true;
            valueAtDragStart = editor.patcher.numberValueFor (uid(),
                                   node[kArgs].toString().getFloatValue())->load();
            return;
        }
        dragger.startDraggingComponent (this, e);
        dragging = true;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (valueDragging)
        {
            const float step = e.mods.isShiftDown() ? 0.001f : 0.01f;
            const float v = valueAtDragStart - (float) e.getDistanceFromDragStartY() * step;
            editor.patcher.numberValueFor (uid())->store (v);
            repaint();
            return;
        }
        if (! dragging)
        {
            editor.dragCable (toCanvas (e.position));
            return;
        }
        dragger.dragComponent (this, e, nullptr);
        node.setProperty (id::x, getX(), nullptr);
        node.setProperty (id::y, getY(), nullptr);
        getParentComponent()->repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (valueDragging)
        {
            // persist the value into the patch (saved with the device state)
            node.setProperty (kArgs, String (editor.patcher.numberValueFor (uid())->load(), 4), nullptr);
            valueDragging = false;
            return;
        }
        if (! dragging)
            editor.endCable (toCanvas (e.position));
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (isNumber)
        {
            auto* w = new juce::AlertWindow ("number", "value", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("v", String (editor.patcher.numberValueFor (uid())->load(), 4));
            w->addButton ("OK", 1); w->addButton ("Cancel", 0);
            auto* ed = &editor;
            ValueTree n = node;
            w->enterModalState (true, juce::ModalCallbackFunction::create ([ed, n, w] (int res) mutable
            {
                if (res != 1) return;
                const float v = w->getTextEditorContents ("v").getFloatValue();
                ed->patcher.numberValueFor (n[id::uid].toString())->store (v);
                n.setProperty (kArgs, String (v, 4), nullptr);
            }), true);
            return;
        }
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
    bool isNumber = false;
    bool valueDragging = false;
    float valueAtDragStart = 0.0f;
};

// ============================================== NodeCanvas::Delegate (WIRES)

float PatcherEditor::canvasZoom() const
{
    return canvas != nullptr ? canvas->zoom : 1.0f;
}

void PatcherEditor::paintCables (juce::Graphics& g)
{
    for (const auto& c : patcher.patch)
    {
        if (! c.hasType (kCableId)) continue;
        const auto a = outletPos (c[id::src].toString(), (int) c[kSrcPort]);
        const auto b = inletPos (c[kDst].toString(), (int) c[kDstPort]);
        if (a.isOrigin() || b.isOrigin()) continue;
        // NODES.md cable table: signal thick, number thin solid, event thin
        char t = 's';
        for (auto* nc : nodeComps)
            if (nc->uid() == c[id::src].toString())
            { t = portType (nc->node[id::type].toString(), true, (int) c[kSrcPort]); break; }
        g.setColour (cableColour (t).withAlpha (t == 's' ? 0.8f : 0.9f));
        g.strokePath (NodeCanvas::cablePath (a, b), juce::PathStrokeType (t == 's' ? 2.4f : 1.3f));
    }
    if (draggingCable)
    {
        g.setColour (col::text.withAlpha (0.8f));
        g.strokePath (NodeCanvas::cablePath (cableFrom, cableTo), juce::PathStrokeType (1.6f));
    }
}

void PatcherEditor::canvasPopup (juce::Point<float> p)
{
    auto c = cableAt (p);
    if (c.isValid())
        patcher.patch.removeChild (c, nullptr);
}

void PatcherEditor::canvasMouseUp (juce::Point<float> p)
{
    if (draggingCable)
        endCable (p);
}

void PatcherEditor::canvasDoubleClicked (juce::Point<int> p)
{
    objEntry.setBounds (p.x, p.y, 180, 22);
    objEntry.setText ({});
    objEntry.setVisible (true);
    objEntry.grabKeyboardFocus();
}

// =========================================================================== ObjPalette
class PatcherEditor::ObjPalette : public juce::Component, private juce::ListBoxModel
{
public:
    explicit ObjPalette (PatcherEditor& ed) : editor (ed)
    {
        // one section per NODES.md family, in spec order
        static const std::pair<PatcherProcessor::Family, const char*> sections[] = {
            { PatcherProcessor::famSource,  "SOURCES" },
            { PatcherProcessor::famEffect,  "EFFECTS" },
            { PatcherProcessor::famMath,    "NUMBERS & MATH" },
            { PatcherProcessor::famTime,    "TIME & CHANCE" },
            { PatcherProcessor::famRouting, "ROUTING" },
        };
        for (const auto& [fam, name] : sections)
        {
            rows.push_back ({ nullptr, name });
            for (const auto& s : PatcherProcessor::specs())
                if (s.fam == fam)
                    rows.push_back ({ &s, nullptr });
        }

        title.setText ("OBJECTS", juce::dontSendNotification);
        title.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        title.setColour (juce::Label::textColourId, col::accent2);
        addAndMakeVisible (title);
        list.setModel (this);
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, col::panel);
        addAndMakeVisible (list);
    }

    int getNumRows() override { return (int) rows.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (row < 0 || row >= getNumRows()) return;
        const auto& r = rows[(size_t) row];
        if (r.spec == nullptr)              // section header
        {
            g.setColour (col::dim);
            g.setFont (juce::Font (juce::FontOptions (9.0f, juce::Font::bold)));
            g.drawText (r.header, 6, 0, w - 10, h - 6, juce::Justification::bottomLeft);
            g.setColour (col::line);
            g.drawHorizontalLine (h - 3, 4.0f, (float) w - 8.0f);
            return;
        }
        if (selected) { g.setColour (col::accent.withAlpha (0.2f)); g.fillRect (0, 0, w, h); }
        g.setColour (famColour (r.spec->fam));
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold)));
        g.drawText (r.spec->name, 6, 1, w - 10, 14, juce::Justification::left);
        g.setColour (col::dim);
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText (r.spec->desc, 6, 15, w - 10, 12, juce::Justification::left);
    }

    juce::var getDragSourceDescription (const juce::SparseSet<int>& selection) override
    {
        if (selection.size() > 0)
            if (const auto* s = rows[(size_t) selection[0]].spec)
                return "obj:" + String (s->name);
        return {};
    }

    void listBoxItemClicked (int row, const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked() && row >= 0)
            if (const auto* s = rows[(size_t) row].spec)
                editor.placeObject (s->name, { -1, -1 });
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::panel);
        g.setColour (col::line);
        g.drawVerticalLine (getWidth() - 1, 0, (float) getHeight());
    }

    void resized() override
    {
        auto b = getLocalBounds();
        title.setBounds (b.removeFromTop (18).reduced (6, 0));
        list.setBounds (b);
    }

private:
    struct Row { const PatcherProcessor::Spec* spec; const char* header; };

    PatcherEditor& editor;
    std::vector<Row> rows;
    juce::Label title;
    juce::ListBox list;
};

// =========================================================================== PatcherEditor

PatcherEditor::PatcherEditor (PatcherProcessor& p)
    : juce::AudioProcessorEditor (p), patcher (p)
{
    canvas.reset (new NodeCanvas (*this));     // private Delegate base: convert in member scope
    addAndMakeVisible (*canvas);
    canvas->pan = { kPaletteW, 54 };
    canvas->applyView();

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
    canvas->addChildComponent (objEntry);

    palette = std::make_unique<ObjPalette> (*this);
    addAndMakeVisible (*palette);

    hint.setText ("palette: click/drag to place - double-click canvas: type an object - ctrl-wheel zooms, drag space pans",
                  juce::dontSendNotification);
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
    canvas->repaint();
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
        nc->setTopLeftPosition (juce::jmax (4, (int) n.getProperty (id::x, 40)),
                                juce::jmax (4, (int) n.getProperty (id::y, 70)));
        canvas->addAndMakeVisible (nc);
    }
    lastPatchHash = h;
    repaint();
    canvas->repaint();
}

void PatcherEditor::placeObject (const String& specName, juce::Point<int> pos)
{
    String text = specName;
    for (const auto& s : PatcherProcessor::specs())
        if (specName == s.name && s.defaults[0] != 0)
            text += " " + String (s.defaults);

    juce::Point<int> cpos;
    if (pos.x < kPaletteW)      // palette click: cascade into the current view
    {
        cpos = canvas->getLocalPoint (this, juce::Point<int> (kPaletteW + 60, 80)).toInt()
               + juce::Point<int> ((placeStagger % 4) * 130, ((placeStagger / 4) % 5) * 70);
        ++placeStagger;
    }
    else
        cpos = canvas->getLocalPoint (this, pos);

    patcher.addNode (text, juce::jmax (4, cpos.x), juce::jmax (4, cpos.y));
    rebuildNodes();
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

void PatcherEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
}

ValueTree PatcherEditor::cableAt (juce::Point<float> p) const
{
    for (const auto& c : patcher.patch)
    {
        if (! c.hasType (kCableId)) continue;
        const auto a = outletPos (c[id::src].toString(), (int) c[kSrcPort]);
        const auto b = inletPos (c[kDst].toString(), (int) c[kDstPort]);
        if (a.isOrigin() || b.isOrigin()) continue;
        auto path = NodeCanvas::cablePath (a, b);
        juce::Point<float> nearest;
        path.getNearestPoint (p, nearest);
        if (nearest.getDistanceFrom (p) < 7.0f)
            return c;
    }
    return {};
}

void PatcherEditor::beginCable (const String& srcUid, int port, juce::Point<float> from)
{
    cableSrc = srcUid;
    cableSrcPort = port;
    cableFrom = outletPos (srcUid, port);
    cableTo = from;
    draggingCable = true;
    canvas->repaint();
}

void PatcherEditor::dragCable (juce::Point<float> p)
{
    if (! draggingCable) return;
    cableTo = p;
    canvas->repaint();
}

void PatcherEditor::endCable (juce::Point<float> p)
{
    if (! draggingCable) return;
    draggingCable = false;
    for (auto* nc : nodeComps)
    {
        if (! nc->getBounds().toFloat().expanded (4.0f).contains (p)) continue;
        const int inlet = nc->inletAt (nc->getLocalPoint (canvas.get(), p.toInt()));
        const int fallback = portCount (nc->node[id::type].toString(), false) > 0 ? 0 : -1;
        const int use = inlet >= 0 ? inlet : fallback;
        if (use >= 0 && nc->uid() != cableSrc)
            patcher.addCable (cableSrc, cableSrcPort, nc->uid(), use);
        break;
    }
    canvas->repaint();
}

void PatcherEditor::resized()
{
    auto b = getLocalBounds();
    auto top = b.removeFromTop (54).reduced (6, 2);
    top.removeFromLeft (kPaletteW);
    const int kw = juce::jmin (64, juce::jmax (30, top.getWidth() / 8));
    for (int i = 0; i < 8; ++i)
    {
        auto cell = top.removeFromLeft (kw);
        pLabels[i].setBounds (cell.removeFromBottom (12));
        pKnobs[i].setBounds (cell);
    }
    if (palette != nullptr)
        palette->setBounds (0, 0, kPaletteW, getHeight() - 18);
    hint.setBounds (getLocalBounds().removeFromBottom (18).reduced (kPaletteW + 6, 0));
}

} // namespace dg
