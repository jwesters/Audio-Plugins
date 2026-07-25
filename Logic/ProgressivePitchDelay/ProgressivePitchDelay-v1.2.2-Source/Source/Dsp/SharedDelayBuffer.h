#pragma once

#include <JuceHeader.h>

class SharedDelayBuffer
{
public:
    void prepare (int channels, int maximumDelaySamples)
    {
        numChannels = juce::jmax (1, channels);
        bufferLength = juce::jmax (8, maximumDelaySamples + 8);
        buffer.setSize (numChannels, bufferLength, false, true, true);
        reset();
    }

    void reset()
    {
        buffer.clear();
        writePosition = 0;
    }

    void writeSample (int channel, float sample) noexcept
    {
        buffer.setSample (channel, writePosition, sample);
    }

    float readSample (int channel, float delayInSamples) const noexcept
    {
        const auto limitedDelay = juce::jlimit (0.0f,
                                                static_cast<float> (bufferLength - 4),
                                                delayInSamples);
        auto readPosition = static_cast<float> (writePosition) - limitedDelay;

        while (readPosition < 0.0f)
            readPosition += static_cast<float> (bufferLength);

        const auto indexA = static_cast<int> (readPosition);
        const auto indexB = (indexA + 1) % bufferLength;
        const auto fraction = readPosition - static_cast<float> (indexA);
        const auto* data = buffer.getReadPointer (juce::jlimit (0, numChannels - 1, channel));
        return data[indexA] + fraction * (data[indexB] - data[indexA]);
    }

    void advance() noexcept
    {
        if (++writePosition >= bufferLength)
            writePosition = 0;
    }

private:
    juce::AudioBuffer<float> buffer;
    int numChannels = 1;
    int bufferLength = 8;
    int writePosition = 0;
};

struct DelayTapState
{
    void reset (float delaySamples = 0.0f) noexcept
    {
        oldDelay = targetDelay = delaySamples;
        fadePosition = fadeLength = 0;
    }

    void setTarget (float newDelay, int requestedFadeLength) noexcept
    {
        if (std::abs (newDelay - targetDelay) < 0.5f)
            return;

        if (fadePosition < fadeLength)
        {
            const auto alpha = static_cast<float> (fadePosition)
                             / static_cast<float> (juce::jmax (1, fadeLength));
            oldDelay = juce::jmap (alpha, oldDelay, targetDelay);
        }
        else
        {
            oldDelay = targetDelay;
        }

        targetDelay = newDelay;
        fadeLength = juce::jmax (1, requestedFadeLength);
        fadePosition = 0;
    }

    float read (const SharedDelayBuffer& delayBuffer, int channel) const noexcept
    {
        if (fadePosition >= fadeLength)
            return delayBuffer.readSample (channel, targetDelay);

        const auto alpha = static_cast<float> (fadePosition)
                         / static_cast<float> (juce::jmax (1, fadeLength));
        return juce::jmap (alpha,
                           delayBuffer.readSample (channel, oldDelay),
                           delayBuffer.readSample (channel, targetDelay));
    }

    void advance() noexcept
    {
        if (fadePosition < fadeLength)
            ++fadePosition;
    }

    float getTarget() const noexcept { return targetDelay; }

private:
    float oldDelay = 0.0f;
    float targetDelay = 0.0f;
    int fadePosition = 0;
    int fadeLength = 0;
};
