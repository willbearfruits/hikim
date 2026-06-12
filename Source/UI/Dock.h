#pragma once
#include "Look.h"
#include "UIState.h"

namespace dg
{

// Modular panel docking: three zones (LEFT / RIGHT / BOTTOM) around the center
// view. Panels keep their existing names and components; each can live in any
// zone. Tab chips per zone: click selects (clicking the active chip collapses
// the zone), right-click moves the panel somewhere else. Zone edges drag to
// resize. Focus mode hides every zone for a full-canvas view. All of it
// persists across sessions.
class Dock : public juce::Component
{
public:
    enum ZoneId { zLeft = 0, zRight = 1, zBottom = 2 };

    explicit Dock (juce::PropertiesFile* propsToUse) : props (propsToUse) {}

    void registerPanel (const String& name, juce::Component* comp, int defaultZone)
    {
        panels.push_back ({ name, comp, defaultZone });
        addChildComponent (comp);
    }

    // call once after registering everything: restores zones/sizes/state
    void restore()
    {
        zones[zLeft].size = getInt ("dock.size.0", 240);
        zones[zRight].size = getInt ("dock.size.1", 300);
        zones[zBottom].size = getInt ("dock.size.2", 240);
        for (int z = 0; z < 3; ++z)
        {
            zones[z].open = getInt ("dock.open." + String (z), 1) != 0;
            zones[z].active = getInt ("dock.active." + String (z), 0);
        }
        for (auto& p : panels)
            p.zone = juce::jlimit (0, 2, getInt ("dock.zone." + p.name, p.zone));
        focus = getInt ("dock.focus", 0) != 0;
        rebuildZoneLists();
    }

    // lays the zones out inside our bounds and reports the remaining center
    // hole in PARENT coordinates (the arrange/session/patcher views live there)
    juce::Rectangle<int> layoutAndGetCenter()
    {
        auto b = getLocalBounds();
        tabRects.clear();

        if (focus)
        {
            for (auto& p : panels) p.comp->setVisible (false);
            return b + getPosition();
        }

        auto layoutZone = [this] (int z, juce::Rectangle<int>& area)
        {
            auto& zn = zones[z];
            if (zn.panels.empty()) return;
            const bool vertical = z != zBottom;
            const int barTh = 24;
            const int th = (zn.open ? zn.size : 0) + barTh;

            juce::Rectangle<int> strip =
                z == zLeft   ? area.removeFromLeft (th)
              : z == zRight  ? area.removeFromRight (th)
              :                area.removeFromBottom (th);

            // tab bar on the outer edge
            juce::Rectangle<int> bar =
                z == zLeft   ? strip.removeFromLeft (barTh)
              : z == zRight  ? strip.removeFromRight (barTh)
              :                strip.removeFromTop (barTh);

            int pos = 4;
            for (int i = 0; i < (int) zn.panels.size(); ++i)
            {
                const auto& nm = panels[(size_t) zn.panels[(size_t) i]].name;
                const int len = 16 + nm.length() * 7;       // chip length from label
                juce::Rectangle<int> r = vertical
                    ? juce::Rectangle<int> (bar.getX() + 1, bar.getY() + pos, barTh - 2, len)
                    : juce::Rectangle<int> (bar.getX() + pos, bar.getY() + 1, len, barTh - 2);
                tabRects.push_back ({ r, z, i });
                pos += len + 4;
            }

            // content + the active panel
            zn.contentArea = strip;
            for (int i = 0; i < (int) zn.panels.size(); ++i)
            {
                auto* c = panels[(size_t) zn.panels[(size_t) i]].comp;
                const bool show = zn.open && i == zn.active;
                c->setVisible (show);
                if (show) c->setBounds (strip.reduced (2));
            }
        };

        // rails take full height, the bottom strip spans between them
        layoutZone (zLeft, b);
        layoutZone (zRight, b);
        layoutZone (zBottom, b);

        for (auto& p : panels)      // panels parked in a closed list stay hidden
            if (! zones[p.zone].open || zones[p.zone].panels.empty())
                if (! isActiveIn (p)) p.comp->setVisible (false);

        return b + getPosition();
    }

