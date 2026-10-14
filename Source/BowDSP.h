#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>

namespace bow
{

// Coefficient for a one-pole smoother with the given time constant
inline float onePoleCoeff (float ms, double sampleRate)
{
    if (ms <= 0.0f)
        return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * (double) ms * sampleRate));
}

//==============================================================================
// Monophonic pitch tracker (YIN algorithm) running on a ~12 kHz decimated signal.
// Window ~21 ms + lag range ~21 ms, analysed every ~4 ms, median-of-3 smoothing.
class PitchTracker
{
public:
    void prepare (double sampleRate)
    {
        decim   = juce::jmax (1, (int) std::round (sampleRate / 12000.0));
        dsRate  = (float) (sampleRate / decim);
        lpCoeff = (float) std::exp (-2.0 * juce::MathConstants<double>::pi * 1800.0 / sampleRate);

        ring.fill (0.0f);
        frame.fill (0.0f);
        cmnd.fill (1.0f);
        history.fill (0.0f);
        writePos = accCount = hopCount = histIdx = 0;
        acc = lp1 = lp2 = 0.0f;
        pitch = 0.0f;
        voiced = false;
    }

    void push (float x) noexcept
    {
        // two one-pole low-passes as a cheap anti-alias before decimating
        lp1 = x   + (lp1 - x)   * lpCoeff;
        lp2 = lp1 + (lp2 - lp1) * lpCoeff;
        acc += lp2;

        if (++accCount >= decim)
        {
            ring[(size_t) writePos] = acc / (float) decim;
            writePos = (writePos + 1) % N;
            acc = 0.0f;
            accCount = 0;

            if (++hopCount >= hop)
            {
                hopCount = 0;
                analyse();
            }
        }
    }

    bool  isVoiced() const noexcept { return voiced; }
    float getPitch() const noexcept { return pitch; }
    int   getFrameId() const noexcept { return frameId; }

    // forget earlier estimates so a new note isn't smoothed toward the old one
    void resetHistory() noexcept { history.fill (0.0f); }

private:
    static constexpr int   W = 256, tauMax = 256, N = W + tauMax, hop = 48;
    static constexpr float threshold = 0.15f;

    void analyse() noexcept
    {
        ++frameId;
        for (int i = 0; i < N; ++i)
            frame[(size_t) i] = ring[(size_t) ((writePos + i) % N)];

        float energy = 0.0f;
        for (int i = 0; i < W; ++i)
            energy += frame[(size_t) i] * frame[(size_t) i];

        if (energy < 1.0e-7f * (float) W)
        {
            voiced = false;
            return;
        }

        // cumulative mean normalised difference function
        cmnd[0] = 1.0f;
        float running = 0.0f;

        for (int tau = 1; tau < tauMax; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < W; ++j)
            {
                const float d = frame[(size_t) j] - frame[(size_t) (j + tau)];
                sum += d * d;
            }
            running += sum;
            cmnd[(size_t) tau] = running > 0.0f ? sum * (float) tau / running : 1.0f;
        }

        const int tauMin = juce::jmax (2, (int) (dsRate / 1600.0f));
        int found = -1;

        for (int tau = tauMin; tau < tauMax - 1; ++tau)
        {
            if (cmnd[(size_t) tau] < threshold)
            {
                while (tau + 1 < tauMax - 1 && cmnd[(size_t) tau + 1] < cmnd[(size_t) tau])
                    ++tau;
                found = tau;
                break;
            }
        }

        if (found < 0)
        {
            voiced = false;
            return;
        }

        // parabolic interpolation around the minimum
        const float s0 = cmnd[(size_t) found - 1], s1 = cmnd[(size_t) found], s2 = cmnd[(size_t) found + 1];
        const float denom = s0 - 2.0f * s1 + s2;
        float shift = std::abs (denom) > 1.0e-9f ? 0.5f * (s0 - s2) / denom : 0.0f;
        shift = juce::jlimit (-1.0f, 1.0f, shift);

        const float f = dsRate / ((float) found + shift);

        history[(size_t) histIdx] = f;
        histIdx = (histIdx + 1) % 3;

