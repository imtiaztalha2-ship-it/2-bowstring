#include "PluginProcessor.h"
#include "PluginEditor.h"

using APVTS  = juce::AudioProcessorValueTreeState;
using Coeffs = juce::dsp::IIR::ArrayCoefficients<float>;

//==============================================================================
APVTS::ParameterLayout BowStringProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto skewed = [] (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    };

    auto addFloat = [&] (const char* id, const char* name, juce::NormalisableRange<float> range,
                         float def, juce::String suffix, int decimals)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [suffix, decimals] (float v, int)
                {
                    return (decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals)) + suffix;
                })));
    };

    auto addPercent = [&] (const char* id, const char* name, float def, float max = 1.0f)
    {
        params.push_back (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.0f, max), def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })));
    };

    auto addBool = [&] (const char* id, const char* name, bool def)
    {
        params.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def));
    };

    auto addChoice = [&] (const char* id, const char* name, juce::StringArray choices, int def)
    {
        params.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def));
    };

    // Instrument
    addChoice ("mode",   "Instrument", { "Violin", "Sarangi" }, 0);
    addChoice ("engine", "Engine", { "Modeled", "Real violin" }, 0);
    addChoice ("artic",  "Articulation", { "Arco (bowed)", "Pizzicato" }, 0);
    addChoice ("octave", "Octave", { "-1", "0", "+1", "+2" }, 1);
    addPercent ("blend",    "Bowed blend", 0.85f);
    addPercent ("bright",   "Brightness",  0.55f);
    addPercent ("bownoise", "Rosin noise", 0.35f);
    addChoice ("rosintype", "Rosin type", { "Light - smooth", "Amber - balanced", "Dark - grippy", "Gold - bright", "Old - harsh" }, 1);
    addPercent ("sympath",  "Sympathetic strings", 0.20f);

    // Bowing
    addFloat ("attack",   "Soft-pick swell", skewed (10.0f, 1000.0f, 150.0f), 200.0f, " ms", 0);
    addFloat ("release",  "Bow release",   skewed (20.0f, 2000.0f, 300.0f), 200.0f, " ms", 0);
    addPercent ("dynamics", "Dynamics", 0.5f);
    addFloat ("glide",    "Glide",         skewed (0.0f, 500.0f, 80.0f), 60.0f, " ms", 0);
    addFloat ("vibrate",  "Vibrato rate",  juce::NormalisableRange<float> (2.0f, 9.0f), 6.4f, " Hz", 1);
    addFloat ("vibdepth", "Vibrato depth", juce::NormalisableRange<float> (0.0f, 40.0f), 11.0f, " ct", 0);
    addPercent ("bowmotion", "Bow motion", 0.5f);
    addFloat ("stroke",   "Stroke length", skewed (0.3f, 4.0f, 1.0f), 0.9f, " s", 1);
    addFloat ("pizzdecay", "Pizzicato decay", skewed (0.15f, 2.0f, 0.6f), 0.6f, " s", 2);

    // Pedals
    addBool  ("gate_on", "Gate on", true);
    addFloat ("gate",    "Gate threshold", juce::NormalisableRange<float> (-80.0f, -20.0f), -55.0f, " dB", 0);

    addBool    ("comp_on", "Compressor on", true);
    addPercent ("sustain", "Sustain", 0.4f);

    addBool    ("drive_on", "Drive on", false);
    addPercent ("drive",    "Drive", 0.3f);
    addFloat   ("drivelvl", "Drive level", juce::NormalisableRange<float> (-24.0f, 6.0f), -6.0f, " dB", 1);

    addBool  ("eq_on",  "EQ on", true);
    addFloat ("bass",   "Bass",   juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, " dB", 1);
    addFloat ("mid",    "Mid",    juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, " dB", 1);
    addFloat ("treble", "Treble", juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, " dB", 1);

    addBool    ("chorus_on",   "Chorus on", false);
    addFloat   ("chorusrate",  "Chorus rate", juce::NormalisableRange<float> (0.1f, 5.0f), 0.8f, " Hz", 2);
    addPercent ("chorusdepth", "Chorus depth", 0.3f);
    addPercent ("chorusmix",   "Chorus mix", 0.3f);

    addBool    ("delay_on",  "Delay on", true);
    addFloat   ("delaytime", "Delay time", skewed (20.0f, 1500.0f, 400.0f), 380.0f, " ms", 0);
    addPercent ("delayfb",   "Delay feedback", 0.3f, 0.9f);
    addPercent ("delaymix",  "Delay mix", 0.18f);

    addBool    ("reverb_on", "Reverb on", true);
    addPercent ("revsize",   "Reverb size", 0.7f);
    addPercent ("revdamp",   "Reverb damping", 0.4f);
    addPercent ("revmix",    "Reverb mix", 0.3f);

    addFloat ("output", "Output", juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, " dB", 1);

    return { params.begin(), params.end() };
}

