#include "PluginEditor.h"

namespace PluginColours
{
    const auto background = juce::Colour::fromRGB (14, 18, 25);
    const auto panel = juce::Colour::fromRGB (24, 30, 40);
    const auto panelBorder = juce::Colour::fromRGB (52, 62, 78);
    const auto text = juce::Colour::fromRGB (236, 241, 248);
    const auto mutedText = juce::Colour::fromRGB (153, 164, 181);
    const auto accent = juce::Colour::fromRGB (89, 190, 255);
    const auto accentTwo = juce::Colour::fromRGB (127, 116, 255);
    const auto control = juce::Colour::fromRGB (35, 43, 56);
}

ProgressivePitchDelayLookAndFeel::ProgressivePitchDelayLookAndFeel()
{
    setColour (juce::Label::textColourId, PluginColours::text);
    setColour (juce::Slider::textBoxTextColourId, PluginColours::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::textColourId, PluginColours::text);
    setColour (juce::ComboBox::backgroundColourId, PluginColours::control);
    setColour (juce::ComboBox::outlineColourId, PluginColours::panelBorder);
    setColour (juce::PopupMenu::backgroundColourId, PluginColours::panel);
    setColour (juce::PopupMenu::textColourId, PluginColours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, PluginColours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, PluginColours::text);
    setColour (juce::TooltipWindow::backgroundColourId, PluginColours::panel.brighter (0.08f));
    setColour (juce::TooltipWindow::textColourId, PluginColours::text);
    setColour (juce::TooltipWindow::outlineColourId, PluginColours::panelBorder);
}

