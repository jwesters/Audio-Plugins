/*
  ==============================================================================

    Binaural Journey - plug-in editor

  ==============================================================================
*/

/*
    BinauralJourney
    Copyright (c) 2026 jwesters
    SPDX-License-Identifier: MIT
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class BinauralJourneyAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit BinauralJourneyAudioProcessorEditor (BinauralJourneyAudioProcessor&);
    ~BinauralJourneyAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    static void configureFrequencySlider (juce::Slider& slider,
                                          double defaultValue,
                                          int decimalPlaces = 1);
    static void configurePercentSlider (juce::Slider& slider,
                                        double defaultValue);
    static void configureSecondsSlider (juce::Slider& slider,
                                        double defaultValue);
    static void configureMinutesSlider (juce::Slider& slider,
                                        double defaultValue);
    void configureSectionLabel (juce::Label& label,
                                const juce::String& text);
    void configureComboBox (juce::ComboBox& box);
    void configureActionButton (juce::TextButton& button, bool primary);
    void showHelpAbout();
    void applySelectedAnchorNote();
    void updateAnchorModeControls();
    void updateNoiseControls();
    void updateToneControls();
    void updateJourneyControls();
    void updatePlaybackSourceControls();
    void openJourneyWindow();
    void bringJourneyWindowToFront();
    void resizeJourneyWindowForStageCount (int stageCount);
    void closeJourneyWindow();
    void updateOutputMeters();
    void setActivePage (int pageIndex);
    void updatePageVisibility();
    void configureTabButton (juce::TextButton& button, const juce::String& text);
    void setParameterValue (const juce::String& parameterID, float value);
    double getEffectiveAnchorFrequency() const;
    void applySelectedBeatPreset();
    double getSelectedPresetFrequency() const;
    void updateFrequencyReadout();
    void applySelectedSoundPreset();
    void restoreDefaultSettings();

    juce::File getUserPresetFile() const;
    std::unique_ptr<juce::XmlElement> loadUserPresetDocument() const;
    bool writeUserPresetDocument (const juce::XmlElement& document) const;
    void refreshUserPresetList (const juce::String& selectedName = {});
    void updateUserPresetButtons();
    void saveCurrentUserPreset();
    void saveCurrentUserPresetNamed (const juce::String& name);
    void loadSelectedUserPreset();
    void renameSelectedUserPreset();
    void renameSelectedUserPresetTo (const juce::String& oldName,
                                     const juce::String& newName);
    void deleteSelectedUserPreset();
    void timerCallback() override;
    void updateSessionDisplay();
    static juce::String formatSessionTime (double seconds);

    BinauralJourneyAudioProcessor& audioProcessor;
    juce::TooltipWindow tooltipWindow;

    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::TextButton helpAboutButton;
    juce::Label versionLabel;

    juce::TextButton setupTabButton;
    juce::TextButton toneTabButton;
    juce::TextButton mixTabButton;
    int activePage = 0;

    juce::ToggleButton outputEnabledButton;
    juce::ToggleButton toneEnabledButton;

    juce::Label soundPresetLabel;
    juce::ComboBox soundPresetBox;
    juce::TextButton resetButton;
    juce::Label presetHintLabel;

    juce::Label userPresetLabel;
    juce::ComboBox userPresetBox;
    juce::TextButton saveUserPresetButton;
    juce::TextButton renameUserPresetButton;
    juce::TextButton deleteUserPresetButton;
    juce::Label userPresetHintLabel;

    juce::Label journeyModeLabel;
    juce::ComboBox journeyModeBox;
    juce::TextButton editJourneyButton;
    juce::Label journeySummaryLabel;

    juce::Label playbackSourceLabel;
    juce::ComboBox playbackSourceBox;

    juce::Label sessionDurationLabel;
    juce::Slider sessionDurationSlider;
    juce::TextButton playPauseButton;
    juce::TextButton restartSessionButton;
    juce::TextButton stopSessionButton;
    juce::Label sessionTimeLabel;

    juce::Label anchorLabel;
    juce::Slider anchorSlider;

    juce::Label anchorModeLabel;
    juce::ComboBox anchorModeBox;

    juce::Label anchorNoteLabel;
    juce::ComboBox anchorNoteBox;

    juce::Label anchorOctaveLabel;
    juce::ComboBox anchorOctaveBox;

    juce::Label tuningReferenceLabel;
    juce::Slider tuningReferenceSlider;

    juce::Label beatLabel;
    juce::Slider beatSlider;

    juce::Label beatPresetLabel;
    juce::ComboBox beatPresetBox;

    juce::Label waveformLabel;
    juce::ComboBox waveformBox;

    juce::Label anchorEarLabel;
    juce::ComboBox anchorEarBox;

    juce::Label noiseTypeLabel;
    juce::ComboBox noiseTypeBox;

    juce::Label noiseVolumeLabel;
    juce::Slider noiseVolumeSlider;
    juce::Label noiseHintLabel;

    juce::Label toneVolumeLabel;
    juce::Slider toneVolumeSlider;

    juce::Label leftVolumeLabel;
    juce::Slider leftVolumeSlider;

    juce::Label rightVolumeLabel;
    juce::Slider rightVolumeSlider;

    juce::Label masterVolumeLabel;
    juce::Slider masterVolumeSlider;

    juce::Label fadeInLabel;
    juce::Slider fadeInSlider;

    juce::Label fadeOutLabel;
    juce::Slider fadeOutSlider;
    juce::Label outputHintLabel;

    juce::Label outputMeterLabel;
    juce::Label leftOutputMeterLabel;
    juce::Label rightOutputMeterLabel;
    double leftOutputMeterValue = 0.0;
    double rightOutputMeterValue = 0.0;
    juce::ProgressBar leftOutputMeter { leftOutputMeterValue };
    juce::ProgressBar rightOutputMeter { rightOutputMeterValue };
    juce::Label peakGuardStatusLabel;

    juce::Label leftFrequencyLabel;
    juce::Label rightFrequencyLabel;
    juce::Label waveformHintLabel;
    juce::Label safetyLabel;

    std::unique_ptr<ButtonAttachment> outputEnabledAttachment;
    std::unique_ptr<ButtonAttachment> toneEnabledAttachment;
    std::unique_ptr<SliderAttachment> anchorAttachment;
    std::unique_ptr<ComboBoxAttachment> anchorModeAttachment;
    std::unique_ptr<ComboBoxAttachment> anchorNoteAttachment;
    std::unique_ptr<ComboBoxAttachment> anchorOctaveAttachment;
    std::unique_ptr<SliderAttachment> tuningReferenceAttachment;
    std::unique_ptr<SliderAttachment> beatAttachment;
    std::unique_ptr<ComboBoxAttachment> beatPresetAttachment;
    std::unique_ptr<ComboBoxAttachment> waveformAttachment;
    std::unique_ptr<ComboBoxAttachment> anchorEarAttachment;
    std::unique_ptr<ComboBoxAttachment> noiseTypeAttachment;
    std::unique_ptr<SliderAttachment> noiseVolumeAttachment;
    std::unique_ptr<SliderAttachment> toneVolumeAttachment;
    std::unique_ptr<SliderAttachment> leftVolumeAttachment;
    std::unique_ptr<SliderAttachment> rightVolumeAttachment;
    std::unique_ptr<SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<SliderAttachment> fadeInAttachment;
    std::unique_ptr<SliderAttachment> fadeOutAttachment;
    std::unique_ptr<ComboBoxAttachment> journeyModeAttachment;
    std::unique_ptr<ComboBoxAttachment> playbackSourceAttachment;
    std::unique_ptr<SliderAttachment> sessionDurationAttachment;

    bool applyingAnchorNote = false;
    bool applyingBeatPreset = false;
    bool refreshingUserPresetList = false;
    std::unique_ptr<juce::AlertWindow> presetDialog;
    std::unique_ptr<juce::Component> helpAboutOverlay;
    std::unique_ptr<juce::DocumentWindow> journeyEditorWindow;
    int journeyWindowHeight = 780;
    std::uint64_t lastPeakGuardEventCount = 0;
    int peakGuardStatusTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BinauralJourneyAudioProcessorEditor)
};