//==============================================================================
BowStringProcessor::BowStringProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id); };

    p.mode = get ("mode");         p.engine = get ("engine");     p.artic = get ("artic");           p.octave = get ("octave");
    p.blend = get ("blend");       p.bright = get ("bright");     p.bowNoise = get ("bownoise");
    p.sympath = get ("sympath");   p.attack = get ("attack");     p.release = get ("release");
    p.dynamics = get ("dynamics"); p.glide = get ("glide");       p.vibRate = get ("vibrate");
    p.vibDepth = get ("vibdepth"); p.pizzDecay = get ("pizzdecay");
    p.bowMotion = get ("bowmotion"); p.stroke = get ("stroke"); p.rosinType = get ("rosintype"); p.gateOn = get ("gate_on");    p.gate = get ("gate");
    p.compOn = get ("comp_on");    p.sustain = get ("sustain");   p.driveOn = get ("drive_on");
    p.drive = get ("drive");       p.driveLvl = get ("drivelvl"); p.eqOn = get ("eq_on");
    p.bass = get ("bass");         p.mid = get ("mid");           p.treble = get ("treble");
    p.chorusOn = get ("chorus_on");     p.chorusRate = get ("chorusrate");
    p.chorusDepth = get ("chorusdepth"); p.chorusMix = get ("chorusmix");
    p.delayOn = get ("delay_on");  p.delayTime = get ("delaytime");
    p.delayFb = get ("delayfb");   p.delayMix = get ("delaymix");
    p.reverbOn = get ("reverb_on"); p.revSize = get ("revsize");
    p.revDamp = get ("revdamp");   p.revMix = get ("revmix");     p.output = get ("output");

    engineSeen = (int) p.engine->load();
    apvts.addParameterListener ("engine", this);
}

BowStringProcessor::~BowStringProcessor()
{
    apvts.removeParameterListener ("engine", this);
    cancelPendingUpdate();
}

// Switching to the Real violin engine sets Vib depth to 0 (the recordings already have the
// player's vibrato); switching back to Modeled restores the default if it is still 0.
// Done on the message thread, and never when a project or preset is being loaded.
void BowStringProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate();
}

void BowStringProcessor::handleAsyncUpdate()
{
    const int e = (int) p.engine->load();
    if (e == engineSeen.exchange (e))
        return;

    if (auto* vib = apvts.getParameter ("vibdepth"))
    {
        if (e == 1)
            vib->setValueNotifyingHost (vib->convertTo0to1 (0.0f));
        else if (p.vibDepth->load() < 0.5f)
            vib->setValueNotifyingHost (vib->getDefaultValue());
    }
}

bool BowStringProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();

    const bool inOk  = in  == juce::AudioChannelSet::mono() || in  == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk;
}

