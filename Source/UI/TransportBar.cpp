#include "TransportBar.h"

namespace dg
{

TransportBar::TransportBar (AudioEngine& e, SessionModel& s, UIState& u)
    : engine (e), session (s), ui (u)
{
    auto styleLabel = [this] (juce::Label& l, float size, juce::Colour c)
    {
        l.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold)));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    rtzBtn.onClick = [this] { engine.seekSeconds (0.0); };
    playBtn.onClick = [this] { engine.play(); };
    stopBtn.onClick = [this] { engine.stop(); };
    recBtn.onClick = [this] { engine.toggleRecord(); };
    viewBtn.onClick = [this] { if (onToggleView) onToggleView(); };
    viewBtn.setColour (juce::TextButton::buttonColourId, col::accent.darker (0.55f));
    for (auto* b : { &rtzBtn, &playBtn, &stopBtn, &recBtn, &viewBtn })
        addAndMakeVisible (*b);

    auto bindTransportToggle = [this] (juce::TextButton& b, const Identifier& prop)
    {
        b.setClickingTogglesState (true);
        b.onClick = [this, &b, prop]
        {
            auto tr = session.transport();
            tr.setProperty (prop, b.getToggleState(), &session.undo);
        };
        addAndMakeVisible (b);
    };
    bindTransportToggle (loopBtn, id::loopOn);
    bindTransportToggle (metroBtn, id::metro);

    dubBtn.setClickingTogglesState (true);
    dubBtn.setToggleState (true, juce::dontSendNotification);
    dubBtn.onClick = [this] { engine.overdubMidi = dubBtn.getToggleState(); };
    addAndMakeVisible (dubBtn);

    styleLabel (posBars, 20.0f, col::text);
    styleLabel (posTime, 13.0f, col::dim);
    styleLabel (bpmLabel, 16.0f, col::accent2);
    styleLabel (cpuLabel, 11.0f, col::dim);

    bpmLabel.setEditable (false, true);
    bpmLabel.onTextChange = [this]
    {
        const double bpm = juce::jlimit (10.0, 999.0, bpmLabel.getText().getDoubleValue());
        auto tm = session.tempoMap();
        auto first = tm.getChildWithName (id::TEMPO);
        if (first.isValid())
            first.setProperty (id::bpm, bpm, &session.undo);
    };

    snapBox.addItemList (kSnapNames, 1);
    snapBox.setSelectedItemIndex (ui.snapMode, juce::dontSendNotification);
    snapBox.onChange = [this] { ui.snapMode = snapBox.getSelectedItemIndex(); };
    addAndMakeVisible (snapBox);

    engine.onTransportStateChanged = [this] { updateToggleStates(); };
    startTimerHz (15);
    updateToggleStates();
}

void TransportBar::updateToggleStates()
{
    auto tr = session.transport();
    loopBtn.setToggleState ((bool) tr[id::loopOn], juce::dontSendNotification);
    metroBtn.setToggleState ((bool) tr[id::metro], juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    // re-apply palette colours so theme switches reach explicitly-coloured labels
    posBars.setColour (juce::Label::textColourId, col::text);
    posTime.setColour (juce::Label::textColourId, col::dim);
    bpmLabel.setColour (juce::Label::textColourId, col::accent2);
    cpuLabel.setColour (juce::Label::textColourId, col::dim);

    auto map = engine.getTempoMap();
    const auto pos = engine.getPositionSamples();
    posBars.setText (map->formatBarsBeats (pos), juce::dontSendNotification);
    posTime.setText (map->formatTimecode (pos), juce::dontSendNotification);
    if (! bpmLabel.isBeingEdited())
        bpmLabel.setText (String (map->bpmAtBeat (map->samplesToBeats (pos)), 1), juce::dontSendNotification);
    cpuLabel.setText ("CPU " + String (engine.deviceManager.getCpuUsage() * 100.0, 1) + "%  "
                      + "LAT " + String (engine.getTotalLatencySamples()) + "sa",
                      juce::dontSendNotification);

    playBtn.setColour (juce::TextButton::buttonColourId, engine.isPlaying() ? col::play.darker (0.4f) : col::panelHi);
    const bool rec = engine.isRecording() || engine.isRecordPending();
    recBtn.setColour (juce::TextButton::buttonColourId, rec ? col::record.darker (engine.isRecording() ? 0.0f : 0.5f)
                                                            : col::panelHi);
    updateToggleStates();
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (col::panel);
    g.setColour (col::line);
    g.drawLine (0, (float) getHeight() - 0.5f, (float) getWidth(), (float) getHeight() - 0.5f);
}

void TransportBar::resized()
{
    auto b = getLocalBounds().reduced (6, 5);
    rtzBtn.setBounds (b.removeFromLeft (34));
    b.removeFromLeft (2);
    playBtn.setBounds (b.removeFromLeft (44));
    b.removeFromLeft (2);
    stopBtn.setBounds (b.removeFromLeft (44));
    b.removeFromLeft (2);
    recBtn.setBounds (b.removeFromLeft (48));
    b.removeFromLeft (8);
    viewBtn.setBounds (b.removeFromLeft (84));
    b.removeFromLeft (8);
    posBars.setBounds (b.removeFromLeft (130));
    posTime.setBounds (b.removeFromLeft (110));
    b.removeFromLeft (6);
    bpmLabel.setBounds (b.removeFromLeft (64));
    b.removeFromLeft (6);
    snapBox.setBounds (b.removeFromLeft (96).reduced (0, 3));
    b.removeFromLeft (10);
    loopBtn.setBounds (b.removeFromLeft (54));
    b.removeFromLeft (2);
    metroBtn.setBounds (b.removeFromLeft (56));
    b.removeFromLeft (2);
    dubBtn.setBounds (b.removeFromLeft (72));
    cpuLabel.setBounds (b.removeFromRight (170));
}

} // namespace dg
