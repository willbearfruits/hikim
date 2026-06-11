#include "StretchCache.h"

#if DG_HAVE_RUBBERBAND
 #include <rubberband/RubberBandStretcher.h>
#endif

namespace dg
{

StretchCache::StretchCache (juce::AudioFormatManager& fm)
    : juce::Thread ("dg stretch"), formats (fm)
{
    dir = File::getSpecialLocation (File::tempDirectory)
              .getChildFile (String (names::appName) + "-stretchcache");
    dir.createDirectory();
    startThread (juce::Thread::Priority::low);
}

StretchCache::~StretchCache()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (5000);
}

File StretchCache::cacheFileFor (const File& src, double ratio) const
{
    const String key = src.getFullPathName() + "|"
                     + String (src.getLastModificationTime().toMilliseconds()) + "|"
                     + String (ratio, 5);
    return dir.getChildFile (String::toHexString (key.hashCode64()) + ".wav");
}

File StretchCache::get (const File& src, double ratio, std::function<void()> onReady)
{
    if (! available() || ! src.existsAsFile())
        return {};

    const File dest = cacheFileFor (src, ratio);
    if (dest.existsAsFile())
        return dest;

    const juce::ScopedLock sl (lock);
    if (! inFlightOrFailed.contains (dest.getFileName()))
    {
        inFlightOrFailed.add (dest.getFileName());
        jobs.push_back ({ src, ratio, dest, std::move (onReady) });
        wake.signal();
    }
    return {};
}

void StretchCache::run()
{
    while (! threadShouldExit())
    {
        Job job;
        {
            const juce::ScopedLock sl (lock);
            if (! jobs.empty())
            {
                job = std::move (jobs.front());
                jobs.erase (jobs.begin());
            }
        }
        if (job.src == File())
        {
            wake.wait (500);
            continue;
        }

        const bool ok = renderJob (job.src, job.ratio, job.dest);
        if (ok)
        {
            {
                const juce::ScopedLock sl (lock);
                inFlightOrFailed.removeString (job.dest.getFileName());
            }
            if (job.onReady)
                juce::MessageManager::callAsync (job.onReady);
        }
        // failures stay in inFlightOrFailed so playlist rebuilds don't re-queue forever
    }
}

bool StretchCache::renderJob (const File& src, double ratio, const File& dest)
{
   #if DG_HAVE_RUBBERBAND
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (src));
    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    const int chans = juce::jmin (2, (int) reader->numChannels);
    const juce::int64 total = reader->lengthInSamples;
    using RB = RubberBand::RubberBandStretcher;
    RB st ((size_t) reader->sampleRate, (size_t) chans,
           RB::OptionProcessOffline | RB::OptionEngineFiner | RB::OptionChannelsTogether,
           ratio, 1.0);
    st.setExpectedInputDuration ((size_t) total);

    const int block = 16384;
    juce::AudioBuffer<float> in (chans, block), out (chans, block);

    for (juce::int64 pos = 0; pos < total; pos += block)        // study pass
    {
        if (threadShouldExit()) return false;
        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        reader->read (in.getArrayOfWritePointers(), chans, pos, n);
        st.study (in.getArrayOfReadPointers(), (size_t) n, pos + n >= total);
    }

    dest.deleteFile();
    auto stream = dest.createOutputStream();
    if (stream == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.get(), reader->sampleRate, (unsigned int) chans, 32, {}, 0));
    if (writer == nullptr) return false;
    stream.release();

    auto drain = [&]() -> bool
    {
        for (int avail; (avail = st.available()) > 0;)
        {
            const int take = juce::jmin (avail, block);
            st.retrieve (out.getArrayOfWritePointers(), (size_t) take);
            if (! writer->writeFromAudioSampleBuffer (out, 0, take))
                return false;
        }
        return true;
    };

    for (juce::int64 pos = 0; pos < total; pos += block)        // process pass
    {
        if (threadShouldExit()) { writer.reset(); dest.deleteFile(); return false; }
        const int n = (int) juce::jmin ((juce::int64) block, total - pos);
        reader->read (in.getArrayOfWritePointers(), chans, pos, n);
        st.process (in.getArrayOfReadPointers(), (size_t) n, pos + n >= total);
        if (! drain()) { writer.reset(); dest.deleteFile(); return false; }
    }
    if (! drain()) { writer.reset(); dest.deleteFile(); return false; }
    return true;
   #else
    juce::ignoreUnused (src, ratio, dest);
    return false;
   #endif
}

} // namespace dg
