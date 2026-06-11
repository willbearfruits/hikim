#include "RoutingView.h"

namespace dg
{

static const char* kModNames[] = { "LFO 1", "LFO 2", "LFO 3", "LFO 4", "CHAOS", "FOLLOW" };
static const char* kModIds[] = { "lfo1", "lfo2", "lfo3", "lfo4", "chaos", "follower" };
static constexpr int kBoxW = 190;

RoutingView::RoutingView (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    startTimerHz (20);
}

RoutingView::~RoutingView() = default;

std::vector<RoutingView::Box> RoutingView::layoutBoxes()
{
    std::vector<Box> out;
    int idx = 0;
    for (auto track : session.tracks())
    {
        const String type = track[id::type];
        if (type == "video") continue;

        Box b;
        b.track = track;
        for (auto ins : SessionModel::insertsOf (track))
            if (ins[id::type].toString() == "instrument") b.chips.push_back (ins);
        for (auto ins : SessionModel::insertsOf (track))
            if (ins[id::type].toString() != "instrument") b.chips.push_back (ins);

        const int defX = 180 + (idx % 4) * (kBoxW + 30);
        const int defY = 40 + (idx / 4) * 170;
        const int x = (int) track.getProperty (id::x, defX);
        const int y = (int) track.getProperty (id::y, defY);
        const int h = 56 + (int) b.chips.size() * 18 + 14;
        b.r = { x, y, kBoxW, h };
        out.push_back (std::move (b));
        ++idx;
    }
    return out;
}

juce::Point<float> RoutingView::outPort (const Box& b) const
{
    return b.r.toFloat().getBottomLeft().translated ((float) kBoxW * 0.3f, 0.0f);
}
juce::Point<float> RoutingView::sendPort (const Box& b, int which) const
{
    return b.r.toFloat().getBottomRight().translated (which == 0 ? -52.0f : -24.0f, 0.0f);
}
juce::Point<float> RoutingView::inPort (const Box& b) const
{
    return b.r.toFloat().getTopLeft().translated ((float) kBoxW * 0.5f, 0.0f);
}
juce::Rectangle<int> RoutingView::modBox (int idx) const
{
    return { 8, 34 + idx * 56, 130, 48 };
}

const RoutingView::Box* RoutingView::boxAt (const std::vector<Box>& boxes, juce::Point<float> p) const
{
    for (const auto& b : boxes)
        if (b.r.toFloat().expanded (6.0f).contains (p))
            return &b;
    return nullptr;
}

static juce::Path routeCable (juce::Point<float> a, juce::Point<float> b)
{
    juce::Path p;
    p.startNewSubPath (a);
    const float dy = juce::jmax (28.0f, std::abs (b.y - a.y) * 0.45f);
    p.cubicTo (a.translated (0, dy), b.translated (0, -dy), b);
    return p;
}

void RoutingView::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto boxes = layoutBoxes();

    g.setColour (col::dim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("PATCHER  -  channels are boxes; drag output/send ports onto buses; drag a mod source onto a channel; double-click for a new track",
                8, 4, getWidth() - 16, 18, juce::Justification::centredLeft);

    auto findBox = [&boxes] (const String& uid) -> const Box*
    {
        for (const auto& b : boxes)
            if (b.track[id::uid].toString() == uid) return &b;
        return nullptr;
    };

    // ---- cables under the boxes ----
    for (const auto& b : boxes)
    {
        const String type = b.track[id::type];
        if (type == "master") continue;

        // main output
        const String outUid = type == "bus" ? "master" : b.track[id::outputBus].toString();
        if (auto* dest = findBox (outUid.isEmpty() ? "master" : outUid))
        {
            g.setColour (col::text.withAlpha (0.55f));
            g.strokePath (routeCable (outPort (b), inPort (*dest)), juce::PathStrokeType (2.0f));
        }
        // sends
        for (int sIdx = 0; sIdx < 2; ++sIdx)
        {
            const String busUid = b.track[sIdx == 0 ? id::sendABus : id::sendBBus].toString();
            if (busUid.isEmpty()) continue;
            if (auto* dest = findBox (busUid))
            {
                g.setColour (col::accent2.withAlpha (0.7f));
                g.strokePath (routeCable (sendPort (b, sIdx), inPort (*dest)), juce::PathStrokeType (1.3f));
            }
        }
    }

    // mod cables (dashed): source box -> the channel owning the target param
    auto modsTree = session.mods();
    for (const auto& m : modsTree)
    {
        if (! m.hasType (id::MOD)) continue;
        auto t = modsTree.getChildWithProperty (id::uid, m[id::target]);
        if (! t.isValid()) continue;
        int srcIdx = 5;
        for (int i = 0; i < 6; ++i)
            if (m[id::src].toString() == kModIds[i]) srcIdx = i;
        if (auto* dest = findBox (t[id::track].toString()))
        {
            auto a = modBox (srcIdx).toFloat().getCentre().translated (65.0f, 0.0f);
            auto path = routeCable (a, inPort (*dest));
            juce::Path dashed;
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, path, dashes, 2);
            g.setColour (col::clipMidi.withAlpha (0.4f + 0.6f * std::abs (engine.getModSourceValue (srcIdx))));
            g.fillPath (dashed);
        }
    }