void ProgressivePitchDelayLookAndFeel::drawRotarySlider (juce::Graphics& g,
                                                           int x, int y, int width, int height,
                                                           float sliderPos,
                                                           float rotaryStartAngle,
                                                           float rotaryEndAngle,
                                                           juce::Slider& slider)
{
    g.setOpacity (slider.isEnabled() ? 1.0f : 0.35f);

    const auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                                 static_cast<float> (width), static_cast<float> (height))
                            .reduced (12.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto lineWidth = juce::jmax (3.0f, radius * 0.105f);
    const auto arcRadius = radius - lineWidth * 0.7f;
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path backgroundArc;
    backgroundArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                                  rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (PluginColours::control.brighter (0.25f));
    g.strokePath (backgroundArc, juce::PathStrokeType (lineWidth,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                            rotaryStartAngle, angle, true);
    juce::ColourGradient gradient (PluginColours::accentTwo, centre.x - radius, centre.y,
                                    PluginColours::accent, centre.x + radius, centre.y, false);
    g.setGradientFill (gradient);
    g.strokePath (valueArc, juce::PathStrokeType (lineWidth,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

    g.setColour (PluginColours::text);
    const auto pointerLength = radius * 0.58f;
    const auto pointerThickness = juce::jmax (2.0f, radius * 0.06f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerThickness * 0.5f, -pointerLength,
                                  pointerThickness, pointerLength * 0.46f,
                                  pointerThickness * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.fillPath (pointer);

    g.setColour (PluginColours::control);
    g.fillEllipse (centre.x - radius * 0.46f, centre.y - radius * 0.46f,
                   radius * 0.92f, radius * 0.92f);
}

void ProgressivePitchDelayLookAndFeel::drawToggleButton (juce::Graphics& g,
                                                           juce::ToggleButton& button,
                                                           bool highlighted,
                                                           bool down)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto switchWidth = 42.0f;
    const auto switchHeight = 22.0f;
    auto switchBounds = juce::Rectangle<float> (bounds.getX(), bounds.getCentreY() - switchHeight * 0.5f,
                                                 switchWidth, switchHeight);

    const auto enabled = button.getToggleState();
    auto switchColour = enabled ? PluginColours::accent : PluginColours::control.brighter (0.2f);
    if (highlighted || down)
        switchColour = switchColour.brighter (0.12f);

    g.setColour (switchColour);
    g.fillRoundedRectangle (switchBounds, switchHeight * 0.5f);

    const auto thumbDiameter = switchHeight - 4.0f;
    const auto thumbX = enabled ? switchBounds.getRight() - thumbDiameter - 2.0f
                                : switchBounds.getX() + 2.0f;
    g.setColour (PluginColours::text);
    g.fillEllipse (thumbX, switchBounds.getY() + 2.0f, thumbDiameter, thumbDiameter);

    g.setColour (button.isEnabled() ? PluginColours::text : PluginColours::mutedText.withAlpha (0.55f));
    g.setFont (juce::FontOptions (14.0f, juce::Font::plain));
    g.drawFittedText (button.getButtonText(),
                      bounds.withTrimmedLeft (switchWidth + 10.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);
}

void ProgressivePitchDelayLookAndFeel::drawComboBox (juce::Graphics& g,
                                                       int width, int height,
                                                       bool isButtonDown,
                                                       int, int, int, int,
                                                       juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f,
                                           static_cast<float> (width), static_cast<float> (height));
    g.setColour (isButtonDown ? PluginColours::control.brighter (0.15f) : PluginColours::control);
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (box.isEnabled() ? PluginColours::panelBorder : PluginColours::panelBorder.withAlpha (0.4f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    juce::Path arrow;
    const auto centreX = static_cast<float> (width - 18);
    const auto centreY = static_cast<float> (height) * 0.5f;
    arrow.startNewSubPath (centreX - 4.0f, centreY - 2.0f);
    arrow.lineTo (centreX, centreY + 2.0f);
    arrow.lineTo (centreX + 4.0f, centreY - 2.0f);
    g.setColour (box.isEnabled() ? PluginColours::mutedText : PluginColours::mutedText.withAlpha (0.35f));
    g.strokePath (arrow, juce::PathStrokeType (1.8f));
}

void ProgressivePitchDelayLookAndFeel::positionComboBoxText (juce::ComboBox& box,
                                                               juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 36, box.getHeight() - 2);
    label.setFont (juce::FontOptions (14.0f, juce::Font::plain));
}

ProgressivePitchDelayAboutOverlay::ProgressivePitchDelayAboutOverlay()
{
    setInterceptsMouseClicks (true, true);

    closeButton.setColour (juce::TextButton::buttonColourId, PluginColours::accent.withAlpha (0.85f));
    closeButton.setColour (juce::TextButton::buttonOnColourId, PluginColours::accent);
    closeButton.setColour (juce::TextButton::textColourOffId, PluginColours::background);
    closeButton.setColour (juce::TextButton::textColourOnId, PluginColours::background);
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (closeButton);
}

juce::Rectangle<int> ProgressivePitchDelayAboutOverlay::getPanelBounds() const
{
    const auto width = juce::jmin (560, getWidth() - 48);
    const auto height = juce::jmin (420, getHeight() - 48);
    return juce::Rectangle<int> (width, height).withCentre (getLocalBounds().getCentre());
}

void ProgressivePitchDelayAboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.74f));

    const auto panel = getPanelBounds();
    g.setColour (PluginColours::panel);
    g.fillRoundedRectangle (panel.toFloat(), 16.0f);
    g.setColour (PluginColours::panelBorder);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 16.0f, 1.0f);

    auto content = panel.reduced (34, 26);

    g.setColour (PluginColours::text);
    g.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    g.drawText ("Progressive Pitch Delay", content.removeFromTop (36),
                juce::Justification::centredLeft);

    g.setColour (PluginColours::accent);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Version " JucePlugin_VersionString " | AU and VST3",
                content.removeFromTop (26), juce::Justification::centredLeft);

    content.removeFromTop (10);
    const auto juceVersion = juce::String (JUCE_MAJOR_VERSION) + "."
                           + juce::String (JUCE_MINOR_VERSION) + "."
                           + juce::String (JUCE_BUILDNUMBER);
    const auto aboutText = juce::String ("Developed by jwesters\n\n")
                         + "Creates progressively pitch-shifted multi-tap echoes in normal or delay-sized reverse modes.\n\n"
                         + "Built with JUCE " + juceVersion + ".\n\n"
                         + "The JUCE Framework is not included in this source package. Anyone compiling, modifying, or redistributing Progressive Pitch Delay must obtain JUCE separately and comply with the JUCE licence applicable to their use.";

    g.setColour (PluginColours::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::plain));
    g.drawFittedText (aboutText, content.withTrimmedBottom (56),
                      juce::Justification::topLeft, 14, 0.82f);
}

