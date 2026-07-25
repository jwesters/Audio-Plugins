#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    constexpr int currentPitchDirectionMappingVersion = 2;
    const juce::Identifier pitchDirectionMappingVersionProperty { "pitchDirectionMappingVersion" };
    const juce::Identifier factoryPresetIndexProperty { "factoryPresetIndex" };
    const juce::Identifier userPresetNameProperty { "userPresetName" };
    const juce::Identifier userPresetFormatVersionProperty { "userPresetFormatVersion" };
    const juce::Identifier pluginVersionProperty { "pluginVersion" };
    constexpr auto presetSettingsTag = "PROGRESSIVE_PITCH_DELAY_SETTINGS";
    constexpr auto lastPresetDirectoryAttribute = "lastPresetDirectory";
    constexpr int currentUserPresetFormatVersion = 1;
    constexpr auto userPresetFileExtension = ".ppdpreset";

    juce::File getPresetSettingsFile()
    {
        auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("jwesters")
                             .getChildFile ("Progressive Pitch Delay");
        directory.createDirectory();
        return directory.getChildFile ("Settings.xml");
    }

    struct FactoryPreset
    {
        const char* name;
        int delayMode;
        int pitchDirection;
        float semitones;
        int repeats;
        float delaySeconds;
        bool tempoSync;
        int syncDivision;
        float dryDb;
        float firstRepeatDb;
        float decayDb;
        bool headroom;
        int quality;
    };

    // Choice values use their host-facing indices. Pitch direction is
    // Lower = 0 and Higher = 1, matching Logic's automation orientation.
    constexpr std::array<FactoryPreset, 11> factoryPresets
    {{
        { "Default",               1, 0,  1.0f,  5, 0.350f, false,  8,  0.0f,  -6.0f, 3.0f, true, 1 },
        { "Gentle Rising Echo",    0, 1,  2.0f,  4, 0.400f, false,  8,  0.0f,  -9.0f, 3.0f, true, 1 },
        { "Gentle Falling Echo",   0, 0,  2.0f,  4, 0.400f, false,  8,  0.0f,  -9.0f, 3.0f, true, 1 },
        { "Octave Staircase",      0, 1, 12.0f,  4, 0.350f, false,  8,  0.0f, -12.0f, 4.5f, true, 1 },
        { "Reverse Shimmer",       1, 1,  7.0f,  5, 0.500f, false,  8,  0.0f, -10.0f, 3.0f, true, 1 },
        { "Reverse Descent",       1, 0,  3.0f,  6, 0.450f, false,  8,  0.0f,  -8.0f, 3.0f, true, 1 },
        { "Rhythmic Fifths",       0, 1,  7.0f,  5, 0.300f, true,   8,  0.0f,  -9.0f, 3.0f, true, 1 },
        { "Short Metallic Steps",  0, 1,  1.0f,  8, 0.090f, false,  5,  0.0f, -12.0f, 2.5f, true, 1 },
        { "Long Ambient Rise",     0, 1,  1.0f, 10, 0.750f, false, 11,  0.0f, -11.0f, 1.7f, true, 1 },
        { "Guitar Cascade",        0, 1,  2.0f,  5, 0.320f, false,  8,  0.0f,  -7.0f, 2.8f, true, 1 },
        { "Vocal Dream Delay",     1, 1,  3.0f,  6, 0.500f, true,  11, -1.0f, -10.0f, 2.2f, true, 1 }
    }};

    constexpr std::array<const char*, 12> allParameterIDs
    {{
        ParameterIDs::delayMode,
        ParameterIDs::pitchDirection,
        ParameterIDs::semitones,
        ParameterIDs::repeatCount,
        ParameterIDs::delayTime,
        ParameterIDs::tempoSync,
        ParameterIDs::syncDivision,
        ParameterIDs::dryDb,
        ParameterIDs::firstRepeatDb,
        ParameterIDs::decayDb,
        ParameterIDs::headroom,
        ParameterIDs::quality
    }};

    int getTrackedParameterIndex (const juce::String& parameterID) noexcept
    {
        for (size_t index = 0; index < allParameterIDs.size(); ++index)
            if (parameterID == allParameterIDs[index])
                return static_cast<int> (index);

        return -1;
    }

    void migratePitchDirectionStateIfNeeded (juce::ValueTree& state)
    {
        const auto savedMappingVersion = static_cast<int> (
            state.getProperty (pitchDirectionMappingVersionProperty, 1));

        if (savedMappingVersion >= currentPitchDirectionMappingVersion)
            return;

        // Versions through 1.0.2 exposed Higher as 0 and Lower as 1.
        // Version 1.0.3 reverses the host-facing order so Logic displays
        // Lower at the bottom and Higher at the top. Invert old saved values
        // to preserve their audible direction when a project is reopened.
        for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
        {
            auto child = state.getChild (childIndex);

            if (child.getProperty ("id").toString() != ParameterIDs::pitchDirection)
                continue;

            const auto oldValue = juce::jlimit (
                0.0, 1.0, static_cast<double> (child.getProperty ("value", 0.0)));
            child.setProperty ("value", 1.0 - oldValue, nullptr);
            break;
        }

        state.setProperty (pitchDirectionMappingVersionProperty,
                           currentPitchDirectionMappingVersion, nullptr);
    }
}