    // ---- mod source boxes ----
    for (int i = 0; i < 6; ++i)
    {
        auto r = modBox (i);
        g.setColour (col::panel);
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (col::clipMidi.withAlpha (0.8f));
        g.drawRoundedRectangle (r.toFloat(), 4.0f, 1.0f);
        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (kModNames[i], r.reduced (8, 4), juce::Justification::topLeft);
        const float v = engine.getModSourceValue (i);
        g.setColour (col::clipMidi);
        g.fillRect ((float) r.getX() + 8, (float) r.getBottom() - 12,
                    juce::jmax (2.0f, std::abs (v) * (r.getWidth() - 16.0f)), 4.0f);
        g.setColour (col::clipMidi);
        g.fillEllipse ((float) r.getRight() - 8, (float) r.getCentreY() - 5, 10, 10);
    }

    // ---- channel boxes ----
    for (const auto& b : boxes)
    {
        const String type = b.track[id::type];
        const String uid = b.track[id::uid];
        const bool selected = ui.selectedTrack == uid;
        auto base = juce::Colour::fromString (b.track.getProperty (id::colour, "ff808080").toString());

        g.setColour (selected ? col::panelHi : col::panel);
        g.fillRoundedRectangle (b.r.toFloat(), 5.0f);
        g.setColour (selected ? col::accent : base.withAlpha (0.8f));
        g.drawRoundedRectangle (b.r.toFloat(), 5.0f, selected ? 1.8f : 1.2f);
        g.setColour (base);
        g.fillRect (b.r.getX(), b.r.getY(), b.r.getWidth(), 4);

        g.setColour (col::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (b.track[id::name].toString(), b.r.reduced (10, 8).removeFromTop (16),
                    juce::Justification::centredLeft);

        // live meter
        if (auto* strip = engine.getStrip (uid))
        {
            const float pk = juce::jmax (strip->peakL.load(), strip->peakR.load());
            const float db = juce::Decibels::gainToDecibels (pk, -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f);
            g.setColour (col::bg);
            g.fillRect (b.r.getX() + 10, b.r.getY() + 26, b.r.getWidth() - 20, 5);
            g.setColour (db > -12 ? col::accent2 : col::play);
            g.fillRect ((float) b.r.getX() + 10, (float) b.r.getY() + 26,
                        (b.r.getWidth() - 20) * frac, 5.0f);
        }

        // device chips
        int cy = b.r.getY() + 38;
        g.setFont (juce::Font (juce::FontOptions (10.0f)));
        for (const auto& chip : b.chips)
        {
            juce::Rectangle<int> cr (b.r.getX() + 10, cy, b.r.getWidth() - 20, 15);
            const String itype = chip[id::type];
            g.setColour (itype == "rack" ? col::accent.withAlpha (0.25f)
                         : itype == "patcher" ? col::play.withAlpha (0.2f)
                         : col::panelHi);
            g.fillRoundedRectangle (cr.toFloat(), 2.0f);
            g.setColour ((bool) chip[id::bypass] ? col::dim : col::text);
            String label = chip[id::name].toString();
            if (label.isEmpty()) label = itype;
            g.drawText (label, cr.reduced (4, 0), juce::Justification::centredLeft);
            cy += 18;
        }
        // "+" chip
        g.setColour (col::dim);
        g.drawText ("+ device", b.r.getX() + 10, cy, b.r.getWidth() - 20, 14, juce::Justification::centredLeft);

        // ports
        if (type != "master")
        {
            g.setColour (col::text);
            auto op = outPort (b);
            g.fillEllipse (op.x - 5, op.y - 5, 10, 10);
            g.setColour (col::accent2);
            for (int sIdx = 0; sIdx < 2; ++sIdx)
            {
                auto sp = sendPort (b, sIdx);
                g.fillEllipse (sp.x - 4, sp.y - 4, 8, 8);
                g.setFont (juce::Font (juce::FontOptions (8.0f)));
                g.drawText (sIdx == 0 ? "a" : "b", (int) sp.x - 4, (int) sp.y - 16, 8, 10,
                            juce::Justification::centred);
            }
        }
        if (type == "bus" || type == "master")
        {
            g.setColour (col::play);
            auto ip = inPort (b);
            g.fillEllipse (ip.x - 5, ip.y - 5, 10, 10);
        }
    }

    // live drag cable
    if (drag == Drag::out || drag == Drag::sendA || drag == Drag::sendB || drag == Drag::mod)
    {
        juce::Point<float> from;
        if (drag == Drag::mod)
            from = modBox (dragMod).toFloat().getCentre().translated (65.0f, 0.0f);
        else
        {
            for (const auto& b : boxes)
                if (b.track[id::uid].toString() == dragTrack)
                    from = drag == Drag::out ? outPort (b) : sendPort (b, drag == Drag::sendA ? 0 : 1);
        }
        g.setColour (col::text.withAlpha (0.85f));
        g.strokePath (routeCable (from, dragPos), juce::PathStrokeType (1.6f));
    }
}

void RoutingView::mouseDown (const juce::MouseEvent& e)
{
    auto boxes = layoutBoxes();
    const auto p = e.position;

    // mod source ports
    for (int i = 0; i < 6; ++i)
        if (modBox (i).toFloat().expanded (4.0f).contains (p))
        {
            drag = Drag::mod;
            dragMod = i;
            dragPos = p;
            return;
        }

    for (const auto& b : boxes)
    {
        const String type = b.track[id::type];
        if (type != "master")
        {
            if (outPort (b).getDistanceFrom (p) < 10) { drag = Drag::out; dragTrack = b.track[id::uid].toString(); dragPos = p; return; }
            if (sendPort (b, 0).getDistanceFrom (p) < 9) { drag = Drag::sendA; dragTrack = b.track[id::uid].toString(); dragPos = p; return; }
            if (sendPort (b, 1).getDistanceFrom (p) < 9) { drag = Drag::sendB; dragTrack = b.track[id::uid].toString(); dragPos = p; return; }
        }
        if (b.r.toFloat().contains (p))
        {
            ui.selectedTrack = b.track[id::uid].toString();

            // chip click -> open device editor; "+" -> fx menu
            int cy = b.r.getY() + 38;
            for (const auto& chip : b.chips)
            {
                if (juce::Rectangle<int> (b.r.getX() + 10, cy, b.r.getWidth() - 20, 15).contains (e.getPosition()))
                {
                    if (e.mods.isPopupMenu())
                    {
                        ValueTree c = chip, track = b.track;
                        auto* s = &session;
                        juce::PopupMenu m;
                        m.addItem (1, "Remove device");
                        m.showMenuAsync ({}, [c, track, s] (int r) mutable
                        { if (r == 1) SessionModel::insertsOf (track).removeChild (c, &s->undo); });
                    }
                    else if (ui.openInsertEditor)
                        ui.openInsertEditor (b.track[id::uid].toString(), chip[id::uid].toString());
                    return;
                }
                cy += 18;
            }
            if (juce::Rectangle<int> (b.r.getX() + 10, cy, b.r.getWidth() - 20, 14).contains (e.getPosition()))
            {
                if (showFxMenu) showFxMenu (b.track, this);
                return;
            }

            drag = Drag::moveBox;
            movingTrack = b.track;
            moveOffset = e.getPosition() - b.r.getPosition();
            return;
        }
    }
}

void RoutingView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::moveBox && movingTrack.isValid())
    {
        movingTrack.setProperty (id::x, juce::jmax (150, e.x - moveOffset.x), nullptr);
        movingTrack.setProperty (id::y, juce::jmax (28, e.y - moveOffset.y), nullptr);
        repaint();
        return;
    }
    if (drag != Drag::none)
    {
        dragPos = e.position;
        repaint();
    }
}

