#include "SampleEditor.h"

namespace dg
{

SampleEditor::SampleEditor (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    auto setupSlider = [this] (juce::Slider& sl, juce::Label& l, const String& text,
                               double lo, double hi, const Identifier& prop)
    {
        sl.setRange (lo, hi, 0.01);
        sl.setSliderStyle (juce::Slider::LinearBar);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16);
        sl.onValueChange = [this, &sl, prop]
        {
            if (clip.isValid())
                clip.setProperty (prop, sl.getValue(), nullptr);
        };
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::Font (juce::FontOptions (10.0f)));
        l.setColour (juce::Label::textColourId, col::dim);
        addAndMakeVisible (sl);
        addAndMakeVisible (l);
    };
    setupSlider (gainSl, gainL, "GAIN dB", -24.0, 24.0, id::clipGain);
    setupSlider (fadeInSl, fadeInL, "FADE IN s", 0.0, 4.0, id::fadeIn);
    setupSlider (fadeOutSl, fadeOutL, "FADE OUT s", 0.0, 4.0, id::fadeOut);

    nameL.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    nameL.setColour (juce::Label::textColourId, col::accent2);
    addAndMakeVisible (nameL);
    infoL.setFont (juce::Font (juce::FontOptions (10.0f)));
    infoL.setColour (juce::Label::textColourId, col::dim);
    addAndMakeVisible (infoL);

    pitchLockBtn.setClickingTogglesState (true);
    pitchLockBtn.onClick = [this]
    {
        if (clip.isValid())
            clip.setProperty (id::stretchMode, pitchLockBtn.getToggleState() ? 1 : 0, &session.undo);
    };
    addAndMakeVisible (pitchLockBtn);

    conformBtn.onClick = [this]
    {
        if (! clip.isValid()) return;
        double fileBpm = clip.hasProperty (id::bpm) ? (double) clip[id::bpm]
                        : engine.estimateFileBpm (File (clip[id::file].toString()));
        if (fileBpm <= 0) return;
        auto map = engine.getTempoMap();
        const double proj = map->bpmAtBeat (map->samplesToBeats (engine.getPositionSamples()));
        const double oldStretch = clip.getProperty (id::stretch, 1.0);
        const double stretchF = fileBpm / proj;
        const double rawDur = (double) clip[id::length] / oldStretch;
        const double spbSec = 60.0 / proj;
        const double beats = juce::jmax (1.0, std::round (rawDur * stretchF / spbSec));
        session.undo.beginNewTransaction ("conform");
        clip.setProperty (id::bpm, fileBpm, &session.undo);
        clip.setProperty (id::stretch, stretchF, &session.undo);
        clip.setProperty (id::length, beats * spbSec, &session.undo);
        setClip (clip);
    };
    addAndMakeVisible (conformBtn);

    previewBtn.onClick = [this]
    {
        if (! clip.isValid()) return;
        const File f (clip[id::file].toString());
        if (engine.getPreviewFile() == f) engine.stopPreview();
        else engine.startPreview (f);
    };
    addAndMakeVisible (previewBtn);

    startTimerHz (8);
}

SampleEditor::~SampleEditor() = default;

void SampleEditor::setClip (ValueTree audioClip)
{
    clip = audioClip;
    thumb.reset();
    if (clip.isValid())
    {
        thumb = std::make_unique<juce::AudioThumbnail> (512, engine.formatManager, engine.thumbCache);
        thumb->setSource (new juce::FileInputSource (engine.mediaFileFor (File (clip[id::file].toString()))));
        nameL.setText (clip[id::name].toString(), juce::dontSendNotification);
        gainSl.setValue ((double) clip.getProperty (id::clipGain, 0.0), juce::dontSendNotification);
        fadeInSl.setValue ((double) clip.getProperty (id::fadeIn, 0.0), juce::dontSendNotification);
        fadeOutSl.setValue ((double) clip.getProperty (id::fadeOut, 0.0), juce::dontSendNotification);
        pitchLockBtn.setToggleState ((int) clip.getProperty (id::stretchMode, 0) == 1, juce::dontSendNotification);
    }
    else
        nameL.setText ("(no sample)", juce::dontSendNotification);
    repaint();
}

double SampleEditor::fileDur() const     { return thumb != nullptr ? thumb->getTotalLength() : 0.0; }
double SampleEditor::regionStart() const
{
    const double fileSR = clip.getProperty (id::fileSR, 48000.0);
    return (double) clip[id::offset] / fileSR;
}
double SampleEditor::regionDur() const
{
    const double stretch = juce::jmax (0.01, (double) clip.getProperty (id::stretch, 1.0));
    return (double) clip[id::length] / stretch;
}

juce::Rectangle<int> SampleEditor::waveArea() const
{
    return getLocalBounds().reduced (8).withTrimmedTop (22).withTrimmedBottom (30);
}

double SampleEditor::xToFileSec (int x) const
{
    auto w = waveArea();
    return fileDur() * juce::jlimit (0.0, 1.0, (double) (x - w.getX()) / juce::jmax (1, w.getWidth()));
}

int SampleEditor::fileSecToX (double sec) const
{
    auto w = waveArea();
    return w.getX() + (int) (sec / juce::jmax (1.0e-9, fileDur()) * w.getWidth());
}

void SampleEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    if (! clip.isValid() || thumb == nullptr || fileDur() <= 0)
    {
        g.setColour (col::dim);
        g.drawText ("double-click an audio clip (arrange or session) to edit it",
                    getLocalBounds(), juce::Justification::centred);
        return;
    }

    auto w = waveArea();
    g.setColour (col::bg);
    g.fillRect (w);

    // whole file, dim
    g.setColour (col::dim.withAlpha (0.5f));
    thumb->drawChannels (g, w, 0.0, fileDur(), 0.85f);

    // used region, bright
    const double rs = regionStart(), rd = regionDur();
    juce::Rectangle<int> region (fileSecToX (rs), w.getY(),
                                 juce::jmax (4, fileSecToX (rs + rd) - fileSecToX (rs)), w.getHeight());
    g.saveState();
    g.reduceClipRegion (region);
    g.setColour (col::bg.brighter (0.08f));
    g.fillRect (region);
    g.setColour (col::accent.brighter (0.2f));
    thumb->drawChannels (g, w, 0.0, fileDur(), 0.85f);
    g.restoreState();

    g.setColour (col::accent);
    g.drawRect (region, 2);
    g.fillRect (region.getX() - 2, w.getY(), 4, w.getHeight());            // edge handles
    g.fillRect (region.getRight() - 2, w.getY(), 4, w.getHeight());

    infoL.setText ("region " + String (rs, 2) + "s + " + String (rd, 2) + "s of "
                   + String (fileDur(), 2) + "s   stretch x" + String ((double) clip.getProperty (id::stretch, 1.0), 2)
                   + (clip.hasProperty (id::bpm) ? "   " + String ((double) clip[id::bpm], 0) + " bpm" : String()),
                   juce::dontSendNotification);
}

void SampleEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! clip.isValid() || fileDur() <= 0) return;
    auto w = waveArea();
    if (! w.contains (e.getPosition())) return;

    const int lx = fileSecToX (regionStart());
    const int rx = fileSecToX (regionStart() + regionDur());
    session.undo.beginNewTransaction ("sample region");
    dragStartSec = xToFileSec (e.x);
    origOffset = regionStart();
    origLen = regionDur();

    if (std::abs (e.x - lx) < 8) drag = Drag::left;
    else if (std::abs (e.x - rx) < 8) drag = Drag::right;
    else if (e.x > lx && e.x < rx) drag = Drag::body;
    else drag = Drag::none;
}

void SampleEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || ! clip.isValid()) return;
    const double fileSR = clip.getProperty (id::fileSR, 48000.0);
    const double stretch = juce::jmax (0.01, (double) clip.getProperty (id::stretch, 1.0));
    const double delta = xToFileSec (e.x) - dragStartSec;

    if (drag == Drag::body)
    {
        const double ns = juce::jlimit (0.0, juce::jmax (0.0, fileDur() - origLen), origOffset + delta);
        clip.setProperty (id::offset, ns * fileSR, &session.undo);
    }
    else if (drag == Drag::left)
    {
        const double end = origOffset + origLen;
        const double ns = juce::jlimit (0.0, end - 0.02, origOffset + delta);
        clip.setProperty (id::offset, ns * fileSR, &session.undo);
        clip.setProperty (id::length, (end - ns) * stretch, &session.undo);
    }
    else
    {
        const double ne = juce::jlimit (origOffset + 0.02, fileDur(), origOffset + origLen + delta);
        clip.setProperty (id::length, (ne - origOffset) * stretch, &session.undo);
    }
    repaint();
}

void SampleEditor::mouseMove (const juce::MouseEvent& e)
{
    if (! clip.isValid() || fileDur() <= 0) return;
    const int lx = fileSecToX (regionStart());
    const int rx = fileSecToX (regionStart() + regionDur());
    if (std::abs (e.x - lx) < 8 || std::abs (e.x - rx) < 8)
        setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
    else if (e.x > lx && e.x < rx && waveArea().contains (e.getPosition()))
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor (juce::MouseCursor::NormalCursor);
}

void SampleEditor::resized()
{
    auto b = getLocalBounds().reduced (8);
    auto top = b.removeFromTop (20);
    nameL.setBounds (top.removeFromLeft (200));
    previewBtn.setBounds (top.removeFromRight (74).reduced (1));
    conformBtn.setBounds (top.removeFromRight (80).reduced (1));
    pitchLockBtn.setBounds (top.removeFromRight (90).reduced (1));
    infoL.setBounds (top);

    auto bottom = b.removeFromBottom (24);
    auto third = bottom.getWidth() / 3;
    auto c1 = bottom.removeFromLeft (third);
    gainL.setBounds (c1.removeFromLeft (54));
    gainSl.setBounds (c1.reduced (2, 2));
    auto c2 = bottom.removeFromLeft (third);
    fadeInL.setBounds (c2.removeFromLeft (58));
    fadeInSl.setBounds (c2.reduced (2, 2));
    fadeOutL.setBounds (bottom.removeFromLeft (62));
    fadeOutSl.setBounds (bottom.reduced (2, 2));
}

} // namespace dg
