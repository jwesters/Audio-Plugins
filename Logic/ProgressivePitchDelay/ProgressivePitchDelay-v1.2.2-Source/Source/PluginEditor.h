#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class ProgressivePitchDelayLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ProgressivePitchDelayLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
};

class ProgressivePitchDelayAboutOverlay final : public juce::Component
{
public:
    ProgressivePitchDelayAboutOverlay();

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;

private:
    juce::TextButton closeButton { "OK" };

    juce::Rectangle<int> getPanelBounds() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgressivePitchDelayAboutOverlay)
};

class ProgressivePitchDelayAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                          private juce::Timer
{
public:
    explicit ProgressivePitchDelayAudioProcessorEditor (ProgressivePitchDelayAudioProcessor&);
    ~ProgressivePitchDelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void configureKnob (juce::Slider&, const juce::String& suffix, int decimals);
    void configureCombo (juce::ComboBox&, const juce::StringArray& choices);
    void configureLabel (juce::Label&, const juce::String&, juce::Justification);
    void configureTooltips();
    void configureHeaderButton (juce::TextButton& button);
    void showAboutOverlay();
    void showSavePresetDialog();
    void showLoadPresetDialog();
    void revealPresetFolder();
    void showPresetError (const juce::String& message);
    void drawPanel (juce::Graphics&, juce::Rectangle<int>, const juce::String&) const;
    void drawMeter (juce::Graphics&, juce::Rectangle<float>) const;

    ProgressivePitchDelayAudioProcessor& audioProcessor;
    ProgressivePitchDelayLookAndFeel lookAndFeel;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label effectiveTimeLabel;
    juce::Label presetLabel;
    juce::Label semitonesLabel;
    juce::Label repeatCountLabel;
    juce::Label delayTimeLabel;
    juce::Label delayModeLabel;
    juce::Label pitchDirectionLabel;
    juce::Label qualityLabel;
    juce::Label syncDivisionLabel;
    juce::Label dryLabel;
    juce::Label firstRepeatLabel;
    juce::Label decayLabel;

    juce::Slider semitonesSlider;
    juce::Slider repeatCountSlider;
    juce::Slider delayTimeSlider;
    juce::Slider drySlider;
    juce::Slider firstRepeatSlider;
    juce::Slider decaySlider;

    juce::ComboBox presetBox;
    juce::ComboBox delayModeBox;
    juce::ComboBox pitchDirectionBox;
    juce::ComboBox qualityBox;
    juce::ComboBox syncDivisionBox;

    juce::ToggleButton tempoSyncButton { "Tempo Sync" };
    juce::ToggleButton headroomButton { "Auto Headroom" };
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton loadPresetButton { "Load" };
    juce::TextButton resetButton { "Reset" };
    juce::TextButton presetFolderButton { "Folder" };
    juce::TextButton aboutButton { "About" };
    ProgressivePitchDelayAboutOverlay aboutOverlay;

    std::unique_ptr<SliderAttachment> semitonesAttachment;
    std::unique_ptr<SliderAttachment> repeatCountAttachment;
    std::unique_ptr<SliderAttachment> delayTimeAttachment;
    std::unique_ptr<SliderAttachment> dryAttachment;
    std::unique_ptr<SliderAttachment> firstRepeatAttachment;
    std::unique_ptr<SliderAttachment> decayAttachment;
    std::unique_ptr<ComboAttachment> delayModeAttachment;
    std::unique_ptr<ComboAttachment> pitchDirectionAttachment;
    std::unique_ptr<ComboAttachment> qualityAttachment;
    std::unique_ptr<ComboAttachment> syncDivisionAttachment;
    std::unique_ptr<ButtonAttachment> tempoSyncAttachment;
    std::unique_ptr<ButtonAttachment> headroomAttachment;

    std::unique_ptr<juce::FileChooser> presetFileChooser;

    int lastDisplayedPreset = -2;
    juce::String lastDisplayedUserPreset;
    float displayedPeak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ProgressivePitchDelayAudioProcessorEditor)
};
