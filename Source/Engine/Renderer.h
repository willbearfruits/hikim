#pragma once
#include "AudioEngine.h"

namespace dg
{

struct RenderSpec
{
    File file;
    bool aiff = false;
    int bits = 24;                  // 16 / 24 / 32 (32 = float, WAV only)
    double sampleRate = 48000.0;
    double startSec = 0.0, endSec = 0.0;
    bool stems = false;
};

// Offline bounce through the full graph (master chain applied, PDC intact).
// Stems are rendered as one pass per track with everything else force-muted.
// EXTEND: single-pass multi-writer stem export.
class Renderer : public juce::ThreadWithProgressWindow
{
public:
    Renderer (AudioEngine& e, SessionModel& s, RenderSpec spec);

    static double computeSessionEndSec (const SessionModel&);
    bool runRender();                // begin offline, run modal thread, end offline

    void run() override;

private:
    bool renderOne (const File& f, const String& soloUid, double progressBase, double progressSpan);

    AudioEngine& engine;
    SessionModel& session;
    RenderSpec spec;
    bool ok = false;
};

} // namespace dg