        float a = history[0], b = history[1], c = history[2];
        if (a <= 0.0f || b <= 0.0f || c <= 0.0f)
        {
            pitch = f;
        }
        else
        {
            if (a > b) std::swap (a, b);
            if (b > c) std::swap (b, c);
            if (a > b) std::swap (a, b);
            pitch = b; // median of last three estimates
        }

        voiced = true;
    }

    std::array<float, N>      ring {}, frame {};
    std::array<float, tauMax> cmnd {};
    std::array<float, 3>      history {};
    int   decim = 4, writePos = 0, accCount = 0, hopCount = 0, histIdx = 0, frameId = 0;
    float dsRate = 12000.0f, lpCoeff = 0.0f, lp1 = 0.0f, lp2 = 0.0f, acc = 0.0f;
    float pitch = 0.0f;
    bool  voiced = false;
};

//==============================================================================
// A "bowed string" voice: anti-aliased sawtooth (Helmholtz-like motion) with
// cycle-to-cycle pitch jitter and amplitude shimmer, pitch-synchronous bow noise,
// a per-note attack set by how hard the string was picked, glide and delayed vibrato.
class BowedVoice
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        phase = 0.0f;
        logFreq = targetLogFreq = std::log (220.0f);
        amp = targetAmp = 0.0f;
        vibPhase = noteTime = jit = shim = scratch = 0.0f;
        attCoeff = onePoleCoeff (50.0f, sr);

        jitA = onePoleCoeff (20.0f, sr);
        shimA = onePoleCoeff (60.0f, sr);
        jitNorm  = 1.0f / (0.577f * std::sqrt ((1.0f - jitA)  / (1.0f + jitA)));
        shimNorm = 1.0f / (0.577f * std::sqrt ((1.0f - shimA) / (1.0f + shimA)));
        scratchDecay = onePoleCoeff (40.0f, sr);
        grainA = onePoleCoeff (40.0f, sr);
        grainNorm = 1.0f / (0.577f * std::sqrt ((1.0f - grainA) / (1.0f + grainA)));
        grain = 0.0f;
        bowContact = 0.0f;
        contactCoeff = onePoleCoeff (15.0f, sr);
        strokeTime = 0.0f;
        changeTime = -1.0f;
        bowDir = 1.0f;
        nextStrokeLen = strokeLen;
        strokeGain = changeGain = 1.0f;
        noiseBoost = 1.0f;

        juce::dsp::ProcessSpec spec { sr, 512, 1 };
        noiseFilter.prepare (spec);
        // woody rosin noise, mostly 500-2000 Hz (measured from a solo violin recording)
        noiseFilter.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        noiseFilter.setCutoffFrequency (650.0f);
        noiseFilter.setResonance (0.7f);
        noiseLowpass.prepare (spec);
        noiseLowpass.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        noiseLowpass.setCutoffFrequency (2000.0f);
        noiseLowpass.setResonance (0.7f);
        rosinType = -1;
        setRosinType (1);

        toneFilter.prepare (spec);
        toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        toneFilter.setResonance (0.6f);
        toneFilter.setCutoffFrequency (4000.0f);
    }

    void setParams (float releaseMs, float glideMs, float vibRateHz, float vibDepthCents,
                    float noiseAmount, float brightness, float bowMotionAmount, float strokeSeconds) noexcept
    {
        bowMotion = bowMotionAmount;
        strokeLen = strokeSeconds;
        relCoeff   = onePoleCoeff (releaseMs, sampleRate);
        glideCoeff = onePoleCoeff (glideMs,   sampleRate);
        vibInc     = (float) (juce::MathConstants<double>::twoPi * vibRateHz / sampleRate);
        vibDepth   = vibDepthCents;
        noiseAmt   = noiseAmount;
        noiseGain  = std::pow (noiseAmount, 1.5f) * 2.0f; // gentle taper: fine control at low settings
        bright     = brightness;
    }

    // Rosin types change how the bow hair grips the string:
    // smoother rosin = less, finer noise and a soft start; harsher rosin = more grit and a scratchier bite.
    void setRosinType (int type) noexcept
    {
        struct Rosin { float noise, flicker, grit, bite, tone, hp, lp; };
        static constexpr Rosin types[5] = {
            { 0.45f, 0.15f, 0.45f, 0.5f, 0.95f, 800.0f, 1600.0f },  // Light: smooth, clean
            { 1.00f, 0.35f, 0.70f, 1.0f, 1.00f, 650.0f, 2000.0f },  // Amber: balanced (default)
            { 1.40f, 0.55f, 0.85f, 1.5f, 0.92f, 500.0f, 1700.0f },  // Dark: grippy, warm, gritty
            { 1.00f, 0.30f, 0.70f, 1.1f, 1.10f, 900.0f, 3200.0f },  // Gold: bright, sparkly
            { 2.40f, 0.90f, 0.95f, 2.2f, 1.08f, 600.0f, 4000.0f },  // Old/cheap: harsh, scratchy
        };

        type = juce::jlimit (0, 4, type);
        if (type == rosinType)
            return;
        rosinType = type;

        const auto& r = types[type];
        rosinNoise = r.noise;
        rosinFlicker = r.flicker;
        rosinGrit = r.grit;
        rosinBite = r.bite;
        rosinTone = r.tone;
        noiseFilter.setCutoffFrequency (r.hp);
        noiseLowpass.setCutoffFrequency (juce::jmin (r.lp, (float) sampleRate * 0.45f));
    }

    // Begin a bow stroke. Hard picks give a short, biting attack; soft picks a slow swell.
    void startNote (float hz, float attackMs, float strength, bool legato) noexcept
    {
        attCoeff = onePoleCoeff (attackMs, sampleRate);
        targetLogFreq = std::log (juce::jlimit (30.0f, 5000.0f, hz));

        // a newly picked note is a finger change: the pitch moves instantly.
        // (Glide only applies to slides and bends while a note is held.)
        logFreq = targetLogFreq;

        // every picked note is a new bow stroke in the opposite direction
        bowDir = -bowDir;
        strokeTime = 0.0f;
        nextStrokeLen = strokeLen * (0.8f + 0.4f * random.nextFloat());

        if (! legato)
        {
            noteTime = 0.0f;
        }
        else
        {
            noteTime = juce::jmin (noteTime, 0.1f);
            changeTime = 0.0f; // re-picked while sounding: audible bow change
        }

        noteStrength = strength;
        // down-bows (from the heavy frog end) bite harder than up-bows
        scratch = (0.3f + 0.7f * strength) * (bowDir > 0.0f ? 1.0f : 0.7f) * rosinBite;
    }

    // Finger change without a new pick (hammer-on, pull-off, soft pick): instant pitch change
    void jumpPitch (float hz) noexcept { logFreq = targetLogFreq = std::log (juce::jlimit (30.0f, 5000.0f, hz)); }

    // Follow bends and slides while a note is held
    void followPitch (float hz) noexcept { targetLogFreq = std::log (juce::jlimit (30.0f, 5000.0f, hz)); }

    void  setGate (float a) noexcept { targetAmp = a; }
    float getAmp() const noexcept    { return amp; }

    // Real-violin engine: this voice still runs the bow (pitch, glide, vibrato knob, envelope,
    // bow changes, rosin noise) but the tone itself comes from recorded samples.
    // tick() then returns only the rosin noise; the sample engine reads the rest from these getters.
    void  setExternalTone (bool e) noexcept { externalTone = e; }
    float getHz() const noexcept       { return lastHz; }       // current pitch, incl. glide + vibrato knob
    float getBowGain() const noexcept  { return lastBowGain; }  // envelope x stroke shape x bow-change dip
    float getPressure() const noexcept { return lastPressure; } // 0..1, chooses pp / mf / ff

    float tick() noexcept
    {
        amp     = targetAmp     + (amp - targetAmp) * (targetAmp > amp ? attCoeff : relCoeff);
        logFreq = targetLogFreq + (logFreq - targetLogFreq) * glideCoeff;

        const float invSr = (float) (1.0 / sampleRate);
        if (amp < 0.0005f)
        {
            noteTime = 0.0f;
        }
        else
        {
            noteTime += invSr;
            strokeTime += invSr;

            // the bow runs out: change direction while the note is held
            if (strokeTime >= nextStrokeLen && changeTime < 0.0f)
            {
                changeTime = 0.0f;
                bowDir = -bowDir;
                strokeTime = 0.0f;
                nextStrokeLen = strokeLen * (0.8f + 0.4f * random.nextFloat());
            }
        }

        // bow change: ~110 ms where the tone dips and the rosin noise rises
        float w = 0.0f;
        if (changeTime >= 0.0f)
        {
            const float x = changeTime / 0.11f;
            const float sn = std::sin (juce::MathConstants<float>::pi * x);
            w = sn * sn;
            changeTime += invSr;
            if (changeTime > 0.11f)
                changeTime = -1.0f;
        }

        const float fade = juce::jlimit (0.0f, 1.0f, (noteTime - 0.15f) / 0.4f);

        vibPhase += vibInc;
        if (vibPhase > juce::MathConstants<float>::twoPi)
            vibPhase -= juce::MathConstants<float>::twoPi;

        // slow random wander of pitch and level: no two cycles are identical
        jit  = jit  * jitA  + (1.0f - jitA)  * (random.nextFloat() * 2.0f - 1.0f);
        shim = shim * shimA + (1.0f - shimA) * (random.nextFloat() * 2.0f - 1.0f);

        // (recorded samples already carry the player's own pitch life, so no jitter on top)
        const float cents = std::sin (vibPhase) * vibDepth * fade + (externalTone ? 0.0f : jit * jitNorm * 2.5f);
        const float f  = std::exp (logFreq) * std::exp2 (cents / 1200.0f);
        const float dt = juce::jmin (0.45f, f / (float) sampleRate);

        const float saw = 2.0f * phase - 1.0f - polyBlep (phase, dt);
        phase += dt;
        if (phase >= 1.0f)
            phase -= 1.0f;

        // rosin noise: bursts after each stick-slip, constant flicker, a "bite" at the start,
        // and a rise during bow changes. Its level follows sqrt(amp), so it shows more on soft bowing.
        float slip = 1.0f - phase;
        slip *= slip;
        slip *= slip;
        scratch *= scratchDecay;
        grain = grain * grainA + (1.0f - grainA) * (random.nextFloat() * 2.0f - 1.0f);
        const float flicker = juce::jmax (0.1f, 1.0f + rosinFlicker * grain * grainNorm);

        // rosin noise only exists while the bow touches the string: it stops when the bow lifts,
        // even though the tone keeps ringing for a moment
        const float contactTarget = targetAmp > 0.0f ? 1.0f : 0.0f;
        bowContact = contactTarget + (bowContact - contactTarget) * contactCoeff;

        float n = (random.nextFloat() * 2.0f - 1.0f) * noiseGain * rosinNoise
                  * (1.0f - rosinGrit + rosinGrit * slip) * (1.0f + scratch) * flicker
                  * (1.0f + w * (noiseBoost - 1.0f)) * bowContact
                  / std::sqrt (std::sqrt (juce::jmax (amp, 0.2f)));
        n = noiseLowpass.processSample (0, noiseFilter.processSample (0, n));

        if (++ctrlCount >= 32)
        {
            ctrlCount = 0;
            const float pressure = juce::jmin (1.0f, amp) * (0.7f + 0.3f * noteStrength);
            const float cutoff = (900.0f + bright * bright * 9000.0f * (0.4f + 0.6f * pressure)) * (1.0f - 0.4f * w) * rosinTone;
            toneFilter.setCutoffFrequency (juce::jmin (cutoff, (float) sampleRate * 0.45f));

            // stroke shape: down-bows fade a little, up-bows grow a little
            const float s = juce::jlimit (0.0f, 1.0f, strokeTime / juce::jmax (0.05f, nextStrokeLen));
            const float strokeDb = bowDir > 0.0f ? -3.0f * bowMotion * s : bowMotion * (2.0f * s - 1.0f);
            strokeGain = juce::Decibels::decibelsToGain (strokeDb);
            changeGain = juce::Decibels::decibelsToGain (-9.0f * bowMotion);   // dip depth at the middle of a change
            noiseBoost = juce::Decibels::decibelsToGain (8.0f * bowMotion);    // rosin rise during a change
        }

        const float dip = 1.0f - w * (1.0f - changeGain);

        if (externalTone)
        {
            lastHz = f;
            lastBowGain = amp * strokeGain * dip;
            lastPressure = juce::jlimit (0.0f, 1.0f, juce::jmin (1.0f, amp) * (0.7f + 0.3f * noteStrength));
            return n * 0.6f * amp * strokeGain * dip; // real recordings already contain some bow noise
        }

        const float out = toneFilter.processSample (0, saw * 0.6f + n);
        return out * amp * strokeGain * dip * (1.0f + shim * shimNorm * 0.11f);
    }