void ProgressivePitchDelayAboutOverlay::resized()
{
    const auto panel = getPanelBounds();
    closeButton.setBounds (panel.getCentreX() - 50, panel.getBottom() - 58, 100, 34);
}

ProgressivePitchDelayAudioProcessorEditor::ProgressivePitchDelayAudioProcessorEditor (
    ProgressivePitchDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    setLookAndFeel (&lookAndFeel);
    setSize (940, 600);
    setResizable (true, false);
    setResizeLimits (880, 560, 1180, 680);

    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 650);

    titleLabel.setText ("Progressive Pitch Delay", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (28.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, PluginColours::text);
    titleLabel.setMinimumHorizontalScale (0.78f);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText ("Progressively shifted echoes | normal or delay-sized reverse blocks",
                           juce::dontSendNotification);
    subtitleLabel.setFont (juce::FontOptions (13.0f, juce::Font::plain));
    subtitleLabel.setColour (juce::Label::textColourId, PluginColours::mutedText);
    subtitleLabel.setMinimumHorizontalScale (0.72f);
    addAndMakeVisible (subtitleLabel);

    effectiveTimeLabel.setJustificationType (juce::Justification::centredRight);
    effectiveTimeLabel.setFont (juce::FontOptions (13.0f, juce::Font::plain));
    effectiveTimeLabel.setColour (juce::Label::textColourId, PluginColours::mutedText);
    addAndMakeVisible (effectiveTimeLabel);

    configureLabel (presetLabel, "PRESET", juce::Justification::centredLeft);
    presetBox.addItemList (audioProcessor.getFactoryPresetNames(), 1);
    presetBox.setTextWhenNothingSelected ("Custom");
    presetBox.onChange = [this]
    {
        const auto selected = presetBox.getSelectedItemIndex();
        if (selected >= 0)
            audioProcessor.applyFactoryPreset (selected);
    };
    addAndMakeVisible (presetBox);

    configureHeaderButton (savePresetButton);
    configureHeaderButton (loadPresetButton);
    configureHeaderButton (resetButton);
    configureHeaderButton (presetFolderButton);
    configureHeaderButton (aboutButton);
    savePresetButton.onClick = [this] { showSavePresetDialog(); };
    loadPresetButton.onClick = [this] { showLoadPresetDialog(); };
    resetButton.onClick = [this]
    {
        audioProcessor.applyFactoryPreset (0);
        lastDisplayedPreset = -2;
    };
    presetFolderButton.onClick = [this] { revealPresetFolder(); };
    aboutButton.onClick = [this] { showAboutOverlay(); };
    addAndMakeVisible (savePresetButton);
    addAndMakeVisible (loadPresetButton);
    addAndMakeVisible (resetButton);
    addAndMakeVisible (presetFolderButton);
    addAndMakeVisible (aboutButton);

    configureKnob (semitonesSlider, " st", 2);
    configureKnob (repeatCountSlider, "", 0);
    configureKnob (delayTimeSlider, " s", 3);
    configureKnob (drySlider, " dB", 1);
    configureKnob (firstRepeatSlider, " dB", 1);
    configureKnob (decaySlider, " dB", 1);

    configureLabel (semitonesLabel, "PITCH / REPEAT", juce::Justification::centred);
    configureLabel (repeatCountLabel, "REPEATS", juce::Justification::centred);
    configureLabel (delayTimeLabel, "DELAY TIME", juce::Justification::centred);
    configureLabel (delayModeLabel, "REPEAT MODE", juce::Justification::centredLeft);
    configureLabel (pitchDirectionLabel, "PITCH DIRECTION", juce::Justification::centredLeft);
    configureLabel (qualityLabel, "PITCH QUALITY", juce::Justification::centredLeft);
    configureLabel (syncDivisionLabel, "DIVISION", juce::Justification::centredLeft);
    configureLabel (dryLabel, "ORIGINAL", juce::Justification::centred);
    configureLabel (firstRepeatLabel, "FIRST REPEAT", juce::Justification::centred);
    configureLabel (decayLabel, "REDUCTION / REPEAT", juce::Justification::centred);

    repeatCountSlider.setNumDecimalPlacesToDisplay (0);

    configureCombo (delayModeBox, ParameterChoices::delayModes);
    configureCombo (pitchDirectionBox, ParameterChoices::pitchDirections);
    configureCombo (qualityBox, ParameterChoices::qualities);
    configureCombo (syncDivisionBox, ParameterChoices::syncDivisions);

    addAndMakeVisible (tempoSyncButton);
    addAndMakeVisible (headroomButton);

    semitonesAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, ParameterIDs::semitones, semitonesSlider);
    repeatCountAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, ParameterIDs::repeatCount, repeatCountSlider);
    delayTimeAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, ParameterIDs::delayTime, delayTimeSlider);
    dryAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, ParameterIDs::dryDb, drySlider);
    firstRepeatAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, ParameterIDs::firstRepeatDb, firstRepeatSlider);
    decayAttachment = std::make_unique<SliderAttachment> (audioProcessor.parameters, ParameterIDs::decayDb, decaySlider);
    delayModeAttachment = std::make_unique<ComboAttachment> (audioProcessor.parameters, ParameterIDs::delayMode, delayModeBox);
    pitchDirectionAttachment = std::make_unique<ComboAttachment> (audioProcessor.parameters, ParameterIDs::pitchDirection, pitchDirectionBox);
    qualityAttachment = std::make_unique<ComboAttachment> (audioProcessor.parameters, ParameterIDs::quality, qualityBox);
    syncDivisionAttachment = std::make_unique<ComboAttachment> (audioProcessor.parameters, ParameterIDs::syncDivision, syncDivisionBox);
    tempoSyncAttachment = std::make_unique<ButtonAttachment> (audioProcessor.parameters, ParameterIDs::tempoSync, tempoSyncButton);
    headroomAttachment = std::make_unique<ButtonAttachment> (audioProcessor.parameters, ParameterIDs::headroom, headroomButton);

    configureTooltips();

    aboutOverlay.onClose = [this] { aboutOverlay.setVisible (false); };
    addChildComponent (aboutOverlay);

    startTimerHz (30);
}

