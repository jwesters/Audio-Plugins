#pragma once

#include <JuceHeader.h>

class ReverseBlockProcessor
{
public:
    void prepare (int channels, int maximumBlockSamples, double sampleRate)
    {
        numChannels = juce::jmax (1, channels);
        maxBlockSize = juce::jmax (2, maximumBlockSamples);
        defaultBoundaryFadeSamples = juce::jmax (8, static_cast<int> (std::round (0.003 * sampleRate)));

        for (auto& bank : banks)
            bank.setSize (numChannels, maxBlockSize, false, true, true);

        reset();
    }

    void reset() noexcept
    {
        for (auto& bank : banks)
            bank.clear();

        captureBank = 0;
        playbackBank = 1;
        capturePosition = 0;
        playbackPosition = -1;
        blockSize = 2;
        playbackReady = false;
        boundaryFadePosition = 0;
        boundaryFadeLength = 0;
        boundaryStart.fill (0.0f);
        lastOutput.fill (0.0f);
    }

    void setBlockSize (int requestedSamples) noexcept
    {
        const auto newSize = juce::jlimit (2, maxBlockSize, requestedSamples);

        // Ignore tiny tempo jitter so reverse capture does not repeatedly restart.
        const auto jitterTolerance = juce::jmax (32, blockSize / 200);
        if (std::abs (newSize - blockSize) <= jitterTolerance)
            return;

        blockSize = newSize;
        capturePosition = 0;
        playbackPosition = -1;
        playbackReady = false;
        boundaryFadePosition = 0;
        boundaryFadeLength = juce::jmin (defaultBoundaryFadeSamples, juce::jmax (1, blockSize / 8));
        boundaryStart = lastOutput;
    }

    void processFrame (const float* const* input,
                       float* const* output,
                       int availableChannels) noexcept
    {
        const auto channelsToProcess = juce::jmin (numChannels, availableChannels);

        for (int channel = 0; channel < channelsToProcess; ++channel)
        {
            auto rawOutput = playbackReady
                           ? banks[static_cast<std::size_t> (playbackBank)].getSample (channel, playbackPosition)
                           : 0.0f;

            if (boundaryFadePosition < boundaryFadeLength)
            {
                const auto normalised = static_cast<float> (boundaryFadePosition + 1)
                                      / static_cast<float> (juce::jmax (1, boundaryFadeLength));
                const auto alpha = std::sin (0.5f * juce::MathConstants<float>::pi * normalised);
                const auto equalPowerAlpha = alpha * alpha;
                rawOutput = boundaryStart[static_cast<std::size_t> (channel)]
                          + equalPowerAlpha
                          * (rawOutput - boundaryStart[static_cast<std::size_t> (channel)]);
            }

            output[channel][0] = rawOutput;
            lastOutput[static_cast<std::size_t> (channel)] = rawOutput;
            banks[static_cast<std::size_t> (captureBank)].setSample (channel, capturePosition, input[channel][0]);
        }

        ++capturePosition;

        if (playbackReady)
        {
            --playbackPosition;
            if (playbackPosition < 0)
                playbackReady = false;
        }

        if (boundaryFadePosition < boundaryFadeLength)
            ++boundaryFadePosition;

        if (capturePosition >= blockSize)
        {
            std::swap (captureBank, playbackBank);
            capturePosition = 0;
            playbackPosition = blockSize - 1;
            playbackReady = true;

            boundaryStart = lastOutput;
            boundaryFadeLength = juce::jmin (defaultBoundaryFadeSamples, juce::jmax (1, blockSize / 8));
            boundaryFadePosition = 0;
        }
    }

private:
    std::array<juce::AudioBuffer<float>, 2> banks;
    std::array<float, 2> boundaryStart {};
    std::array<float, 2> lastOutput {};
    int numChannels = 1;
    int maxBlockSize = 2;
    int blockSize = 2;
    int defaultBoundaryFadeSamples = 128;
    int captureBank = 0;
    int playbackBank = 1;
    int capturePosition = 0;
    int playbackPosition = -1;
    int boundaryFadePosition = 0;
    int boundaryFadeLength = 0;
    bool playbackReady = false;
};