private:
    static float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }
        return 0.0f;
    }

    double sampleRate = 44100.0;
    float phase = 0.0f, logFreq = 0.0f, targetLogFreq = 0.0f;
    float amp = 0.0f, targetAmp = 0.0f;
    float attCoeff = 0.0f, relCoeff = 0.0f, glideCoeff = 0.0f;
    float vibPhase = 0.0f, vibInc = 0.0f, vibDepth = 0.0f, noteTime = 0.0f;
    float noiseAmt = 0.0f, bright = 0.5f, noteStrength = 1.0f, scratch = 0.0f, scratchDecay = 0.0f;
    float jit = 0.0f, shim = 0.0f, jitA = 0.0f, shimA = 0.0f, jitNorm = 1.0f, shimNorm = 1.0f;
    float grain = 0.0f, grainA = 0.0f, grainNorm = 1.0f;
    float bowMotion = 0.5f, strokeLen = 0.9f, nextStrokeLen = 0.9f, strokeTime = 0.0f, changeTime = -1.0f, bowDir = 1.0f;
    float strokeGain = 1.0f, changeGain = 1.0f, noiseBoost = 1.0f;
    float noiseGain = 0.0f, bowContact = 0.0f, contactCoeff = 0.0f;
    int   rosinType = -1;
    float rosinNoise = 1.0f, rosinFlicker = 0.35f, rosinGrit = 0.7f, rosinBite = 1.0f, rosinTone = 1.0f;
    int   ctrlCount = 0;
    bool  externalTone = false;
    float lastHz = 440.0f, lastBowGain = 0.0f, lastPressure = 0.0f;
    juce::Random random { 1234 };
    juce::dsp::StateVariableTPTFilter<float> noiseFilter, noiseLowpass, toneFilter;
};