ProgressivePitchDelayAudioProcessor::ProgressivePitchDelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "STATE", createParameterLayout())
{
    for (auto& value : userPresetParameterValues)
        value.store (0.0f, std::memory_order_relaxed);

    addParameterListeners();
}

ProgressivePitchDelayAudioProcessor::~ProgressivePitchDelayAudioProcessor()
{
    removeParameterListeners();
}

juce::AudioProcessorValueTreeState::ParameterLayout
ProgressivePitchDelayAudioProcessor::createParameterLayout()
{
    using APF = juce::AudioParameterFloat;
    using API = juce::AudioParameterInt;
    using APC = juce::AudioParameterChoice;
    using APB = juce::AudioParameterBool;
    using PID = juce::ParameterID;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<APC> (PID { ParameterIDs::delayMode, 1 },
                                             "Delay repeats", ParameterChoices::delayModes, 1));
    params.push_back (std::make_unique<APC> (PID { ParameterIDs::pitchDirection, 1 },
                                             "Pitch direction", ParameterChoices::pitchDirections, 0));
    params.push_back (std::make_unique<APF> (PID { ParameterIDs::semitones, 1 },
                                             "Pitch change per repeat",
                                             juce::NormalisableRange<float> (0.0f, 12.0f, 0.01f), 1.0f,
                                             juce::AudioParameterFloatAttributes().withLabel ("semitones")));
    params.push_back (std::make_unique<API> (PID { ParameterIDs::repeatCount, 1 },
                                             "Number of repeats", 1, maxRepeats, 5));
    params.push_back (std::make_unique<APF> (PID { ParameterIDs::delayTime, 1 },
                                             "Delay time",
                                             juce::NormalisableRange<float> (0.01f, 5.0f, 0.001f, 0.35f), 0.35f,
                                             juce::AudioParameterFloatAttributes().withLabel ("seconds")));
    params.push_back (std::make_unique<APB> (PID { ParameterIDs::tempoSync, 1 },
                                             "Tempo sync", false));
    params.push_back (std::make_unique<APC> (PID { ParameterIDs::syncDivision, 1 },
                                             "Sync division", ParameterChoices::syncDivisions, 8));
    params.push_back (std::make_unique<APF> (PID { ParameterIDs::dryDb, 1 },
                                             "Original audio level",
                                             juce::NormalisableRange<float> (-30.0f, 0.0f, 0.1f), 0.0f,
                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<APF> (PID { ParameterIDs::firstRepeatDb, 1 },
                                             "First repeat level",
                                             juce::NormalisableRange<float> (-30.0f, 0.0f, 0.1f), -6.0f,
                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<APF> (PID { ParameterIDs::decayDb, 1 },
                                             "Reduction per repeat",
                                             juce::NormalisableRange<float> (0.0f, 12.0f, 0.1f), 3.0f,
                                             juce::AudioParameterFloatAttributes().withLabel ("dB")));
    params.push_back (std::make_unique<APB> (PID { ParameterIDs::headroom, 1 },
                                             "Automatic clipping protection", true));
    params.push_back (std::make_unique<APC> (PID { ParameterIDs::quality, 1 },
                                             "Pitch quality", ParameterChoices::qualities, 1));

    return { params.begin(), params.end() };
}

int ProgressivePitchDelayAudioProcessor::getNumPrograms()
{
    return static_cast<int> (factoryPresets.size());
}

int ProgressivePitchDelayAudioProcessor::getCurrentProgram()
{
    const auto selected = currentFactoryPreset.load();
    return juce::jlimit (0, getNumPrograms() - 1, selected < 0 ? 0 : selected);
}

void ProgressivePitchDelayAudioProcessor::setCurrentProgram (int index)
{
    applyFactoryPreset (index);
}

const juce::String ProgressivePitchDelayAudioProcessor::getProgramName (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms()))
        return factoryPresets[static_cast<size_t> (index)].name;

    return {};
}