ProgressivePitchDelayAudioProcessorEditor::~ProgressivePitchDelayAudioProcessorEditor()
{
    tooltipWindow.reset();
    setLookAndFeel (nullptr);
}

void ProgressivePitchDelayAudioProcessorEditor::configureKnob (juce::Slider& slider,
                                                                 const juce::String& suffix,
                                                                 int decimals)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 82, 22);
    slider.setTextValueSuffix (suffix);
    slider.setNumDecimalPlacesToDisplay (decimals);
    addAndMakeVisible (slider);
}

void ProgressivePitchDelayAudioProcessorEditor::configureCombo (juce::ComboBox& combo,
                                                                  const juce::StringArray& choices)
{
    combo.addItemList (choices, 1);
    addAndMakeVisible (combo);
}

void ProgressivePitchDelayAudioProcessorEditor::configureLabel (juce::Label& label,
                                                                  const juce::String& text,
                                                                  juce::Justification justification)
{
    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (justification);
    label.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, PluginColours::mutedText);
    addAndMakeVisible (label);
}

void ProgressivePitchDelayAudioProcessorEditor::configureHeaderButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, PluginColours::control);
    button.setColour (juce::TextButton::buttonOnColourId, PluginColours::control.brighter (0.15f));
    button.setColour (juce::TextButton::textColourOffId, PluginColours::text);
    button.setColour (juce::TextButton::textColourOnId, PluginColours::text);
}