//==============================================================================
// Pizzicato: a Karplus-Strong plucked string excited by a finger-like noise burst.
// It runs through the same violin/sarangi body as the bowed voice.
class PluckedString
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        len = (int) (sr / 30.0) + 4;
        buf.assign ((size_t) len, 0.0f);
        write = 0;
        prev = excLp = 0.0f;
        excRemaining = 0;
        delay.reset (sr, 0.02);
        delay.setCurrentAndTargetValue (100.0f);
        t60 = 0.6f;
        updateGain();
    }

    void pluck (float hz, float strength, float decaySeconds) noexcept
    {
        const float d = juce::jlimit (2.0f, (float) len - 3.0f, (float) (sampleRate / juce::jmax (30.0f, hz)) - 0.5f);
        delay.setCurrentAndTargetValue (d);
        t60 = decaySeconds;
        updateGain();

        for (auto& v : buf)
            v *= 0.3f; // the finger stops most of the previous vibration

        excRemaining = (int) d;
        excAmp = 0.25f + 0.75f * strength;
        excCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * (1200.0 + 5000.0 * strength) / sampleRate);
        excNorm = 1.0f / (0.577f * std::sqrt ((1.0f - excCoeff) / (1.0f + excCoeff)));
        excLp = 0.0f;
    }

    void setFreq (float hz) noexcept
    {
        delay.setTargetValue (juce::jlimit (2.0f, (float) len - 3.0f, (float) (sampleRate / juce::jmax (30.0f, hz)) - 0.5f));
        updateGain();
    }

    void mute() noexcept
    {
        t60 = juce::jmin (t60, 0.08f);
        updateGain();
    }

    float tick() noexcept
    {
        const float d = delay.getNextValue();
        float rp = (float) write - d;
        if (rp < 0.0f) rp += (float) len;
        const int   i0 = (int) rp;
        const int   i1 = (i0 + 1) % len;
        const float fr = rp - (float) i0;
        const float y  = buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * fr;

        const float filtered = 0.5f * (y + prev);
        prev = y;

        float in = 0.0f;
        if (excRemaining > 0)
        {
            --excRemaining;
            const float r = random.nextFloat() * 2.0f - 1.0f;
            excLp = r + (excLp - r) * excCoeff;
            in = excLp * excNorm * excAmp * 0.5f;
        }

        buf[(size_t) write] = filtered * loopGain + in;
        write = (write + 1) % len;
        return y;
    }