const juce::StringArray& ProgressivePitchDelayAudioProcessor::getFactoryPresetNames() const
{
    static const juce::StringArray names = []
    {
        juce::StringArray result;

        for (const auto& preset : factoryPresets)
            result.add (preset.name);

        return result;
    }();

    return names;
}

juce::File ProgressivePitchDelayAudioProcessor::getDefaultUserPresetDirectory() const
{
    auto directory = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                         .getChildFile ("jwesters")
                         .getChildFile ("Progressive Pitch Delay")
                         .getChildFile ("Presets");
    directory.createDirectory();
    return directory;
}

juce::File ProgressivePitchDelayAudioProcessor::getLastUserPresetDirectory() const
{
    const auto fallback = getDefaultUserPresetDirectory();
    const auto settingsFile = getPresetSettingsFile();

    if (settingsFile.existsAsFile())
    {
        if (const auto xml = juce::XmlDocument::parse (settingsFile))
        {
            if (xml->hasTagName (presetSettingsTag))
            {
                const juce::File savedDirectory (
                    xml->getStringAttribute (lastPresetDirectoryAttribute));

                if (savedDirectory.isDirectory())
                    return savedDirectory;
            }
        }
    }

    return fallback;
}

void ProgressivePitchDelayAudioProcessor::setLastUserPresetDirectory (
    const juce::File& requestedDirectory) const
{
    auto directory = requestedDirectory;

    if (directory == juce::File {})
        return;

    if (! directory.isDirectory())
        directory = directory.getParentDirectory();

    if (directory == juce::File {})
        return;

    const auto result = directory.createDirectory();
    if (result.failed())
        return;

    juce::XmlElement settings (presetSettingsTag);
    settings.setAttribute (lastPresetDirectoryAttribute, directory.getFullPathName());
    settings.writeTo (getPresetSettingsFile());
}

juce::String ProgressivePitchDelayAudioProcessor::getCurrentUserPresetName() const
{
    if (! userPresetIsCurrent.load())
        return {};

    const juce::ScopedLock lock (presetNameLock);
    return currentUserPresetName;
}

void ProgressivePitchDelayAudioProcessor::setCurrentUserPresetName (const juce::String& name)
{
    const auto trimmedName = name.trim();
    {
        const juce::ScopedLock lock (presetNameLock);
        currentUserPresetName = trimmedName;
    }

    const auto hasName = trimmedName.isNotEmpty();
    userPresetIsCurrent.store (hasName, std::memory_order_release);

    if (! hasName)
        userPresetReferenceValid.store (false, std::memory_order_release);
}

void ProgressivePitchDelayAudioProcessor::captureCurrentUserPresetParameterValues() noexcept
{
    static_assert (trackedParameterCount == allParameterIDs.size());

    for (size_t index = 0; index < allParameterIDs.size(); ++index)
    {
        const auto* rawValue = parameters.getRawParameterValue (allParameterIDs[index]);
        userPresetParameterValues[index].store (rawValue != nullptr ? rawValue->load() : 0.0f,
                                                std::memory_order_relaxed);
    }

    userPresetReferenceValid.store (true, std::memory_order_release);
}

