/*
  ==============================================================================

    Binaural Journey - audio processor

  ==============================================================================
*/

/*
    BinauralJourney
    Copyright (c) 2026 jwesters
    SPDX-License-Identifier: MIT
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <array>

namespace
{
constexpr float defaultAnchorFrequency = 200.0f;
constexpr float defaultBeatFrequency = 10.0f;
constexpr float defaultToneVolumePercent = 10.0f;
constexpr float defaultChannelVolumePercent = 100.0f;
constexpr float defaultNoiseVolumePercent = 8.0f;
constexpr float defaultMasterVolumePercent = 100.0f;
constexpr float defaultFadeInSeconds = 0.25f;
constexpr float defaultFadeOutSeconds = 0.25f;
constexpr float defaultSessionDurationMinutes = 10.0f;
constexpr int minimumJourneyStages = 2;
constexpr int maximumJourneyStages = 6;
constexpr int defaultJourneyStages = 3;
constexpr float defaultStageBeats[maximumJourneyStages] = {
    10.0f, 6.0f, 2.0f, 10.0f, 6.0f, 2.0f
};
constexpr float defaultStageHoldMinutes[maximumJourneyStages] = {
    5.0f, 10.0f, 15.0f, 5.0f, 5.0f, 5.0f
};
constexpr float defaultStageTransitionMinutes[maximumJourneyStages - 1] = {
    1.0f, 2.0f, 1.0f, 1.0f, 1.0f
};
constexpr int defaultStagePresetIndices[maximumJourneyStages] = {
    4, 2, 1, 4, 2, 1
};
constexpr double startupSilenceSeconds = 0.18;
constexpr double minimumStartupRampSeconds = 0.20;
constexpr int currentDefaultsVersion = 23;
const juce::Identifier defaultsVersionProperty { "defaultsVersion" };

float percentToGain (float percent) noexcept
{
    return juce::jlimit (0.0f, 1.0f, percent * 0.01f);
}

float renderWaveform (int waveform, double phase) noexcept
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    switch (waveform)
    {
        case 1: // Triangle
            return static_cast<float> (
                (2.0 / juce::MathConstants<double>::pi) * std::asin (std::sin (phase)));

        case 2: // Square
            return std::sin (phase) >= 0.0 ? 1.0f : -1.0f;

        case 3: // Sawtooth
            return static_cast<float> (2.0 * (phase / twoPi) - 1.0);

        case 0: // Sine
        default:
            return static_cast<float> (std::sin (phase));
    }
}


float selectNoiseSample (int noiseType,
                         float white,
                         float pink,
                         float brown) noexcept
{
    switch (noiseType)
    {
        case 1: return white;
        case 2: return pink;
        case 3: return brown;
        case 0:
        default: return 0.0f;
    }
}

bool stateContainsParameter (const juce::ValueTree& state,
                             const juce::String& parameterID)
{
    if (state.hasProperty (juce::Identifier (parameterID)))
        return true;

    for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
    {
        const auto child = state.getChild (childIndex);

        if (child.getProperty ("id").toString() == parameterID
            || stateContainsParameter (child, parameterID))
        {
            return true;
        }
    }

    return false;
}
}

//==============================================================================
void BinauralJourneyAudioProcessor::NoiseState::reset (std::uint32_t seed) noexcept
{
    randomState = seed != 0u ? seed : 0x12345678u;
    pinkB0 = pinkB1 = pinkB2 = pinkB3 = pinkB4 = pinkB5 = pinkB6 = 0.0f;
    brown = 0.0f;
}

float BinauralJourneyAudioProcessor::NoiseState::nextWhite() noexcept
{
    // Fast xorshift generator: deterministic, allocation-free, and safe to
    // call from the real-time audio thread.
    randomState ^= randomState << 13;
    randomState ^= randomState >> 17;
    randomState ^= randomState << 5;

    constexpr float scale = 2.0f / 4294967295.0f;
    return static_cast<float> (randomState) * scale - 1.0f;
}

float BinauralJourneyAudioProcessor::NoiseState::nextPink (float white) noexcept
{
    // Paul Kellet's efficient pink-noise filter, scaled to useful headroom.
    pinkB0 = 0.99886f * pinkB0 + white * 0.0555179f;
    pinkB1 = 0.99332f * pinkB1 + white * 0.0750759f;
    pinkB2 = 0.96900f * pinkB2 + white * 0.1538520f;
    pinkB3 = 0.86650f * pinkB3 + white * 0.3104856f;
    pinkB4 = 0.55000f * pinkB4 + white * 0.5329522f;
    pinkB5 = -0.7616f * pinkB5 - white * 0.0168980f;

    const float pink = pinkB0 + pinkB1 + pinkB2 + pinkB3
                     + pinkB4 + pinkB5 + pinkB6 + white * 0.5362f;

    pinkB6 = white * 0.115926f;
    return pink * 0.11f;
}

float BinauralJourneyAudioProcessor::NoiseState::nextBrown (float white) noexcept
{
    // Leaky integration avoids the long-term DC drift of a pure integrator.
    brown = (brown + 0.02f * white) / 1.02f;
    return brown * 3.5f;
}

double BinauralJourneyAudioProcessor::calculateMusicalNoteFrequency (
    int noteIndex,
    int octave,
    double a4Frequency) noexcept
{
    const int safeNoteIndex = juce::jlimit (0, 11, noteIndex);
    const int safeOctave = juce::jlimit (0, 9, octave);
    const double safeA4 = juce::jlimit (400.0, 480.0, a4Frequency);

    // MIDI note 69 is A4. MIDI note 12 is C0.
    const int midiNote = (safeOctave + 1) * 12 + safeNoteIndex;
    return safeA4 * std::pow (2.0, (static_cast<double> (midiNote) - 69.0) / 12.0);
}

//==============================================================================
BinauralJourneyAudioProcessor::BinauralJourneyAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
      parameterState (*this, nullptr, "PARAMETERS", createParameterLayout())
#else
    : parameterState (*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    // Mark newly-created plug-in instances with the current defaults version.
    // Older saved states are migrated once when they are restored.
    parameterState.state.setProperty (
        defaultsVersionProperty, currentDefaultsVersion, nullptr);
}

BinauralJourneyAudioProcessor::~BinauralJourneyAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
BinauralJourneyAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto anchorRange = juce::NormalisableRange<float> (20.0f, 2000.0f, 0.1f, 0.35f);
    auto beatRange = juce::NormalisableRange<float> (0.5f, 100.0f, 0.01f, 0.45f);
    auto percentageRange = juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f);

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "toneEnabled", 1 },
        "Tone - Enabled",
        true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "anchorFrequency", 1 },
        "Tone - Anchor Frequency",
        anchorRange,
        defaultAnchorFrequency,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "anchorMode", 1 },
        "Anchor Input",
        juce::StringArray { "Frequency", "Musical Note" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "anchorNote", 1 },
        "Anchor Note",
        juce::StringArray {
            "C", "C sharp / D flat", "D", "D sharp / E flat",
            "E", "F", "F sharp / G flat", "G",
            "G sharp / A flat", "A", "A sharp / B flat", "B"
        },
        9,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "anchorOctave", 1 },
        "Anchor Octave",
        juce::StringArray { "2", "3", "4", "5", "6" },
        2,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "tuningReference", 1 },
        "A4 Tuning Reference",
        juce::NormalisableRange<float> (400.0f, 480.0f, 0.1f),
        440.0f,
        juce::AudioParameterFloatAttributes()
            .withLabel ("Hz")
            .withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "beatFrequency", 1 },
        "Tone - Binaural Beat Frequency",
        beatRange,
        defaultBeatFrequency,
        juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "beatPreset", 1 },
        "Beat Preset",
        juce::StringArray {
            "Custom",
            "Delta - 2 Hz",
            "Theta - 6 Hz",
            "Schumann - 7.83 Hz",
            "Alpha - 10 Hz",
            "Beta - 20 Hz",
            "Gamma - 40 Hz"
        },
        4,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "waveform", 1 },
        "Tone - Waveform",
        juce::StringArray { "Sine", "Triangle", "Square", "Sawtooth" },
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "anchorEar", 1 },
        "Tone - Anchor Ear",
        juce::StringArray { "Left", "Right" },
        0));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "toneVolume", 1 },
        "Mix - Tone Volume",
        percentageRange,
        defaultToneVolumePercent,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "leftVolume", 1 },
        "Mix - Left Channel Volume",
        percentageRange,
        defaultChannelVolumePercent,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "rightVolume", 1 },
        "Mix - Right Channel Volume",
        percentageRange,
        defaultChannelVolumePercent,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "noiseType", 1 },
        "Ambient Noise",
        juce::StringArray { "None", "White", "Pink", "Brown" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "noiseVolume", 1 },
        "Mix - Ambient Noise Volume",
        percentageRange,
        defaultNoiseVolumePercent,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "outputEnabled", 1 },
        "Output - Enabled",
        true));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "masterVolume", 1 },
        "Output - Master Volume",
        percentageRange,
        defaultMasterVolumePercent,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "fadeInSeconds", 1 },
        "Output - Fade In",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.01f, 0.5f),
        defaultFadeInSeconds,
        juce::AudioParameterFloatAttributes().withLabel ("s")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "fadeOutSeconds", 1 },
        "Output - Fade Out",
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.01f, 0.5f),
        defaultFadeOutSeconds,
        juce::AudioParameterFloatAttributes().withLabel ("s")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "sessionDurationMinutes", 1 },
        "Session Duration",
        juce::NormalisableRange<float> (1.0f, 60.0f, 1.0f),
        defaultSessionDurationMinutes,
        juce::AudioParameterFloatAttributes()
            .withLabel ("min")
            .withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "journeyMode", 1 },
        "Playback Mode",
        juce::StringArray { "Single Tone", "Journey" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "playbackSource", 1 },
        "Playback Source",
        juce::StringArray { "Internal Controls", "Follow Logic Transport" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "journeyStageCount", 1 },
        "Journey Stage Count",
        juce::StringArray { "2", "3", "4", "5", "6" },
        defaultJourneyStages - minimumJourneyStages,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "transitionCurve", 1 },
        "Journey Transition Curve",
        juce::StringArray { "Linear", "Smooth", "Extra Gentle" },
        0,
        juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    const auto journeyBeatRange =
        juce::NormalisableRange<float> (0.5f, 100.0f, 0.01f, 0.45f);
    const auto holdRange =
        juce::NormalisableRange<float> (0.1f, 60.0f, 0.1f, 0.45f);
    const auto transitionRange =
        juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f, 0.5f);

    const auto journeyPresetChoices = juce::StringArray {
        "Custom",
        "Delta - 2 Hz",
        "Theta - 6 Hz",
        "Schumann - 7.83 Hz",
        "Alpha - 10 Hz",
        "Beta - 20 Hz",
        "Gamma - 40 Hz"
    };

    for (int stage = 0; stage < maximumJourneyStages; ++stage)
    {
        const auto number = juce::String (stage + 1);
        const auto prefix = juce::String ("stage") + number;

        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { prefix + "BeatPreset", 1 },
            "Stage " + number + " Beat Type",
            journeyPresetChoices,
            defaultStagePresetIndices[stage],
            juce::AudioParameterChoiceAttributes().withAutomatable (false)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "Beat", 1 },
            "Stage " + number + " Beat",
            journeyBeatRange,
            defaultStageBeats[stage],
            juce::AudioParameterFloatAttributes()
                .withLabel ("Hz")
                .withAutomatable (false)));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { prefix + "HoldMinutes", 1 },
            "Stage " + number + " Hold",
            holdRange,
            defaultStageHoldMinutes[stage],
            juce::AudioParameterFloatAttributes()
                .withLabel ("min")
                .withAutomatable (false)));

        if (stage < maximumJourneyStages - 1)
        {
            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { prefix + "TransitionMinutes", 1 },
                "Stage " + number + " Transition",
                transitionRange,
                defaultStageTransitionMinutes[stage],
                juce::AudioParameterFloatAttributes()
                    .withLabel ("min")
                    .withAutomatable (false)));
        }
    }

    return layout;
}

//==============================================================================
const juce::String BinauralJourneyAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool BinauralJourneyAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool BinauralJourneyAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool BinauralJourneyAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double BinauralJourneyAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int BinauralJourneyAudioProcessor::getNumPrograms()
{
    return 1;
}

int BinauralJourneyAudioProcessor::getCurrentProgram()
{
    return 0;
}

void BinauralJourneyAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String BinauralJourneyAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void BinauralJourneyAudioProcessor::changeProgramName (int index,
                                                        const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}


//==============================================================================
void BinauralJourneyAudioProcessor::requestSessionCommand (
    SessionCommand command) noexcept
{
    pendingSessionCommand.store (static_cast<int> (command),
                                 std::memory_order_release);
}

void BinauralJourneyAudioProcessor::startSession() noexcept
{
    requestSessionCommand (SessionCommand::play);
}

void BinauralJourneyAudioProcessor::pauseSession() noexcept
{
    requestSessionCommand (SessionCommand::pause);
}

void BinauralJourneyAudioProcessor::restartSession() noexcept
{
    requestSessionCommand (SessionCommand::restart);
}

void BinauralJourneyAudioProcessor::stopSession() noexcept
{
    requestSessionCommand (SessionCommand::stop);
}

bool BinauralJourneyAudioProcessor::isSessionPlaying() const noexcept
{
    return reportedSessionPlaying.load (std::memory_order_acquire);
}

bool BinauralJourneyAudioProcessor::isSessionFinished() const noexcept
{
    return reportedSessionFinished.load (std::memory_order_acquire);
}

double BinauralJourneyAudioProcessor::getSessionElapsedSeconds() const noexcept
{
    const double sampleRate = reportedSampleRate.load (std::memory_order_acquire);
    const auto elapsedSamples = reportedSessionElapsedSamples.load (
        std::memory_order_acquire);

    return sampleRate > 0.0
        ? static_cast<double> (elapsedSamples) / sampleRate
        : 0.0;
}

double BinauralJourneyAudioProcessor::getSessionDurationSeconds() const noexcept
{
    if (isJourneyModeActive())
    {
        const auto readValue = [this] (const juce::String& parameterID,
                                       double fallback) noexcept
        {
            const auto* parameter = parameterState.getRawParameterValue (parameterID);
            return parameter != nullptr
                ? static_cast<double> (parameter->load())
                : fallback;
        };

        const int stageCount = juce::jlimit (
            minimumJourneyStages,
            maximumJourneyStages,
            minimumJourneyStages + juce::roundToInt (
                readValue ("journeyStageCount",
                           defaultJourneyStages - minimumJourneyStages)));

        double totalMinutes = 0.0;

        for (int stage = 0; stage < stageCount; ++stage)
        {
            const auto prefix = juce::String ("stage") + juce::String (stage + 1);
            totalMinutes += readValue (prefix + "HoldMinutes",
                                       defaultStageHoldMinutes[stage]);

            if (stage < stageCount - 1)
            {
                totalMinutes += readValue (
                    prefix + "TransitionMinutes",
                    defaultStageTransitionMinutes[stage]);
            }
        }

        return totalMinutes * 60.0;
    }

    const auto* durationParameter =
        parameterState.getRawParameterValue ("sessionDurationMinutes");

    return durationParameter != nullptr
        ? static_cast<double> (durationParameter->load()) * 60.0
        : static_cast<double> (defaultSessionDurationMinutes) * 60.0;
}

bool BinauralJourneyAudioProcessor::isJourneyModeActive() const noexcept
{
    const auto* modeParameter =
        parameterState.getRawParameterValue ("journeyMode");

    return modeParameter != nullptr && modeParameter->load() >= 0.5f;
}

bool BinauralJourneyAudioProcessor::isFollowingHostTransport() const noexcept
{
    const auto* sourceParameter =
        parameterState.getRawParameterValue ("playbackSource");

    return sourceParameter != nullptr && sourceParameter->load() >= 0.5f;
}

bool BinauralJourneyAudioProcessor::isHostTransportAvailable() const noexcept
{
    return reportedHostTransportAvailable.load (std::memory_order_acquire);
}

bool BinauralJourneyAudioProcessor::isHostTransportPlaying() const noexcept
{
    return reportedHostTransportPlaying.load (std::memory_order_acquire);
}

int BinauralJourneyAudioProcessor::getJourneySegment() const noexcept
{
    return reportedJourneySegment.load (std::memory_order_acquire);
}

double BinauralJourneyAudioProcessor::getCurrentBeatFrequency() const noexcept
{
    return reportedCurrentBeatFrequency.load (std::memory_order_acquire);
}

float BinauralJourneyAudioProcessor::getLeftOutputPeak() const noexcept
{
    return reportedLeftOutputPeak.load (std::memory_order_acquire);
}

float BinauralJourneyAudioProcessor::getRightOutputPeak() const noexcept
{
    return reportedRightOutputPeak.load (std::memory_order_acquire);
}

std::uint64_t BinauralJourneyAudioProcessor::getPeakGuardEventCount() const noexcept
{
    return reportedPeakGuardEventCount.load (std::memory_order_acquire);
}

//==============================================================================
void BinauralJourneyAudioProcessor::startOutputFade (float targetGain,
                                                       double durationSeconds) noexcept
{
    outputFadeTarget = juce::jlimit (0.0f, 1.0f, targetGain);

    if (currentSampleRate <= 0.0 || durationSeconds <= 0.0)
    {
        outputFadeGain = outputFadeTarget;
        outputFadeStep = 0.0f;
        outputFadeSamplesRemaining = 0;
        return;
    }

    outputFadeSamplesRemaining = juce::jmax (
        1, juce::roundToInt (currentSampleRate * durationSeconds));
    outputFadeStep = (outputFadeTarget - outputFadeGain)
                   / static_cast<float> (outputFadeSamplesRemaining);
}

float BinauralJourneyAudioProcessor::getNextOutputFadeGain() noexcept
{
    if (outputFadeSamplesRemaining > 0)
    {
        outputFadeGain += outputFadeStep;
        --outputFadeSamplesRemaining;

        if (outputFadeSamplesRemaining == 0)
            outputFadeGain = outputFadeTarget;
    }

    return outputFadeGain;
}

//==============================================================================
void BinauralJourneyAudioProcessor::prepareToPlay (double sampleRate,
                                                   int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    currentSampleRate = sampleRate;
    reportedSampleRate.store (sampleRate, std::memory_order_release);
    leftPhase = 0.0;
    rightPhase = 0.0;

    const auto anchorMode = juce::roundToInt (
        parameterState.getRawParameterValue ("anchorMode")->load());
    const auto customAnchor = static_cast<double> (
        parameterState.getRawParameterValue ("anchorFrequency")->load());
    const auto noteIndex = juce::roundToInt (
        parameterState.getRawParameterValue ("anchorNote")->load());
    const auto octave = 2 + juce::roundToInt (
        parameterState.getRawParameterValue ("anchorOctave")->load());
    const auto tuningReference = static_cast<double> (
        parameterState.getRawParameterValue ("tuningReference")->load());
    const auto anchor = anchorMode == 1
        ? calculateMusicalNoteFrequency (noteIndex, octave, tuningReference)
        : customAnchor;
    const bool journeyMode =
        parameterState.getRawParameterValue ("journeyMode")->load() >= 0.5f;
    const auto manualBeat = static_cast<double> (
        parameterState.getRawParameterValue ("beatFrequency")->load());
    const auto stage1Beat = static_cast<double> (
        parameterState.getRawParameterValue ("stage1Beat")->load());
    const auto beat = journeyMode ? stage1Beat : manualBeat;

    const auto toneGain = percentToGain (
        parameterState.getRawParameterValue ("toneVolume")->load());
    const auto leftGain = percentToGain (
        parameterState.getRawParameterValue ("leftVolume")->load());
    const auto rightGain = percentToGain (
        parameterState.getRawParameterValue ("rightVolume")->load());
    const auto isEnabled =
        parameterState.getRawParameterValue ("toneEnabled")->load() >= 0.5f;
    const auto noiseGain = percentToGain (
        parameterState.getRawParameterValue ("noiseVolume")->load());
    const auto masterGain = percentToGain (
        parameterState.getRawParameterValue ("masterVolume")->load());
    const auto outputEnabled =
        parameterState.getRawParameterValue ("outputEnabled")->load() >= 0.5f;

    activeWaveform = juce::roundToInt (
        parameterState.getRawParameterValue ("waveform")->load());
    previousWaveform = activeWaveform;
    waveformCrossfadeSamplesRemaining = 0;
    waveformCrossfadeTotalSamples = juce::jmax (
        1, juce::roundToInt (currentSampleRate * 0.03));

    activeNoiseType = juce::roundToInt (
        parameterState.getRawParameterValue ("noiseType")->load());
    previousNoiseType = activeNoiseType;
    noiseCrossfadeSamplesRemaining = 0;
    noiseCrossfadeTotalSamples = juce::jmax (
        1, juce::roundToInt (currentSampleRate * 0.05));

    leftNoiseState.reset (0x13579bdfu);
    rightNoiseState.reset (0x2468ace1u);

    // A short ramp prevents clicks or zipper noise when a control is moved.
    constexpr double frequencySmoothingSeconds = 0.05;
    constexpr double gainSmoothingSeconds = 0.05;

    smoothedAnchorFrequency.reset (currentSampleRate, frequencySmoothingSeconds);
    smoothedBeatFrequency.reset (currentSampleRate, frequencySmoothingSeconds);
    smoothedAnchorFrequency.setCurrentAndTargetValue (anchor);
    smoothedBeatFrequency.setCurrentAndTargetValue (beat);

    smoothedToneGain.reset (currentSampleRate, gainSmoothingSeconds);
    smoothedLeftGain.reset (currentSampleRate, gainSmoothingSeconds);
    smoothedRightGain.reset (currentSampleRate, gainSmoothingSeconds);
    smoothedEnabledGain.reset (currentSampleRate, gainSmoothingSeconds);
    smoothedNoiseGain.reset (currentSampleRate, gainSmoothingSeconds);
    smoothedMasterGain.reset (currentSampleRate, gainSmoothingSeconds);

    smoothedToneGain.setCurrentAndTargetValue (toneGain);
    smoothedLeftGain.setCurrentAndTargetValue (leftGain);
    smoothedRightGain.setCurrentAndTargetValue (rightGain);
    smoothedEnabledGain.setCurrentAndTargetValue (isEnabled ? 1.0f : 0.0f);
    smoothedNoiseGain.setCurrentAndTargetValue (noiseGain);
    smoothedMasterGain.setCurrentAndTargetValue (masterGain);

    // Always begin from silence so opening the plug-in cannot create a click.
    outputFadeGain = 0.0f;
    outputFadeTarget = 0.0f;
    outputFadeStep = 0.0f;
    outputFadeSamplesRemaining = 0;
    sessionPlaying = true;
    sessionFinished = false;
    sessionEnding = false;
    sessionElapsedSamples = 0;
    pendingSessionCommand.store (static_cast<int> (SessionCommand::none),
                                 std::memory_order_release);
    reportedSessionPlaying.store (true, std::memory_order_release);
    reportedSessionFinished.store (false, std::memory_order_release);
    reportedSessionElapsedSamples.store (0, std::memory_order_release);
    reportedJourneySegment.store (journeyMode ? 1 : 0,
                                  std::memory_order_release);
    reportedCurrentBeatFrequency.store (beat, std::memory_order_release);
    reportedHostTransportAvailable.store (false, std::memory_order_release);
    reportedHostTransportPlaying.store (false, std::memory_order_release);
    reportedLeftOutputPeak.store (0.0f, std::memory_order_release);
    reportedRightOutputPeak.store (0.0f, std::memory_order_release);
    reportedPeakGuardEventCount.store (0, std::memory_order_release);
    previousFollowHostTransport = false;
    previousHostTransportPlaying = false;
    previousHostPositionAvailable = false;
    previousHostPositionSamples = -1;
    previousHostBlockSize = 0;

    previousOutputEnabled = outputEnabled;

    // Keep the first few audio callbacks completely silent. This allows the
    // standalone audio device and Logic's Audio Unit host to settle before the
    // oscillators become audible. The user-selected fade-in begins afterward.
    startupSilenceSamplesRemaining = juce::jmax<std::int64_t> (
        1,
        static_cast<std::int64_t> (
            std::llround (currentSampleRate * startupSilenceSeconds)));
}

void BinauralJourneyAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool BinauralJourneyAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This synth must have a stereo output so each ear receives its own tone.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void BinauralJourneyAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    juce::ignoreUnused (midiMessages);

    buffer.clear();

    if (buffer.getNumChannels() < 2 || currentSampleRate <= 0.0)
        return;

    const auto anchorMode = juce::roundToInt (
        parameterState.getRawParameterValue ("anchorMode")->load());
    const auto customAnchor = static_cast<double> (
        parameterState.getRawParameterValue ("anchorFrequency")->load());
    const auto noteIndex = juce::roundToInt (
        parameterState.getRawParameterValue ("anchorNote")->load());
    const auto octave = 2 + juce::roundToInt (
        parameterState.getRawParameterValue ("anchorOctave")->load());
    const auto tuningReference = static_cast<double> (
        parameterState.getRawParameterValue ("tuningReference")->load());
    const auto targetAnchor = anchorMode == 1
        ? calculateMusicalNoteFrequency (noteIndex, octave, tuningReference)
        : customAnchor;
    const bool journeyMode =
        parameterState.getRawParameterValue ("journeyMode")->load() >= 0.5f;
    const bool followHostTransport =
        parameterState.getRawParameterValue ("playbackSource")->load() >= 0.5f;
    const int transitionCurve = juce::roundToInt (
        parameterState.getRawParameterValue ("transitionCurve")->load());
    const auto manualBeat = static_cast<double> (
        parameterState.getRawParameterValue ("beatFrequency")->load());

    const int journeyStageCount = juce::jlimit (
        minimumJourneyStages,
        maximumJourneyStages,
        minimumJourneyStages + juce::roundToInt (
            parameterState.getRawParameterValue ("journeyStageCount")->load()));

    const std::array<double, maximumJourneyStages> stageBeats {{
        static_cast<double> (parameterState.getRawParameterValue ("stage1Beat")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage2Beat")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage3Beat")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage4Beat")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage5Beat")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage6Beat")->load())
    }};

    const std::array<double, maximumJourneyStages> stageHoldMinutes {{
        static_cast<double> (parameterState.getRawParameterValue ("stage1HoldMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage2HoldMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage3HoldMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage4HoldMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage5HoldMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage6HoldMinutes")->load())
    }};

    const std::array<double, maximumJourneyStages - 1> stageTransitionMinutes {{
        static_cast<double> (parameterState.getRawParameterValue ("stage1TransitionMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage2TransitionMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage3TransitionMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage4TransitionMinutes")->load()),
        static_cast<double> (parameterState.getRawParameterValue ("stage5TransitionMinutes")->load())
    }};

    const auto shapeTransition = [transitionCurve] (double progress) noexcept
    {
        const double p = juce::jlimit (0.0, 1.0, progress);

        switch (transitionCurve)
        {
            case 1: // Smoothstep: softer start and finish.
                return p * p * (3.0 - 2.0 * p);

            case 2: // Smootherstep: extra-gentle start and finish.
                return p * p * p * (p * (p * 6.0 - 15.0) + 10.0);

            case 0: // Linear
            default:
                return p;
        }
    };

    const auto calculateJourneyBeat =
        [&] (double elapsedSeconds, int& segment) noexcept
    {
        double cursor = 0.0;

        for (int stage = 0; stage < journeyStageCount; ++stage)
        {
            const double holdSeconds = stageHoldMinutes[stage] * 60.0;

            if (elapsedSeconds < cursor + holdSeconds)
            {
                segment = stage * 2 + 1;
                return stageBeats[stage];
            }

            cursor += holdSeconds;

            if (stage < journeyStageCount - 1)
            {
                const double transitionSeconds =
                    stageTransitionMinutes[stage] * 60.0;

                if (transitionSeconds > 0.0
                    && elapsedSeconds < cursor + transitionSeconds)
                {
                    segment = stage * 2 + 2;
                    const double progress = shapeTransition (
                        (elapsedSeconds - cursor) / transitionSeconds);
                    return stageBeats[stage]
                         + progress * (stageBeats[stage + 1] - stageBeats[stage]);
                }

                cursor += transitionSeconds;
            }
        }

        segment = journeyStageCount * 2 - 1;
        return stageBeats[journeyStageCount - 1];
    };

    int blockJourneySegment = 0;
    double targetBeat = manualBeat;

    const auto targetToneGain = percentToGain (
        parameterState.getRawParameterValue ("toneVolume")->load());
    const auto targetLeftGain = percentToGain (
        parameterState.getRawParameterValue ("leftVolume")->load());
    const auto targetRightGain = percentToGain (
        parameterState.getRawParameterValue ("rightVolume")->load());
    const auto isEnabled =
        parameterState.getRawParameterValue ("toneEnabled")->load() >= 0.5f;
    const auto targetNoiseGain = percentToGain (
        parameterState.getRawParameterValue ("noiseVolume")->load());
    const auto targetMasterGain = percentToGain (
        parameterState.getRawParameterValue ("masterVolume")->load());
    const auto outputEnabled =
        parameterState.getRawParameterValue ("outputEnabled")->load() >= 0.5f;
    const auto fadeInSeconds = static_cast<double> (
        parameterState.getRawParameterValue ("fadeInSeconds")->load());
    const auto fadeOutSeconds = static_cast<double> (
        parameterState.getRawParameterValue ("fadeOutSeconds")->load());
    const auto targetNoiseType = juce::roundToInt (
        parameterState.getRawParameterValue ("noiseType")->load());
    const auto anchorEar = juce::roundToInt (
        parameterState.getRawParameterValue ("anchorEar")->load());
    const auto targetWaveform = juce::roundToInt (
        parameterState.getRawParameterValue ("waveform")->load());

    double journeyDurationMinutes = 0.0;

    for (int stage = 0; stage < journeyStageCount; ++stage)
    {
        journeyDurationMinutes += stageHoldMinutes[stage];

        if (stage < journeyStageCount - 1)
            journeyDurationMinutes += stageTransitionMinutes[stage];
    }

    const double sessionDurationMinutes = journeyMode
        ? journeyDurationMinutes
        : static_cast<double> (
            parameterState.getRawParameterValue (
                "sessionDurationMinutes")->load());

    const auto sessionDurationSamples = juce::jmax<std::int64_t> (
        1,
        static_cast<std::int64_t> (
            std::llround (currentSampleRate * sessionDurationMinutes * 60.0)));
    const auto fadeOutSamples = juce::jmax<std::int64_t> (
        0,
        static_cast<std::int64_t> (
            std::llround (currentSampleRate * fadeOutSeconds)));

    bool hostTransportAvailable = false;
    bool hostTransportPlaying = false;
    bool hostPositionAvailable = false;
    std::int64_t hostPositionSamples = sessionElapsedSamples;

    if (followHostTransport)
    {
        if (auto* playHead = getPlayHead())
        {
            if (const auto position = playHead->getPosition())
            {
                hostTransportAvailable = true;
                hostTransportPlaying = position->getIsPlaying();

                if (const auto samplePosition = position->getTimeInSamples())
                {
                    hostPositionSamples = juce::jmax<std::int64_t> (
                        0, *samplePosition);
                    hostPositionAvailable = true;
                }
                else if (const auto secondsPosition = position->getTimeInSeconds())
                {
                    hostPositionSamples = juce::jmax<std::int64_t> (
                        0, static_cast<std::int64_t> (std::llround (
                            *secondsPosition * currentSampleRate)));
                    hostPositionAvailable = true;
                }
            }
        }
    }

    reportedHostTransportAvailable.store (hostTransportAvailable,
                                          std::memory_order_release);
    reportedHostTransportPlaying.store (hostTransportPlaying,
                                        std::memory_order_release);

    const auto command = static_cast<SessionCommand> (
        pendingSessionCommand.exchange (
            static_cast<int> (SessionCommand::none),
            std::memory_order_acq_rel));

    if (followHostTransport)
    {
        const bool enteringHostMode = ! previousFollowHostTransport;
        const bool hostJustStarted = hostTransportPlaying
                                  && ! previousHostTransportPlaying;
        const bool hostJustStopped = ! hostTransportPlaying
                                  && previousHostTransportPlaying;

        const auto expectedHostPosition = previousHostPositionSamples >= 0
            ? previousHostPositionSamples + previousHostBlockSize
            : hostPositionSamples;
        const auto seekTolerance = juce::jmax<std::int64_t> (
            64, static_cast<std::int64_t> (buffer.getNumSamples()) * 2);
        const auto hostPositionDifference = hostPositionSamples
            >= expectedHostPosition
                ? hostPositionSamples - expectedHostPosition
                : expectedHostPosition - hostPositionSamples;
        const bool hostPositionJumped = hostPositionAvailable
            && (enteringHostMode
                || ! previousHostPositionAvailable
                || hostPositionDifference > seekTolerance);

        if (hostPositionAvailable)
        {
            sessionElapsedSamples = juce::jlimit<std::int64_t> (
                0, sessionDurationSamples, hostPositionSamples);
        }
        else if (hostTransportPlaying && previousHostTransportPlaying)
        {
            // A few hosts provide play/stop state without a timeline position.
            // Continue from the internal sample count so playback still behaves
            // sensibly, while Logic's sample position remains the preferred path.
        }

        sessionFinished = hostPositionAvailable
                       && hostPositionSamples >= sessionDurationSamples;
        sessionPlaying = hostTransportAvailable
                      && hostTransportPlaying
                      && ! sessionFinished;

        if (hostPositionJumped)
        {
            // Logic may jump because the user moved the playhead, started a
            // bounce, or began an offline render. Reset timeline-dependent DSP
            // state so the new position starts cleanly and repeatably.
            leftPhase = 0.0;
            rightPhase = 0.0;
            leftNoiseState.reset (0x13579bdfu);
            rightNoiseState.reset (0x2468ace1u);
            waveformCrossfadeSamplesRemaining = 0;
            noiseCrossfadeSamplesRemaining = 0;
            smoothedAnchorFrequency.setCurrentAndTargetValue (targetAnchor);

            if (journeyMode)
            {
                const double hostElapsedSeconds =
                    static_cast<double> (sessionElapsedSamples)
                    / currentSampleRate;
                targetBeat = calculateJourneyBeat (
                    hostElapsedSeconds, blockJourneySegment);
            }
            else
            {
                targetBeat = manualBeat;
            }

            smoothedBeatFrequency.setCurrentAndTargetValue (targetBeat);
            outputFadeGain = 0.0f;
            outputFadeTarget = 0.0f;
            outputFadeStep = 0.0f;
            outputFadeSamplesRemaining = 0;
        }

        if (hostJustStarted
            || (enteringHostMode && sessionPlaying)
            || (hostPositionJumped && sessionPlaying))
        {
            sessionEnding = false;
            const bool beginningOfTimeline = sessionElapsedSamples
                                           <= buffer.getNumSamples();
            const double hostStartRamp = beginningOfTimeline
                ? juce::jmax (minimumStartupRampSeconds, fadeInSeconds)
                : 0.03;
            startOutputFade (outputEnabled ? 1.0f : 0.0f,
                             outputEnabled ? hostStartRamp : 0.0);
        }
        else if (hostJustStopped
                 || (enteringHostMode && ! sessionPlaying))
        {
            sessionEnding = false;
            startOutputFade (0.0f, 0.08);
        }

        if (sessionFinished)
        {
            sessionPlaying = false;
            sessionEnding = false;
            startOutputFade (0.0f, juce::jmin (0.08, fadeOutSeconds));
        }

        previousFollowHostTransport = true;
        previousHostTransportPlaying = hostTransportPlaying;
        previousHostPositionAvailable = hostPositionAvailable;
        previousHostPositionSamples = hostPositionAvailable
            ? hostPositionSamples : -1;
        previousHostBlockSize = buffer.getNumSamples();
    }
    else
    {
        if (previousFollowHostTransport)
        {
            // Switching back to Internal Controls leaves the session paused at
            // the current position rather than unexpectedly starting playback.
            sessionPlaying = false;
            sessionFinished = false;
            sessionEnding = false;
            startOutputFade (0.0f, 0.08);
        }

        previousFollowHostTransport = false;
        previousHostTransportPlaying = false;
        previousHostPositionAvailable = false;
        previousHostPositionSamples = -1;
        previousHostBlockSize = 0;

        switch (command)
        {
            case SessionCommand::play:
                if (sessionFinished || sessionElapsedSamples >= sessionDurationSamples)
                    sessionElapsedSamples = 0;

                sessionPlaying = true;
                sessionFinished = false;
                sessionEnding = false;
                startOutputFade (outputEnabled ? 1.0f : 0.0f,
                                 outputEnabled ? fadeInSeconds : 0.0);
                break;

            case SessionCommand::pause:
                sessionPlaying = false;
                sessionEnding = false;
                startOutputFade (0.0f, 0.10);
                break;

            case SessionCommand::restart:
                sessionElapsedSamples = 0;
                sessionPlaying = true;
                sessionFinished = false;
                sessionEnding = false;
                outputFadeGain = 0.0f;
                startOutputFade (outputEnabled ? 1.0f : 0.0f,
                                 outputEnabled ? fadeInSeconds : 0.0);
                break;

            case SessionCommand::stop:
                sessionElapsedSamples = 0;
                sessionPlaying = false;
                sessionFinished = false;
                sessionEnding = false;
                startOutputFade (0.0f, 0.10);
                break;

            case SessionCommand::none:
            default:
                break;
        }

        if (sessionPlaying && sessionElapsedSamples >= sessionDurationSamples)
        {
            sessionElapsedSamples = sessionDurationSamples;
            sessionPlaying = false;
            sessionFinished = true;
            sessionEnding = false;
            outputFadeGain = 0.0f;
            outputFadeTarget = 0.0f;
            outputFadeStep = 0.0f;
            outputFadeSamplesRemaining = 0;
        }
    }

    if (journeyMode)
    {
        const double elapsedSeconds =
            static_cast<double> (sessionElapsedSamples) / currentSampleRate;
        targetBeat = calculateJourneyBeat (
            elapsedSeconds, blockJourneySegment);
    }

    if (outputEnabled != previousOutputEnabled)
    {
        const bool shouldBeAudible = outputEnabled
                                   && sessionPlaying
                                   && ! sessionEnding;
        startOutputFade (shouldBeAudible ? 1.0f : 0.0f,
                         shouldBeAudible ? fadeInSeconds : fadeOutSeconds);
        previousOutputEnabled = outputEnabled;
    }

    if (targetWaveform != activeWaveform)
    {
        previousWaveform = activeWaveform;
        activeWaveform = targetWaveform;
        waveformCrossfadeSamplesRemaining = waveformCrossfadeTotalSamples;
    }

    if (targetNoiseType != activeNoiseType)
    {
        previousNoiseType = activeNoiseType;
        activeNoiseType = targetNoiseType;
        noiseCrossfadeSamplesRemaining = noiseCrossfadeTotalSamples;
    }

    smoothedAnchorFrequency.setTargetValue (targetAnchor);
    smoothedBeatFrequency.setTargetValue (targetBeat);
    smoothedToneGain.setTargetValue (targetToneGain);
    smoothedLeftGain.setTargetValue (targetLeftGain);
    smoothedRightGain.setTargetValue (targetRightGain);
    smoothedEnabledGain.setTargetValue (isEnabled ? 1.0f : 0.0f);
    smoothedNoiseGain.setTargetValue (targetNoiseGain);
    smoothedMasterGain.setTargetValue (targetMasterGain);

    auto* leftChannel = buffer.getWritePointer (0);
    auto* rightChannel = buffer.getWritePointer (1);

    constexpr double twoPi = juce::MathConstants<double>::twoPi;
    double lastRenderedBeat = targetBeat;
    float blockLeftPeak = 0.0f;
    float blockRightPeak = 0.0f;
    bool peakGuardTriggered = false;

    if (sessionPlaying && fadeOutSamples > 0)
    {
        const auto fadeStartSample = juce::jmax<std::int64_t> (
            0, sessionDurationSamples - fadeOutSamples);

        // If the duration is lengthened while the ending fade is active,
        // return smoothly to normal playback instead of staying silent.
        if (sessionEnding && sessionElapsedSamples < fadeStartSample)
        {
            sessionEnding = false;
            startOutputFade (outputEnabled ? 1.0f : 0.0f, 0.10);
        }
        else if (! sessionEnding && sessionElapsedSamples >= fadeStartSample)
        {
            sessionEnding = true;
            const auto samplesRemaining = juce::jmax<std::int64_t> (
                0, sessionDurationSamples - sessionElapsedSamples);
            startOutputFade (0.0f,
                             static_cast<double> (samplesRemaining)
                                 / currentSampleRate);
        }
    }

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        if (startupSilenceSamplesRemaining > 0)
        {
            leftChannel[sample] = 0.0f;
            rightChannel[sample] = 0.0f;
            --startupSilenceSamplesRemaining;

            if (startupSilenceSamplesRemaining == 0)
            {
                leftPhase = 0.0;
                rightPhase = 0.0;
                leftNoiseState.reset (0x13579bdfu);
                rightNoiseState.reset (0x2468ace1u);

                outputFadeGain = 0.0f;
                outputFadeTarget = 0.0f;
                outputFadeStep = 0.0f;
                outputFadeSamplesRemaining = 0;

                const bool shouldStartAudible = outputEnabled
                                               && sessionPlaying
                                               && ! sessionEnding;
                const double safeStartupRamp = shouldStartAudible
                    ? juce::jmax (minimumStartupRampSeconds, fadeInSeconds)
                    : 0.0;
                startOutputFade (shouldStartAudible ? 1.0f : 0.0f,
                                 safeStartupRamp);
            }

            continue;
        }

        const double anchorFrequency = smoothedAnchorFrequency.getNextValue();
        const double beatFrequency = smoothedBeatFrequency.getNextValue();
        lastRenderedBeat = beatFrequency;

        const bool rightIsAnchor = anchorEar == 1;
        const double leftFrequency = rightIsAnchor
                                       ? anchorFrequency + beatFrequency
                                       : anchorFrequency;
        const double rightFrequency = rightIsAnchor
                                        ? anchorFrequency
                                        : anchorFrequency + beatFrequency;

        const float outputGain = getNextOutputFadeGain()
                               * smoothedMasterGain.getNextValue();
        const float toneCommonGain = outputGain
                                   * smoothedEnabledGain.getNextValue()
                                   * smoothedToneGain.getNextValue();
        const float leftGain = toneCommonGain * smoothedLeftGain.getNextValue();
        const float rightGain = toneCommonGain * smoothedRightGain.getNextValue();

        const float newLeftWave = renderWaveform (activeWaveform, leftPhase);
        const float newRightWave = renderWaveform (activeWaveform, rightPhase);

        float leftWave = newLeftWave;
        float rightWave = newRightWave;

        if (waveformCrossfadeSamplesRemaining > 0)
        {
            const float progress = 1.0f
                - static_cast<float> (waveformCrossfadeSamplesRemaining)
                  / static_cast<float> (waveformCrossfadeTotalSamples);

            const float oldLeftWave = renderWaveform (previousWaveform, leftPhase);
            const float oldRightWave = renderWaveform (previousWaveform, rightPhase);

            leftWave = oldLeftWave + progress * (newLeftWave - oldLeftWave);
            rightWave = oldRightWave + progress * (newRightWave - oldRightWave);

            --waveformCrossfadeSamplesRemaining;

            if (waveformCrossfadeSamplesRemaining == 0)
                previousWaveform = activeWaveform;
        }

        const float leftWhite = leftNoiseState.nextWhite();
        const float rightWhite = rightNoiseState.nextWhite();
        const float leftPink = leftNoiseState.nextPink (leftWhite);
        const float rightPink = rightNoiseState.nextPink (rightWhite);
        const float leftBrown = leftNoiseState.nextBrown (leftWhite);
        const float rightBrown = rightNoiseState.nextBrown (rightWhite);

        const float newLeftNoise = selectNoiseSample (
            activeNoiseType, leftWhite, leftPink, leftBrown);
        const float newRightNoise = selectNoiseSample (
            activeNoiseType, rightWhite, rightPink, rightBrown);

        float leftNoise = newLeftNoise;
        float rightNoise = newRightNoise;

        if (noiseCrossfadeSamplesRemaining > 0)
        {
            const float progress = 1.0f
                - static_cast<float> (noiseCrossfadeSamplesRemaining)
                  / static_cast<float> (noiseCrossfadeTotalSamples);

            const float oldLeftNoise = selectNoiseSample (
                previousNoiseType, leftWhite, leftPink, leftBrown);
            const float oldRightNoise = selectNoiseSample (
                previousNoiseType, rightWhite, rightPink, rightBrown);

            leftNoise = oldLeftNoise + progress * (newLeftNoise - oldLeftNoise);
            rightNoise = oldRightNoise + progress * (newRightNoise - oldRightNoise);

            --noiseCrossfadeSamplesRemaining;

            if (noiseCrossfadeSamplesRemaining == 0)
                previousNoiseType = activeNoiseType;
        }

        const float noiseGain = outputGain * smoothedNoiseGain.getNextValue();

        const float leftMix = leftGain * leftWave + noiseGain * leftNoise;
        const float rightMix = rightGain * rightWave + noiseGain * rightNoise;

        blockLeftPeak = juce::jmax (blockLeftPeak, std::abs (leftMix));
        blockRightPeak = juce::jmax (blockRightPeak, std::abs (rightMix));

        // A transparent peak guard only acts on unexpected overloads. Normal
        // listening levels remain unchanged, while accidental spikes are kept
        // below full scale.
        if (std::abs (leftMix) > 0.98f || std::abs (rightMix) > 0.98f)
            peakGuardTriggered = true;

        leftChannel[sample] = juce::jlimit (-0.98f, 0.98f, leftMix);
        rightChannel[sample] = juce::jlimit (-0.98f, 0.98f, rightMix);

        leftPhase += twoPi * leftFrequency / currentSampleRate;
        rightPhase += twoPi * rightFrequency / currentSampleRate;

        // fmod also behaves correctly if a future parameter jump advances a
        // phase by more than one complete cycle.
        if (leftPhase >= twoPi)
            leftPhase = std::fmod (leftPhase, twoPi);

        if (rightPhase >= twoPi)
            rightPhase = std::fmod (rightPhase, twoPi);

        if (sessionPlaying)
        {
            ++sessionElapsedSamples;

            if (sessionElapsedSamples >= sessionDurationSamples)
            {
                sessionElapsedSamples = sessionDurationSamples;
                sessionPlaying = false;
                sessionFinished = true;
                sessionEnding = false;
                outputFadeGain = 0.0f;
                outputFadeTarget = 0.0f;
                outputFadeStep = 0.0f;
                outputFadeSamplesRemaining = 0;
            }
        }
    }

    const float previousLeftPeak =
        reportedLeftOutputPeak.load (std::memory_order_relaxed);
    const float previousRightPeak =
        reportedRightOutputPeak.load (std::memory_order_relaxed);

    // Fast attack and a gentle release make the UI meter readable without
    // adding any locks or allocations to the audio thread.
    reportedLeftOutputPeak.store (
        juce::jmax (blockLeftPeak, previousLeftPeak * 0.82f),
        std::memory_order_release);
    reportedRightOutputPeak.store (
        juce::jmax (blockRightPeak, previousRightPeak * 0.82f),
        std::memory_order_release);

    if (peakGuardTriggered)
        reportedPeakGuardEventCount.fetch_add (1, std::memory_order_release);

    reportedSessionPlaying.store (sessionPlaying, std::memory_order_release);
    reportedSessionFinished.store (sessionFinished, std::memory_order_release);
    reportedSessionElapsedSamples.store (sessionElapsedSamples,
                                         std::memory_order_release);
    reportedJourneySegment.store (journeyMode ? blockJourneySegment : 0,
                                  std::memory_order_release);
    reportedCurrentBeatFrequency.store (lastRenderedBeat,
                                        std::memory_order_release);
}

//==============================================================================
bool BinauralJourneyAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* BinauralJourneyAudioProcessor::createEditor()
{
    return new BinauralJourneyAudioProcessorEditor (*this);
}

//==============================================================================
void BinauralJourneyAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = parameterState.copyState();
    const auto xml = state.createXml();
    copyXmlToBinary (*xml, destData);
}

void BinauralJourneyAudioProcessor::setStateInformation (const void* data,
                                                         int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameterState.state.getType()))
        return;

    auto restoredState = juce::ValueTree::fromXml (*xml);
    const bool hasStageCount = stateContainsParameter (
        restoredState, "journeyStageCount");
    const bool hasStage3Transition = stateContainsParameter (
        restoredState, "stage3TransitionMinutes");
    const bool hasNoiseType = stateContainsParameter (
        restoredState, "noiseType");
    const bool hasPlaybackSource = stateContainsParameter (
        restoredState, "playbackSource");
    const int restoredDefaultsVersion = static_cast<int> (
        restoredState.getProperty (defaultsVersionProperty, 0));
    const bool needsDefaultsMigration =
        restoredDefaultsVersion < currentDefaultsVersion;

    parameterState.replaceState (restoredState);

    const auto setActualParameterValue = [this] (const juce::String& parameterID,
                                                  float value)
    {
        if (auto* parameter = parameterState.getParameter (parameterID))
        {
            parameter->setValueNotifyingHost (
                parameter->convertTo0to1 (value));
        }
    };

    if (! hasStageCount)
        setActualParameterValue ("journeyStageCount",
                                 static_cast<float> (defaultJourneyStages
                                                     - minimumJourneyStages));

    if (! hasPlaybackSource)
        setActualParameterValue ("playbackSource", 0.0f);

    // Older or incomplete states may not contain the noise choice. Keep the
    // safe default explicit so the control shows None instead of a blank item.
    if (! hasNoiseType)
        setActualParameterValue ("noiseType", 0.0f);

    if (const auto* noiseValue =
            parameterState.getRawParameterValue ("noiseType"))
    {
        const float value = noiseValue->load();

        if (! std::isfinite (value) || value < 0.0f || value > 3.0f)
            setActualParameterValue ("noiseType", 0.0f);
    }

    if (! hasStage3Transition)
    {
        setActualParameterValue ("stage3TransitionMinutes",
                                 defaultStageTransitionMinutes[2]);

        for (int stage = 3; stage < maximumJourneyStages; ++stage)
        {
            const auto prefix = juce::String ("stage") + juce::String (stage + 1);
            setActualParameterValue (prefix + "BeatPreset",
                                     static_cast<float> (defaultStagePresetIndices[stage]));
            setActualParameterValue (prefix + "Beat", defaultStageBeats[stage]);
            setActualParameterValue (prefix + "HoldMinutes",
                                     defaultStageHoldMinutes[stage]);

            if (stage < maximumJourneyStages - 1)
            {
                setActualParameterValue (prefix + "TransitionMinutes",
                                         defaultStageTransitionMinutes[stage]);
            }
        }
    }

    if (needsDefaultsMigration)
    {
        parameterState.state.setProperty (
            defaultsVersionProperty, currentDefaultsVersion, nullptr);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BinauralJourneyAudioProcessor();
}