void RoutingView::mouseUp (const juce::MouseEvent& e)
{
    auto boxes = layoutBoxes();
    const auto* dest = boxAt (boxes, e.position);

    if (drag == Drag::out && dest != nullptr)
    {
        const String dtype = dest->track[id::type];
        if (dtype == "bus" || dtype == "master")
        {
            auto track = session.findTrack (dragTrack);
            if (track.isValid())
                track.setProperty (id::outputBus, dest->track[id::uid].toString(), &session.undo);
        }
    }
    else if ((drag == Drag::sendA || drag == Drag::sendB) && dest != nullptr)
    {
        const String dtype = dest->track[id::type];
        auto track = session.findTrack (dragTrack);
        if (track.isValid())
        {
            const auto prop = drag == Drag::sendA ? id::sendABus : id::sendBBus;
            if (dtype == "bus")
                track.setProperty (prop, dest->track[id::uid].toString(), &session.undo);
            else if (dest->track[id::uid].toString() == dragTrack || dtype == "master")
                track.setProperty (prop, "", &session.undo);     // drop on self/master clears
        }
    }
    else if (drag == Drag::mod && dest != nullptr)
    {
        const String dtype = dest->track[id::type];
        if (dtype != "master" || true)
            openModTargetMenu (dragMod, dest->track);
    }
    drag = Drag::none;
    movingTrack = {};
    repaint();
}

