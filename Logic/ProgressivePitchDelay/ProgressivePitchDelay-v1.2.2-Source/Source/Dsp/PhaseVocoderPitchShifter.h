#pragma once

#include <JuceHeader.h>

// A streaming phase-vocoder pitch shifter. Each delay tap owns one instance,
// so every repeat is generated independently from the unshifted source history.
// Frequency-domain bin remapping naturally rejects content that would move
// above Nyquist, avoiding the metallic whistle/aliasing produced by the earlier
// rotating-delay granular shifter.
class PhaseVocoderPitchShifter
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = juce::jmax (1.0, newSampleRate);

        // Keep the analysis window close to 43 ms at common sample rates.
        // 2048 samples is used through 48 kHz and 4096 at 88.2/96 kHz and above.
        fftOrder = sampleRate > 70000.0 ? 12 : 11;
        fftSize = 1 << fftOrder;
        hopSize = fftSize / overlapFactor;
        latencySamples = fftSize - hopSize;

        fft = std::make_unique<juce::dsp::FFT> (fftOrder);

        inputFifo.assign (static_cast<std::size_t> (fftSize), 0.0f);
        outputFifo.assign (static_cast<std::size_t> (hopSize), 0.0f);
        outputAccumulator.assign (static_cast<std::size_t> (fftSize), 0.0f);
        fftData.assign (static_cast<std::size_t> (2 * fftSize), 0.0f);

        const auto bins = static_cast<std::size_t> (fftSize / 2 + 1);
        lastInputPhase.assign (bins, 0.0f);
        synthesisPhase.assign (bins, 0.0f);
        analysisMagnitude.assign (bins, 0.0f);
        analysisFrequency.assign (bins, 0.0f);
        synthesisMagnitude.assign (bins, 0.0f);
        synthesisFrequencySum.assign (bins, 0.0f);

        unityDelay.assign (static_cast<std::size_t> (juce::jmax (1, latencySamples)), 0.0f);

        ratio.reset (sampleRate, 0.050);
        ratio.setCurrentAndTargetValue (1.0f);
        pitchedBlend.reset (sampleRate, 0.020);
        pitchedBlend.setCurrentAndTargetValue (0.0f);

        reset();
    }

    void reset() noexcept
    {
        clearVector (inputFifo);
        clearVector (outputFifo);
        clearVector (outputAccumulator);
        clearVector (fftData);
        clearVector (lastInputPhase);
        clearVector (synthesisPhase);
        clearVector (analysisMagnitude);
        clearVector (analysisFrequency);
        clearVector (synthesisMagnitude);
        clearVector (synthesisFrequencySum);
        clearVector (unityDelay);

        rover = latencySamples;
        unityWritePosition = 0;
        previousFrameRatio = ratio.getCurrentValue();
        postFilterState1 = 0.0f;
        postFilterState2 = 0.0f;
    }

    void setHighQuality (bool shouldUseHighQuality) noexcept
    {
        highQuality = shouldUseHighQuality;
    }

    void setTargetRatio (float newRatio) noexcept
    {
        // Ratios beyond four octaves are not musically useful here and would
        // leave too few source bins for a stable reconstruction.
        const auto safeRatio = juce::jlimit (1.0f / 16.0f, 16.0f, newRatio);
        ratio.setTargetValue (safeRatio);

        const auto semitoneDistance = std::abs (12.0f * std::log2 (safeRatio));
        pitchedBlend.setTargetValue (semitoneDistance < 0.001f ? 0.0f : 1.0f);
    }

    int getNominalLatencySamples() const noexcept { return latencySamples; }

    float processSample (float input) noexcept
    {
        if (fft == nullptr || fftSize <= 0 || hopSize <= 0)
            return input;

        const auto unitySample = processUnityDelay (input);
        const auto outputIndex = juce::jlimit (0, hopSize - 1, rover - latencySamples);
        auto shiftedSample = outputFifo[static_cast<std::size_t> (outputIndex)];

        inputFifo[static_cast<std::size_t> (rover)] = input;
        const auto currentRatio = ratio.getNextValue();

        if (++rover >= fftSize)
        {
            rover = latencySamples;
            processFrame (currentRatio);
        }

        // A gentle two-pole reconstruction filter removes isolated spectral-bin
        // residue without dulling normal guitar harmonics. Its cutoff remains
        // above the useful guitar range and below the region where the old
        // shifter's whistle was most obvious.
        const auto cutoff = juce::jmin (18000.0f, static_cast<float> (0.45 * sampleRate));
        const auto pole = std::exp (-juce::MathConstants<float>::twoPi
                                    * cutoff / static_cast<float> (sampleRate));
        const auto feed = 1.0f - pole;
        postFilterState1 = feed * shiftedSample + pole * postFilterState1;
        postFilterState2 = feed * postFilterState1 + pole * postFilterState2;
        shiftedSample = highQuality ? postFilterState2
                                    : 0.75f * shiftedSample + 0.25f * postFilterState2;

        const auto blend = pitchedBlend.getNextValue();
        const auto output = unitySample + blend * (shiftedSample - unitySample);
        return std::isfinite (output) ? juce::jlimit (-4.0f, 4.0f, output) : 0.0f;
    }

