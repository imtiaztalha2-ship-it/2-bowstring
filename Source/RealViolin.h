#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <cmath>
#include "SampleMap.h"

namespace bow
{

//==============================================================================
// One recorded violin note, decoded into memory.
struct RealSample
{
    const float* data = nullptr;
    int   length = 0;
    int   loopStart = -1, loopEnd = -1;   // arco only: seamless loop (crossfade already baked in)
    int   legatoStart = 0;                // where the bow is already moving (skip the attack on finger changes)
    float rootHz = 440.0f;
    int   midi = 69;
};

//==============================================================================
// All the University of Iowa violin samples, decoded once in the background and shared
// by every BowString instance.  Real-time code only reads it after isReady() is true.
class SampleBank
{
public:
    enum Artic { arco = 0, pizz = 1 };
    static constexpr int numLayers = 3; // pp, mf, ff

    SampleBank()
    {
        loader = std::thread ([this] { load(); });
    }

    ~SampleBank()
    {
        if (loader.joinable())
            loader.join();
    }

    bool isReady() const noexcept   { return ready.load (std::memory_order_acquire); }
    int  getNumSamples() const      { return (int) samples.size(); }
    const RealSample& getSample (int i) const { return samples[(size_t) i]; }

    // nearest recorded note to this frequency, for one articulation and dynamic layer
    const RealSample* nearest (int artic, int layer, float hz) const noexcept
    {
        const int m = juce::jlimit (0, 127, (int) std::lround (69.0 + 12.0 * std::log2 (juce::jmax (20.0f, hz) / 440.0f)));
        const int idx = table[(size_t) artic][(size_t) layer][(size_t) m];
        return idx >= 0 ? &samples[(size_t) idx] : nullptr;
    }

private:
    void load()
    {
        juce::OggVorbisAudioFormat ogg;
        auto stream = std::make_unique<juce::MemoryInputStream> (samplemap::packData, (size_t) samplemap::packSize, false);
        std::unique_ptr<juce::AudioFormatReader> reader (ogg.createReaderFor (stream.release(), true));

        if (reader == nullptr)
            return;

        const auto total = (int) juce::jmin ((juce::int64) samplemap::totalSamples, reader->lengthInSamples);
        pcm.setSize (1, total);
        reader->read (&pcm, 0, total, 0, true, false);

        samples.reserve ((size_t) samplemap::numSamples);
        for (int i = 0; i < samplemap::numSamples; ++i)
        {
            const auto& e = samplemap::entries[i];
            RealSample s;
            s.data        = pcm.getReadPointer (0, juce::jmin (e.offset, total - 1));
            s.length      = juce::jmin (e.length, total - e.offset);
            s.loopStart   = e.loopStart;
            s.loopEnd     = juce::jmin (e.loopEnd, s.length);
            s.legatoStart = e.legatoStart;
            s.rootHz      = e.rootHz;
            s.midi        = e.midi;

            // apply the level-matching gain once here
            auto* d = pcm.getWritePointer (0, e.offset);
            juce::FloatVectorOperations::multiply (d, e.gain, s.length);
            samples.push_back (s);
        }

        // nearest-root lookup per articulation / layer / MIDI note.  If a layer has nothing within
        // 2 semitones, borrow the neighbouring dynamic layer rather than stretch a far-away note.
        for (int a = 0; a < 2; ++a)
            for (int l = 0; l < numLayers; ++l)
                for (int m = 0; m < 128; ++m)
                    table[(size_t) a][(size_t) l][(size_t) m] = findNearest (a, l, m);

        for (int a = 0; a < 2; ++a)
            for (int l = 0; l < numLayers; ++l)
                for (int m = 0; m < 128; ++m)
                {
                    auto& t = table[(size_t) a][(size_t) l][(size_t) m];
                    if (t >= 0 && distance (t, m) <= 2.0f)
                        continue;

                    for (int alt : { l == 0 ? 1 : (l == 2 ? 1 : 2), l == 0 ? 2 : 0 })
                    {
                        const int cand = findNearest (a, alt, m);
                        if (cand >= 0 && (t < 0 || distance (cand, m) + 0.5f < distance (t, m)))
                            t = cand;
                    }
                }

        ready.store (true, std::memory_order_release);
    }

