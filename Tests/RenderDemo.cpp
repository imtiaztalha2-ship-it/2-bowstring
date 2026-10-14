// Offline test renderer: runs a guitar recording through BowString exactly as a DAW would.
//
//   BowStringRender <in.wav> <out.wav> [preset=<index>] [<paramID>=<value> ...]
//   BowStringRender --check            (sample bank self-test)
//
// Parameter values are plain values (e.g. engine=1 octave=2 vibdepth=0 revmix=0.3).

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"

static int checkBank()
{
    juce::SharedResourcePointer<bow::SampleBank> bank;
    for (int i = 0; i < 400 && ! bank->isReady(); ++i)
        juce::Thread::sleep (25);

    if (! bank->isReady()) { std::puts ("FAIL: samples did not load"); return 1; }

    // decoded length must cover the whole pack (Ogg Vorbis must be sample-accurate for the loops)
    juce::OggVorbisAudioFormat ogg;
    std::unique_ptr<juce::AudioFormatReader> r (ogg.createReaderFor (
        new juce::MemoryInputStream (samplemap::packData, (size_t) samplemap::packSize, false), true));
    std::printf ("pack: %d samples expected, %lld decoded, %d notes\n", samplemap::totalSamples,
                 (long long) r->lengthInSamples, samplemap::numSamples);
    int fails = r->lengthInSamples == samplemap::totalSamples ? 0 : 1;

    // every arco loop seam: the jump from loopEnd-1 back to loopStart must look like any other step
    for (int i = 0; i < samplemap::numSamples; ++i)
    {
        const auto& e = samplemap::entries[i];
        if (e.loopStart < 0) continue;
        const auto* s = &bank->getSample (i);
        float typical = 0.0f;
        for (int k = e.loopStart; k < e.loopEnd - 1; ++k)
            typical = juce::jmax (typical, std::abs (s->data[k + 1] - s->data[k]));
        const float seam = std::abs (s->data[e.loopStart] - s->data[e.loopEnd - 1]);
        if (seam > typical * 1.05f)
        {
            std::printf ("loop seam too big: midi %d layer %d (%.4f vs %.4f)\n", e.midi, e.layer, seam, typical);
            ++fails;
        }
    }
    std::printf (fails == 0 ? "bank check OK\n" : "bank check: %d problems\n", fails);
    return fails == 0 ? 0 : 1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;

    if (argc >= 2 && juce::String (argv[1]) == "--check")
        return checkBank();

    if (argc < 3) { std::puts ("usage: BowStringRender in.wav out.wav [preset=N] [param=value ...]"); return 2; }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])));
    if (reader == nullptr) { std::puts ("cannot read input"); return 1; }

    const double sr = reader->sampleRate;
    const int len = (int) reader->lengthInSamples, tail = (int) (3.0 * sr);
    juce::AudioBuffer<float> in (2, len + tail);
    in.clear();
    reader->read (&in, 0, len, 0, true, true);

    BowStringProcessor proc;
    proc.setPlayConfigDetails (2, 2, sr, 256);

    for (int i = 3; i < argc; ++i)
    {
        const auto arg = juce::String (argv[i]);
        const auto key = arg.upToFirstOccurrenceOf ("=", false, false);
        const auto val = arg.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        if (key == "preset")
            proc.setCurrentProgram ((int) val);
        else if (auto* prm = proc.apvts.getParameter (key))
            prm->setValueNotifyingHost (prm->convertTo0to1 (val));
        else
            std::printf ("unknown parameter %s\n", key.toRawUTF8());
    }

    juce::SharedResourcePointer<bow::SampleBank> bank;
    for (int i = 0; i < 400 && ! bank->isReady(); ++i)
        juce::Thread::sleep (25);

    proc.prepareToPlay (sr, 256);

    juce::MidiBuffer midi;
    const auto start = juce::Time::getMillisecondCounterHiRes();
    for (int pos = 0; pos < in.getNumSamples(); pos += 256)
    {
        const int n = juce::jmin (256, in.getNumSamples() - pos);
        juce::AudioBuffer<float> block (in.getArrayOfWritePointers(), 2, pos, n);
        proc.processBlock (block, midi);
    }
    const double secs = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
    std::printf ("rendered %.1f s of audio in %.2f s (%.0fx real time)\n", in.getNumSamples() / sr, secs,
                 in.getNumSamples() / sr / secs);

    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (new juce::FileOutputStream (out), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (in, 0, in.getNumSamples());
    return 0;
}