void ProgressivePitchDelayAudioProcessor::clearCurrentUserPreset() noexcept
{
    userPresetIsCurrent.store (false, std::memory_order_release);
    userPresetReferenceValid.store (false, std::memory_order_release);
}

bool ProgressivePitchDelayAudioProcessor::saveUserPreset (const juce::File& requestedFile,
                                                            juce::String& errorMessage)
{
    errorMessage.clear();

    if (requestedFile == juce::File {})
    {
        errorMessage = "No preset file was selected.";
        return false;
    }

    auto file = requestedFile.hasFileExtension (userPresetFileExtension)
                  ? requestedFile
                  : requestedFile.withFileExtension (userPresetFileExtension);

    const auto directoryResult = file.getParentDirectory().createDirectory();
    if (directoryResult.failed())
    {
        errorMessage = "The preset folder could not be created: " + directoryResult.getErrorMessage();
        return false;
    }

    auto state = parameters.copyState();
    const auto presetName = file.getFileNameWithoutExtension().trim();
    state.setProperty (pitchDirectionMappingVersionProperty,
                       currentPitchDirectionMappingVersion, nullptr);
    state.setProperty (factoryPresetIndexProperty, -1, nullptr);
    state.setProperty (userPresetNameProperty, presetName, nullptr);
    state.setProperty (userPresetFormatVersionProperty,
                       currentUserPresetFormatVersion, nullptr);
    state.setProperty (pluginVersionProperty, JucePlugin_VersionString, nullptr);

    const auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
    {
        errorMessage = "The preset could not be written to:\n" + file.getFullPathName();
        return false;
    }

    currentFactoryPreset.store (-1);
    setCurrentUserPresetName (presetName);
    captureCurrentUserPresetParameterValues();
    setLastUserPresetDirectory (file.getParentDirectory());
    return true;
}

bool ProgressivePitchDelayAudioProcessor::loadUserPreset (const juce::File& file,
                                                            juce::String& errorMessage)
{
    errorMessage.clear();

    if (! file.existsAsFile())
    {
        errorMessage = "The selected preset file does not exist.";
        return false;
    }

    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
    {
        errorMessage = "This is not a valid Progressive Pitch Delay preset.";
        return false;
    }

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
    {
        errorMessage = "The preset data could not be read.";
        return false;
    }

    const auto presetFormatVersion = static_cast<int> (
        state.getProperty (userPresetFormatVersionProperty, 1));

    if (presetFormatVersion < 1)
    {
        errorMessage = "This preset uses an invalid preset format version.";
        return false;
    }

    if (presetFormatVersion > currentUserPresetFormatVersion)
    {
        errorMessage = "This preset was created by a newer version of Progressive Pitch Delay and cannot be loaded by this build.";
        return false;
    }

    migratePitchDirectionStateIfNeeded (state);

    auto presetName = state.getProperty (userPresetNameProperty).toString().trim();
    if (presetName.isEmpty())
        presetName = file.getFileNameWithoutExtension().trim();

    applyingUserPreset.store (true);
    restoringState.store (true);
    parameters.replaceState (state);
    currentFactoryPreset.store (-1);
    setCurrentUserPresetName (presetName);
    captureCurrentUserPresetParameterValues();
    restoringState.store (false);
    applyingUserPreset.store (false);
    setLastUserPresetDirectory (file.getParentDirectory());
    return true;
}

void ProgressivePitchDelayAudioProcessor::setParameterFromPreset (const char* parameterID,
                                                                    float plainValue)
{
    if (auto* parameter = parameters.getParameter (parameterID))
    {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (plainValue));
        parameter->endChangeGesture();
    }
}

