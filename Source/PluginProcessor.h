#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "BowDSP.h"
#include "RealViolin.h"

class BowStringProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater
{
public:
    BowStringProcessor();
    ~BowStringProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BowString"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    // Factory presets (also shown in Logic's preset menu)
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    juce::AudioProcessorValueTreeState apvts;

    // read by the editor for the note display and input meter
    std::atomic<float> detectedPitch { 0.0f };
    std::atomic<float> inputLevel    { 0.0f };

    // Real violin engine status, for the editor
    bool isRealEngineSelected() const { return p.engine->load() > 0.5f; }
    bool areSamplesReady() const      { return sampleBank->isReady(); }

private:
    void parameterChanged (const juce::String& id, float newValue) override;
    void handleAsyncUpdate() override;
    void updateParameters();
    void updateBody (int mode);
    void startNote (float hz);
    int currentProgram = 0;

    struct Params
    {
        std::atomic<float> *mode, *engine, *artic, *octave, *blend, *bright, *bowNoise, *sympath,
                           *attack, *release, *dynamics, *glide, *vibRate, *vibDepth, *pizzDecay, *bowMotion, *stroke, *rosinType,
                           *gateOn, *gate, *compOn, *sustain, *driveOn, *drive, *driveLvl,
                           *eqOn, *bass, *mid, *treble,
                           *chorusOn, *chorusRate, *chorusDepth, *chorusMix,
                           *delayOn, *delayTime, *delayFb, *delayMix,
                           *reverbOn, *revSize, *revDamp, *revMix, *output;
    } p {};

    bow::PitchTracker       tracker;
    bow::BowedVoice         voice;
    bow::PluckedString      pluck;
    bow::SympatheticStrings sympathetic;
    bow::PingPongDelay      delay;

    juce::SharedResourcePointer<bow::SampleBank> sampleBank;   // decoded once, shared by all instances
    bow::RealViolinVoice    real;
    juce::dsp::IIR::Filter<float> realBright;                  // Bright knob for the recorded violin
    float  lastRealBright = -1.0f;
    bool   realEngine = false, lastRealEngine = false, lastPizz = false;
    static constexpr float realLevel = 2.0f;                    // level match with the Modeled engine
    std::atomic<int> engineSeen { 0 };                         // for the "vibrato 0 in Real mode" default

    std::array<juce::dsp::IIR::Filter<float>, 6> body; // high-pass + 5 body resonances
    std::array<juce::dsp::IIR::Filter<float>, 3> eq;   // bass / mid / treble
    juce::dsp::Compressor<float> comp;
    juce::dsp::Chorus<float>     chorus;
    juce::dsp::Reverb            reverb;

    juce::SmoothedValue<float> outGain, blendSmoothed;

    double sr = 44100.0;
    int    currentMode = -1;
    float  lastBass = -99.0f, lastMid = -99.0f, lastTreble = -99.0f;
    bool   lastDelayOn = false, lastReverbOn = false;

    // per-block cached settings
    float octaveMul = 1.0f, gateThreshDb = -55.0f, dynamicsAmt = 0.5f, sympathAmt = 0.2f;
    float driveGain = 1.0f, driveLevel = 1.0f, swellInc = 0.001f;
    float attackMax = 200.0f, pizzDecaySec = 0.6f;
    bool  pizzicato = false;

    // input analysis state
    float env = 0.0f, fastEnv = 0.0f, slowEnv = 0.0f, swell = 1.0f, dryGate = 0.0f;
    float envAtt = 0.0f, envRel = 0.0f, fastAtt = 0.0f, fastRel = 0.0f, slowAtt = 0.0f, slowRel = 0.0f;
    float gateAtt = 0.0f, gateRel = 0.0f;
    bool  noteActive = false;
    int   samplesSinceOnset = 0;

    // note-start logic: wait for a stable pitch before the bow touches the string
    enum class NoteState { idle, waiting, playing };
    NoteState noteState = NoteState::idle;
    int   waitSamples = 0, lastFrameId = -1, stableCount = 0, ignoreSamples = 0, timeoutSamples = 0;
    float lastEstimate = 0.0f, pendingJump = 0.0f, notePeak = 0.0f, peakRef = 1.0e-4f, peakRefDecay = 0.0f;
    float noteVel = 1.0f, playingPitch = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BowStringProcessor)
};