//==============================================================================
void BowStringProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    tracker.prepare (sr);
    voice.prepare (sr);
    pluck.prepare (sr);
    sympathetic.prepare (sr);
    delay.prepare (sr);
    real.prepare (sr, &sampleBank.get());
    real.setDampTime (80.0f);
    realBright.reset();
    lastRealBright = -1.0f;
    lastRealEngine = realEngine = false;

    const auto numOut = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec monoSpec   { sr, (juce::uint32) samplesPerBlock, 1 };
    juce::dsp::ProcessSpec stereoSpec { sr, (juce::uint32) samplesPerBlock, numOut };

    for (auto& f : body) { f.coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass (sr, 1000.0f); f.reset(); }
    for (auto& f : eq)   { f.coefficients = juce::dsp::IIR::Coefficients<float>::makeAllPass (sr, 1000.0f); f.reset(); }
    currentMode = -1;
    lastBass = lastMid = lastTreble = -99.0f;

    comp.prepare (monoSpec);
    comp.setAttack (10.0f);
    comp.setRelease (150.0f);

    chorus.prepare (stereoSpec);
    chorus.setCentreDelay (7.0f);
    chorus.setFeedback (0.1f);

    reverb.prepare (stereoSpec);
    reverb.reset();

    envAtt  = bow::onePoleCoeff (2.0f,  sr);  envRel  = bow::onePoleCoeff (90.0f,  sr);
    fastAtt = bow::onePoleCoeff (0.5f,  sr);  fastRel = bow::onePoleCoeff (25.0f,  sr);
    slowAtt = bow::onePoleCoeff (25.0f, sr);  slowRel = bow::onePoleCoeff (250.0f, sr);
    gateAtt = bow::onePoleCoeff (3.0f,  sr);  gateRel = bow::onePoleCoeff (60.0f,  sr);

    env = fastEnv = slowEnv = dryGate = 0.0f;
    swell = 1.0f;
    noteActive = false;
    samplesSinceOnset = 0;

    noteState = NoteState::idle;
    waitSamples = stableCount = 0;
    lastFrameId = -1;
    lastEstimate = pendingJump = notePeak = playingPitch = 0.0f;
    peakRef = 1.0e-4f;
    peakRefDecay   = bow::onePoleCoeff (8000.0f, sr); // pick strength is judged against the last ~8 s of playing
    ignoreSamples  = (int) (0.012 * sr);             // skip frames still full of the previous note
    timeoutSamples = (int) (0.060 * sr);

    outGain.reset (sr, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
    blendSmoothed.reset (sr, 0.05);
    blendSmoothed.setCurrentAndTargetValue (p.blend->load());

    lastDelayOn  = p.delayOn->load()  > 0.5f;
    lastReverbOn = p.reverbOn->load() > 0.5f;

    updateParameters();
}

//==============================================================================
void BowStringProcessor::updateBody (int mode)
{
    struct Band { float freq, q, gainDb; };

    // Violin: air + wood resonances, the dip around 1.6 kHz and the strong 2-2.5 kHz region,
    // then a steep roll-off above 4 kHz (shape measured from a real violin recording)
    static constexpr Band violin[5]  = { { 280.0f, 2.5f, 4.0f }, { 470.0f, 3.0f, 3.0f }, { 1600.0f, 2.0f, -6.0f },
                                         { 2300.0f, 1.6f, 6.0f }, { 5500.0f, 0.6f, -9.0f } };
    // Sarangi: skin-covered body, strong nasal formant
    static constexpr Band sarangi[5] = { { 400.0f, 2.0f, 4.0f }, { 950.0f, 2.5f, 7.0f }, { 1900.0f, 2.0f, 5.0f },
                                         { 3200.0f, 1.5f, 4.0f }, { 6000.0f, 0.7f, -8.0f } };

    const auto& bands = mode == 0 ? violin : sarangi;
    const float nyq = (float) sr * 0.45f;

    *body[0].coefficients = Coeffs::makeHighPass (sr, mode == 0 ? 190.0f : 150.0f, 0.7071f);

    for (size_t i = 0; i < 5; ++i)
        *body[i + 1].coefficients = Coeffs::makePeakFilter (sr, juce::jmin (bands[i].freq, nyq), bands[i].q,
                                                            juce::Decibels::decibelsToGain (bands[i].gainDb));
}