    float distance (int idx, int midi) const
    {
        return std::abs (12.0f * std::log2 (samples[(size_t) idx].rootHz / 440.0f) + 69.0f - (float) midi);
    }

    int findNearest (int artic, int layer, int midi) const
    {
        int best = -1;
        float bestD = 1.0e9f;
        for (int i = 0; i < samplemap::numSamples; ++i)
        {
            const auto& e = samplemap::entries[i];
            if (e.artic != artic || e.layer != layer)
                continue;
            const float d = distance (i, midi);
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    juce::AudioBuffer<float> pcm;
    std::vector<RealSample> samples;
    std::array<std::array<std::array<int, 128>, numLayers>, 2> table {};
    std::atomic<bool> ready { false };
    std::thread loader;
};

//==============================================================================
// Plays the recorded violin.  BowedVoice still owns the bow: pitch (glide, vibrato knob),
// envelope, bow changes and rosin noise; this class turns pitch + bow pressure into real violin tone.
//
//  - nearest sample per note, resampled to the exact played pitch (bends, slides, meend)
//  - pp / mf / ff layers crossfaded from bow pressure
//  - seamless loops for long sustains
//  - finger changes and wide slides crossfade to the nearer sample without re-bowing
//  - real pizzicato samples, picked by how hard you pluck
class RealViolinVoice
{
public:
    void prepare (double sr, const SampleBank* b)
    {
        sampleRate = sr;
        bank = b;
        srRatio = (double) samplemap::sampleRate / sr;
        for (auto& z : zones) z = {};
        current = 0;
        sinceSwitch = 1 << 20;
        pizzHzSmoothed = pizzHzTarget = 440.0f;
        pizzSmooth = onePole (20.0f);
        fadeStep = 1.0f / (float) juce::jmax (1.0, 0.03 * sr);
        legatoFadeStep = 1.0f / (float) juce::jmax (1.0, 0.06 * sr);
        minSwitchSamples = (int) (0.08 * sr);
    }

    bool isReady() const noexcept { return bank != nullptr && bank->isReady(); }

    //-------------------------------------------------------------- arco
    // A new bow stroke (picked note): play the sample from its natural bow attack.
    void startBow (float hz, bool bowChange) noexcept
    {
        startZone (SampleBank::arco, hz, Start::attack, bowChange ? fadeStep : 1.0f);
    }

    // Finger change without re-bowing (hammer-on, pull-off, fast note): crossfade into
    // the new note's sample after its attack, so it sounds like a left-hand change.
    void fingerChange (float hz) noexcept
    {
        startZone (SampleBank::arco, hz, Start::legato, legatoFadeStep);
    }

    // The bow has lifted and the sound has died away: next note starts fresh.
    void stop() noexcept { for (auto& z : zones) z.active = false; }

    //-------------------------------------------------------------- pizzicato
    void pluck (float hz, float strength) noexcept
    {
        pizzHzSmoothed = pizzHzTarget = hz;
        damping = false;
        startZone (SampleBank::pizz, hz, Start::attack, 0.0f);
        auto& z = zones[(size_t) current];

        // strength picks the dynamic layer (and a velocity gain on top)
        const float x = juce::jlimit (0.0f, 1.0f, strength) * 2.0f;
        z.w = layerWeights (x);
        z.velocity = 0.4f + 0.6f * strength;
    }

    void setPizzFreq (float hz) noexcept { pizzHzTarget = hz; }
    void setPizzDecay (float t60Seconds) noexcept { pizzDecayCoeff = std::pow (10.0f, -3.0f / (juce::jmax (0.05f, t60Seconds) * (float) sampleRate)); }
    void damp() noexcept { damping = true; }

    //-------------------------------------------------------------- render
    // hz = current bowed pitch (with glide/vibrato), pressure 0..1 = bow pressure for layer choice
    float tickArco (float hz, float pressure) noexcept
    {
        if (! isReady())
            return 0.0f;

        // wide slide or bend: hand over to a nearer recording so we never stretch a note too far
        ++sinceSwitch;
        auto& cz = zones[(size_t) current];
        if (cz.active && cz.artic == SampleBank::arco && sinceSwitch > minSwitchSamples)
        {
            const auto* ref = cz.h[2].s != nullptr ? cz.h[2].s : cz.h[1].s;
            if (ref != nullptr && std::abs (1200.0f * std::log2 (hz / ref->rootHz)) > 110.0f
                && bank->nearest (SampleBank::arco, 2, hz) != ref)
            {
                        startZone (SampleBank::arco, hz, Start::follow, legatoFadeStep);
            }
        }

        const auto w = layerWeights (juce::jlimit (0.0f, 1.0f, pressure) * 2.0f);
        float out = 0.0f;

        for (auto& z : zones)
        {
            if (! z.active)
                continue;
            advanceFade (z);
            float s = 0.0f;
            for (int l = 0; l < SampleBank::numLayers; ++l)
                if (w[(size_t) l] > 0.0005f && z.h[(size_t) l].s != nullptr)
                    s += w[(size_t) l] * layerGain[(size_t) l] * play (z.h[(size_t) l], hz);
                else if (z.h[(size_t) l].s != nullptr)
                    advance (z.h[(size_t) l], hz); // keep silent layers in step so they can fade in smoothly
            out += s * z.gain;
        }
        return out;
    }

    float tickPizz() noexcept
    {
        if (! isReady())
            return 0.0f;

        pizzHzSmoothed = pizzHzTarget + (pizzHzSmoothed - pizzHzTarget) * pizzSmooth;
        float out = 0.0f;

        for (auto& z : zones)
        {
            if (! z.active || z.artic != SampleBank::pizz)
                continue;
            advanceFade (z);

            // the Pizz decay knob shortens the natural ring; lifting the finger damps the string
            z.decay *= damping ? dampCoeff : pizzDecayCoeff;

            float s = 0.0f;
            for (int l = 0; l < SampleBank::numLayers; ++l)
                if (z.h[(size_t) l].s != nullptr && z.w[(size_t) l] > 0.0005f)
                    s += z.w[(size_t) l] * pizzLayerGain[(size_t) l] * play (z.h[(size_t) l], pizzHzSmoothed);

            out += s * z.gain * z.decay * z.velocity;

            if (z.decay < 1.0e-4f || allFinished (z))
                z.active = false;
        }
        return out;
    }

private:
    enum class Start { attack, legato, follow };

    struct Head
    {
        const RealSample* s = nullptr;
        double pos = 0.0;
        bool finished = false;
    };

    struct Zone
    {
        std::array<Head, 3> h {};
        std::array<float, 3> w { 0.0f, 0.0f, 1.0f };
        float gain = 0.0f, x = 0.0f, fade = 0.0f;   // x: 0..1 fade position; fade > 0 = fading in, < 0 = fading out
        float decay = 1.0f, velocity = 1.0f;
        int   artic = 0;
        bool  active = false;
    };

    float onePole (float ms) const { return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate)); }