    void showPanel (const String& name)
    {
        for (auto& p : panels)
            if (p.name == name)
            {
                auto& zn = zones[p.zone];
                for (int i = 0; i < (int) zn.panels.size(); ++i)
                    if (panels[(size_t) zn.panels[(size_t) i]].comp == p.comp)
                        zn.active = i;
                zn.open = true;
                focus = false;
                persistZone (p.zone);
                relayout();
                return;
            }
    }

    void toggleFocus()
    {
        focus = ! focus;
        setInt ("dock.focus", focus ? 1 : 0);
        relayout();
    }
    bool isFocus() const { return focus; }

    std::function<void()> onLayoutChanged;          // parent re-runs resized()

    // ---- painting / interaction -------------------------------------------
    void paint (juce::Graphics& g) override
    {
        if (focus) return;
        for (int z = 0; z < 3; ++z)
        {
            if (zones[z].panels.empty()) continue;
            if (zones[z].open)
            {
                g.setColour (col::panel.withAlpha (0.35f));
                g.fillRect (zones[z].contentArea);
            }
        }
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        for (const auto& t : tabRects)
        {
            const auto& zn = zones[t.zone];
            const bool active = zn.open && t.index == zn.active;
            const auto& nm = panels[(size_t) zn.panels[(size_t) t.index]].name;

            g.setColour (active ? col::accent.withAlpha (0.85f) : col::panelHi);
            g.fillRoundedRectangle (t.r.toFloat(), 4.0f);
            g.setColour (active ? col::bg : col::dim);
            if (t.zone == zBottom)
                g.drawText (nm, t.r, juce::Justification::centred);
            else
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.addTransform (juce::AffineTransform::rotation (
                    -juce::MathConstants<float>::halfPi,
                    (float) t.r.getCentreX(), (float) t.r.getCentreY()));
                g.drawText (nm, t.r.withSizeKeepingCentre (t.r.getHeight(), t.r.getWidth()),
                            juce::Justification::centred);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragZone = -1;
        for (const auto& t : tabRects)
            if (t.r.contains (e.getPosition()))
            {
                if (e.mods.isPopupMenu()) { showTabMenu (t.zone, t.index); return; }
                auto& zn = zones[t.zone];
                if (zn.open && t.index == zn.active) zn.open = false;   // collapse
                else { zn.open = true; zn.active = t.index; }
                persistZone (t.zone);
                relayout();
                return;
            }
        // zone edge drag (inner edge of each open zone)
        for (int z = 0; z < 3; ++z)
            if (zones[z].open && ! zones[z].panels.empty() && edgeRect (z).contains (e.getPosition()))
            { dragZone = z; dragStartSize = zones[z].size; return; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragZone < 0) return;
        const int d = dragZone == zLeft  ?  e.getDistanceFromDragStartX()
                    : dragZone == zRight ? -e.getDistanceFromDragStartX()
                    :                      -e.getDistanceFromDragStartY();
        zones[dragZone].size = juce::jlimit (120, 560, dragStartSize + d);
        relayout();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragZone >= 0) { setInt ("dock.size." + String (dragZone), zones[dragZone].size); dragZone = -1; }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        for (int z = 0; z < 3; ++z)
            if (zones[z].open && ! zones[z].panels.empty() && edgeRect (z).contains (e.getPosition()))
            {
                setMouseCursor (z == zBottom ? juce::MouseCursor::UpDownResizeCursor
                                             : juce::MouseCursor::LeftRightResizeCursor);
                return;
            }
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    bool hitTest (int x, int y) override
    {
        // the center hole belongs to the views underneath us
        if (focus) return false;
        for (const auto& t : tabRects) if (t.r.contains (x, y)) return true;
        for (int z = 0; z < 3; ++z)
            if (! zones[z].panels.empty()
                && (zones[z].open ? zoneStripRect (z).contains (x, y)
                                  : false))
                return true;
        for (int z = 0; z < 3; ++z)
            if (! zones[z].panels.empty() && barRect (z).contains (x, y)) return true;
        return false;
    }

private:
    struct Panel { String name; juce::Component* comp; int zone; };
    struct ZoneState
    {
        std::vector<int> panels;     // indices into the panel list
        int active = 0;
        bool open = true;
        int size = 240;
        juce::Rectangle<int> contentArea;
    };
    struct TabRect { juce::Rectangle<int> r; int zone; int index; };

    bool isActiveIn (const Panel& p) const
    {
        const auto& zn = zones[p.zone];
        return zn.open && ! zn.panels.empty()
               && panels[(size_t) zn.panels[(size_t) juce::jlimit (0, (int) zn.panels.size() - 1, zn.active)]].comp == p.comp;
    }

    void rebuildZoneLists()
    {
        for (auto& z : zones) z.panels.clear();
        for (int i = 0; i < (int) panels.size(); ++i)
            zones[panels[(size_t) i].zone].panels.push_back (i);
        for (auto& z : zones)
            z.active = juce::jlimit (0, juce::jmax (0, (int) z.panels.size() - 1), z.active);
    }

    void showTabMenu (int zone, int index)
    {
        juce::PopupMenu m;
        m.addSectionHeader (panels[(size_t) zones[zone].panels[(size_t) index]].name);
        m.addItem (1, "Move to LEFT", zone != zLeft);
        m.addItem (2, "Move to RIGHT", zone != zRight);
        m.addItem (3, "Move to BOTTOM", zone != zBottom);
        const int panelIdx = zones[zone].panels[(size_t) index];
        juce::Component::SafePointer<Dock> safe (this);
        m.showMenuAsync ({}, [safe, panelIdx] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& p = safe->panels[(size_t) panelIdx];
            p.zone = r - 1;
            safe->setInt ("dock.zone." + p.name, p.zone);
            safe->rebuildZoneLists();
            auto& zn = safe->zones[p.zone];
            for (int i = 0; i < (int) zn.panels.size(); ++i)
                if (zn.panels[(size_t) i] == panelIdx) zn.active = i;
            zn.open = true;
            safe->persistZone (p.zone);
            safe->relayout();
        });
    }