void BowStringProcessor::updateParameters()
{
    const int mode = (int) p.mode->load();
    if (mode != currentMode)
    {
        updateBody (mode);
        currentMode = mode;
    }

    octaveMul = std::exp2 ((float) ((int) p.octave->load() - 1));
    pizzicato = (int) p.artic->load() == 1;
    attackMax = p.attack->load();
    pizzDecaySec = p.pizzDecay->load();

    voice.setParams (p.release->load(), p.glide->load(),
                     p.vibRate->load(), p.vibDepth->load(), p.bowNoise->load(), p.bright->load(),
                     p.bowMotion->load(), p.stroke->load());
    voice.setRosinType ((int) p.rosinType->load());

    // Engine: the recorded violin plays once its samples have finished loading
    realEngine = p.engine->load() > 0.5f && sampleBank->isReady();
    if (realEngine != lastRealEngine || pizzicato != lastPizz)
        real.stop();
    lastRealEngine = realEngine;
    lastPizz = pizzicato;
    voice.setExternalTone (realEngine);
    real.setPizzDecay (pizzDecaySec * 2.5f);

    const float bright = p.bright->load();
    if (! juce::exactlyEqual (bright, lastRealBright))
    {
        realBright.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf (
            sr, juce::jmin (3000.0f, (float) sr * 0.45f), 0.7071f, juce::Decibels::decibelsToGain ((bright - 0.55f) * 18.0f));
        lastRealBright = bright;
    }

    swellInc     = 1.0f / juce::jmax (1.0f, p.attack->load() * 0.001f * (float) sr);
    dynamicsAmt  = p.dynamics->load();
    sympathAmt   = p.sympath->load();
    gateThreshDb = p.gateOn->load() > 0.5f ? p.gate->load() : -100.0f;
    blendSmoothed.setTargetValue (p.blend->load());

    // Compressor used as a sustainer
    const float sustain = p.sustain->load();
    comp.setThreshold (-10.0f - 30.0f * sustain);
    comp.setRatio (1.0f + 7.0f * sustain);

    // Drive
    driveGain  = juce::Decibels::decibelsToGain (p.drive->load() * 36.0f);
    driveLevel = juce::Decibels::decibelsToGain (p.driveLvl->load());

    // EQ
    const float bass = p.bass->load(), mid = p.mid->load(), treble = p.treble->load();
    if (! juce::exactlyEqual (bass, lastBass))
    {
        *eq[0].coefficients = Coeffs::makeLowShelf (sr, 120.0f, 0.7071f, juce::Decibels::decibelsToGain (bass));
        lastBass = bass;
    }
    if (! juce::exactlyEqual (mid, lastMid))
    {
        *eq[1].coefficients = Coeffs::makePeakFilter (sr, 900.0f, 0.8f, juce::Decibels::decibelsToGain (mid));
        lastMid = mid;
    }
    if (! juce::exactlyEqual (treble, lastTreble))
    {
        *eq[2].coefficients = Coeffs::makeHighShelf (sr, juce::jmin (3500.0f, (float) sr * 0.45f), 0.7071f,
                                                     juce::Decibels::decibelsToGain (treble));
        lastTreble = treble;
    }

    // Chorus
    chorus.setRate (p.chorusRate->load());
    chorus.setDepth (p.chorusDepth->load());
    chorus.setMix (p.chorusMix->load());

    // Delay
    delay.setParams (p.delayTime->load(), p.delayFb->load(), p.delayMix->load());

    // Reverb
    juce::dsp::Reverb::Parameters rp;
    rp.roomSize  = p.revSize->load();
    rp.damping   = p.revDamp->load();
    rp.wetLevel  = p.revMix->load();
    rp.dryLevel  = 1.0f - 0.5f * p.revMix->load();
    rp.width     = 1.0f;
    reverb.setParameters (rp);

    outGain.setTargetValue (juce::Decibels::decibelsToGain (p.output->load()));
}