void ProgressivePitchDelayAudioProcessorEditor::configureTooltips()
{
    presetBox.setTooltip ("Loads a complete factory setting. A loaded or saved user preset is shown here by name. Editing any control afterward changes the display to Custom.");
    savePresetButton.setTooltip ("Saves the current plug-in settings as a reusable .ppdpreset file.");
    loadPresetButton.setTooltip ("Loads a previously saved Progressive Pitch Delay .ppdpreset file.");
    resetButton.setTooltip ("Restores the factory Default preset.");
    presetFolderButton.setTooltip ("Reveals the most recently used custom preset folder in Finder.");
    aboutButton.setTooltip ("Shows version, developer, framework, and licensing information.");
    semitonesSlider.setTooltip ("Sets the cumulative pitch change added to each successive repeat, from 0 to 12 semitones.");
    repeatCountSlider.setTooltip ("Sets how many delayed repeats are generated, from 1 to 12.");
    delayTimeSlider.setTooltip ("Sets the spacing between repeats when Tempo Sync is off.");
    tempoSyncButton.setTooltip ("Uses Logic's project tempo instead of the Delay Time control.");
    syncDivisionBox.setTooltip ("Selects the musical note division used when Tempo Sync is on. T means triplet and D means dotted.");
    delayModeBox.setTooltip ("Normal creates continuous echoes. Reversed captures delay-sized blocks and plays each block backward.");
    pitchDirectionBox.setTooltip ("Chooses whether each repeat moves progressively lower or higher in pitch.");
    qualityBox.setTooltip ("High provides the cleanest pitch shifting. Draft reduces CPU use.");
    headroomButton.setTooltip ("Applies linked output protection to reduce clipping when repeats overlap.");
    drySlider.setTooltip ("Sets the level of the original unprocessed signal.");
    firstRepeatSlider.setTooltip ("Sets the level of the first delayed repeat.");
    decaySlider.setTooltip ("Sets how many decibels each repeat loses compared with the repeat before it.");
}

void ProgressivePitchDelayAudioProcessorEditor::showPresetError (const juce::String& message)
{
    juce::NativeMessageBox::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                  "Preset Error", message, this);
}

void ProgressivePitchDelayAudioProcessorEditor::showSavePresetDialog()
{
    auto presetName = audioProcessor.getCurrentUserPresetName();
    if (presetName.isEmpty())
        presetName = "My Preset";

    const auto legalName = juce::File::createLegalFileName (presetName);
    const auto initialFile = audioProcessor.getLastUserPresetDirectory()
                                     .getChildFile (legalName.isEmpty() ? "My Preset" : legalName)
                                     .withFileExtension (".ppdpreset");

    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Save Progressive Pitch Delay Preset", initialFile, "*.ppdpreset",
        true, false, this);

    const auto flags = juce::FileBrowserComponent::saveMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::warnAboutOverwriting;
    const juce::Component::SafePointer<ProgressivePitchDelayAudioProcessorEditor> safeThis (this);

    presetFileChooser->launchAsync (flags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file == juce::File {})
            return;

        juce::String errorMessage;
        if (! safeThis->audioProcessor.saveUserPreset (file, errorMessage))
            safeThis->showPresetError (errorMessage);
        else
            safeThis->lastDisplayedPreset = -2;
    });
}

void ProgressivePitchDelayAudioProcessorEditor::showLoadPresetDialog()
{
    presetFileChooser = std::make_unique<juce::FileChooser> (
        "Load Progressive Pitch Delay Preset",
        audioProcessor.getLastUserPresetDirectory(),
        "*.ppdpreset", true, false, this);

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles;
    const juce::Component::SafePointer<ProgressivePitchDelayAudioProcessorEditor> safeThis (this);

    presetFileChooser->launchAsync (flags, [safeThis] (const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto file = chooser.getResult();
        if (file == juce::File {})
            return;

        juce::String errorMessage;
        if (! safeThis->audioProcessor.loadUserPreset (file, errorMessage))
            safeThis->showPresetError (errorMessage);
        else
            safeThis->lastDisplayedPreset = -2;
    });
}

void ProgressivePitchDelayAudioProcessorEditor::revealPresetFolder()
{
    auto directory = audioProcessor.getLastUserPresetDirectory();
    const auto result = directory.createDirectory();

    if (result.failed())
    {
        showPresetError ("The preset folder could not be opened in Finder.");
        return;
    }

    directory.revealToUser();
}