    static std::array<float, 3> layerWeights (float x) noexcept
    {
        // pp | mf | ff in thirds of the pressure range, with short equal-power overlaps at the borders.
        // (Two different recordings at the same pitch phase against each other while both sound,
        // so the overlap is kept narrow: you pass through it quickly instead of sitting in it.)
        constexpr float b1 = 0.67f, b2 = 1.33f, h = 0.12f;
        const float halfPi = juce::MathConstants<float>::halfPi;
        std::array<float, 3> w { 0.0f, 0.0f, 0.0f };
        if (x < 1.0f)
        {
            const float t = juce::jlimit (0.0f, 1.0f, (x - (b1 - h)) / (2.0f * h));
            w[0] = std::cos (t * halfPi); w[1] = std::sin (t * halfPi);
        }
        else
        {
            const float t = juce::jlimit (0.0f, 1.0f, (x - (b2 - h)) / (2.0f * h));
            w[1] = std::cos (t * halfPi); w[2] = std::sin (t * halfPi);
        }
        return w;
    }

    void startZone (int artic, float hz, Start start, float fadeInStep) noexcept
    {
        if (bank == nullptr || ! bank->isReady())
            return;

        auto& old = zones[(size_t) current];
        const bool wasLooping = old.active && old.h[2].s != nullptr && old.h[2].s->loopStart >= 0
                                && old.h[2].pos >= (double) old.h[2].s->loopStart;

        // fade every sounding zone out (equal-power, same speed as the new one fades in)
        const float outStep = (fadeInStep > 0.0f && fadeInStep < 1.0f) ? fadeInStep : fadeStep;
        for (auto& z : zones)
            if (z.active)
                z.fade = -outStep;

        // re-use the quietest slot
        int slot = 0;
        float quietest = 2.0f;
        for (int i = 0; i < (int) zones.size(); ++i)
        {
            const float g = zones[(size_t) i].active ? zones[(size_t) i].gain : -1.0f;
            if (g < quietest) { quietest = g; slot = i; }
        }

        auto& z = zones[(size_t) slot];
        z = {};
        z.active = true;
        z.artic = artic;
        const bool instant = fadeInStep >= 1.0f || fadeInStep <= 0.0f;
        z.x    = instant ? 1.0f : 0.0f;
        z.gain = instant ? 1.0f : 0.0f;
        z.fade = instant ? 0.0f : fadeInStep;

        for (int l = 0; l < SampleBank::numLayers; ++l)
        {
            auto& h = z.h[(size_t) l];
            h.s = bank->nearest (artic, l, hz);
            if (h.s == nullptr)
                continue;

            if (start == Start::attack)
                h.pos = 0.0;
            else if (start == Start::follow && wasLooping && h.s->loopStart >= 0)
                h.pos = (double) h.s->loopStart;
            else
                h.pos = (double) juce::jmin (h.s->legatoStart, juce::jmax (0, h.s->length - 2));
        }

        current = slot;
        sinceSwitch = 0;
    }