void ProgressivePitchDelayAudioProcessor::applyFactoryPreset (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPrograms()))
        return;

    const auto& preset = factoryPresets[static_cast<size_t> (index)];
    applyingFactoryPreset.store (true);

    setParameterFromPreset (ParameterIDs::delayMode,       static_cast<float> (preset.delayMode));
    setParameterFromPreset (ParameterIDs::pitchDirection,  static_cast<float> (preset.pitchDirection));
    setParameterFromPreset (ParameterIDs::semitones,       preset.semitones);
    setParameterFromPreset (ParameterIDs::repeatCount,     static_cast<float> (preset.repeats));
    setParameterFromPreset (ParameterIDs::delayTime,       preset.delaySeconds);
    setParameterFromPreset (ParameterIDs::tempoSync,       preset.tempoSync ? 1.0f : 0.0f);
    setParameterFromPreset (ParameterIDs::syncDivision,    static_cast<float> (preset.syncDivision));
    setParameterFromPreset (ParameterIDs::dryDb,           preset.dryDb);
    setParameterFromPreset (ParameterIDs::firstRepeatDb,   preset.firstRepeatDb);
    setParameterFromPreset (ParameterIDs::decayDb,         preset.decayDb);
    setParameterFromPreset (ParameterIDs::headroom,        preset.headroom ? 1.0f : 0.0f);
    setParameterFromPreset (ParameterIDs::quality,         static_cast<float> (preset.quality));

    currentFactoryPreset.store (index);
    clearCurrentUserPreset();
    applyingFactoryPreset.store (false);
}

void ProgressivePitchDelayAudioProcessor::parameterChanged (const juce::String& parameterID,
                                                               float newValue)
{
    if (applyingFactoryPreset.load()
        || applyingUserPreset.load()
        || restoringState.load())
        return;

    if (userPresetIsCurrent.load (std::memory_order_acquire)
        && userPresetReferenceValid.load (std::memory_order_acquire))
    {
        const auto parameterIndex = getTrackedParameterIndex (parameterID);

        if (parameterIndex >= 0)
        {
            const auto expectedValue = userPresetParameterValues[static_cast<size_t> (parameterIndex)]
                                           .load (std::memory_order_relaxed);

            // Logic may re-apply the same parameter values immediately after
            // restoring the plug-in state. Keep the saved user-preset name
            // when those callbacks do not actually change the preset.
            if (std::abs (newValue - expectedValue) <= 1.0e-4f)
                return;
        }
    }

    currentFactoryPreset.store (-1);
    clearCurrentUserPreset();
}

void ProgressivePitchDelayAudioProcessor::addParameterListeners()
{
    for (const auto* parameterID : allParameterIDs)
        parameters.addParameterListener (parameterID, this);
}

void ProgressivePitchDelayAudioProcessor::removeParameterListeners()
{
    for (const auto* parameterID : allParameterIDs)
        parameters.removeParameterListener (parameterID, this);
}

void ProgressivePitchDelayAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = juce::jmax (1.0, sampleRate);
    preparedChannels = juce::jlimit (1, 2, getTotalNumOutputChannels());

    const auto maxSingleDelay = static_cast<int> (std::ceil (maximumDelaySeconds * currentSampleRate));
    const auto maxTotalDelay = maxSingleDelay * maxRepeats + 16;

    sourceDelayBuffer.prepare (preparedChannels, maxTotalDelay);
    reverseProcessor.prepare (preparedChannels, maxSingleDelay + 2, currentSampleRate);

    for (auto& voice : repeatVoices)
    {
        for (int channel = 0; channel < preparedChannels; ++channel)
            voice.shifters[static_cast<size_t> (channel)].prepare (currentSampleRate);

        voice.delayTap.reset();
        voice.gain.reset (currentSampleRate, 0.025);
        voice.gain.setCurrentAndTargetValue (0.0f);
        voice.shifterRunning = false;
    }

    pitchLatencySamples = repeatVoices[0].shifters[0].getNominalLatencySamples();
    setLatencySamples (pitchLatencySamples);

    dryDelayBuffer.prepare (preparedChannels, pitchLatencySamples + 16);
    dryLatencyTap.reset (static_cast<float> (pitchLatencySamples));

    dryGainSmoother.reset (currentSampleRate, 0.025);
    dryGainSmoother.setCurrentAndTargetValue (1.0f);
    limiterGain = 1.0f;
    limiterReleaseCoefficient = static_cast<float> (std::exp (-1.0 / (0.150 * currentSampleRate)));

    tapCrossfadeSamples = juce::jmax (32, static_cast<int> (0.020 * currentSampleRate));

    reverseInputPointers = { &reverseInputFrame[0], &reverseInputFrame[1] };
    reverseOutputPointers = { &reverseOutputFrame[0], &reverseOutputFrame[1] };

    lastReversedMode = parameters.getRawParameterValue (ParameterIDs::delayMode)->load() >= 0.5f;
    outputPeak.store (0.0f);
}