private:
    void updateGain() noexcept
    {
        loopGain = std::pow (10.0f, -3.0f * delay.getTargetValue() / (juce::jmax (0.02f, t60) * (float) sampleRate));
    }

    std::vector<float> buf;
    juce::SmoothedValue<float> delay;
    juce::Random random { 99 };
    double sampleRate = 44100.0;
    int len = 1, write = 0, excRemaining = 0;
    float prev = 0.0f, loopGain = 0.99f, t60 = 0.6f;
    float excLp = 0.0f, excCoeff = 0.0f, excNorm = 1.0f, excAmp = 1.0f;
};

//==============================================================================
// Sympathetic strings tuned to all 12 chromatic notes (C4-B4), so whatever note
// you play, its matching string and harmonics ring along. No fixed key.
class SympatheticStrings
{
public:
    void prepare (double sr)
    {
        for (size_t i = 0; i < combs.size(); ++i)
        {
            auto& c = combs[i];
            c.buf.assign ((size_t) (sr / 100.0) + 4, 0.0f);
            c.write = 0;
            c.lp = 0.0f;
            const float hz = 261.63f * std::exp2 ((float) i / 12.0f);
            c.delay = (float) (sr / hz);
        }
    }

    float process (float in) noexcept
    {
        float sum = 0.0f;
        for (auto& c : combs)
        {
            const int len = (int) c.buf.size();
            float rp = (float) c.write - c.delay;
            if (rp < 0.0f) rp += (float) len;
            const int   i0 = (int) rp;
            const int   i1 = (i0 + 1) % len;
            const float fr = rp - (float) i0;
            const float d  = c.buf[(size_t) i0] + (c.buf[(size_t) i1] - c.buf[(size_t) i0]) * fr;

            c.lp = d * 0.7f + c.lp * 0.3f;
            c.buf[(size_t) c.write] = in * 0.05f + c.lp * 0.985f;
            c.write = (c.write + 1) % len;
            sum += d;
        }
        return sum * 0.2f;
    }

private:
    struct Comb { std::vector<float> buf; int write = 0; float delay = 100.0f, lp = 0.0f; };
    std::array<Comb, 12> combs;
};