void ProgressivePitchDelayAudioProcessorEditor::showAboutOverlay()
{
    aboutOverlay.setBounds (getLocalBounds());
    aboutOverlay.setVisible (true);
    aboutOverlay.toFront (false);
    aboutOverlay.grabKeyboardFocus();
}

void ProgressivePitchDelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (PluginColours::background);

    juce::ColourGradient headerGradient (PluginColours::accentTwo.withAlpha (0.24f), 0.0f, 0.0f,
                                          PluginColours::background, static_cast<float> (getWidth()), 96.0f,
                                          false);
    g.setGradientFill (headerGradient);
    g.fillRect (0, 0, getWidth(), 96);

    drawPanel (g, { 22, 104, getWidth() - 336, 304 }, "DELAY & PITCH");
    drawPanel (g, { getWidth() - 298, 104, 276, 304 }, "BEHAVIOUR");
    drawPanel (g, { 22, 426, getWidth() - 44, getHeight() - 448 }, "LEVELS");

    drawMeter (g, juce::Rectangle<float> (static_cast<float> (getWidth() - 178), 82.0f, 142.0f, 10.0f));
}

void ProgressivePitchDelayAudioProcessorEditor::drawPanel (juce::Graphics& g,
                                                             juce::Rectangle<int> bounds,
                                                             const juce::String& title) const
{
    auto area = bounds.toFloat();
    g.setColour (PluginColours::panel);
    g.fillRoundedRectangle (area, 14.0f);
    g.setColour (PluginColours::panelBorder.withAlpha (0.8f));
    g.drawRoundedRectangle (area.reduced (0.5f), 14.0f, 1.0f);

    g.setColour (PluginColours::mutedText);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (title, bounds.reduced (18, 10).removeFromTop (18), juce::Justification::centredLeft);
}

void ProgressivePitchDelayAudioProcessorEditor::drawMeter (juce::Graphics& g,
                                                             juce::Rectangle<float> bounds) const
{
    g.setColour (PluginColours::control);
    g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);

    const auto db = juce::Decibels::gainToDecibels (juce::jmax (displayedPeak, 0.00001f), -60.0f);
    const auto normalised = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    auto levelBounds = bounds;
    levelBounds.setWidth (bounds.getWidth() * normalised);
    g.setColour (displayedPeak > 0.98f ? juce::Colour::fromRGB (255, 115, 112) : PluginColours::accent);
    g.fillRoundedRectangle (levelBounds, bounds.getHeight() * 0.5f);

    g.setColour (PluginColours::mutedText);
    g.setFont (juce::FontOptions (10.0f, juce::Font::plain));
    g.drawText ("OUT", bounds.translated (-34.0f, -4.0f).withWidth (30.0f), juce::Justification::centredRight);
}