void ProgressivePitchDelayAudioProcessor::releaseResources()
{
}

bool ProgressivePitchDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainInput = layouts.getMainInputChannelSet();
    const auto& mainOutput = layouts.getMainOutputChannelSet();

    if (mainInput != mainOutput)
        return false;

    return mainOutput == juce::AudioChannelSet::mono()
        || mainOutput == juce::AudioChannelSet::stereo();
}

double ProgressivePitchDelayAudioProcessor::calculateDelaySeconds() noexcept
{
    auto bpm = 120.0;

    if (auto* playHead = getPlayHead())
    {
        if (const auto position = playHead->getPosition())
        {
            if (const auto hostBpm = position->getBpm())
                bpm = juce::jlimit (20.0, 400.0, *hostBpm);
        }
    }

    currentBpm.store (bpm);

    const auto sync = parameters.getRawParameterValue (ParameterIDs::tempoSync)->load() >= 0.5f;
    auto seconds = static_cast<double> (parameters.getRawParameterValue (ParameterIDs::delayTime)->load());

    if (sync)
    {
        const auto division = juce::jlimit (0,
                                           static_cast<int> (ParameterChoices::syncQuarterNotes.size()) - 1,
                                           static_cast<int> (std::round (parameters.getRawParameterValue (ParameterIDs::syncDivision)->load())));
        seconds = (60.0 / bpm) * ParameterChoices::syncQuarterNotes[static_cast<size_t> (division)];
    }

    seconds = juce::jlimit (0.01, maximumDelaySeconds, seconds);
    effectiveDelaySeconds.store (seconds);
    return seconds;
}

void ProgressivePitchDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                          juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numInputChannels = getTotalNumInputChannels();
    const auto numOutputChannels = getTotalNumOutputChannels();
    const auto numSamples = buffer.getNumSamples();
    const auto channels = std::min ({ preparedChannels, numInputChannels, numOutputChannels });

    for (int channel = numInputChannels; channel < numOutputChannels; ++channel)
        buffer.clear (channel, 0, numSamples);

    if (channels <= 0)
        return;

    const auto delaySeconds = calculateDelaySeconds();
    const auto delaySamples = static_cast<float> (delaySeconds * currentSampleRate);
    const auto reversed = parameters.getRawParameterValue (ParameterIDs::delayMode)->load() >= 0.5f;
    const auto lower = parameters.getRawParameterValue (ParameterIDs::pitchDirection)->load() < 0.5f;
    const auto highQuality = parameters.getRawParameterValue (ParameterIDs::quality)->load() >= 0.5f;
    const auto semitonesPerRepeat = parameters.getRawParameterValue (ParameterIDs::semitones)->load();
    const auto repeats = juce::jlimit (1, maxRepeats,
                                      static_cast<int> (std::round (parameters.getRawParameterValue (ParameterIDs::repeatCount)->load())));
    const auto dryDb = parameters.getRawParameterValue (ParameterIDs::dryDb)->load();
    const auto firstDb = parameters.getRawParameterValue (ParameterIDs::firstRepeatDb)->load();
    const auto decayDb = parameters.getRawParameterValue (ParameterIDs::decayDb)->load();
    const auto protectionEnabled = parameters.getRawParameterValue (ParameterIDs::headroom)->load() >= 0.5f;

    if (reversed != lastReversedMode)
    {
        reverseProcessor.reset();
        sourceDelayBuffer.reset();

        for (auto& voice : repeatVoices)
        {
            voice.delayTap.reset();
            voice.shifterRunning = false;
            for (int channel = 0; channel < channels; ++channel)
                voice.shifters[static_cast<size_t> (channel)].reset();
        }

        limiterGain = 1.0f;
        lastReversedMode = reversed;
    }

    const auto dryGain = juce::Decibels::decibelsToGain (dryDb);
    const auto firstGain = juce::Decibels::decibelsToGain (firstDb);
    dryGainSmoother.setTargetValue (dryGain);

    reverseProcessor.setBlockSize (static_cast<int> (std::round (delaySamples)));

    for (int repeat = 0; repeat < maxRepeats; ++repeat)
    {
        auto& voice = repeatVoices[static_cast<size_t> (repeat)];
        const auto repeatNumber = repeat + 1;
        const auto tapDelay = reversed ? static_cast<float> (repeat) * delaySamples
                                       : static_cast<float> (repeatNumber) * delaySamples;
        voice.delayTap.setTarget (tapDelay, tapCrossfadeSamples);

        const auto voiceIsSelected = repeat < repeats;
        const auto repeatGain = voiceIsSelected
                              ? firstGain * juce::Decibels::decibelsToGain (-decayDb * static_cast<float> (repeat))
                              : 0.0f;
        voice.gain.setTargetValue (repeatGain);

        if (voiceIsSelected && ! voice.shifterRunning)
        {
            for (int channel = 0; channel < channels; ++channel)
                voice.shifters[static_cast<size_t> (channel)].reset();

            voice.shifterRunning = true;
        }

        const auto signedSemitones = (lower ? -1.0f : 1.0f)
                                   * semitonesPerRepeat
                                   * static_cast<float> (repeatNumber);
        const auto pitchRatio = std::pow (2.0f, signedSemitones / 12.0f);

        for (int channel = 0; channel < channels; ++channel)
        {
            voice.shifters[static_cast<size_t> (channel)].setHighQuality (highQuality);
            voice.shifters[static_cast<size_t> (channel)].setTargetRatio (pitchRatio);
        }
    }

    auto blockPeak = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (int channel = 0; channel < channels; ++channel)
        {
            const auto input = buffer.getSample (channel, sample);
            reverseInputFrame[static_cast<size_t> (channel)] = input;
            dryDelayBuffer.writeSample (channel, input);
        }

        if (channels == 1)
            reverseInputFrame[1] = reverseInputFrame[0];

        if (reversed)
            reverseProcessor.processFrame (reverseInputPointers.data(), reverseOutputPointers.data(), channels);
        else
            reverseOutputFrame = reverseInputFrame;

        for (int channel = 0; channel < channels; ++channel)
            sourceDelayBuffer.writeSample (channel, reverseOutputFrame[static_cast<size_t> (channel)]);

        std::array<float, 2> mixed {};
        const auto currentDryGain = dryGainSmoother.getNextValue();

        for (int channel = 0; channel < channels; ++channel)
            mixed[static_cast<size_t> (channel)] = currentDryGain * dryLatencyTap.read (dryDelayBuffer, channel);

        for (int repeat = 0; repeat < maxRepeats; ++repeat)
        {
            auto& voice = repeatVoices[static_cast<size_t> (repeat)];
            const auto currentRepeatGain = voice.gain.getNextValue();

            if (voice.shifterRunning && (currentRepeatGain > 0.000001f || repeat < repeats))
            {
                for (int channel = 0; channel < channels; ++channel)
                {
                    const auto delayed = voice.delayTap.read (sourceDelayBuffer, channel);
                    const auto shifted = voice.shifters[static_cast<size_t> (channel)].processSample (delayed);
                    mixed[static_cast<size_t> (channel)] += currentRepeatGain * shifted;
                }
            }
            else if (voice.shifterRunning)
            {
                // Once a removed tap has completed its short gain ramp, reset it
                // and stop running FFTs for that inactive repeat.
                for (int channel = 0; channel < channels; ++channel)
                    voice.shifters[static_cast<size_t> (channel)].reset();

                voice.shifterRunning = false;
            }

            voice.delayTap.advance();
        }

        auto linkedPeak = 0.0f;
        for (int channel = 0; channel < channels; ++channel)
            linkedPeak = juce::jmax (linkedPeak, std::abs (mixed[static_cast<size_t> (channel)]));

        if (protectionEnabled)
        {
            constexpr auto ceiling = 0.98f;
            const auto desiredGain = linkedPeak > ceiling ? ceiling / linkedPeak : 1.0f;

            if (desiredGain < limiterGain)
                limiterGain = desiredGain;
            else
                limiterGain = desiredGain
                            + limiterReleaseCoefficient * (limiterGain - desiredGain);
        }
        else
        {
            limiterGain = 1.0f;
        }

        for (int channel = 0; channel < channels; ++channel)
        {
            auto output = limiterGain * mixed[static_cast<size_t> (channel)];

            if (protectionEnabled)
                output = juce::jlimit (-0.98f, 0.98f, output);

            buffer.setSample (channel, sample, output);
            blockPeak = juce::jmax (blockPeak, std::abs (output));
        }

        dryLatencyTap.advance();
        dryDelayBuffer.advance();
        sourceDelayBuffer.advance();
    }

    const auto previousPeak = outputPeak.load();
    outputPeak.store (juce::jmax (blockPeak, previousPeak * 0.92f));
}