//==============================================================================
void BowStringProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);

    if (numOut == 0 || n == 0)
        return;

    updateParameters();

    const bool compOn  = p.compOn->load()  > 0.5f;
    const bool driveOn = p.driveOn->load() > 0.5f;
    const bool eqOn    = p.eqOn->load()    > 0.5f;

    const float gateOnLin  = juce::Decibels::decibelsToGain (gateThreshDb);
    const float gateOffLin = juce::Decibels::decibelsToGain (gateThreshDb - 4.0f);
    const int   minOnsetGap = (int) (0.08 * sr);

    float* out0 = buffer.getWritePointer (0);
    const float* in1 = (numIn > 1 && buffer.getNumChannels() > 1) ? buffer.getReadPointer (1) : nullptr;

    for (int i = 0; i < n; ++i)
    {
        const float x  = in1 != nullptr ? 0.5f * (out0[i] + in1[i]) : out0[i];
        const float ax = std::abs (x);

        env     = ax + (env     - ax) * (ax > env     ? envAtt  : envRel);
        fastEnv = ax + (fastEnv - ax) * (ax > fastEnv ? fastAtt : fastRel);
        slowEnv = ax + (slowEnv - ax) * (ax > slowEnv ? slowAtt : slowRel);

        tracker.push (x);
        peakRef = juce::jmax (peakRef * peakRefDecay, 1.0e-4f);

        // gate with hysteresis decides when a note is being played
        bool onset = false;
        if (! noteActive && env > gateOnLin)
        {
            noteActive = true;
            onset = true;
        }
        else if (noteActive && env < gateOffLin)
        {
            noteActive = false;
        }

        ++samplesSinceOnset;
        if (noteActive && fastEnv > slowEnv * 1.6f && samplesSinceOnset > minOnsetGap)
            onset = true;

        if (onset)
        {
            samplesSinceOnset = 0;
            swell = pizzicato ? 1.0f : 0.0f;
            noteState = NoteState::waiting;
            waitSamples = stableCount = 0;
            notePeak = 0.0f;
            tracker.resetHistory();
            lastFrameId = tracker.getFrameId();
        }

        if (! noteActive && noteState != NoteState::idle)
        {
            noteState = NoteState::idle;
            pluck.mute();
            real.damp();
        }

        const bool newFrame = tracker.getFrameId() != lastFrameId;
        lastFrameId = tracker.getFrameId();

        if (noteState == NoteState::waiting)
        {
            // measure how hard the string was picked, and wait for two agreeing pitch readings
            notePeak = juce::jmax (notePeak, fastEnv);
            ++waitSamples;

            if (newFrame && waitSamples >= ignoreSamples && tracker.isVoiced())
            {
                const float est = tracker.getPitch();
                if (stableCount > 0 && std::abs (1200.0f * std::log2 (est / lastEstimate)) < 50.0f)
                    ++stableCount;
                else
                    stableCount = 1;
                lastEstimate = est;
            }

            if (stableCount >= 2 || (waitSamples >= timeoutSamples && stableCount > 0))
                startNote (lastEstimate * octaveMul);
        }
        else if (noteState == NoteState::playing && newFrame && tracker.isVoiced())
        {
            // Gradual movement = slide/bend/vibrato -> follow with glide.
            // A sudden step (confirmed by two readings) = new fretted note -> change instantly.
            const float est = tracker.getPitch() * octaveMul;
            const float cents = std::abs (1200.0f * std::log2 (est / playingPitch));

            if (cents < 80.0f)
            {
                playingPitch = est;
                pendingJump = 0.0f;
                voice.followPitch (est);
                pluck.setFreq (est);
                real.setPizzFreq (est);
            }
            else if (pendingJump > 0.0f && std::abs (1200.0f * std::log2 (est / pendingJump)) < 50.0f)
            {
                playingPitch = est;
                pendingJump = 0.0f;
                voice.jumpPitch (est);

                if (pizzicato) // a new note on the guitar is a new pluck
                {
                    const float strength = juce::jlimit (0.0f, 1.0f, fastEnv / peakRef);
                    pluck.pluck (est, strength, pizzDecaySec);
                    if (realEngine) real.pluck (est, strength);
                }
                else
                {
                    pluck.setFreq (est);
                    if (realEngine) real.fingerChange (est); // left-hand change, same bow
                }
            }
            else
            {
                pendingJump = est;
            }
        }

        // bow pressure follows the guitar's own dynamics, relative to how the note was picked
        float target = 0.0f;
        if (noteState == NoteState::playing && ! pizzicato)
        {
            const float shape = juce::jlimit (0.0f, 1.0f, env / juce::jmax (notePeak, 1.0e-6f));
            target = noteVel * (1.0f + dynamicsAmt * (shape - 1.0f));
        }
        voice.setGate (target);

        float v;
        if (realEngine)
        {
            // recorded violin: tone from the samples, bow envelope / bow changes / rosin from the bow model.
            // No body filter: the recordings already are a real violin body.
            const float rosin = voice.tick();
            const float tone  = pizzicato ? real.tickPizz()
                                          : real.tickArco (voice.getHz(), voice.getPressure()) * voice.getBowGain();
            v = realBright.processSample (tone * realLevel + rosin);
            if (! pizzicato && voice.getAmp() < 1.0e-4f && noteState != NoteState::playing)
                real.stop();
        }
        else
        {
            // bowed voice + pizzicato string + instrument body
            v = voice.tick() + pluck.tick() * 0.8f;
            for (auto& f : body)
                v = f.processSample (v);
        }
        if (sympathAmt > 0.0f)
            v += sympathetic.process (v) * sympathAmt;
        v *= 0.5f;

        // the real guitar, with the pick attack removed by a volume swell
        swell = juce::jmin (1.0f, swell + swellInc);
        const float gateTarget = noteActive ? 1.0f : 0.0f;
        dryGate = gateTarget + (dryGate - gateTarget) * (gateTarget > dryGate ? gateAtt : gateRel);
        const float dry = x * swell * swell * dryGate;

        const float b = blendSmoothed.getNextValue();
        float y = v * b + dry * (1.0f - b);

        if (compOn)  y = comp.processSample (0, y);
        if (driveOn) y = std::tanh (y * driveGain) * driveLevel;
        if (eqOn)
            for (auto& f : eq)
                y = f.processSample (y);

        out0[i] = y;
    }

    for (int ch = 1; ch < numOut; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, n);

    // stereo pedals
    juce::dsp::AudioBlock<float> block (buffer);
    auto outBlock = block.getSubsetChannelBlock (0, (size_t) numOut);
    juce::dsp::ProcessContextReplacing<float> ctx (outBlock);

    if (p.chorusOn->load() > 0.5f)
        chorus.process (ctx);

    const bool delayOn = p.delayOn->load() > 0.5f;
    if (delayOn && ! lastDelayOn)
        delay.reset();
    lastDelayOn = delayOn;

    if (delayOn)
    {
        float* l = buffer.getWritePointer (0);
        float* r = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            float left = l[i], right = r != nullptr ? r[i] : l[i];
            delay.process (left, right);
            if (r != nullptr) { l[i] = left; r[i] = right; }
            else              { l[i] = 0.5f * (left + right); }
        }
    }

    const bool reverbOn = p.reverbOn->load() > 0.5f;
    if (reverbOn && ! lastReverbOn)
        reverb.reset();
    lastReverbOn = reverbOn;

    if (reverbOn)
        reverb.process (ctx);

    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int ch = 0; ch < numOut; ++ch)
            buffer.getWritePointer (ch)[i] *= g;
    }

    detectedPitch.store (noteState == NoteState::playing ? playingPitch : 0.0f);
    inputLevel.store (env);
}