void RoutingView::openModTargetMenu (int srcIdx, ValueTree track)
{
    juce::PopupMenu m;
    juce::StringArray targets, labels;
    int nextId = 1;
    auto add = [&] (const String& label, const String& target)
    {
        m.addItem (nextId++, label);
        targets.add (target);
        labels.add (track[id::name].toString() + ": " + label);
    };
    add ("Volume", "strip:gain");
    add ("Pan", "strip:pan");
    const String type = track[id::type];
    if (type == "audio" || type == "midi")
    {
        add ("Send A", "send:A");
        add ("Send B", "send:B");
    }
    for (const auto& ins : SessionModel::insertsOf (track))
    {
        if (auto* proc = engine.getInsertProcessor (ins[id::uid].toString()))
        {
            juce::PopupMenu sub;
            const auto& params = proc->getParameters();
            const int count = juce::jmin (64, params.size());
            for (int i = 0; i < count; ++i)
            {
                sub.addItem (nextId++, params[i]->getName (40));
                targets.add ("ins:" + ins[id::uid].toString() + ":" + String (i));
                labels.add (ins[id::name].toString() + ": " + params[i]->getName (24));
            }
            m.addSubMenu (ins[id::name].toString(), sub);
        }
    }

    const String trackUid = track[id::uid];
    auto* s = &session;
    m.showMenuAsync ({}, [s, srcIdx, trackUid, targets, labels] (int r)
    {
        if (r <= 0 || r > targets.size()) return;
        s->undo.beginNewTransaction ("patch mod");
        auto modsTree = s->mods();
        ValueTree t (id::MODTARGET);
        t.setProperty (id::uid, SessionModel::newUID(), nullptr);
        t.setProperty (id::track, trackUid, nullptr);
        t.setProperty (id::param, targets[r - 1], nullptr);
        t.setProperty (id::name, labels[r - 1], nullptr);
        modsTree.appendChild (t, &s->undo);
        ValueTree mc (id::MOD);
        mc.setProperty (id::uid, SessionModel::newUID(), nullptr);
        mc.setProperty (id::src, kModIds[srcIdx], nullptr);
        mc.setProperty (id::target, t[id::uid].toString(), nullptr);
        mc.setProperty (id::amount, 0.5, nullptr);
        modsTree.appendChild (mc, &s->undo);
    });
}

void RoutingView::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto boxes = layoutBoxes();
    if (boxAt (boxes, e.position) != nullptr) return;

    juce::PopupMenu m;
    m.addItem (1, "Add audio track");
    m.addItem (2, "Add MIDI track");
    m.addItem (3, "Add WIRES track");
    m.addItem (4, "Add bus");
    const int px = e.x, py = e.y;
    auto* s = &session;
    m.showMenuAsync ({}, [s, px, py] (int r)
    {
        if (r == 0) return;
        s->undo.beginNewTransaction ("add track");
        ValueTree t;
        if (r == 1) t = s->addTrack ("audio", "Audio " + String (s->tracks().getNumChildren()));
        else if (r == 2) t = s->addTrack ("midi", "Inst " + String (s->tracks().getNumChildren()));
        else if (r == 3)
        {
            t = s->addTrack ("midi", "WIRES " + String (s->tracks().getNumChildren()));
            auto ins = s->addInsert (t, "instrument");
            ins.setProperty (id::ident, "builtin:wires", &s->undo);
            ins.setProperty (id::name, names::patcherName, &s->undo);
        }
        else if (r == 4) t = s->addTrack ("bus", "Bus " + String (s->tracks().getNumChildren()));
        if (t.isValid())
        {
            t.setProperty (id::x, px, nullptr);
            t.setProperty (id::y, py, nullptr);
        }
    });
}

void RoutingView::mouseMove (const juce::MouseEvent& e)
{
    auto boxes = layoutBoxes();
    for (const auto& b : boxes)
        if (b.track[id::type].toString() != "master"
            && (outPort (b).getDistanceFrom (e.position) < 10
                || sendPort (b, 0).getDistanceFrom (e.position) < 9
                || sendPort (b, 1).getDistanceFrom (e.position) < 9))
        {
            setMouseCursor (juce::MouseCursor::CrosshairCursor);
            return;
        }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

} // namespace dg