double ProgressivePitchDelayAudioProcessor::getTailLengthSeconds() const
{
    const auto repeats = juce::jlimit (1, maxRepeats,
                                      static_cast<int> (std::round (parameters.getRawParameterValue (ParameterIDs::repeatCount)->load())));
    const auto delay = effectiveDelaySeconds.load();
    const auto reversed = parameters.getRawParameterValue (ParameterIDs::delayMode)->load() >= 0.5f;

    // Reverse mode may need one additional delay-sized block to finish
    // capturing and playing the final partial block after the input stops.
    const auto delayBlocks = repeats + (reversed ? 1 : 0);
    const auto pitchLatencySeconds = static_cast<double> (pitchLatencySamples)
                                   / juce::jmax (1.0, currentSampleRate);
    const auto maximumTail = maximumDelaySeconds * static_cast<double> (maxRepeats + 1)
                           + pitchLatencySeconds + 0.25;

    return juce::jmin (maximumTail,
                       delay * static_cast<double> (delayBlocks)
                       + pitchLatencySeconds + 0.25);
}

void ProgressivePitchDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (pitchDirectionMappingVersionProperty,
                       currentPitchDirectionMappingVersion, nullptr);
    state.setProperty (factoryPresetIndexProperty,
                       currentFactoryPreset.load(), nullptr);
    state.setProperty (userPresetNameProperty,
                       getCurrentUserPresetName(), nullptr);
    state.setProperty (pluginVersionProperty, JucePlugin_VersionString, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ProgressivePitchDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (! xml->hasTagName (parameters.state.getType()))
            return;

        auto state = juce::ValueTree::fromXml (*xml);
        migratePitchDirectionStateIfNeeded (state);

        const auto savedPreset = static_cast<int> (
            state.getProperty (factoryPresetIndexProperty, -1));
        const auto savedUserPresetName = state.getProperty (userPresetNameProperty)
                                              .toString().trim();
        const auto hasFactoryPreset = juce::isPositiveAndBelow (savedPreset, getNumPrograms());

        restoringState.store (true);
        parameters.replaceState (state);
        currentFactoryPreset.store (hasFactoryPreset ? savedPreset : -1);

        if (hasFactoryPreset)
        {
            setCurrentUserPresetName (juce::String {});
            clearCurrentUserPreset();
        }
        else
        {
            setCurrentUserPresetName (savedUserPresetName);

            if (savedUserPresetName.isNotEmpty())
                captureCurrentUserPresetParameterValues();
            else
                clearCurrentUserPreset();
        }

        restoringState.store (false);
    }
}

juce::AudioProcessorEditor* ProgressivePitchDelayAudioProcessor::createEditor()
{
    return new ProgressivePitchDelayAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ProgressivePitchDelayAudioProcessor();
}