    void advanceFade (Zone& z) noexcept
    {
        if (juce::exactlyEqual (z.fade, 0.0f))
            return;
        z.x += z.fade;
        if (z.x >= 1.0f)      { z.x = 1.0f; z.fade = 0.0f; }
        else if (z.x <= 0.0f) { z.x = 0.0f; z.active = false; }
        z.gain = std::sin (z.x * juce::MathConstants<float>::halfPi);
    }

    static bool allFinished (const Zone& z) noexcept
    {
        for (auto& h : z.h)
            if (h.s != nullptr && ! h.finished)
                return false;
        return true;
    }

    void advance (Head& h, float hz) noexcept
    {
        h.pos += (double) (hz / h.s->rootHz) * srRatio;
        wrap (h);
    }

    void wrap (Head& h) noexcept
    {
        const auto* s = h.s;
        if (s->loopStart >= 0 && s->loopEnd > s->loopStart + 4)
        {
            const double len = (double) (s->loopEnd - s->loopStart);
            while (h.pos >= (double) s->loopEnd)
                h.pos -= len;
        }
        else if (h.pos >= (double) (s->length - 2))
        {
            h.pos = (double) (s->length - 2);
            h.finished = true;
        }
    }

    // 4-point Hermite interpolation, loop-aware
    float play (Head& h, float hz) noexcept
    {
        const auto* s = h.s;
        if (h.finished)
            return 0.0f;

        const int   i  = (int) h.pos;
        const float fr = (float) (h.pos - (double) i);
        auto at = [s] (int k) noexcept
        {
            if (s->loopStart >= 0 && k >= s->loopEnd)
                k -= (s->loopEnd - s->loopStart);
            return s->data[juce::jlimit (0, s->length - 1, k)];
        };

        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        const float y  = ((c3 * fr + c2) * fr + c1) * fr + x0;

        advance (h, hz);
        return y;
    }

    // level offsets between dynamic layers (the samples themselves are loudness-matched)
    static constexpr std::array<float, 3> layerGain     { 0.56f, 0.79f, 1.0f };  // -5 / -2 / 0 dB
    static constexpr std::array<float, 3> pizzLayerGain { 0.45f, 0.71f, 1.0f };  // -7 / -3 / 0 dB

    const SampleBank* bank = nullptr;
    std::array<Zone, 4> zones {};
    int    current = 0, sinceSwitch = 0, minSwitchSamples = 4000;
    double sampleRate = 44100.0, srRatio = 1.0;
    float  fadeStep = 0.001f, legatoFadeStep = 0.001f;
    float  pizzHzSmoothed = 440.0f, pizzHzTarget = 440.0f, pizzSmooth = 0.0f;
    float  pizzDecayCoeff = 0.9999f, dampCoeff = 0.999f;
    bool   damping = false;

public:
    void setDampTime (float ms) noexcept { dampCoeff = std::pow (10.0f, -3.0f / (juce::jmax (1.0f, ms) * 0.001f * (float) sampleRate)); }
};

} // namespace bow
