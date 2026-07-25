#pragma once

#include <JuceHeader.h>
#include "ParameterIDs.h"
#include "Dsp/PhaseVocoderPitchShifter.h"
#include "Dsp/ReverseBlockProcessor.h"
#include "Dsp/SharedDelayBuffer.h"

class ProgressivePitchDelayAudioProcessor final : public juce::AudioProcessor,
                                                   private juce::AudioProcessorValueTreeState::Listener
{
public:
    ProgressivePitchDelayAudioProcessor();
    ~ProgressivePitchDelayAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;

    float getOutputPeak() const noexcept { return outputPeak.load(); }
    double getEffectiveDelaySeconds() const noexcept { return effectiveDelaySeconds.load(); }
    double getCurrentBpm() const noexcept { return currentBpm.load(); }

    const juce::StringArray& getFactoryPresetNames() const;
    int getSelectedFactoryPreset() const noexcept { return currentFactoryPreset.load(); }
    void applyFactoryPreset (int index);

    juce::File getDefaultUserPresetDirectory() const;
    juce::File getLastUserPresetDirectory() const;
    void setLastUserPresetDirectory (const juce::File& directory) const;
    juce::String getCurrentUserPresetName() const;
    bool saveUserPreset (const juce::File& file, juce::String& errorMessage);
    bool loadUserPreset (const juce::File& file, juce::String& errorMessage);

private:
    static constexpr int maxRepeats = 12;
    static constexpr double maximumDelaySeconds = 5.0;

    struct RepeatVoice
    {
        std::array<PhaseVocoderPitchShifter, 2> shifters;
        bool shifterRunning = false;
        DelayTapState delayTap;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain;
    };

    double calculateDelaySeconds() noexcept;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void setParameterFromPreset (const char* parameterID, float plainValue);
    void setCurrentUserPresetName (const juce::String& name);
    void captureCurrentUserPresetParameterValues() noexcept;
    void clearCurrentUserPreset() noexcept;
    void addParameterListeners();
    void removeParameterListeners();

    std::array<RepeatVoice, maxRepeats> repeatVoices;
    SharedDelayBuffer sourceDelayBuffer;
    SharedDelayBuffer dryDelayBuffer;
    DelayTapState dryLatencyTap;
    ReverseBlockProcessor reverseProcessor;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> dryGainSmoother;

    double currentSampleRate = 44100.0;
    int preparedChannels = 2;
    int pitchLatencySamples = 0;
    int tapCrossfadeSamples = 256;
    float limiterGain = 1.0f;
    float limiterReleaseCoefficient = 0.999f;
    bool lastReversedMode = false;

    std::array<float, 2> reverseInputFrame {};
    std::array<float, 2> reverseOutputFrame {};
    std::array<const float*, 2> reverseInputPointers {};
    std::array<float*, 2> reverseOutputPointers {};

    std::atomic<float> outputPeak { 0.0f };
    std::atomic<double> effectiveDelaySeconds { 0.35 };
    std::atomic<double> currentBpm { 120.0 };
    std::atomic<int> currentFactoryPreset { 0 };
    std::atomic<bool> applyingFactoryPreset { false };
    std::atomic<bool> applyingUserPreset { false };
    std::atomic<bool> restoringState { false };
    std::atomic<bool> userPresetIsCurrent { false };
    std::atomic<bool> userPresetReferenceValid { false };
    static constexpr size_t trackedParameterCount = 12;
    std::array<std::atomic<float>, trackedParameterCount> userPresetParameterValues;

    mutable juce::CriticalSection presetNameLock;
    juce::String currentUserPresetName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgressivePitchDelayAudioProcessor)
};