//==============================================================================
// Stereo ping-pong delay with smoothed time and a darkened feedback path
class PingPongDelay
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        len = (int) (sr * 2.0) + 4;
        bufL.assign ((size_t) len, 0.0f);
        bufR.assign ((size_t) len, 0.0f);
        write = 0;
        dampL = dampR = 0.0f;
        timeSamples.reset (sr, 0.08);
        timeSamples.setCurrentAndTargetValue ((float) (sr * 0.38));
    }

    void reset()
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        dampL = dampR = 0.0f;
    }

    void setParams (float ms, float fb, float wet) noexcept
    {
        timeSamples.setTargetValue (juce::jlimit (1.0f, (float) (len - 3), ms * 0.001f * (float) sampleRate));
        feedback = fb;
        mix = wet;
    }

    void process (float& l, float& r) noexcept
    {
        const float d  = timeSamples.getNextValue();
        const float dL = read (bufL, d);
        const float dR = read (bufR, d);

        dampL = dL * 0.6f + dampL * 0.4f;
        dampR = dR * 0.6f + dampR * 0.4f;

        const float in = 0.5f * (l + r);
        bufL[(size_t) write] = in + dampR * feedback;
        bufR[(size_t) write] = dampL * feedback;
        write = (write + 1) % len;

        l += dL * mix;
        r += dR * mix;
    }

private:
    float read (const std::vector<float>& b, float delay) const noexcept
    {
        float rp = (float) write - delay;
        if (rp < 0.0f) rp += (float) len;
        const int   i0 = (int) rp;
        const int   i1 = (i0 + 1) % len;
        const float fr = rp - (float) i0;
        return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * fr;
    }

    std::vector<float> bufL, bufR;
    juce::SmoothedValue<float> timeSamples;
    double sampleRate = 44100.0;
    int len = 1, write = 0;
    float feedback = 0.3f, mix = 0.2f, dampL = 0.0f, dampR = 0.0f;
};

} // namespace bow