    juce::Rectangle<int> barRect (int z) const
    {
        auto b = getLocalBounds();
        const int barTh = 24;
        const int th = (zones[z].open ? zones[z].size : 0) + barTh;
        if (z == zLeft)  return b.removeFromLeft (th).removeFromLeft (barTh);
        if (z == zRight) return b.removeFromRight (th).removeFromRight (barTh);
        auto withoutRails = stripWithoutRails (b);
        return withoutRails.removeFromBottom (th).removeFromTop (barTh);
    }

    juce::Rectangle<int> zoneStripRect (int z) const
    {
        auto b = getLocalBounds();
        const int barTh = 24;
        const int th = (zones[z].open ? zones[z].size : 0) + barTh;
        if (z == zLeft)  return b.removeFromLeft (th);
        if (z == zRight) return b.removeFromRight (th);
        return stripWithoutRails (b).removeFromBottom (th);
    }

    juce::Rectangle<int> stripWithoutRails (juce::Rectangle<int> b) const
    {
        const int barTh = 24;
        if (! zones[zLeft].panels.empty())
            b.removeFromLeft ((zones[zLeft].open ? zones[zLeft].size : 0) + barTh);
        if (! zones[zRight].panels.empty())
            b.removeFromRight ((zones[zRight].open ? zones[zRight].size : 0) + barTh);
        return b;
    }

    juce::Rectangle<int> edgeRect (int z) const
    {
        auto s = zoneStripRect (z);
        if (z == zLeft)  return s.removeFromRight (6);
        if (z == zRight) return s.removeFromLeft (6);
        return s.removeFromTop (6);                        // top edge of the bottom strip
    }

    void persistZone (int z)
    {
        setInt ("dock.open." + String (z), zones[z].open ? 1 : 0);
        setInt ("dock.active." + String (z), zones[z].active);
    }

    void relayout() { if (onLayoutChanged) onLayoutChanged(); repaint(); }

    int getInt (const String& k, int def) const { return props != nullptr ? props->getIntValue (k, def) : def; }
    void setInt (const String& k, int v) { if (props != nullptr) props->setValue (k, v); }

    juce::PropertiesFile* props;
    std::vector<Panel> panels;
    ZoneState zones[3];
    std::vector<TabRect> tabRects;
    bool focus = false;
    int dragZone = -1, dragStartSize = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Dock)
};

} // namespace dg
