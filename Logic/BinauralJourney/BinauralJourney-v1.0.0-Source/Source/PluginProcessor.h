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

#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <atomic>

//==============================================================================
class BinauralJourneyAudioProcessor : public juce::AudioProcessor
{
public:
    //==============================================================================
    BinauralJourneyAudioProcessor();
    ~BinauralJourneyAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept
    {
        return parameterState;
    }

    static double calculateMusicalNoteFrequency (int noteIndex,
                                                 int octave,
                                                 double a4Frequency) noexcept;

    void startSession() noexcept;
    void pauseSession() noexcept;
    void restartSession() noexcept;
    void stopSession() noexcept;

    bool isSessionPlaying() const noexcept;
    bool isSessionFinished() const noexcept;
    double getSessionElapsedSeconds() const noexcept;
    double getSessionDurationSeconds() const noexcept;
    bool isJourneyModeActive() const noexcept;
    bool isFollowingHostTransport() const noexcept;
    bool isHostTransportAvailable() const noexcept;
    bool isHostTransportPlaying() const noexcept;
    int getJourneySegment() const noexcept;
    double getCurrentBeatFrequency() const noexcept;
    float getLeftOutputPeak() const noexcept;
    float getRightOutputPeak() const noexcept;
    std::uint64_t getPeakGuardEventCount() const noexcept;

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameterState;

    double currentSampleRate = 44100.0;
    double leftPhase = 0.0;
    double rightPhase = 0.0;

    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> smoothedAnchorFrequency;
    juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear> smoothedBeatFrequency;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedToneGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLeftGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRightGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedEnabledGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedNoiseGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMasterGain;

    struct NoiseState
    {
        std::uint32_t randomState = 0x12345678u;
        float pinkB0 = 0.0f;
        float pinkB1 = 0.0f;
        float pinkB2 = 0.0f;
        float pinkB3 = 0.0f;
        float pinkB4 = 0.0f;
        float pinkB5 = 0.0f;
        float pinkB6 = 0.0f;
        float brown = 0.0f;

        void reset (std::uint32_t seed) noexcept;
        float nextWhite() noexcept;
        float nextPink (float white) noexcept;
        float nextBrown (float white) noexcept;
    };

    NoiseState leftNoiseState;
    NoiseState rightNoiseState;

    int activeNoiseType = 0;
    int previousNoiseType = 0;
    int noiseCrossfadeSamplesRemaining = 0;
    int noiseCrossfadeTotalSamples = 1;

    // Master output envelope. It starts silently and uses separate fade-in
    // and fade-out times whenever the Output enabled control changes.
    float outputFadeGain = 0.0f;
    float outputFadeTarget = 0.0f;
    float outputFadeStep = 0.0f;
    int outputFadeSamplesRemaining = 0;
    bool previousOutputEnabled = true;

    // Gives the audio device a brief silent settling period when the plug-in
    // first opens. The oscillator and noise state are reset immediately before
    // the startup ramp begins, which further reduces launch-time crackle.
    std::int64_t startupSilenceSamplesRemaining = 0;

    enum class SessionCommand : int
    {
        none = 0,
        play,
        pause,
        restart,
        stop
    };

    std::atomic<int> pendingSessionCommand { static_cast<int> (SessionCommand::none) };
    std::atomic<bool> reportedSessionPlaying { true };
    std::atomic<bool> reportedSessionFinished { false };
    std::atomic<std::int64_t> reportedSessionElapsedSamples { 0 };
    std::atomic<double> reportedSampleRate { 44100.0 };
    std::atomic<int> reportedJourneySegment { 0 };
    std::atomic<double> reportedCurrentBeatFrequency { 10.0 };
    std::atomic<bool> reportedHostTransportAvailable { false };
    std::atomic<bool> reportedHostTransportPlaying { false };
    std::atomic<float> reportedLeftOutputPeak { 0.0f };
    std::atomic<float> reportedRightOutputPeak { 0.0f };
    std::atomic<std::uint64_t> reportedPeakGuardEventCount { 0 };

    bool sessionPlaying = true;
    bool sessionFinished = false;
    bool sessionEnding = false;
    std::int64_t sessionElapsedSamples = 0;
    bool previousFollowHostTransport = false;
    bool previousHostTransportPlaying = false;
    bool previousHostPositionAvailable = false;
    std::int64_t previousHostPositionSamples = -1;
    int previousHostBlockSize = 0;

    void requestSessionCommand (SessionCommand command) noexcept;
    void startOutputFade (float targetGain, double durationSeconds) noexcept;
    float getNextOutputFadeGain() noexcept;

    // Waveform changes are crossfaded instead of switched instantly. This
    // prevents a discontinuity when selecting another waveform during playback.
    int activeWaveform = 0;
    int previousWaveform = 0;
    int waveformCrossfadeSamplesRemaining = 0;
    int waveformCrossfadeTotalSamples = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinauralJourneyAudioProcessor)
};