//==============================================================================
void BowStringProcessor::startNote (float hz)
{
    peakRef = juce::jmax (peakRef, notePeak);
    const float strength = juce::jlimit (0.0f, 1.0f, notePeak / peakRef);
    noteVel = 0.35f + 0.65f * strength;

    if (pizzicato)
    {
        pluck.pluck (hz, strength, pizzDecaySec);
        if (realEngine) real.pluck (hz, strength);
    }
    else
    {
        // hard pick -> fast bite (about 12 ms), soft pick -> slow swell (the Swell knob)
        const float fast = juce::jmin (attackMax, 12.0f);
        const float attackMs = attackMax + (fast - attackMax) * std::pow (strength, 0.7f);
        const bool legato = voice.getAmp() > 0.05f;
        voice.startNote (hz, attackMs, strength, legato);
        if (realEngine) real.startBow (hz, legato); // new bow stroke: the sample's own bow attack
    }

    playingPitch = hz;
    pendingJump = 0.0f;
    noteState = NoteState::playing;
}

//==============================================================================
namespace
{
    struct Preset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values; // plain values; anything not listed = default
    };

    const std::vector<Preset>& presets()
    {
        // choice parameters use their index: mode 0=Violin 1=Sarangi, artic 0=Arco 1=Pizz,
        // octave 0=-1 1=0 2=+1 3=+2, rosintype 0=Light 1=Amber 2=Dark 3=Gold 4=Old
        static const std::vector<Preset> list = {
            { "Violin - Bowed (realistic)", {} },
            { "Violin - Bowed, more bow", { { "bownoise", 0.8f }, { "bowmotion", 0.9f }, { "stroke", 0.7f } } },
            { "Violin - Soft & smooth", { { "rosintype", 0 }, { "bownoise", 0.2f }, { "bowmotion", 0.3f }, { "attack", 400.0f },
                                          { "dynamics", 0.3f }, { "vibdepth", 9.0f }, { "revmix", 0.4f } } },
            { "Violin - Aggressive, close mic", { { "rosintype", 2 }, { "bownoise", 0.6f }, { "bowmotion", 0.8f }, { "attack", 60.0f },
                                                  { "dynamics", 0.8f }, { "treble", 2.0f }, { "revmix", 0.15f }, { "delay_on", 0 } } },
            { "Violin - Octave up (real violin range)", { { "octave", 2 } } },
            { "Violin - Pizzicato", { { "artic", 1 }, { "blend", 1.0f }, { "revmix", 0.25f }, { "delay_on", 0 } } },
            { "Violin - Wide dynamic range", { { "dynamics", 1.0f }, { "comp_on", 0 } } },
            { "Violin - Even dynamics", { { "dynamics", 0.15f }, { "sustain", 0.7f } } },
            { "Sarangi - Bowed with meend", { { "mode", 1 }, { "rosintype", 2 }, { "glide", 220.0f }, { "sympath", 0.55f },
                                              { "vibdepth", 14.0f } } },
            { "Sarangi - Pizzicato", { { "mode", 1 }, { "artic", 1 }, { "blend", 1.0f }, { "sympath", 0.6f } } },
            // engine 1 = Real violin (University of Iowa recordings); vibrato already in the samples
            { "Real violin - Expressive", { { "engine", 1 }, { "vibdepth", 0.0f }, { "blend", 1.0f }, { "bownoise", 0.2f },
                                            { "bowmotion", 0.35f }, { "sympath", 0.1f }, { "dynamics", 0.6f }, { "revmix", 0.28f } } },
            { "Real violin - Octave up", { { "engine", 1 }, { "vibdepth", 0.0f }, { "octave", 2 }, { "blend", 1.0f },
                                           { "bownoise", 0.2f }, { "bowmotion", 0.35f }, { "sympath", 0.1f }, { "revmix", 0.28f } } },
            { "Real violin - Slow & lyrical", { { "engine", 1 }, { "vibdepth", 0.0f }, { "blend", 1.0f }, { "attack", 350.0f },
                                                { "release", 500.0f }, { "glide", 120.0f }, { "bownoise", 0.15f }, { "bowmotion", 0.25f },
                                                { "stroke", 1.8f }, { "revmix", 0.38f } } },
            { "Real violin - Pizzicato", { { "engine", 1 }, { "vibdepth", 0.0f }, { "artic", 1 }, { "blend", 1.0f },
                                           { "pizzdecay", 0.9f }, { "revmix", 0.25f }, { "delay_on", 0 } } },
        };
        return list;
    }
}

int BowStringProcessor::getNumPrograms()
{
    return (int) presets().size();
}

const juce::String BowStringProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? presets()[(size_t) index].name : "";
}

void BowStringProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    currentProgram = index;

    // start every preset from the defaults, then apply its own values
    for (auto* param : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : presets()[(size_t) index].values)
        if (auto* param = apvts.getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));

    engineSeen = (int) p.engine->load(); // the preset sets its own vibrato
}

//==============================================================================
juce::AudioProcessorEditor* BowStringProcessor::createEditor()
{
    return new BowStringEditor (*this);
}

void BowStringProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void BowStringProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            apvts.replaceState (state);
            engineSeen = (int) p.engine->load(); // keep the saved vibrato setting
        }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BowStringProcessor();
}