private:
    static constexpr int overlapFactor = 4;

    static void clearVector (std::vector<float>& values) noexcept
    {
        std::fill (values.begin(), values.end(), 0.0f);
    }

    float processUnityDelay (float input) noexcept
    {
        if (unityDelay.empty())
            return input;

        const auto index = static_cast<std::size_t> (unityWritePosition);
        const auto output = unityDelay[index];
        unityDelay[index] = input;

        if (++unityWritePosition >= static_cast<int> (unityDelay.size()))
            unityWritePosition = 0;

        return output;
    }

    void processFrame (float currentRatio) noexcept
    {
        const auto halfSize = fftSize / 2;
        const auto expectedPhaseAdvance = juce::MathConstants<float>::twoPi
                                        * static_cast<float> (hopSize)
                                        / static_cast<float> (fftSize);

        clearVector (fftData);

        for (int sample = 0; sample < fftSize; ++sample)
        {
            const auto phase = juce::MathConstants<float>::twoPi
                             * static_cast<float> (sample)
                             / static_cast<float> (fftSize);
            const auto window = 0.5f - 0.5f * std::cos (phase);
            fftData[static_cast<std::size_t> (sample)]
                = inputFifo[static_cast<std::size_t> (sample)] * window;
        }

        fft->performRealOnlyForwardTransform (fftData.data(), true);

        auto framePeakMagnitude = 0.0f;

        for (int bin = 0; bin <= halfSize; ++bin)
        {
            const auto real = fftData[static_cast<std::size_t> (2 * bin)];
            const auto imaginary = fftData[static_cast<std::size_t> (2 * bin + 1)];
            const auto magnitude = std::sqrt (real * real + imaginary * imaginary);
            const auto phase = std::atan2 (imaginary, real);

            auto phaseDelta = phase - lastInputPhase[static_cast<std::size_t> (bin)];
            lastInputPhase[static_cast<std::size_t> (bin)] = phase;
            phaseDelta -= expectedPhaseAdvance * static_cast<float> (bin);
            phaseDelta -= juce::MathConstants<float>::twoPi
                        * std::round (phaseDelta / juce::MathConstants<float>::twoPi);

            const auto trueBin = static_cast<float> (bin)
                               + phaseDelta / expectedPhaseAdvance;

            analysisMagnitude[static_cast<std::size_t> (bin)] = magnitude;
            analysisFrequency[static_cast<std::size_t> (bin)] = trueBin;
            framePeakMagnitude = juce::jmax (framePeakMagnitude, magnitude);
        }

        clearVector (synthesisMagnitude);
        clearVector (synthesisFrequencySum);

        // Ignore only extremely low-level FFT residue. This prevents isolated
        // bins from turning into a steady high-frequency tone after remapping.
        const auto spectralFloor = framePeakMagnitude * (highQuality ? 0.000003f : 0.00001f);

        for (int sourceBin = 0; sourceBin <= halfSize; ++sourceBin)
        {
            const auto magnitude = analysisMagnitude[static_cast<std::size_t> (sourceBin)];
            if (magnitude <= spectralFloor)
                continue;

            const auto targetPosition = static_cast<float> (sourceBin) * currentRatio;
            if (targetPosition > static_cast<float> (halfSize))
                continue;

            const auto shiftedFrequency = analysisFrequency[static_cast<std::size_t> (sourceBin)]
                                        * currentRatio;

            if (highQuality)
            {
                const auto lowerBin = static_cast<int> (std::floor (targetPosition));
                const auto fraction = targetPosition - static_cast<float> (lowerBin);
                addToSynthesisBin (lowerBin, magnitude * (1.0f - fraction), shiftedFrequency, halfSize);
                addToSynthesisBin (lowerBin + 1, magnitude * fraction, shiftedFrequency, halfSize);
            }
            else
            {
                addToSynthesisBin (static_cast<int> (std::round (targetPosition)),
                                   magnitude,
                                   shiftedFrequency,
                                   halfSize);
            }
        }

        clearVector (fftData);

        // Reset accumulated phases after a large automated ratio jump. Small
        // changes remain phase-continuous and therefore do not click.
        if (std::abs (12.0f * std::log2 (juce::jmax (0.0001f, currentRatio / previousFrameRatio))) > 3.0f)
            clearVector (synthesisPhase);

        previousFrameRatio = currentRatio;

        for (int bin = 0; bin <= halfSize; ++bin)
        {
            const auto index = static_cast<std::size_t> (bin);
            const auto magnitude = synthesisMagnitude[index];
            const auto trueBin = magnitude > 0.0000001f
                               ? synthesisFrequencySum[index] / magnitude
                               : static_cast<float> (bin);

            synthesisPhase[index] += expectedPhaseAdvance * trueBin;
            synthesisPhase[index] -= juce::MathConstants<float>::twoPi
                                   * std::floor (synthesisPhase[index]
                                               / juce::MathConstants<float>::twoPi);

            fftData[static_cast<std::size_t> (2 * bin)]
                = magnitude * std::cos (synthesisPhase[index]);
            fftData[static_cast<std::size_t> (2 * bin + 1)]
                = magnitude * std::sin (synthesisPhase[index]);
        }

        fft->performRealOnlyInverseTransform (fftData.data());

        // For a Hann analysis/synthesis pair at 4x overlap, the sum of squared
        // windows is 1.5. JUCE's real-only inverse transform restores the FFT
        // scale, so only the overlap/window normalisation is required here.
        constexpr auto reconstructionGain = 1.0f / 1.5f;

        for (int sample = 0; sample < fftSize; ++sample)
        {
            const auto phase = juce::MathConstants<float>::twoPi
                             * static_cast<float> (sample)
                             / static_cast<float> (fftSize);
            const auto window = 0.5f - 0.5f * std::cos (phase);
            outputAccumulator[static_cast<std::size_t> (sample)]
                += fftData[static_cast<std::size_t> (sample)] * window * reconstructionGain;
        }

        std::copy_n (outputAccumulator.begin(), hopSize, outputFifo.begin());

        std::move (outputAccumulator.begin() + hopSize,
                   outputAccumulator.end(),
                   outputAccumulator.begin());
        std::fill (outputAccumulator.end() - hopSize, outputAccumulator.end(), 0.0f);

        std::move (inputFifo.begin() + hopSize,
                   inputFifo.end(),
                   inputFifo.begin());
        std::fill (inputFifo.end() - hopSize, inputFifo.end(), 0.0f);
    }

    void addToSynthesisBin (int targetBin,
                            float magnitude,
                            float shiftedFrequency,
                            int halfSize) noexcept
    {
        if (targetBin < 0 || targetBin > halfSize || magnitude <= 0.0f)
            return;

        const auto index = static_cast<std::size_t> (targetBin);
        synthesisMagnitude[index] += magnitude;
        synthesisFrequencySum[index] += magnitude * shiftedFrequency;
    }

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> inputFifo;
    std::vector<float> outputFifo;
    std::vector<float> outputAccumulator;
    std::vector<float> fftData;
    std::vector<float> lastInputPhase;
    std::vector<float> synthesisPhase;
    std::vector<float> analysisMagnitude;
    std::vector<float> analysisFrequency;
    std::vector<float> synthesisMagnitude;
    std::vector<float> synthesisFrequencySum;
    std::vector<float> unityDelay;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ratio;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> pitchedBlend;

    double sampleRate = 44100.0;
    float previousFrameRatio = 1.0f;
    float postFilterState1 = 0.0f;
    float postFilterState2 = 0.0f;
    int fftOrder = 11;
    int fftSize = 2048;
    int hopSize = 512;
    int latencySamples = 1536;
    int rover = 1536;
    int unityWritePosition = 0;
    bool highQuality = true;
};