void ProgressivePitchDelayAudioProcessorEditor::resized()
{
    constexpr int headerControlsWidth = 484;
    constexpr int headerRightMargin = 24;
    const auto presetX = getWidth() - headerControlsWidth - headerRightMargin;
    const auto titleWidth = juce::jmax (260, presetX - 42);
    titleLabel.setBounds (22, 13, titleWidth, 36);
    subtitleLabel.setBounds (24, 47, titleWidth, 22);

    auto headerX = presetX;
    presetLabel.setBounds (headerX, 4, 166, 16);
    presetBox.setBounds (headerX, 21, 166, 34);
    headerX += 172;
    savePresetButton.setBounds (headerX, 21, 52, 34);
    headerX += 58;
    loadPresetButton.setBounds (headerX, 21, 52, 34);
    headerX += 58;
    resetButton.setBounds (headerX, 21, 58, 34);
    headerX += 64;
    presetFolderButton.setBounds (headerX, 21, 58, 34);
    headerX += 64;
    aboutButton.setBounds (headerX, 21, 68, 34);
    effectiveTimeLabel.setBounds (presetX, 58, headerControlsWidth, 20);

    const auto leftPanel = juce::Rectangle<int> (22, 104, getWidth() - 336, 304);
    auto knobArea = leftPanel.reduced (20, 42);
    const auto knobWidth = knobArea.getWidth() / 3;

    auto placeKnob = [&] (juce::Slider& slider, juce::Label& label, int index)
    {
        auto cell = juce::Rectangle<int> (knobArea.getX() + index * knobWidth,
                                          knobArea.getY(), knobWidth, 178);
        label.setBounds (cell.removeFromTop (20));
        slider.setBounds (cell.reduced (10, 0));
    };

    placeKnob (semitonesSlider, semitonesLabel, 0);
    placeKnob (repeatCountSlider, repeatCountLabel, 1);
    placeKnob (delayTimeSlider, delayTimeLabel, 2);

    tempoSyncButton.setBounds (knobArea.getX() + 14, leftPanel.getBottom() - 66, 154, 32);
    syncDivisionLabel.setBounds (knobArea.getX() + 178, leftPanel.getBottom() - 82, 126, 18);
    syncDivisionBox.setBounds (knobArea.getX() + 178, leftPanel.getBottom() - 62, 126, 32);

    const auto rightX = getWidth() - 278;
    delayModeLabel.setBounds (rightX, 134, 236, 18);
    delayModeBox.setBounds (rightX, 154, 236, 38);
    pitchDirectionLabel.setBounds (rightX, 192, 236, 18);
    pitchDirectionBox.setBounds (rightX, 212, 236, 38);
    qualityLabel.setBounds (rightX, 250, 236, 18);
    qualityBox.setBounds (rightX, 270, 236, 38);
    headroomButton.setBounds (rightX + 4, 334, 206, 32);

    const auto levelsPanel = juce::Rectangle<int> (22, 426, getWidth() - 44, getHeight() - 448);
    auto levels = levelsPanel.reduced (22, 30);
    const auto levelWidth = levels.getWidth() / 3;

    auto placeLevelKnob = [&] (juce::Slider& slider, juce::Label& label, int index)
    {
        auto cell = juce::Rectangle<int> (levels.getX() + index * levelWidth,
                                          levels.getY(), levelWidth, levels.getHeight());
        label.setBounds (cell.removeFromTop (18));
        slider.setBounds (cell);
    };

    placeLevelKnob (drySlider, dryLabel, 0);
    placeLevelKnob (firstRepeatSlider, firstRepeatLabel, 1);
    placeLevelKnob (decaySlider, decayLabel, 2);

    aboutOverlay.setBounds (getLocalBounds());
}

void ProgressivePitchDelayAudioProcessorEditor::timerCallback()
{
    displayedPeak = juce::jmax (audioProcessor.getOutputPeak(), displayedPeak * 0.88f);

    const auto syncEnabled = tempoSyncButton.getToggleState();
    delayTimeSlider.setEnabled (! syncEnabled);
    syncDivisionBox.setEnabled (syncEnabled);

    const auto selectedPreset = audioProcessor.getSelectedFactoryPreset();
    const auto userPresetName = audioProcessor.getCurrentUserPresetName();
    if (selectedPreset != lastDisplayedPreset || userPresetName != lastDisplayedUserPreset)
    {
        if (selectedPreset >= 0)
        {
            presetBox.setTextWhenNothingSelected ("Custom");
            presetBox.setSelectedItemIndex (selectedPreset, juce::dontSendNotification);
        }
        else
        {
            presetBox.setSelectedId (0, juce::dontSendNotification);
            presetBox.setTextWhenNothingSelected (userPresetName.isNotEmpty()
                                                    ? "User: " + userPresetName
                                                    : "Custom");
        }

        presetBox.setTooltip (userPresetName.isNotEmpty()
                                ? "Current user preset: " + userPresetName + ". Editing any control changes the display to Custom."
                                : "Loads a complete factory setting. Editing any control afterward changes the display to Custom.");

        lastDisplayedPreset = selectedPreset;
        lastDisplayedUserPreset = userPresetName;
    }

    const auto seconds = audioProcessor.getEffectiveDelaySeconds();
    const auto bpm = audioProcessor.getCurrentBpm();
    effectiveTimeLabel.setText (syncEnabled
                                  ? juce::String (seconds, 3) + " s per repeat | " + juce::String (bpm, 1) + " BPM"
                                  : juce::String (seconds, 3) + " s per repeat",
                                juce::dontSendNotification);
    repaint();
}
