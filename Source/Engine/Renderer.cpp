#include "Renderer.h"

namespace dg
{

Renderer::Renderer (AudioEngine& e, SessionModel& s, RenderSpec sp)
    : juce::ThreadWithProgressWindow ("Rendering...", true, true),
      engine (e), session (s), spec (sp)
{
}

double Renderer::computeSessionEndSec (const SessionModel& s)
{
    double end = 0.0;
    for (const auto& t : s.tracks())
        for (const auto& c : t.getChildWithName (id::CLIPS))
            end = juce::jmax (end, (double) c[id::start] + (double) c[id::length]);
    return end + 2.0;       // tail room
}

bool Renderer::runRender()
{
    if (spec.endSec <= spec.startSec)
        spec.endSec = computeSessionEndSec (session);
    if (spec.endSec <= spec.startSec)
        return false;

    const bool wasPlaying = engine.isPlaying();
    if (wasPlaying) engine.stop();

    engine.beginOffline (spec.sampleRate, 1024);
    runThread();
    engine.endOffline();
    return ok;
}

void Renderer::run()
{
    ok = true;
    if (! spec.stems)
    {
        ok = renderOne (spec.file, {}, 0.0, 1.0);
        return;
    }

    juce::StringArray uids, namesArr;
    for (const auto& t : session.tracks())
    {
        const String type = t[id::type];
        if (type == "audio" || type == "midi")
        {
            uids.add (t[id::uid].toString());
            namesArr.add (t[id::name].toString().replaceCharacters (" /\\:", "----"));
        }
    }
    for (int i = 0; i < uids.size() && ok && ! threadShouldExit(); ++i)
    {
        setStatusMessage ("Stem: " + namesArr[i]);
        engine.setStemSolo (uids[i]);
        const File f = spec.file.getSiblingFile (spec.file.getFileNameWithoutExtension()
                          + "-" + namesArr[i] + spec.file.getFileExtension());
        ok = renderOne (f, uids[i], (double) i / uids.size(), 1.0 / uids.size());
    }
    engine.setStemSolo ({});
}

bool Renderer::renderOne (const File& f, const String&, double progressBase, double progressSpan)
{
    f.deleteFile();
    auto out = f.createOutputStream();
    if (out == nullptr) return false;

    std::unique_ptr<juce::AudioFormatWriter> writer;
    const int bits = spec.aiff ? juce::jmin (spec.bits, 24) : spec.bits;
    if (spec.aiff)
    {
        juce::AiffAudioFormat fmt;
        writer.reset (fmt.createWriterFor (out.get(), spec.sampleRate, 2, bits, {}, 0));
    }
    else
    {
        juce::WavAudioFormat fmt;
        writer.reset (fmt.createWriterFor (out.get(), spec.sampleRate, 2, bits, {}, 0));
    }
    if (writer == nullptr) return false;
    out.release();

    const juce::int64 startSa = (juce::int64) (spec.startSec * spec.sampleRate);
    const juce::int64 endSa = (juce::int64) (spec.endSec * spec.sampleRate);
    const int block = 1024;
    juce::AudioBuffer<float> buf (2, block);

    for (juce::int64 pos = startSa; pos < endSa; pos += block)
    {
        if (threadShouldExit()) return false;
        const int n = (int) juce::jmin ((juce::int64) block, endSa - pos);
        buf.setSize (2, block, false, false, true);
        engine.processOffline (buf, pos);
        if (! writer->writeFromAudioSampleBuffer (buf, 0, n))
            return false;
        setProgress (progressBase + progressSpan * (double) (pos - startSa) / (double) (endSa - startSa));
    }
    return true;
}

} // namespace dg
