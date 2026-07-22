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

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <array>
#include <functional>

#if JUCE_MAC
 #include <objc/message.h>
 #include <objc/runtime.h>
#endif

namespace
{
const juce::Colour pageBackground { 0xffeef3f9 };
const juce::Colour panelBackground { 0xffffffff };
const juce::Colour headingColour { 0xff172033 };
const juce::Colour bodyColour { 0xff526077 };
const juce::Colour accentColour { 0xff3769d3 };
const juce::Colour accentSoftColour { 0xffedf3ff };
const juce::Colour borderColour { 0xffd4dce8 };
const juce::String userPresetRootTag { "BinauralJourneyUserPresets" };
const juce::String userPresetTag { "Preset" };
const juce::String releaseVersionText { "1.0.0" };

constexpr int journeyWindowWidth = 940;
constexpr int journeyWindowMinimumHeight = 620;
constexpr int journeyWindowMaximumHeight = 960;

int journeyWindowHeightForStageCount (int stageCount)
{
    const int safeStageCount = juce::jlimit (2, 6, stageCount);

    // The user cannot resize this window. Instead, it grows and shrinks
    // automatically as stages are added or removed. The viewport remains in
    // place as a fallback on smaller displays.
    return juce::jlimit (journeyWindowMinimumHeight,
                         journeyWindowMaximumHeight,
                         540 + safeStageCount * 70);
}

#if JUCE_MAC
// Logic hosts plug-in editors inside its own floating native windows. JUCE's
// normal toFront() request can be immediately undone by the host after a button
// click. Attaching the Journey Builder as a native child window keeps it above
// this specific plug-in editor, while the explicit Cocoa front request makes an
// already-open Builder reliably receive focus.
static id getMacWindowForComponent (juce::Component& component)
{
    auto* peer = component.getPeer();

    if (peer == nullptr)
        return nil;

    id nativeView = reinterpret_cast<id> (peer->getNativeHandle());

    if (nativeView == nil)
        return nil;

    using SendId = id (*) (id, SEL);
    using SendBoolSelector = bool (*) (id, SEL, SEL);
    const auto respondsToSelector =
        reinterpret_cast<SendBoolSelector> (objc_msgSend);
    const SEL windowSelector = sel_registerName ("window");

    if (respondsToSelector (nativeView,
                            sel_registerName ("respondsToSelector:"),
                            windowSelector))
    {
        return reinterpret_cast<SendId> (objc_msgSend) (
            nativeView, windowSelector);
    }

    // Some JUCE peers may expose the NSWindow itself rather than its content
    // NSView. In that case the native handle is already what we need.
    return nativeView;
}

static void attachAndRaiseMacWindow (juce::Component& childComponent,
                                     juce::Component* parentComponent)
{
    id childWindow = getMacWindowForComponent (childComponent);

    if (childWindow == nil)
        return;

    using SendId = id (*) (id, SEL);
    using SendVoid = void (*) (id, SEL);
    using SendVoidId = void (*) (id, SEL, id);
    using SendVoidIdInteger = void (*) (id, SEL, id, long);

    const auto sendId = reinterpret_cast<SendId> (objc_msgSend);
    const auto sendVoid = reinterpret_cast<SendVoid> (objc_msgSend);
    const auto sendVoidId = reinterpret_cast<SendVoidId> (objc_msgSend);
    const auto sendVoidIdInteger =
        reinterpret_cast<SendVoidIdInteger> (objc_msgSend);

    if (parentComponent != nullptr)
    {
        id parentWindow = getMacWindowForComponent (*parentComponent);

        if (parentWindow != nil && parentWindow != childWindow)
        {
            id currentParent = sendId (
                childWindow, sel_registerName ("parentWindow"));

            if (currentParent != parentWindow)
            {
                if (currentParent != nil)
                {
                    sendVoidId (currentParent,
                                sel_registerName ("removeChildWindow:"),
                                childWindow);
                }

                // NSWindowAbove == 1. Avoid importing AppKit into this C++
                // translation unit merely for this stable enum value.
                sendVoidIdInteger (parentWindow,
                                   sel_registerName ("addChildWindow:ordered:"),
                                   childWindow, 1L);
            }
        }
    }

    sendVoidId (childWindow,
                sel_registerName ("makeKeyAndOrderFront:"), nil);
    sendVoid (childWindow, sel_registerName ("orderFrontRegardless"));
}

static void detachMacChildWindow (juce::Component& childComponent)
{
    id childWindow = getMacWindowForComponent (childComponent);

    if (childWindow == nil)
        return;

    using SendId = id (*) (id, SEL);
    using SendVoidId = void (*) (id, SEL, id);
    const auto sendId = reinterpret_cast<SendId> (objc_msgSend);
    const auto sendVoidId = reinterpret_cast<SendVoidId> (objc_msgSend);
    id parentWindow = sendId (childWindow, sel_registerName ("parentWindow"));

    if (parentWindow != nil)
    {
        sendVoidId (parentWindow, sel_registerName ("removeChildWindow:"),
                    childWindow);
    }
}
#endif

juce::XmlElement* findUserPresetByName (juce::XmlElement& root,
                                        const juce::String& name)
{
    for (auto* child = root.getFirstChildElement(); child != nullptr;
         child = child->getNextElement())
    {
        if (child->hasTagName (userPresetTag)
            && child->getStringAttribute ("name").equalsIgnoreCase (name))
        {
            return child;
        }
    }

    return nullptr;
}

juce::String formatDurationMinutes (double minutes)
{
    const double safeMinutes = juce::jmax (0.0, minutes);
    return juce::String (safeMinutes, 1) + " min";
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

struct EditorLayout
{
    juce::Rectangle<int> header;
    juce::Rectangle<int> tabs;
    juce::Rectangle<int> contentCard;
    juce::Rectangle<int> footer;
};

EditorLayout calculateEditorLayout (juce::Rectangle<int> bounds)
{
    auto content = bounds.reduced (16);

    EditorLayout layout;
    layout.header = content.removeFromTop (48);
    content.removeFromTop (6);
    layout.tabs = content.removeFromTop (34);
    content.removeFromTop (8);
    layout.footer = content.removeFromBottom (24);
    content.removeFromBottom (6);
    layout.contentCard = content;

    return layout;
}

class HelpAboutOverlay final : public juce::Component
{
public:
    explicit HelpAboutOverlay (std::function<void()> closeCallback)
        : closeRequested (std::move (closeCallback))
    {
        setWantsKeyboardFocus (true);

        title.setText ("BinauralJourney Help and About",
                       juce::dontSendNotification);
        title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, headingColour);
        title.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (title);

        subtitle.setText ("v" + releaseVersionText + "  -  Created by jwesters",
                          juce::dontSendNotification);
        subtitle.setFont (juce::FontOptions (12.5f, juce::Font::bold));
        subtitle.setColour (juce::Label::textColourId, bodyColour);
        subtitle.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (subtitle);

        helpText.setMultiLine (true, true);
        helpText.setReadOnly (true);
        helpText.setScrollbarsShown (true);
        helpText.setCaretVisible (false);
        helpText.setPopupMenuEnabled (false);
        helpText.setFont (juce::FontOptions (14.0f));
        helpText.setColour (juce::TextEditor::backgroundColourId,
                            panelBackground);
        helpText.setColour (juce::TextEditor::textColourId, headingColour);
        helpText.setColour (juce::TextEditor::outlineColourId,
                            juce::Colours::transparentBlack);
        helpText.setColour (juce::TextEditor::focusedOutlineColourId,
                            juce::Colours::transparentBlack);
        helpText.setColour (juce::TextEditor::shadowColourId,
                            juce::Colours::transparentBlack);
        helpText.setText (
            "SETUP\n"
            "Choose Single tone for one steady beat, or Journey for 2 to 6 "
            "stages. Follow Logic transport keeps playback aligned with "
            "Logic's playhead.\n\n"
            "TONE AND PITCH\n"
            "Set the anchor by frequency or musical note. In Single tone "
            "mode, Beat frequency sets the left/right difference. In Journey "
            "mode, each stage controls its own beat rate in the Journey "
            "Builder.\n\n"
            "MIX AND OUTPUT\n"
            "Balance tone, ambient noise, channels, master output, and fades. "
            "Ambient noise starts at None.\n\n"
            "LOGIC EXPORT\n"
            "Set Logic's Cycle range to the exact journey length before Bounce "
            "or Share Song to Music. Realtime and Offline bounce are both "
            "supported.\n\n"
            "HEADPHONE SAFETY\n"
            "Begin at a low listening level and avoid prolonged loud playback.\n\n"
            "Built with JUCE",
            false);
        addAndMakeVisible (helpText);

        closeButton.setButtonText ("Close");
        closeButton.setTooltip ("Close Help and About");
        closeButton.setColour (juce::TextButton::buttonColourId, accentColour);
        closeButton.setColour (juce::TextButton::buttonOnColourId, accentColour);
        closeButton.setColour (juce::TextButton::textColourOffId,
                               juce::Colours::white);
        closeButton.setColour (juce::TextButton::textColourOnId,
                               juce::Colours::white);
        closeButton.onClick = [this] { requestClose(); };
        addAndMakeVisible (closeButton);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.42f));

        const auto panel = panelBounds.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.20f));
        g.fillRoundedRectangle (panel.translated (0.0f, 4.0f), 13.0f);

        g.setColour (panelBackground);
        g.fillRoundedRectangle (panel, 13.0f);

        g.setColour (borderColour);
        g.drawRoundedRectangle (panel, 13.0f, 1.0f);
    }

    void resized() override
    {
        auto available = getLocalBounds().reduced (22);
        const int panelWidth = juce::jmin (690, available.getWidth());
        const int panelHeight = juce::jmin (535, available.getHeight());

        panelBounds = juce::Rectangle<int> (0, 0, panelWidth, panelHeight)
                          .withCentre (available.getCentre());

        auto inner = panelBounds.reduced (22);
        auto titleRow = inner.removeFromTop (38);
        closeButton.setBounds (titleRow.removeFromRight (88).reduced (0, 3));
        title.setBounds (titleRow);

        subtitle.setBounds (inner.removeFromTop (22));
        inner.removeFromTop (8);
        helpText.setBounds (inner);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            requestClose();
            return true;
        }

        return juce::Component::keyPressed (key);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (! panelBounds.contains (event.getPosition()))
            requestClose();
    }

private:
    void requestClose()
    {
        if (closeRequested)
            closeRequested();
        else
            setVisible (false);
    }

    std::function<void()> closeRequested;
    juce::Rectangle<int> panelBounds;
    juce::Label title;
    juce::Label subtitle;
    juce::TextEditor helpText;
    juce::TextButton closeButton;
};

class JourneyEditorComponent : public juce::Component,
                               public juce::DragAndDropContainer,
                               public juce::DragAndDropTarget,
                               private juce::Timer
{
public:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    explicit JourneyEditorComponent (
        BinauralJourneyAudioProcessor& processor,
        std::function<void()> closeCallback = {},
        std::function<void(int)> stageCountCallback = {})
        : audioProcessor (processor),
          parameters (processor.getParameterState()),
          tooltipWindow (this, 650),
          closeRequested (std::move (closeCallback)),
          stageCountChanged (std::move (stageCountCallback))
    {
        setSize (880, 900);

        title.setText ("Journey builder", juce::dontSendNotification);
        title.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, headingColour);
        addAndMakeVisible (title);

        closeButton.setButtonText ("x");
        closeButton.setTooltip ("Close journey editor");
        closeButton.setColour (juce::TextButton::buttonColourId,
                               juce::Colours::transparentBlack);
        closeButton.setColour (juce::TextButton::buttonOnColourId,
                               accentSoftColour);
        closeButton.setColour (juce::TextButton::textColourOffId, bodyColour);
        closeButton.setColour (juce::TextButton::textColourOnId, headingColour);
        closeButton.onClick = [this]
        {
            if (closeRequested)
            {
                closeRequested();
                return;
            }

            if (auto* callOut = findParentComponentOfClass<juce::CallOutBox>())
                callOut->dismiss();
        };
        addAndMakeVisible (closeButton);

        help.setText (
            "Choose from 2 to 6 stages. Drag a Stage handle to reorder; Copy or delete as needed.",
            juce::dontSendNotification);
        help.setFont (juce::FontOptions (12.5f));
        help.setColour (juce::Label::textColourId, bodyColour);
        addAndMakeVisible (help);

        configureLabel (journeyPresetLabel, "Journey template");
        addAndMakeVisible (journeyPresetLabel);

        journeyPresetBox.addItem ("Choose a journey...", 1);
        journeyPresetBox.addItem ("Sleep descent", 2);
        journeyPresetBox.addItem ("Meditation flow", 3);
        journeyPresetBox.addItem ("Focus arc", 4);
        journeyPresetBox.addItem ("Gentle relaxation", 5);
        journeyPresetBox.setSelectedId (1, juce::dontSendNotification);
        configureComboBox (journeyPresetBox);
        journeyPresetBox.setTooltip (
            "Apply a complete journey template. Every stage remains editable.");
        addAndMakeVisible (journeyPresetBox);

        journeyPresetHint.setText (
            "Selecting a template applies it immediately; every value stays editable.",
            juce::dontSendNotification);
        journeyPresetHint.setFont (juce::FontOptions (11.5f));
        journeyPresetHint.setColour (juce::Label::textColourId, bodyColour);
        journeyPresetHint.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (journeyPresetHint);

        configureLabel (stageCountLabel, "Number of stages");
        addAndMakeVisible (stageCountLabel);

        for (int count = minimumStages; count <= maximumStages; ++count)
            stageCountBox.addItem (juce::String (count), count - minimumStages + 1);

        configureComboBox (stageCountBox);
        stageCountBox.setTooltip (
            "Choose between 2 and 6 stages. The window height adjusts automatically.");
        addAndMakeVisible (stageCountBox);

        configureLabel (transitionCurveLabel, "Transition curve");
        addAndMakeVisible (transitionCurveLabel);

        transitionCurveBox.addItem ("Linear", 1);
        transitionCurveBox.addItem ("Smooth", 2);
        transitionCurveBox.addItem ("Extra gentle", 3);
        configureComboBox (transitionCurveBox);
        transitionCurveBox.setTooltip (
            "Choose how smoothly the beat rate moves between stages.");
        addAndMakeVisible (transitionCurveBox);

        transitionCurveHint.setText (
            "Smooth and Extra gentle ease into and out of every transition.",
            juce::dontSendNotification);
        transitionCurveHint.setFont (juce::FontOptions (11.5f));
        transitionCurveHint.setColour (juce::Label::textColourId, bodyColour);
        transitionCurveHint.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (transitionCurveHint);

        total.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        total.setColour (juce::Label::textColourId, headingColour);
        total.setColour (juce::Label::backgroundColourId, accentSoftColour);
        total.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (total);

        configureLabel (actionHeader, "Actions");
        configureLabel (presetHeader, "Beat type");
        configureLabel (beatHeader, "Rate");
        configureLabel (holdHeader, "Hold (minutes)");
        configureLabel (transitionHeader, "Transition to next (minutes)");
        configureLabel (previewHeader, "Journey preview (beat rate over time)");
        addAndMakeVisible (actionHeader);
        addAndMakeVisible (presetHeader);
        addAndMakeVisible (beatHeader);
        addAndMakeVisible (holdHeader);
        addAndMakeVisible (transitionHeader);
        addAndMakeVisible (previewHeader);

        for (int stage = 0; stage < maximumStages; ++stage)
        {
            auto& controls = stages[stage];
            controls.dragHandle = std::make_unique<StageDragHandle> (*this, stage);
            configureBeatPresetBox (controls.preset);
            configureBeatSlider (controls.beat, defaultBeatForStage (stage));
            configureMinutesSlider (controls.hold, defaultHoldForStage (stage));
            configureMinutesSlider (controls.transition,
                                    defaultTransitionForStage (stage));
            controls.preset.setTooltip (
                "Choose a named beat type or Custom for an exact rate.");
            controls.beat.setTooltip (
                "Beat frequency for this stage. Enabled when Beat type is Custom.");
            controls.hold.setTooltip (
                "How long this stage stays at its selected beat rate, in minutes.");
            controls.transition.setTooltip (
                "How long the change to the next stage takes, in minutes.");
            configureStageButton (controls.duplicate, "Copy",
                                  "Duplicate this stage after itself");
            configureStageButton (controls.remove, "Del",
                                  "Remove this stage");

            addAndMakeVisible (*controls.dragHandle);
            addAndMakeVisible (controls.duplicate);
            addAndMakeVisible (controls.remove);
            addAndMakeVisible (controls.preset);
            addAndMakeVisible (controls.beat);
            addAndMakeVisible (controls.hold);
            addAndMakeVisible (controls.transition);

            const auto prefix = stagePrefix (stage);
            controls.presetAttachment = std::make_unique<ComboBoxAttachment> (
                parameters, prefix + "BeatPreset", controls.preset);
            controls.beatAttachment = std::make_unique<SliderAttachment> (
                parameters, prefix + "Beat", controls.beat);
            controls.holdAttachment = std::make_unique<SliderAttachment> (
                parameters, prefix + "HoldMinutes", controls.hold);

            if (stage < maximumStages - 1)
            {
                controls.transitionAttachment = std::make_unique<SliderAttachment> (
                    parameters, prefix + "TransitionMinutes", controls.transition);
            }

            controls.preset.onChange = [this, stage]
            {
                applyStagePreset (stage);
            };

            controls.beat.onValueChange = [this, stage]
            {
                handleBeatValueChange (stage);
            };

            controls.hold.onValueChange = [this]
            {
                updateTotal();
                repaint (timelineBounds);
            };

            controls.transition.onValueChange = [this]
            {
                updateTotal();
                repaint (timelineBounds);
            };

            controls.duplicate.onClick = [this, stage]
            {
                duplicateStage (stage);
            };

            controls.remove.onClick = [this, stage]
            {
                removeStage (stage);
            };
        }

        stageCountAttachment = std::make_unique<ComboBoxAttachment> (
            parameters, "journeyStageCount", stageCountBox);
        transitionCurveAttachment = std::make_unique<ComboBoxAttachment> (
            parameters, "transitionCurve", transitionCurveBox);

        previousActiveStageCount = getActiveStageCount();

        stageCountBox.onChange = [this]
        {
            const int newStageCount = getActiveStageCount();

            if (newStageCount > previousActiveStageCount)
            {
                // A stage that used to be the final stage may have a zero
                // transition. Give each newly-exposed transition a sensible
                // default without overwriting transitions that were already
                // active or deliberately edited.
                for (int stage = previousActiveStageCount - 1;
                     stage < newStageCount - 1; ++stage)
                {
                    if (stage >= 0
                        && stage < maximumStages - 1
                        && stages[stage].transition.getValue() <= 0.0)
                    {
                        stages[stage].transition.setValue (
                            defaultTransitionForStage (stage),
                            juce::sendNotificationSync);
                    }
                }
            }

            previousActiveStageCount = newStageCount;
            updateStageVisibility();
            updateTotal();
            resized();
            repaint();

            if (stageCountChanged)
                stageCountChanged (newStageCount);
        };

        transitionCurveBox.onChange = [this]
        {
            repaint (timelineBounds);
        };

        journeyPresetBox.onChange = [this]
        {
            applySelectedJourneyPreset();
        };

        for (int stage = 0; stage < maximumStages; ++stage)
            applyStagePreset (stage);

        updateStageVisibility();
        updatePresetControls();
        updateTotal();
        startTimerHz (5);
    }

    ~JourneyEditorComponent() override
    {
        stopTimer();
    }

    void visibilityChanged() override
    {
        if (isShowing())
            refreshStageDragHandles();
    }

    void parentHierarchyChanged() override
    {
        if (isShowing())
            refreshStageDragHandles();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panelBackground);
        g.setColour (borderColour);
        g.drawRoundedRectangle (
            getLocalBounds().toFloat().reduced (0.5f), 10.0f, 1.0f);

        if (! columnHeaderBounds.isEmpty())
        {
            g.setColour (accentSoftColour);
            g.fillRoundedRectangle (columnHeaderBounds.toFloat(), 6.0f);
        }

        if (dragTargetStage >= 0
            && dragTargetStage < getActiveStageCount()
            && ! stageRowBounds[dragTargetStage].isEmpty())
        {
            const auto highlight = stageRowBounds[dragTargetStage].toFloat();
            g.setColour (accentColour.withAlpha (0.10f));
            g.fillRoundedRectangle (highlight, 6.0f);
            g.setColour (accentColour.withAlpha (0.72f));
            g.drawRoundedRectangle (highlight.reduced (0.75f), 6.0f, 1.5f);
        }

        drawJourneyPreview (g);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (18);
        auto titleRow = area.removeFromTop (29);
        closeButton.setBounds (titleRow.removeFromRight (28).reduced (1));
        title.setBounds (titleRow);
        help.setBounds (area.removeFromTop (22));
        area.removeFromTop (5);

        auto templateRow = area.removeFromTop (38);
        journeyPresetLabel.setBounds (templateRow.removeFromLeft (116));
        templateRow.removeFromLeft (8);
        journeyPresetBox.setBounds (templateRow.removeFromLeft (220).reduced (0, 5));
        templateRow.removeFromLeft (12);
        journeyPresetHint.setBounds (templateRow);

        area.removeFromTop (4);
        auto setupRow = area.removeFromTop (38);
        stageCountLabel.setBounds (setupRow.removeFromLeft (116));
        setupRow.removeFromLeft (8);
        stageCountBox.setBounds (setupRow.removeFromLeft (86).reduced (0, 5));
        setupRow.removeFromLeft (18);
        transitionCurveLabel.setBounds (setupRow.removeFromLeft (120));
        setupRow.removeFromLeft (8);
        transitionCurveBox.setBounds (setupRow.removeFromLeft (170).reduced (0, 5));
        setupRow.removeFromLeft (12);
        transitionCurveHint.setBounds (setupRow);

        area.removeFromTop (7);
        total.setBounds (area.removeFromTop (32));
        area.removeFromTop (8);

        constexpr int stageWidth = 94;
        constexpr int actionWidth = 92;
        constexpr int presetWidth = 150;
        constexpr int controlWidth = 150;
        constexpr int gap = 6;

        auto setHeaderColumns = [&] (juce::Rectangle<int> row)
        {
            // Keep Rate, Hold, and Transition in equal-width columns.
            // The small horizontal reduction centres the fixed-width grid.
            row = row.reduced (14, 0);
            row.removeFromLeft (stageWidth);
            row.removeFromLeft (gap);
            actionHeader.setBounds (row.removeFromLeft (actionWidth));
            row.removeFromLeft (gap);
            presetHeader.setBounds (row.removeFromLeft (presetWidth));
            row.removeFromLeft (gap);
            beatHeader.setBounds (row.removeFromLeft (controlWidth));
            row.removeFromLeft (gap);
            holdHeader.setBounds (row.removeFromLeft (controlWidth));
            row.removeFromLeft (gap);
            transitionHeader.setBounds (row.removeFromLeft (controlWidth));
        };

        auto setStageRow = [&] (juce::Rectangle<int> row,
                                StageControls& controls,
                                bool hasTransition)
        {
            row = row.reduced (14, 0);

            if (controls.dragHandle != nullptr)
            {
                controls.dragHandle->setBounds (
                    row.removeFromLeft (stageWidth).reduced (0, 15));
            }
            else
            {
                row.removeFromLeft (stageWidth);
            }

            row.removeFromLeft (gap);

            auto actionArea = row.removeFromLeft (actionWidth).reduced (0, 16);
            constexpr int actionGap = 4;
            const int actionButtonWidth =
                (actionArea.getWidth() - actionGap) / 2;
            controls.duplicate.setBounds (
                actionArea.removeFromLeft (actionButtonWidth));
            actionArea.removeFromLeft (actionGap);
            controls.remove.setBounds (actionArea);

            row.removeFromLeft (gap);
            controls.preset.setBounds (
                row.removeFromLeft (presetWidth).reduced (0, 16));
            row.removeFromLeft (gap);

            // All three adjustable controls receive the same-sized cell.
            // Rate uses a vertical slider; Hold and Transition remain horizontal.
            controls.beat.setBounds (row.removeFromLeft (controlWidth));
            row.removeFromLeft (gap);
            controls.hold.setBounds (row.removeFromLeft (controlWidth));
            row.removeFromLeft (gap);

            if (hasTransition)
                controls.transition.setBounds (row.removeFromLeft (controlWidth));
            else
                controls.transition.setBounds (juce::Rectangle<int>());
        };

        columnHeaderBounds = area.removeFromTop (28);
        setHeaderColumns (columnHeaderBounds);
        area.removeFromTop (5);

        const int activeStages = getActiveStageCount();

        for (auto& bounds : stageRowBounds)
            bounds = {};

        for (int stage = 0; stage < activeStages; ++stage)
        {
            auto stageRow = area.removeFromTop (72);
            stageRowBounds[stage] = stageRow;
            setStageRow (stageRow, stages[stage],
                         stage < activeStages - 1);

            if (stage < activeStages - 1)
                area.removeFromTop (8);
        }

        area.removeFromTop (10);
        previewHeader.setBounds (area.removeFromTop (22));
        timelineBounds = area.removeFromTop (96);
    }

    bool isInterestedInDragSource (
        const juce::DragAndDropTarget::SourceDetails& details) override
    {
        return details.description.toString().startsWith ("stage:");
    }

    void itemDragEnter (
        const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateDragTarget (details.localPosition.y);
    }

    void itemDragMove (
        const juce::DragAndDropTarget::SourceDetails& details) override
    {
        updateDragTarget (details.localPosition.y);
    }

    void itemDragExit (
        const juce::DragAndDropTarget::SourceDetails&) override
    {
        dragTargetStage = -1;
        repaint();
    }

    void itemDropped (
        const juce::DragAndDropTarget::SourceDetails& details) override
    {
        const auto description = details.description.toString();
        const int sourceStage = description.fromFirstOccurrenceOf (
            "stage:", false, false).getIntValue();
        const int destinationStage = stageIndexForY (details.localPosition.y);
        dragTargetStage = -1;

        if (destinationStage >= 0)
            reorderStage (sourceStage, destinationStage);
        else
            repaint();
    }

private:
    static constexpr int minimumStages = 2;
    static constexpr int maximumStages = 6;

    class StageDragHandle : public juce::Component,
                            private juce::SettableTooltipClient
    {
    public:
        StageDragHandle (JourneyEditorComponent& editor, int index)
            : owner (editor), stageIndex (index)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip ("Drag Stage " + juce::String (stageIndex + 1)
                        + " to change its position");
        }

        void paint (juce::Graphics& g) override
        {
            const auto bounds = getLocalBounds().toFloat();
            g.setColour (panelBackground);
            g.fillRoundedRectangle (bounds, 5.0f);
            g.setColour (borderColour);
            g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

            auto content = getLocalBounds().reduced (8, 5);
            auto grip = content.removeFromLeft (15);
            g.setColour (bodyColour.withAlpha (0.72f));
            const float centreY = static_cast<float> (grip.getCentreY());
            for (int line = -1; line <= 1; ++line)
            {
                const float y = centreY + static_cast<float> (line * 5);
                g.drawLine (static_cast<float> (grip.getX()), y,
                            static_cast<float> (grip.getRight()), y, 1.4f);
            }

            content.removeFromLeft (5);
            g.setColour (headingColour);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("Stage " + juce::String (stageIndex + 1), content,
                        juce::Justification::centredLeft);
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            dragStarted = false;
        }

        void mouseDrag (const juce::MouseEvent& event) override
        {
            if (! dragStarted && event.getDistanceFromDragStart() > 4)
            {
                dragStarted = true;
                owner.beginStageDrag (stageIndex, this);
            }
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragStarted = false;
        }

    private:
        JourneyEditorComponent& owner;
        int stageIndex = 0;
        bool dragStarted = false;
    };

    struct StageControls
    {
        std::unique_ptr<StageDragHandle> dragHandle;
        juce::TextButton duplicate;
        juce::TextButton remove;
        juce::ComboBox preset;
        juce::Slider beat;
        juce::Slider hold;
        juce::Slider transition;
        std::unique_ptr<ComboBoxAttachment> presetAttachment;
        std::unique_ptr<SliderAttachment> beatAttachment;
        std::unique_ptr<SliderAttachment> holdAttachment;
        std::unique_ptr<SliderAttachment> transitionAttachment;
    };

    struct StageSnapshot
    {
        int presetId = 1;
        double beat = 10.0;
        double hold = 5.0;
        double transition = 1.0;
    };

    static juce::String stagePrefix (int stage)
    {
        return juce::String ("stage") + juce::String (stage + 1);
    }

    static double defaultBeatForStage (int stage) noexcept
    {
        constexpr double values[maximumStages] = {
            10.0, 6.0, 2.0, 10.0, 6.0, 2.0
        };
        return values[juce::jlimit (0, maximumStages - 1, stage)];
    }

    static double defaultHoldForStage (int stage) noexcept
    {
        constexpr double values[maximumStages] = {
            5.0, 10.0, 15.0, 5.0, 5.0, 5.0
        };
        return values[juce::jlimit (0, maximumStages - 1, stage)];
    }

    static double defaultTransitionForStage (int stage) noexcept
    {
        constexpr double values[maximumStages - 1] = {
            1.0, 2.0, 1.0, 1.0, 1.0
        };

        if (stage >= maximumStages - 1)
            return 0.0;

        return values[juce::jlimit (0, maximumStages - 2, stage)];
    }

    int getActiveStageCount() const noexcept
    {
        const int selectedId = stageCountBox.getSelectedId();

        if (selectedId > 0)
        {
            return juce::jlimit (minimumStages, maximumStages,
                                 minimumStages + selectedId - 1);
        }

        // ComboBoxAttachment may update the visible selection just after the
        // callout's first layout pass. Reading the parameter here prevents the
        // editor from falling back to three visible rows when a saved journey
        // actually contains four, five, or six stages.
        if (const auto* value = parameters.getRawParameterValue (
                "journeyStageCount"))
        {
            return juce::jlimit (minimumStages, maximumStages,
                                 minimumStages
                                     + juce::roundToInt (value->load()));
        }

        return 3;
    }

    void setActiveStageCount (int count)
    {
        const int safeCount = juce::jlimit (minimumStages, maximumStages, count);
        stageCountBox.setSelectedId (
            safeCount - minimumStages + 1, juce::sendNotificationSync);
    }

    void updateStageVisibility()
    {
        const int activeStages = getActiveStageCount();

        for (int stage = 0; stage < maximumStages; ++stage)
        {
            const bool visible = stage < activeStages;
            if (stages[stage].dragHandle != nullptr)
                stages[stage].dragHandle->setVisible (visible);
            stages[stage].duplicate.setVisible (visible);
            stages[stage].remove.setVisible (visible);
            stages[stage].preset.setVisible (visible);
            stages[stage].beat.setVisible (visible);
            stages[stage].hold.setVisible (visible);
            stages[stage].transition.setVisible (
                visible && stage < activeStages - 1);

            stages[stage].duplicate.setEnabled (
                visible && activeStages < maximumStages);
            stages[stage].remove.setEnabled (
                visible && activeStages > minimumStages);
        }
    }

    void refreshStageDragHandles()
    {
        updateStageVisibility();
        resized();

        const int activeStages = getActiveStageCount();

        for (int stage = 0; stage < maximumStages; ++stage)
        {
            if (auto* handle = stages[stage].dragHandle.get())
            {
                const bool active = stage < activeStages;
                handle->setEnabled (active);
                handle->setInterceptsMouseClicks (active, false);

                if (active)
                    handle->toFront (false);

                handle->repaint();
            }
        }

        repaint();
    }

    void beginStageDrag (int stage, juce::Component* sourceComponent)
    {
        if (sourceComponent == nullptr
            || stage < 0
            || stage >= getActiveStageCount())
        {
            return;
        }

        dragTargetStage = stage;
        repaint();
        startDragging ("stage:" + juce::String (stage), sourceComponent);
    }

    int stageIndexForY (int y) const
    {
        const int activeStages = getActiveStageCount();

        if (activeStages <= 0)
            return -1;

        for (int stage = 0; stage < activeStages; ++stage)
        {
            const auto row = stageRowBounds[stage];
            if (! row.isEmpty() && y < row.getCentreY())
                return stage;
        }

        return activeStages - 1;
    }

    void updateDragTarget (int y)
    {
        const int newTarget = stageIndexForY (y);

        if (newTarget != dragTargetStage)
        {
            dragTargetStage = newTarget;
            repaint();
        }
    }

    void drawJourneyPreview (juce::Graphics& g)
    {
        if (timelineBounds.isEmpty())
            return;

        const int activeStages = getActiveStageCount();
        std::array<double, maximumStages> beats {};
        std::array<double, maximumStages> holds {};
        std::array<double, maximumStages - 1> transitions {};
        double totalMinutes = 0.0;

        for (int stage = 0; stage < activeStages; ++stage)
        {
            beats[stage] = stages[stage].beat.getValue();
            holds[stage] = stages[stage].hold.getValue();
            totalMinutes += holds[stage];

            if (stage < activeStages - 1)
            {
                transitions[stage] = stages[stage].transition.getValue();
                totalMinutes += transitions[stage];
            }
        }

        auto outer = timelineBounds.toFloat();
        g.setColour (accentSoftColour);
        g.fillRoundedRectangle (outer, 7.0f);
        g.setColour (borderColour);
        g.drawRoundedRectangle (outer.reduced (0.5f), 7.0f, 1.0f);

        if (totalMinutes <= 0.0)
            return;

        auto graph = outer.reduced (13.0f, 9.0f);
        auto labels = graph.removeFromBottom (18.0f);
        graph.removeFromBottom (3.0f);

        double minimumBeat = beats[0];
        double maximumBeat = beats[0];

        for (int stage = 1; stage < activeStages; ++stage)
        {
            minimumBeat = juce::jmin (minimumBeat, beats[stage]);
            maximumBeat = juce::jmax (maximumBeat, beats[stage]);
        }

        if (maximumBeat - minimumBeat < 1.0)
        {
            minimumBeat -= 0.5;
            maximumBeat += 0.5;
        }
        else
        {
            const double padding = (maximumBeat - minimumBeat) * 0.15;
            minimumBeat = juce::jmax (0.0, minimumBeat - padding);
            maximumBeat += padding;
        }

        const auto xForMinutes = [&] (double minutes)
        {
            return graph.getX()
                 + static_cast<float> (minutes / totalMinutes)
                   * graph.getWidth();
        };

        const auto yForBeat = [&] (double beat)
        {
            const double normalised = (beat - minimumBeat)
                                    / (maximumBeat - minimumBeat);
            return graph.getBottom()
                 - static_cast<float> (normalised) * graph.getHeight();
        };

        g.setColour (borderColour.withAlpha (0.8f));
        g.drawHorizontalLine (juce::roundToInt (graph.getCentreY()),
                              graph.getX(), graph.getRight());

        const auto shapeProgress = [this] (double progress)
        {
            const double p = juce::jlimit (0.0, 1.0, progress);

            switch (transitionCurveBox.getSelectedId())
            {
                case 2: return p * p * (3.0 - 2.0 * p);
                case 3: return p * p * p * (p * (p * 6.0 - 15.0) + 10.0);
                case 1:
                default: return p;
            }
        };

        juce::Path path;
        double cursor = 0.0;
        path.startNewSubPath (xForMinutes (0.0), yForBeat (beats[0]));

        for (int stage = 0; stage < activeStages; ++stage)
        {
            cursor += holds[stage];
            path.lineTo (xForMinutes (cursor), yForBeat (beats[stage]));

            if (stage < activeStages - 1 && transitions[stage] > 0.0)
            {
                constexpr int curveSteps = 28;

                for (int step = 1; step <= curveSteps; ++step)
                {
                    const double rawProgress = static_cast<double> (step)
                                             / static_cast<double> (curveSteps);
                    const double shaped = shapeProgress (rawProgress);
                    path.lineTo (
                        xForMinutes (cursor + rawProgress * transitions[stage]),
                        yForBeat (beats[stage]
                                  + shaped * (beats[stage + 1] - beats[stage])));
                }

                cursor += transitions[stage];
            }
        }

        g.setColour (accentColour);
        g.strokePath (path, juce::PathStrokeType (2.5f));

        const auto drawMarker = [&] (double minutes,
                                     double beat,
                                     int stageNumber)
        {
            const float x = xForMinutes (minutes);
            const float y = yForBeat (beat);
            g.setColour (panelBackground);
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            g.setColour (accentColour);
            g.drawEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.5f);
            g.setColour (headingColour);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawText ("S" + juce::String (stageNumber) + " "
                            + juce::String (beat, 2) + " Hz",
                        juce::Rectangle<int> (juce::roundToInt (x - 40.0f),
                                              juce::roundToInt (y - 19.0f),
                                              80, 14),
                        juce::Justification::centred);
        };

        cursor = 0.0;
        for (int stage = 0; stage < activeStages; ++stage)
        {
            drawMarker (cursor, beats[stage], stage + 1);
            cursor += holds[stage];

            if (stage < activeStages - 1)
                cursor += transitions[stage];
        }

        const auto drawSegment = [&] (double start,
                                      double end,
                                      const juce::String& name,
                                      bool transition)
        {
            const float left = xForMinutes (start);
            const float right = xForMinutes (end);
            auto segmentArea = juce::Rectangle<float> (
                left, labels.getY(), juce::jmax (0.0f, right - left),
                labels.getHeight());

            g.setColour (transition
                             ? bodyColour.withAlpha (0.10f)
                             : accentColour.withAlpha (0.16f));
            g.fillRect (segmentArea);

            if (segmentArea.getWidth() >= 22.0f)
            {
                g.setColour (bodyColour);
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (name, segmentArea.toNearestInt(),
                            juce::Justification::centred);
            }
        };

        cursor = 0.0;
        for (int stage = 0; stage < activeStages; ++stage)
        {
            const double holdEnd = cursor + holds[stage];
            drawSegment (cursor, holdEnd,
                         "S" + juce::String (stage + 1), false);
            cursor = holdEnd;

            if (stage < activeStages - 1)
            {
                const double transitionEnd = cursor + transitions[stage];
                drawSegment (cursor, transitionEnd,
                             "T" + juce::String (stage + 1), true);
                cursor = transitionEnd;
            }
        }

        if (audioProcessor.isJourneyModeActive())
        {
            const double elapsedMinutes =
                audioProcessor.getSessionElapsedSeconds() / 60.0;
            const double clampedMinutes = juce::jlimit (
                0.0, totalMinutes, elapsedMinutes);
            const float playheadX = xForMinutes (clampedMinutes);
            g.setColour (headingColour.withAlpha (0.82f));
            g.drawLine (playheadX, graph.getY(), playheadX,
                        labels.getBottom(), 1.5f);

            juce::Path triangle;
            triangle.addTriangle (playheadX - 4.0f, graph.getY(),
                                  playheadX + 4.0f, graph.getY(),
                                  playheadX, graph.getY() + 6.0f);
            g.fillPath (triangle);
        }
    }

    static void configureLabel (juce::Label& label,
                                const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        label.setColour (juce::Label::textColourId, headingColour);
        label.setJustificationType (juce::Justification::centredLeft);
    }

    static void configureComboBox (juce::ComboBox& box)
    {
        box.setColour (juce::ComboBox::backgroundColourId, panelBackground);
        box.setColour (juce::ComboBox::textColourId, headingColour);
        box.setColour (juce::ComboBox::outlineColourId, borderColour);
        box.setColour (juce::ComboBox::arrowColourId, accentColour);
    }

    static void configureStageButton (juce::TextButton& button,
                                      const juce::String& text,
                                      const juce::String& tooltip)
    {
        button.setButtonText (text);
        button.setTooltip (tooltip);
        button.setColour (juce::TextButton::buttonColourId, panelBackground);
        button.setColour (juce::TextButton::buttonOnColourId, accentSoftColour);
        button.setColour (juce::TextButton::textColourOffId, headingColour);
        button.setColour (juce::TextButton::textColourOnId, headingColour);
    }

    static void configureBeatPresetBox (juce::ComboBox& box)
    {
        box.addItem ("Custom", 1);
        box.addItem ("Delta - 2 Hz", 2);
        box.addItem ("Theta - 6 Hz", 3);
        box.addItem ("Schumann - 7.83 Hz", 4);
        box.addItem ("Alpha - 10 Hz", 5);
        box.addItem ("Beta - 20 Hz", 6);
        box.addItem ("Gamma - 40 Hz", 7);
        configureComboBox (box);
        box.setTextWhenNothingSelected ("Choose type");
    }

    static void configureBeatSlider (juce::Slider& slider,
                                     double defaultValue)
    {
        configureSlider (slider, defaultValue, " Hz", 2);
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setTextBoxStyle (
            juce::Slider::TextBoxBelow, false, 94, 24);
    }

    static void configureMinutesSlider (juce::Slider& slider,
                                        double defaultValue)
    {
        configureSlider (slider, defaultValue, {}, 1);
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (
            juce::Slider::TextBoxBelow, false, 94, 24);

        slider.textFromValueFunction = [] (double minutes)
        {
            return juce::String (minutes, 1) + " min";
        };

        slider.valueFromTextFunction = [] (const juce::String& text)
        {
            return text.getDoubleValue();
        };
    }

    static void configureSlider (juce::Slider& slider,
                                 double defaultValue,
                                 const juce::String& suffix,
                                 int decimals)
    {
        slider.setSliderStyle (juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle (
            juce::Slider::TextBoxRight, false, 78, 26);
        slider.setTextValueSuffix (suffix);
        slider.setNumDecimalPlacesToDisplay (decimals);
        slider.setDoubleClickReturnValue (true, defaultValue);
        slider.setColour (juce::Slider::trackColourId, accentColour);
        slider.setColour (juce::Slider::thumbColourId, accentColour);
        slider.setColour (
            juce::Slider::textBoxTextColourId, headingColour);
        slider.setColour (
            juce::Slider::textBoxBackgroundColourId,
            juce::Colours::transparentBlack);
        slider.setColour (
            juce::Slider::textBoxOutlineColourId, borderColour);
    }

    static double presetFrequencyForId (int selectedId) noexcept
    {
        switch (selectedId)
        {
            case 2: return 2.0;
            case 3: return 6.0;
            case 4: return 7.83;
            case 5: return 10.0;
            case 6: return 20.0;
            case 7: return 40.0;
            default: return 0.0;
        }
    }

    void applyStagePreset (int stage)
    {
        auto& controls = stages[stage];
        const double frequency = presetFrequencyForId (
            controls.preset.getSelectedId());

        if (frequency > 0.0)
        {
            const juce::ScopedValueSetter<bool> guard (applyingPreset, true);
            controls.beat.setValue (frequency, juce::sendNotificationSync);
        }

        updatePresetControls();
        repaint (timelineBounds);
    }

    void handleBeatValueChange (int stage)
    {
        auto& controls = stages[stage];

        if (! applyingPreset && controls.preset.getSelectedId() != 1)
        {
            const double expected = presetFrequencyForId (
                controls.preset.getSelectedId());

            if (expected > 0.0
                && std::abs (controls.beat.getValue() - expected) > 0.005)
            {
                controls.preset.setSelectedId (
                    1, juce::sendNotificationSync);
            }
        }

        updatePresetControls();
        repaint (timelineBounds);
    }

    StageSnapshot captureStage (int stage) const
    {
        const auto& controls = stages[stage];
        return {
            controls.preset.getSelectedId(),
            controls.beat.getValue(),
            controls.hold.getValue(),
            controls.transition.getValue()
        };
    }

    StageSnapshot makeDefaultStage (int stage) const
    {
        const double beat = defaultBeatForStage (stage);
        int presetId = 1;

        if (std::abs (beat - 2.0) < 0.005) presetId = 2;
        else if (std::abs (beat - 6.0) < 0.005) presetId = 3;
        else if (std::abs (beat - 7.83) < 0.005) presetId = 4;
        else if (std::abs (beat - 10.0) < 0.005) presetId = 5;
        else if (std::abs (beat - 20.0) < 0.005) presetId = 6;
        else if (std::abs (beat - 40.0) < 0.005) presetId = 7;

        return {
            presetId,
            beat,
            defaultHoldForStage (stage),
            defaultTransitionForStage (stage)
        };
    }

    void applyStageSnapshot (int stage, const StageSnapshot& snapshot)
    {
        auto& controls = stages[stage];
        const juce::ScopedValueSetter<bool> guard (applyingPreset, true);
        controls.preset.setSelectedId (
            juce::jlimit (1, 7, snapshot.presetId),
            juce::sendNotificationSync);
        controls.beat.setValue (snapshot.beat, juce::sendNotificationSync);
        controls.hold.setValue (snapshot.hold, juce::sendNotificationSync);

        if (stage < maximumStages - 1)
        {
            controls.transition.setValue (
                snapshot.transition, juce::sendNotificationSync);
        }
    }

    void finishStageStructureChange()
    {
        dragTargetStage = -1;
        journeyPresetHint.setText (
            "Journey customised. Stage order and settings remain editable.",
            juce::dontSendNotification);
        updateStageVisibility();
        updatePresetControls();
        updateTotal();
        resized();
        repaint();
    }

    void reorderStage (int fromStage, int toStage)
    {
        const int activeStages = getActiveStageCount();

        if (fromStage < 0 || fromStage >= activeStages
            || toStage < 0 || toStage >= activeStages
            || fromStage == toStage)
        {
            repaint();
            return;
        }

        std::array<StageSnapshot, maximumStages> snapshots {};
        for (int stage = 0; stage < activeStages; ++stage)
            snapshots[stage] = captureStage (stage);

        const auto moved = snapshots[fromStage];

        if (fromStage < toStage)
        {
            for (int stage = fromStage; stage < toStage; ++stage)
                snapshots[stage] = snapshots[stage + 1];
        }
        else
        {
            for (int stage = fromStage; stage > toStage; --stage)
                snapshots[stage] = snapshots[stage - 1];
        }

        snapshots[toStage] = moved;

        for (int stage = 0; stage < activeStages; ++stage)
            applyStageSnapshot (stage, snapshots[stage]);

        for (int stage = 0; stage < activeStages - 1; ++stage)
        {
            if (stages[stage].transition.getValue() <= 0.0)
            {
                stages[stage].transition.setValue (
                    defaultTransitionForStage (stage),
                    juce::sendNotificationSync);
            }
        }

        finishStageStructureChange();
    }

    void duplicateStage (int stage)
    {
        const int activeStages = getActiveStageCount();

        if (stage < 0 || stage >= activeStages
            || activeStages >= maximumStages)
        {
            return;
        }

        const auto duplicate = captureStage (stage);

        for (int destination = activeStages;
             destination > stage + 1; --destination)
        {
            applyStageSnapshot (destination,
                                captureStage (destination - 1));
        }

        applyStageSnapshot (stage + 1, duplicate);
        setActiveStageCount (activeStages + 1);
        previousActiveStageCount = activeStages + 1;
        finishStageStructureChange();
    }

    void removeStage (int stage)
    {
        const int activeStages = getActiveStageCount();

        if (stage < 0 || stage >= activeStages
            || activeStages <= minimumStages)
        {
            return;
        }

        for (int destination = stage;
             destination < activeStages - 1; ++destination)
        {
            applyStageSnapshot (destination,
                                captureStage (destination + 1));
        }

        applyStageSnapshot (activeStages - 1,
                            makeDefaultStage (activeStages - 1));
        setActiveStageCount (activeStages - 1);
        previousActiveStageCount = activeStages - 1;
        finishStageStructureChange();
    }

    void setJourneyStage (int stage,
                          int presetId,
                          double holdMinutes,
                          double transitionMinutes)
    {
        auto& controls = stages[stage];
        controls.preset.setSelectedId (presetId,
                                       juce::sendNotificationSync);
        applyStagePreset (stage);
        controls.hold.setValue (holdMinutes, juce::sendNotificationSync);

        if (stage < maximumStages - 1)
        {
            controls.transition.setValue (
                transitionMinutes, juce::sendNotificationSync);
        }
    }

    void applySelectedJourneyPreset()
    {
        const int presetId = journeyPresetBox.getSelectedId();

        if (presetId <= 1)
        {
            journeyPresetHint.setText (
                "Choose a journey template to apply it automatically.",
                juce::dontSendNotification);
            return;
        }

        setActiveStageCount (3);

        switch (presetId)
        {
            case 2: // Sleep descent: Alpha -> Theta -> Delta
                setJourneyStage (0, 5, 5.0, 4.0);
                setJourneyStage (1, 3, 10.0, 5.0);
                setJourneyStage (2, 2, 30.0, 0.0);
                break;

            case 3: // Meditation flow: Alpha -> Schumann -> Theta
                setJourneyStage (0, 5, 5.0, 3.0);
                setJourneyStage (1, 4, 15.0, 3.0);
                setJourneyStage (2, 3, 20.0, 0.0);
                break;

            case 4: // Focus arc: Alpha -> Beta -> Alpha
                setJourneyStage (0, 5, 5.0, 2.0);
                setJourneyStage (1, 6, 25.0, 2.0);
                setJourneyStage (2, 5, 5.0, 0.0);
                break;

            case 5: // Gentle relaxation: Alpha -> Schumann -> Theta
                setJourneyStage (0, 5, 10.0, 3.0);
                setJourneyStage (1, 4, 20.0, 3.0);
                setJourneyStage (2, 3, 15.0, 0.0);
                break;

            default:
                return;
        }

        journeyPresetHint.setText (
            journeyPresetBox.getText()
                + " applied. Change the stage count or any stage afterward.",
            juce::dontSendNotification);
        updateStageVisibility();
        updatePresetControls();
        updateTotal();
        resized();
        repaint();
    }

    void updatePresetControls()
    {
        for (int stage = 0; stage < maximumStages; ++stage)
        {
            const bool custom = stages[stage].preset.getSelectedId() == 1;
            stages[stage].beat.setEnabled (custom);
            stages[stage].beat.setAlpha (custom ? 1.0f : 0.62f);
        }
    }

    void timerCallback() override
    {
        if (! initialDragHandleRefreshDone && isShowing())
        {
            initialDragHandleRefreshDone = true;
            refreshStageDragHandles();
        }

        updatePresetControls();
        updateTotal();
        repaint (timelineBounds);
    }

    void updateTotal()
    {
        const int activeStages = getActiveStageCount();
        double minutes = 0.0;

        for (int stage = 0; stage < activeStages; ++stage)
        {
            minutes += stages[stage].hold.getValue();

            if (stage < activeStages - 1)
                minutes += stages[stage].transition.getValue();
        }

        total.setText (
            "Total journey duration: " + formatDurationMinutes (minutes)
                + " | " + juce::String (activeStages) + " stages",
            juce::dontSendNotification);
    }

    BinauralJourneyAudioProcessor& audioProcessor;
    juce::AudioProcessorValueTreeState& parameters;
    juce::TooltipWindow tooltipWindow;
    bool applyingPreset = false;
    bool initialDragHandleRefreshDone = false;
    int previousActiveStageCount = 3;

    juce::Label title;
    juce::TextButton closeButton;
    juce::Label help;
    juce::Label journeyPresetLabel;
    juce::ComboBox journeyPresetBox;
    juce::Label journeyPresetHint;
    juce::Label stageCountLabel;
    juce::ComboBox stageCountBox;
    juce::Label transitionCurveLabel;
    juce::ComboBox transitionCurveBox;
    juce::Label transitionCurveHint;
    juce::Label total;
    juce::Label actionHeader;
    juce::Label presetHeader;
    juce::Label beatHeader;
    juce::Label holdHeader;
    juce::Label transitionHeader;
    juce::Label previewHeader;
    juce::Rectangle<int> timelineBounds;
    juce::Rectangle<int> columnHeaderBounds;
    std::array<juce::Rectangle<int>, maximumStages> stageRowBounds {};
    int dragTargetStage = -1;

    std::array<StageControls, maximumStages> stages;
    std::unique_ptr<ComboBoxAttachment> stageCountAttachment;
    std::unique_ptr<ComboBoxAttachment> transitionCurveAttachment;
    std::function<void()> closeRequested;
    std::function<void(int)> stageCountChanged;
};

class JourneyWindowContent final : public juce::Component
{
public:
    explicit JourneyWindowContent (
        std::unique_ptr<JourneyEditorComponent> editor)
    {
        viewport.setScrollBarsShown (true, false);
        viewport.setViewedComponent (editor.release(), true);
        viewport.setWantsKeyboardFocus (false);
        addAndMakeVisible (viewport);
    }

    void resized() override
    {
        viewport.setBounds (getLocalBounds());
    }

private:
    juce::Viewport viewport;
};

class JourneyDocumentWindow final : public juce::DocumentWindow
{
public:
    JourneyDocumentWindow (std::function<void()> closeCallback,
                           int initialWidth,
                           int initialHeight)
        : juce::DocumentWindow (
              "Binaural Journey Builder",
              pageBackground,
              juce::DocumentWindow::closeButton,
              true),
          closeRequested (std::move (closeCallback)),
          lockedWidth (initialWidth),
          lockedHeight (initialHeight)
    {
        // A JUCE title bar avoids Logic/macOS re-enabling the native window's
        // resize style on a later opening. The window remains draggable, but
        // has no resize edges, zoom control, or corner resizer.
        setUsingNativeTitleBar (false);
        setResizable (false, false);
        setResizeLimits (lockedWidth, lockedHeight,
                         lockedWidth, lockedHeight);
        setSize (lockedWidth, lockedHeight);
        setAlwaysOnTop (true);
        setBroughtToFrontOnMouseClick (true);
        setWantsKeyboardFocus (true);
    }

    void setLockedBounds (juce::Rectangle<int> bounds)
    {
        lockedWidth = journeyWindowWidth;
        lockedHeight = juce::jlimit (journeyWindowMinimumHeight,
                                     journeyWindowMaximumHeight,
                                     bounds.getHeight());

        bounds.setWidth (lockedWidth);
        bounds.setHeight (lockedHeight);

        const juce::ScopedValueSetter<bool> guard (enforcingLockedBounds, true);
        setResizable (false, false);
        setResizeLimits (lockedWidth, lockedHeight,
                         lockedWidth, lockedHeight);
        setBounds (bounds);
    }

    void resized() override
    {
        juce::DocumentWindow::resized();

        if (enforcingLockedBounds || lockedWidth <= 0 || lockedHeight <= 0)
            return;

        if (getWidth() == lockedWidth && getHeight() == lockedHeight)
            return;

        // Defensive fallback: even if Logic or macOS changes the native style
        // mask later, reject the resize and restore the stage-derived size.
        juce::Component::SafePointer<JourneyDocumentWindow> safeThis (this);
        const auto lockedPosition = getPosition();
        const int width = lockedWidth;
        const int height = lockedHeight;

        juce::MessageManager::callAsync ([safeThis, lockedPosition, width, height]
        {
            if (safeThis != nullptr)
            {
                safeThis->setLockedBounds ({ lockedPosition.x, lockedPosition.y,
                                              width, height });
            }
        });
    }

    void closeButtonPressed() override
    {
        setVisible (false);

        if (closeRequested)
            closeRequested();
    }

private:
    std::function<void()> closeRequested;
    int lockedWidth = journeyWindowWidth;
    int lockedHeight = journeyWindowMinimumHeight;
    bool enforcingLockedBounds = false;
};

}

//==============================================================================
BinauralJourneyAudioProcessorEditor::BinauralJourneyAudioProcessorEditor (
    BinauralJourneyAudioProcessor& processor)
    : AudioProcessorEditor (&processor),
      audioProcessor (processor),
      tooltipWindow (this, 650)
{
    setOpaque (true);
    setSize (820, 650);

    titleLabel.setText ("Binaural Journey", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (25.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, headingColour);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    subtitleLabel.setText (
        "Stereo binaural tone and journey generator",
        juce::dontSendNotification);
    subtitleLabel.setFont (juce::FontOptions (13.0f));
    subtitleLabel.setColour (juce::Label::textColourId, bodyColour);
    subtitleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (subtitleLabel);

    configureTabButton (setupTabButton, "Setup");
    configureTabButton (toneTabButton, "Tone and pitch");
    configureTabButton (mixTabButton, "Mix and output");

    helpAboutButton.setButtonText ("?");
    configureActionButton (helpAboutButton, false);
    helpAboutButton.setTooltip ("Help, workflow notes, and version information");
    helpAboutButton.onClick = [this] { showHelpAbout(); };

    versionLabel.setText ("v" + releaseVersionText,
                          juce::dontSendNotification);
    versionLabel.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    versionLabel.setColour (juce::Label::textColourId, bodyColour);
    versionLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (versionLabel);

    setupTabButton.onClick = [this] { setActivePage (0); };
    toneTabButton.onClick = [this] { setActivePage (1); };
    mixTabButton.onClick = [this] { setActivePage (2); };

    outputEnabledButton.setButtonText ("Output enabled");
    outputEnabledButton.setColour (juce::ToggleButton::textColourId, headingColour);
    outputEnabledButton.setColour (juce::ToggleButton::tickColourId, accentColour);
    outputEnabledButton.setColour (juce::ToggleButton::tickDisabledColourId, borderColour);
    addAndMakeVisible (outputEnabledButton);

    toneEnabledButton.setButtonText ("Tone enabled");
    toneEnabledButton.setColour (juce::ToggleButton::textColourId, headingColour);
    toneEnabledButton.setColour (juce::ToggleButton::tickColourId, accentColour);
    toneEnabledButton.setColour (juce::ToggleButton::tickDisabledColourId, borderColour);
    addAndMakeVisible (toneEnabledButton);

    configureSectionLabel (soundPresetLabel, "Factory sound preset");
    configureSectionLabel (userPresetLabel, "My presets");
    configureSectionLabel (journeyModeLabel, "Playback mode");
    configureSectionLabel (playbackSourceLabel, "Playback source");
    configureSectionLabel (sessionDurationLabel, "Session duration");
    configureSectionLabel (anchorLabel, "Anchor frequency");
    configureSectionLabel (anchorModeLabel, "Anchor input");
    configureSectionLabel (anchorNoteLabel, "Musical note");
    configureSectionLabel (anchorOctaveLabel, "Octave");
    configureSectionLabel (tuningReferenceLabel, "A4 tuning");
    configureSectionLabel (beatLabel, "Beat frequency (difference)");
    configureSectionLabel (beatPresetLabel, "Beat preset");
    configureSectionLabel (waveformLabel, "Waveform");
    configureSectionLabel (anchorEarLabel, "Anchor ear");
    configureSectionLabel (noiseTypeLabel, "Ambient noise");
    configureSectionLabel (noiseVolumeLabel, "Noise volume");
    configureSectionLabel (toneVolumeLabel, "Overall tone volume");
    configureSectionLabel (leftVolumeLabel, "Left channel volume");
    configureSectionLabel (rightVolumeLabel, "Right channel volume");
    configureSectionLabel (masterVolumeLabel, "Master output volume");
    configureSectionLabel (fadeInLabel, "Fade in");
    configureSectionLabel (fadeOutLabel, "Fade out");

    configureFrequencySlider (anchorSlider, 200.0, 1);
    configureFrequencySlider (tuningReferenceSlider, 440.0, 1);
    configureFrequencySlider (beatSlider, 10.0, 2);
    configurePercentSlider (noiseVolumeSlider, 8.0);
    configurePercentSlider (toneVolumeSlider, 10.0);
    configurePercentSlider (leftVolumeSlider, 100.0);
    configurePercentSlider (rightVolumeSlider, 100.0);
    configurePercentSlider (masterVolumeSlider, 100.0);
    configureSecondsSlider (fadeInSlider, 0.25);
    configureSecondsSlider (fadeOutSlider, 0.25);
    configureMinutesSlider (sessionDurationSlider, 10.0);

    addAndMakeVisible (anchorSlider);
    addAndMakeVisible (tuningReferenceSlider);
    addAndMakeVisible (beatSlider);
    addAndMakeVisible (noiseVolumeSlider);
    addAndMakeVisible (toneVolumeSlider);
    addAndMakeVisible (leftVolumeSlider);
    addAndMakeVisible (rightVolumeSlider);
    addAndMakeVisible (masterVolumeSlider);
    addAndMakeVisible (fadeInSlider);
    addAndMakeVisible (fadeOutSlider);
    addAndMakeVisible (sessionDurationSlider);

    anchorEarBox.addItem ("Left", 1);
    anchorEarBox.addItem ("Right", 2);
    configureComboBox (anchorEarBox);

    anchorModeBox.addItem ("Frequency", 1);
    anchorModeBox.addItem ("Musical note", 2);
    configureComboBox (anchorModeBox);

    anchorNoteBox.addItem ("C", 1);
    anchorNoteBox.addItem ("C sharp / D flat", 2);
    anchorNoteBox.addItem ("D", 3);
    anchorNoteBox.addItem ("D sharp / E flat", 4);
    anchorNoteBox.addItem ("E", 5);
    anchorNoteBox.addItem ("F", 6);
    anchorNoteBox.addItem ("F sharp / G flat", 7);
    anchorNoteBox.addItem ("G", 8);
    anchorNoteBox.addItem ("G sharp / A flat", 9);
    anchorNoteBox.addItem ("A", 10);
    anchorNoteBox.addItem ("A sharp / B flat", 11);
    anchorNoteBox.addItem ("B", 12);
    configureComboBox (anchorNoteBox);

    anchorOctaveBox.addItem ("2", 1);
    anchorOctaveBox.addItem ("3", 2);
    anchorOctaveBox.addItem ("4", 3);
    anchorOctaveBox.addItem ("5", 4);
    anchorOctaveBox.addItem ("6", 5);
    configureComboBox (anchorOctaveBox);

    noiseTypeBox.addItem ("None", 1);
    noiseTypeBox.addItem ("White", 2);
    noiseTypeBox.addItem ("Pink", 3);
    noiseTypeBox.addItem ("Brown", 4);
    noiseTypeBox.setTextWhenNothingSelected ("None");
    noiseTypeBox.setSelectedId (1, juce::dontSendNotification);
    configureComboBox (noiseTypeBox);

    waveformBox.addItem ("Sine", 1);
    waveformBox.addItem ("Triangle", 2);
    waveformBox.addItem ("Square", 3);
    waveformBox.addItem ("Sawtooth", 4);
    configureComboBox (waveformBox);

    beatPresetBox.addItem ("Custom", 1);
    beatPresetBox.addItem ("Delta - 2 Hz", 2);
    beatPresetBox.addItem ("Theta - 6 Hz", 3);
    beatPresetBox.addItem ("Schumann - 7.83 Hz", 4);
    beatPresetBox.addItem ("Alpha - 10 Hz", 5);
    beatPresetBox.addItem ("Beta - 20 Hz", 6);
    beatPresetBox.addItem ("Gamma - 40 Hz", 7);
    configureComboBox (beatPresetBox);

    soundPresetBox.addItem ("Choose a preset...", 1);
    soundPresetBox.addItem ("Deep Rest - Delta 2 Hz", 2);
    soundPresetBox.addItem ("Meditation - Theta 6 Hz", 3);
    soundPresetBox.addItem ("Schumann - 7.83 Hz", 4);
    soundPresetBox.addItem ("Relaxation - Alpha 10 Hz", 5);
    soundPresetBox.addItem ("Focus - Beta 20 Hz", 6);
    soundPresetBox.addItem ("Clarity - Gamma 40 Hz", 7);
    configureComboBox (soundPresetBox);
    soundPresetBox.setSelectedId (1, juce::dontSendNotification);

    resetButton.setButtonText ("Reset defaults");
    configureActionButton (resetButton, false);

    userPresetBox.addItem ("Choose a saved preset...", 1);
    configureComboBox (userPresetBox);
    userPresetBox.setSelectedId (1, juce::dontSendNotification);

    saveUserPresetButton.setButtonText ("Save as...");
    configureActionButton (saveUserPresetButton, true);

    renameUserPresetButton.setButtonText ("Rename");
    configureActionButton (renameUserPresetButton, false);

    deleteUserPresetButton.setButtonText ("Delete");
    configureActionButton (deleteUserPresetButton, false);

    userPresetHintLabel.setText (
        "Save either a single tone or the complete flexible journey.",
        juce::dontSendNotification);
    userPresetHintLabel.setFont (juce::FontOptions (12.5f));
    userPresetHintLabel.setColour (juce::Label::textColourId, bodyColour);
    userPresetHintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (userPresetHintLabel);

    presetHintLabel.setText (
        "Presets set the beat range and leave every control adjustable.",
        juce::dontSendNotification);
    presetHintLabel.setFont (juce::FontOptions (12.5f));
    presetHintLabel.setColour (juce::Label::textColourId, bodyColour);
    presetHintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (presetHintLabel);

    journeyModeBox.addItem ("Single tone", 1);
    journeyModeBox.addItem ("Journey (2 to 6 stages)", 2);
    configureComboBox (journeyModeBox);

    playbackSourceBox.addItem ("Internal controls", 1);
    playbackSourceBox.addItem ("Follow Logic transport", 2);
    playbackSourceBox.setTextWhenNothingSelected ("Internal controls");
    playbackSourceBox.setSelectedId (1, juce::dontSendNotification);
    configureComboBox (playbackSourceBox);

    editJourneyButton.setButtonText ("Edit journey...");
    configureActionButton (editJourneyButton, false);

    journeySummaryLabel.setFont (juce::FontOptions (12.5f));
    journeySummaryLabel.setColour (juce::Label::textColourId, bodyColour);
    journeySummaryLabel.setJustificationType (
        juce::Justification::centredLeft);
    addAndMakeVisible (journeySummaryLabel);

    playPauseButton.setButtonText ("Pause");
    configureActionButton (playPauseButton, true);

    restartSessionButton.setButtonText ("Restart");
    configureActionButton (restartSessionButton, false);

    stopSessionButton.setButtonText ("Stop");
    configureActionButton (stopSessionButton, false);

    sessionTimeLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    sessionTimeLabel.setColour (juce::Label::textColourId, headingColour);
    sessionTimeLabel.setColour (juce::Label::backgroundColourId, accentSoftColour);
    sessionTimeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (sessionTimeLabel);

    leftFrequencyLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    leftFrequencyLabel.setColour (juce::Label::textColourId, headingColour);
    leftFrequencyLabel.setColour (juce::Label::backgroundColourId, accentSoftColour);
    leftFrequencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (leftFrequencyLabel);

    rightFrequencyLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    rightFrequencyLabel.setColour (juce::Label::textColourId, headingColour);
    rightFrequencyLabel.setColour (juce::Label::backgroundColourId, accentSoftColour);
    rightFrequencyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (rightFrequencyLabel);

    waveformHintLabel.setText (
        "Sine is the cleanest choice for traditional binaural beats.",
        juce::dontSendNotification);
    waveformHintLabel.setFont (juce::FontOptions (12.5f));
    waveformHintLabel.setColour (juce::Label::textColourId, bodyColour);
    waveformHintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (waveformHintLabel);

    noiseHintLabel.setText (
        "White is bright; pink is balanced; brown is deeper.",
        juce::dontSendNotification);
    noiseHintLabel.setFont (juce::FontOptions (12.5f));
    noiseHintLabel.setColour (juce::Label::textColourId, bodyColour);
    noiseHintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (noiseHintLabel);

    outputHintLabel.setText (
        "Output enabled fades both the tones and ambient noise.",
        juce::dontSendNotification);
    outputHintLabel.setFont (juce::FontOptions (12.5f));
    outputHintLabel.setColour (juce::Label::textColourId, bodyColour);
    outputHintLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (outputHintLabel);

    configureSectionLabel (outputMeterLabel, "Live output level");

    leftOutputMeterLabel.setText ("L", juce::dontSendNotification);
    rightOutputMeterLabel.setText ("R", juce::dontSendNotification);

    for (auto* label : { &leftOutputMeterLabel, &rightOutputMeterLabel })
    {
        label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        label->setColour (juce::Label::textColourId, headingColour);
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*label);
    }

    for (auto* meter : { &leftOutputMeter, &rightOutputMeter })
    {
        meter->setPercentageDisplay (false);
        meter->setColour (juce::ProgressBar::backgroundColourId,
                          borderColour.withAlpha (0.65f));
        meter->setColour (juce::ProgressBar::foregroundColourId,
                          accentColour);
        addAndMakeVisible (*meter);
    }

    peakGuardStatusLabel.setText (
        "Peak guard ready",
        juce::dontSendNotification);
    peakGuardStatusLabel.setFont (juce::FontOptions (11.5f));
    peakGuardStatusLabel.setColour (juce::Label::textColourId, bodyColour);
    peakGuardStatusLabel.setJustificationType (
        juce::Justification::centredLeft);
    addAndMakeVisible (peakGuardStatusLabel);

    safetyLabel.setText ("Keep the overall volume low when using headphones.",
                         juce::dontSendNotification);
    safetyLabel.setFont (juce::FontOptions (12.5f));
    safetyLabel.setColour (juce::Label::textColourId, bodyColour);
    safetyLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (safetyLabel);

    setupTabButton.setTooltip ("Presets, playback mode, transport, and session controls");
    toneTabButton.setTooltip ("Anchor pitch, beat frequency, waveform, and stereo placement");
    mixTabButton.setTooltip ("Ambient noise, channel levels, fades, and output meters");
    outputEnabledButton.setTooltip ("Turn the plug-in's complete audio output on or off");
    toneEnabledButton.setTooltip ("Turn the generated binaural tone on or off");
    soundPresetBox.setTooltip ("Apply a factory tone and beat preset");
    resetButton.setTooltip ("Restore the plug-in's recommended default settings");
    userPresetBox.setTooltip ("Load one of your saved BinauralJourney presets");
    saveUserPresetButton.setTooltip ("Save the current single tone or complete journey");
    renameUserPresetButton.setTooltip ("Rename the selected saved preset");
    deleteUserPresetButton.setTooltip ("Delete the selected saved preset");
    journeyModeBox.setTooltip (
        "Choose Single tone or a multi-stage Journey");
    editJourneyButton.setTooltip (
        "Open the Journey Builder. Close it when returning to the main editor.");
    playbackSourceBox.setTooltip (
        "Use the plug-in controls or follow Logic's transport and timeline");
    sessionDurationSlider.setTooltip (
        "Length of Single tone playback. Journey duration is calculated from its stages.");
    playPauseButton.setTooltip ("Play or pause when using Internal controls");
    restartSessionButton.setTooltip ("Restart playback from the beginning");
    stopSessionButton.setTooltip ("Stop playback and return to the beginning");
    anchorModeBox.setTooltip ("Enter the anchor as a frequency or musical note");
    anchorEarBox.setTooltip ("Choose which ear receives the lower anchor frequency");
    anchorSlider.setTooltip ("Base tone frequency in hertz");
    anchorNoteBox.setTooltip ("Musical note used for the anchor tone");
    anchorOctaveBox.setTooltip ("Octave of the selected anchor note");
    tuningReferenceSlider.setTooltip ("Reference tuning for A4, normally 440 Hz");
    beatPresetBox.setTooltip (
        "Choose a named beat rate. Disabled in Journey mode because each stage controls its rate.");
    beatSlider.setTooltip (
        "Difference between the left and right tones. Disabled in Journey mode.");
    waveformBox.setTooltip ("Waveform used by both generated tone channels");
    noiseTypeBox.setTooltip ("Choose None, White, Pink, or Brown ambient noise");
    noiseVolumeSlider.setTooltip ("Level of the selected ambient noise");
    toneVolumeSlider.setTooltip ("Overall level of the generated tone before channel controls");
    leftVolumeSlider.setTooltip ("Left channel tone level");
    rightVolumeSlider.setTooltip ("Right channel tone level");
    masterVolumeSlider.setTooltip ("Final output level for tone and ambient noise");
    fadeInSlider.setTooltip ("Output fade-in duration in seconds");
    fadeOutSlider.setTooltip ("Output fade-out duration in seconds");

    auto& parameters = audioProcessor.getParameterState();

    outputEnabledAttachment = std::make_unique<ButtonAttachment> (
        parameters, "outputEnabled", outputEnabledButton);
    toneEnabledAttachment = std::make_unique<ButtonAttachment> (
        parameters, "toneEnabled", toneEnabledButton);
    anchorAttachment = std::make_unique<SliderAttachment> (
        parameters, "anchorFrequency", anchorSlider);
    anchorModeAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "anchorMode", anchorModeBox);
    anchorNoteAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "anchorNote", anchorNoteBox);
    anchorOctaveAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "anchorOctave", anchorOctaveBox);
    tuningReferenceAttachment = std::make_unique<SliderAttachment> (
        parameters, "tuningReference", tuningReferenceSlider);
    beatAttachment = std::make_unique<SliderAttachment> (
        parameters, "beatFrequency", beatSlider);
    beatPresetAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "beatPreset", beatPresetBox);
    waveformAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "waveform", waveformBox);
    anchorEarAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "anchorEar", anchorEarBox);
    noiseTypeAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "noiseType", noiseTypeBox);
    noiseVolumeAttachment = std::make_unique<SliderAttachment> (
        parameters, "noiseVolume", noiseVolumeSlider);
    toneVolumeAttachment = std::make_unique<SliderAttachment> (
        parameters, "toneVolume", toneVolumeSlider);
    leftVolumeAttachment = std::make_unique<SliderAttachment> (
        parameters, "leftVolume", leftVolumeSlider);
    rightVolumeAttachment = std::make_unique<SliderAttachment> (
        parameters, "rightVolume", rightVolumeSlider);
    masterVolumeAttachment = std::make_unique<SliderAttachment> (
        parameters, "masterVolume", masterVolumeSlider);
    fadeInAttachment = std::make_unique<SliderAttachment> (
        parameters, "fadeInSeconds", fadeInSlider);
    fadeOutAttachment = std::make_unique<SliderAttachment> (
        parameters, "fadeOutSeconds", fadeOutSlider);
    journeyModeAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "journeyMode", journeyModeBox);
    playbackSourceAttachment = std::make_unique<ComboBoxAttachment> (
        parameters, "playbackSource", playbackSourceBox);
    sessionDurationAttachment = std::make_unique<SliderAttachment> (
        parameters, "sessionDurationMinutes", sessionDurationSlider);

    anchorModeBox.onChange = [this]
    {
        updateAnchorModeControls();

        if (anchorModeBox.getSelectedId() == 2)
            applySelectedAnchorNote();

        updateFrequencyReadout();
    };

    anchorNoteBox.onChange = [this]
    {
        applySelectedAnchorNote();
        updateFrequencyReadout();
    };

    anchorOctaveBox.onChange = [this]
    {
        applySelectedAnchorNote();
        updateFrequencyReadout();
    };

    tuningReferenceSlider.onValueChange = [this]
    {
        applySelectedAnchorNote();
        updateFrequencyReadout();
    };

    beatPresetBox.onChange = [this]
    {
        applySelectedBeatPreset();
        updateFrequencyReadout();
    };

    beatSlider.onValueChange = [this]
    {
        if (! applyingBeatPreset && beatPresetBox.getSelectedId() != 1)
        {
            const double presetFrequency = getSelectedPresetFrequency();

            if (presetFrequency > 0.0
                && std::abs (beatSlider.getValue() - presetFrequency) > 0.005)
            {
                beatPresetBox.setSelectedId (1, juce::sendNotificationSync);
            }
        }

        updateFrequencyReadout();
    };

    anchorSlider.onValueChange = [this] { updateFrequencyReadout(); };
    anchorEarBox.onChange = [this] { updateFrequencyReadout(); };
    noiseTypeBox.onChange = [this] { updateNoiseControls(); };
    toneEnabledButton.onClick = [this] { updateToneControls(); };

    soundPresetBox.onChange = [this]
    {
        applySelectedSoundPreset();
    };
    resetButton.onClick = [this] { restoreDefaultSettings(); };

    userPresetBox.onChange = [this]
    {
        if (! refreshingUserPresetList)
            loadSelectedUserPreset();

        updateUserPresetButtons();
    };
    saveUserPresetButton.onClick = [this] { saveCurrentUserPreset(); };
    renameUserPresetButton.onClick = [this] { renameSelectedUserPreset(); };
    deleteUserPresetButton.onClick = [this] { deleteSelectedUserPreset(); };

    journeyModeBox.onChange = [this]
    {
        updateJourneyControls();
        updateSessionDisplay();
        updateFrequencyReadout();
    };

    playbackSourceBox.onChange = [this]
    {
        updatePlaybackSourceControls();
        updateSessionDisplay();
    };

    editJourneyButton.onClick = [this]
    {
        openJourneyWindow();
    };

    playPauseButton.onClick = [this]
    {
        if (audioProcessor.isSessionPlaying())
            audioProcessor.pauseSession();
        else
            audioProcessor.startSession();

        updateSessionDisplay();
    };

    restartSessionButton.onClick = [this]
    {
        audioProcessor.restartSession();
        updateSessionDisplay();
    };

    stopSessionButton.onClick = [this]
    {
        audioProcessor.stopSession();
        updateSessionDisplay();
    };

    sessionDurationSlider.onValueChange = [this] { updateSessionDisplay(); };

    refreshUserPresetList();
    updateAnchorModeControls();
    updateNoiseControls();
    updateToneControls();
    updateJourneyControls();
    updatePlaybackSourceControls();

    if (anchorModeBox.getSelectedId() == 2)
        applySelectedAnchorNote();

    updateFrequencyReadout();
    updateSessionDisplay();
    setActivePage (0);

    // Attachments can finish their first UI synchronisation immediately after
    // construction. Re-check the visible selections once on the message thread
    // so Ambient Noise never appears blank and summaries use the saved stage
    // count from the first frame.
    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync ([safeThis]
    {
        if (safeThis == nullptr)
            return;

        if (safeThis->noiseTypeBox.getSelectedId() == 0)
            safeThis->noiseTypeBox.setSelectedId (1,
                                                   juce::sendNotificationSync);

        if (safeThis->playbackSourceBox.getSelectedId() == 0)
            safeThis->playbackSourceBox.setSelectedId (
                1, juce::sendNotificationSync);

        safeThis->updateNoiseControls();
        safeThis->updateJourneyControls();
        safeThis->updatePlaybackSourceControls();
        safeThis->updateSessionDisplay();
    });

    startTimerHz (10);
}

BinauralJourneyAudioProcessorEditor::~BinauralJourneyAudioProcessorEditor()
{
    stopTimer();
    closeJourneyWindow();
}

void BinauralJourneyAudioProcessorEditor::showHelpAbout()
{
    if (helpAboutOverlay == nullptr)
    {
        juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor>
            safeThis (this);

        helpAboutOverlay = std::make_unique<HelpAboutOverlay> ([safeThis]
        {
            if (safeThis != nullptr && safeThis->helpAboutOverlay != nullptr)
                safeThis->helpAboutOverlay->setVisible (false);
        });

        addAndMakeVisible (*helpAboutOverlay);
    }

    helpAboutOverlay->setBounds (getLocalBounds());
    helpAboutOverlay->setVisible (true);
    helpAboutOverlay->toFront (true);
    helpAboutOverlay->grabKeyboardFocus();
}

void BinauralJourneyAudioProcessorEditor::configureSectionLabel (
    juce::Label& label,
    const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, headingColour);
    addAndMakeVisible (label);
}

void BinauralJourneyAudioProcessorEditor::configureComboBox (juce::ComboBox& box)
{
    box.setColour (juce::ComboBox::backgroundColourId, panelBackground);
    box.setColour (juce::ComboBox::textColourId, headingColour);
    box.setColour (juce::ComboBox::outlineColourId, borderColour);
    box.setColour (juce::ComboBox::arrowColourId, accentColour);
    addAndMakeVisible (box);
}

void BinauralJourneyAudioProcessorEditor::configureActionButton (
    juce::TextButton& button,
    bool primary)
{
    button.setColour (juce::TextButton::buttonColourId,
                      primary ? accentColour : panelBackground);
    button.setColour (juce::TextButton::buttonOnColourId, accentColour);
    button.setColour (juce::TextButton::textColourOffId,
                      primary ? juce::Colours::white : headingColour);
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    addAndMakeVisible (button);
}

void BinauralJourneyAudioProcessorEditor::configureTabButton (
    juce::TextButton& button,
    const juce::String& text)
{
    button.setButtonText (text);
    button.setColour (juce::TextButton::buttonColourId, panelBackground);
    button.setColour (juce::TextButton::buttonOnColourId, accentColour);
    button.setColour (juce::TextButton::textColourOffId, headingColour);
    button.setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    button.setColour (juce::TextButton::buttonOnColourId, accentColour);
    addAndMakeVisible (button);
}

void BinauralJourneyAudioProcessorEditor::setActivePage (int pageIndex)
{
    activePage = juce::jlimit (0, 2, pageIndex);
    setupTabButton.setToggleState (activePage == 0, juce::dontSendNotification);
    toneTabButton.setToggleState (activePage == 1, juce::dontSendNotification);
    mixTabButton.setToggleState (activePage == 2, juce::dontSendNotification);
    updatePageVisibility();
    resized();
    repaint();
}

void BinauralJourneyAudioProcessorEditor::updatePageVisibility()
{
    const auto setGroupVisible = [] (
        bool visible,
        std::initializer_list<juce::Component*> components)
    {
        for (auto* component : components)
            if (component != nullptr)
                component->setVisible (visible);
    };

    const bool setupVisible = activePage == 0;
    const bool toneVisible = activePage == 1;
    const bool mixVisible = activePage == 2;

    setGroupVisible (setupVisible, {
        &soundPresetLabel, &soundPresetBox, &resetButton, &presetHintLabel,
        &userPresetLabel, &userPresetBox, &saveUserPresetButton,
        &renameUserPresetButton, &deleteUserPresetButton, &userPresetHintLabel,
        &journeyModeLabel, &journeyModeBox, &editJourneyButton,
        &journeySummaryLabel, &playbackSourceLabel, &playbackSourceBox,
        &sessionDurationLabel, &sessionDurationSlider, &playPauseButton,
        &restartSessionButton, &stopSessionButton, &sessionTimeLabel
    });

    setGroupVisible (toneVisible, {
        &anchorModeLabel, &anchorModeBox, &anchorEarLabel, &anchorEarBox,
        &anchorLabel, &anchorSlider, &anchorNoteLabel, &anchorNoteBox,
        &anchorOctaveLabel, &anchorOctaveBox, &tuningReferenceLabel,
        &tuningReferenceSlider, &beatPresetLabel, &beatPresetBox,
        &beatLabel, &beatSlider, &waveformLabel, &waveformBox,
        &waveformHintLabel, &leftFrequencyLabel, &rightFrequencyLabel
    });

    setGroupVisible (mixVisible, {
        &noiseTypeLabel, &noiseTypeBox, &noiseVolumeLabel, &noiseVolumeSlider,
        &noiseHintLabel, &toneVolumeLabel, &toneVolumeSlider,
        &leftVolumeLabel, &leftVolumeSlider, &rightVolumeLabel,
        &rightVolumeSlider, &masterVolumeLabel, &masterVolumeSlider,
        &fadeInLabel, &fadeInSlider, &fadeOutLabel, &fadeOutSlider,
        &outputHintLabel, &outputMeterLabel, &leftOutputMeterLabel,
        &leftOutputMeter, &rightOutputMeterLabel, &rightOutputMeter,
        &peakGuardStatusLabel
    });
}

void BinauralJourneyAudioProcessorEditor::configureFrequencySlider (
    juce::Slider& slider,
    double defaultValue,
    int decimalPlaces)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 88, 26);
    slider.setTextValueSuffix (" Hz");
    slider.setNumDecimalPlacesToDisplay (decimalPlaces);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setColour (juce::Slider::trackColourId, accentColour);
    slider.setColour (juce::Slider::thumbColourId, accentColour);
    slider.setColour (juce::Slider::textBoxTextColourId, headingColour);
    slider.setColour (juce::Slider::textBoxBackgroundColourId,
                      juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, borderColour);
}

void BinauralJourneyAudioProcessorEditor::configurePercentSlider (
    juce::Slider& slider,
    double defaultValue)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 26);
    slider.setTextValueSuffix (" %");
    slider.setNumDecimalPlacesToDisplay (1);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setColour (juce::Slider::trackColourId, accentColour);
    slider.setColour (juce::Slider::thumbColourId, accentColour);
    slider.setColour (juce::Slider::textBoxTextColourId, headingColour);
    slider.setColour (juce::Slider::textBoxBackgroundColourId,
                      juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, borderColour);
}

void BinauralJourneyAudioProcessorEditor::configureSecondsSlider (
    juce::Slider& slider,
    double defaultValue)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 26);
    slider.setTextValueSuffix (" s");
    slider.setNumDecimalPlacesToDisplay (2);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setColour (juce::Slider::trackColourId, accentColour);
    slider.setColour (juce::Slider::thumbColourId, accentColour);
    slider.setColour (juce::Slider::textBoxTextColourId, headingColour);
    slider.setColour (juce::Slider::textBoxBackgroundColourId,
                      juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, borderColour);
}

void BinauralJourneyAudioProcessorEditor::configureMinutesSlider (
    juce::Slider& slider,
    double defaultValue)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 26);
    slider.setTextValueSuffix (" min");
    slider.setNumDecimalPlacesToDisplay (0);
    slider.setDoubleClickReturnValue (true, defaultValue);
    slider.setColour (juce::Slider::trackColourId, accentColour);
    slider.setColour (juce::Slider::thumbColourId, accentColour);
    slider.setColour (juce::Slider::textBoxTextColourId, headingColour);
    slider.setColour (juce::Slider::textBoxBackgroundColourId,
                      juce::Colours::transparentBlack);
    slider.setColour (juce::Slider::textBoxOutlineColourId, borderColour);
}

juce::String BinauralJourneyAudioProcessorEditor::formatSessionTime (
    double seconds)
{
    const int totalSeconds = juce::jmax (0, juce::roundToInt (seconds));
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int remainingSeconds = totalSeconds % 60;

    if (hours > 0)
        return juce::String (hours) + ":"
             + juce::String (minutes).paddedLeft ('0', 2) + ":"
             + juce::String (remainingSeconds).paddedLeft ('0', 2);

    return juce::String (minutes) + ":"
         + juce::String (remainingSeconds).paddedLeft ('0', 2);
}

void BinauralJourneyAudioProcessorEditor::timerCallback()
{
    updateSessionDisplay();
    updateJourneyControls();
    updatePlaybackSourceControls();
    updateFrequencyReadout();
    updateOutputMeters();
}

void BinauralJourneyAudioProcessorEditor::updateSessionDisplay()
{
    const double duration = audioProcessor.getSessionDurationSeconds();
    const double elapsed = juce::jlimit (
        0.0, duration, audioProcessor.getSessionElapsedSeconds());
    const bool followLogic = playbackSourceBox.getSelectedId() == 2;

    juce::String status;

    if (followLogic)
    {
        if (! audioProcessor.isHostTransportAvailable())
            status = "Waiting for Logic";
        else if (audioProcessor.isSessionFinished())
            status = "Journey finished";
        else if (audioProcessor.isHostTransportPlaying())
            status = "Following Logic";
        else
            status = "Logic stopped";
    }
    else if (audioProcessor.isSessionFinished())
    {
        status = "Finished";
    }
    else if (audioProcessor.isSessionPlaying())
    {
        status = "Playing";
    }
    else if (elapsed <= 0.01)
    {
        status = "Stopped";
    }
    else
    {
        status = "Paused";
    }

    sessionTimeLabel.setText (
        status + "   " + formatSessionTime (elapsed)
            + " / " + formatSessionTime (duration),
        juce::dontSendNotification);

    playPauseButton.setButtonText (
        followLogic
            ? "Logic"
            : (audioProcessor.isSessionPlaying() ? "Pause" : "Play"));
}

void BinauralJourneyAudioProcessorEditor::applySelectedAnchorNote()
{
    if (anchorModeBox.getSelectedId() != 2)
        return;

    const int noteIndex = juce::jmax (0, anchorNoteBox.getSelectedId() - 1);
    const int octave = juce::jlimit (2, 6, anchorOctaveBox.getSelectedId() + 1);
    const double tuning = tuningReferenceSlider.getValue();
    const double frequency =
        BinauralJourneyAudioProcessor::calculateMusicalNoteFrequency (
            noteIndex, octave, tuning);

    const juce::ScopedValueSetter<bool> noteGuard (applyingAnchorNote, true);
    anchorSlider.setValue (frequency, juce::sendNotificationSync);
}

void BinauralJourneyAudioProcessorEditor::updateAnchorModeControls()
{
    const bool usingMusicalNote = anchorModeBox.getSelectedId() == 2;

    anchorSlider.setEnabled (! usingMusicalNote);
    anchorNoteBox.setEnabled (usingMusicalNote);
    anchorOctaveBox.setEnabled (usingMusicalNote);
    tuningReferenceSlider.setEnabled (usingMusicalNote);

    anchorLabel.setAlpha (usingMusicalNote ? 0.45f : 1.0f);
    anchorSlider.setAlpha (usingMusicalNote ? 0.45f : 1.0f);
    anchorNoteLabel.setAlpha (usingMusicalNote ? 1.0f : 0.45f);
    anchorNoteBox.setAlpha (usingMusicalNote ? 1.0f : 0.45f);
    anchorOctaveLabel.setAlpha (usingMusicalNote ? 1.0f : 0.45f);
    anchorOctaveBox.setAlpha (usingMusicalNote ? 1.0f : 0.45f);
    tuningReferenceLabel.setAlpha (usingMusicalNote ? 1.0f : 0.45f);
    tuningReferenceSlider.setAlpha (usingMusicalNote ? 1.0f : 0.45f);
}

void BinauralJourneyAudioProcessorEditor::updateNoiseControls()
{
    const bool hasNoise = noiseTypeBox.getSelectedId() > 1;
    noiseVolumeSlider.setEnabled (hasNoise);
    noiseVolumeLabel.setAlpha (hasNoise ? 1.0f : 0.45f);
    noiseVolumeSlider.setAlpha (hasNoise ? 1.0f : 0.45f);
}

void BinauralJourneyAudioProcessorEditor::updateToneControls()
{
    const bool toneOn = toneEnabledButton.getToggleState();

    toneVolumeSlider.setEnabled (toneOn);
    leftVolumeSlider.setEnabled (toneOn);
    rightVolumeSlider.setEnabled (toneOn);

    toneVolumeLabel.setAlpha (toneOn ? 1.0f : 0.45f);
    toneVolumeSlider.setAlpha (toneOn ? 1.0f : 0.45f);
    leftVolumeLabel.setAlpha (toneOn ? 1.0f : 0.45f);
    leftVolumeSlider.setAlpha (toneOn ? 1.0f : 0.45f);
    rightVolumeLabel.setAlpha (toneOn ? 1.0f : 0.45f);
    rightVolumeSlider.setAlpha (toneOn ? 1.0f : 0.45f);
}

void BinauralJourneyAudioProcessorEditor::updateJourneyControls()
{
    const bool journeyMode = journeyModeBox.getSelectedId() == 2;

    editJourneyButton.setEnabled (journeyMode);
    editJourneyButton.setAlpha (journeyMode ? 1.0f : 0.45f);

    sessionDurationSlider.setEnabled (! journeyMode);
    sessionDurationLabel.setAlpha (journeyMode ? 0.45f : 1.0f);
    sessionDurationSlider.setAlpha (journeyMode ? 0.45f : 1.0f);

    beatPresetBox.setEnabled (! journeyMode);
    beatSlider.setEnabled (! journeyMode);
    beatPresetLabel.setAlpha (journeyMode ? 0.45f : 1.0f);
    beatPresetBox.setAlpha (journeyMode ? 0.45f : 1.0f);
    beatLabel.setAlpha (journeyMode ? 0.45f : 1.0f);
    beatSlider.setAlpha (journeyMode ? 0.45f : 1.0f);

    if (! journeyMode)
    {
        journeySummaryLabel.setText (
            "Single tone uses the beat controls in Tone and pitch.",
            juce::dontSendNotification);
        return;
    }

    juce::String segmentText;
    const int segment = audioProcessor.getJourneySegment();

    if (segment <= 0)
    {
        segmentText = "Ready";
    }
    else if ((segment % 2) == 1)
    {
        segmentText = "Stage " + juce::String ((segment + 1) / 2);
    }
    else
    {
        const int fromStage = segment / 2;
        segmentText = "Transition " + juce::String (fromStage)
                    + " to " + juce::String (fromStage + 1);
    }

    const auto* stageCountParameter =
        audioProcessor.getParameterState().getRawParameterValue (
            "journeyStageCount");
    const int stageCount = stageCountParameter != nullptr
        ? 2 + juce::roundToInt (stageCountParameter->load())
        : 3;

    journeySummaryLabel.setText (
        "Total " + formatDurationMinutes (
            audioProcessor.getSessionDurationSeconds() / 60.0)
            + " | " + juce::String (stageCount) + " stages | "
            + segmentText + " | "
            + juce::String (
                audioProcessor.getCurrentBeatFrequency(), 2)
            + " Hz",
        juce::dontSendNotification);
}

void BinauralJourneyAudioProcessorEditor::updatePlaybackSourceControls()
{
    const bool followLogic = playbackSourceBox.getSelectedId() == 2;
    const float controlAlpha = followLogic ? 0.45f : 1.0f;

    playPauseButton.setEnabled (! followLogic);
    restartSessionButton.setEnabled (! followLogic);
    stopSessionButton.setEnabled (! followLogic);
    playPauseButton.setAlpha (controlAlpha);
    restartSessionButton.setAlpha (controlAlpha);
    stopSessionButton.setAlpha (controlAlpha);

    playbackSourceBox.setTooltip (
        followLogic
            ? "Logic's play, stop, and timeline position control the session."
            : "Use the plug-in's Play, Pause, Restart, and Stop controls.");
}

void BinauralJourneyAudioProcessorEditor::bringJourneyWindowToFront()
{
    if (journeyEditorWindow == nullptr)
        return;

    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeEditor (
        this);

    auto bringForward = [] (
        juce::Component::SafePointer<juce::DocumentWindow> safeWindow,
        juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeEditor,
        bool refreshVisibility)
    {
        if (safeWindow == nullptr)
            return;

        safeWindow->setMinimised (false);

        if (refreshVisibility)
        {
            safeWindow->setVisible (false);
            safeWindow->setVisible (true);
        }
        else
        {
            safeWindow->setVisible (true);
        }

        safeWindow->setAlwaysOnTop (true);
        safeWindow->toFront (true);
        safeWindow->grabKeyboardFocus();

        if (auto* peer = safeWindow->getPeer())
            peer->toFront (true);

       #if JUCE_MAC
        attachAndRaiseMacWindow (*safeWindow, safeEditor.getComponent());
       #endif
    };

    juce::Component::SafePointer<juce::DocumentWindow> safeWindow (
        journeyEditorWindow.get());

    bringForward (safeWindow, safeEditor, false);

    // Logic often restores focus to its plug-in shell after the button event.
    // Repeat after the event and again after the host's delayed focus pass.
    juce::Timer::callAfterDelay (60, [safeWindow, safeEditor, bringForward]
    {
        bringForward (safeWindow, safeEditor, true);
    });

    juce::Timer::callAfterDelay (180, [safeWindow, safeEditor, bringForward]
    {
        bringForward (safeWindow, safeEditor, false);
    });

    juce::Timer::callAfterDelay (420, [safeWindow, safeEditor, bringForward]
    {
        bringForward (safeWindow, safeEditor, false);
    });
}

void BinauralJourneyAudioProcessorEditor::resizeJourneyWindowForStageCount (
    int stageCount)
{
    if (journeyEditorWindow == nullptr)
        return;

    int desiredHeight = journeyWindowHeightForStageCount (stageCount);

    if (auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (
            journeyEditorWindow->getBounds()))
    {
        desiredHeight = juce::jmin (desiredHeight,
                                    display->userArea.getHeight() - 40);
    }

    desiredHeight = juce::jmax (journeyWindowMinimumHeight, desiredHeight);

    const auto oldBounds = journeyEditorWindow->getBounds();
    auto newBounds = oldBounds.withSizeKeepingCentre (journeyWindowWidth,
                                                       desiredHeight);

    if (auto* lockedWindow = dynamic_cast<JourneyDocumentWindow*> (
            journeyEditorWindow.get()))
    {
        lockedWindow->setLockedBounds (newBounds);
    }
    else
    {
        journeyEditorWindow->setBounds (newBounds);
    }

    journeyWindowHeight = desiredHeight;
}

void BinauralJourneyAudioProcessorEditor::openJourneyWindow()
{
    if (journeyEditorWindow != nullptr)
    {
        bringJourneyWindowToFront();
        return;
    }

    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeThis (
        this);

    const auto requestClose = [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->closeJourneyWindow();
        });
    };

    const auto stageCountChanged = [safeThis] (int stageCount)
    {
        juce::MessageManager::callAsync ([safeThis, stageCount]
        {
            if (safeThis != nullptr)
                safeThis->resizeJourneyWindowForStageCount (stageCount);
        });
    };

    const auto* stageCountParameter =
        audioProcessor.getParameterState().getRawParameterValue (
            "journeyStageCount");
    const int stageCount = stageCountParameter != nullptr
        ? juce::jlimit (2, 6, 2 + juce::roundToInt (stageCountParameter->load()))
        : 3;

    auto journeyEditor = std::make_unique<JourneyEditorComponent> (
        audioProcessor, requestClose, stageCountChanged);
    auto content = std::make_unique<JourneyWindowContent> (
        std::move (journeyEditor));
    journeyWindowHeight = journeyWindowHeightForStageCount (stageCount);
    auto window = std::make_unique<JourneyDocumentWindow> (
        requestClose, journeyWindowWidth, journeyWindowHeight);

    window->setContentOwned (content.release(), true);
    window->centreAroundComponent (this, journeyWindowWidth, journeyWindowHeight);
    window->setLockedBounds (window->getBounds());
    journeyEditorWindow = std::move (window);
    resizeJourneyWindowForStageCount (stageCount);
    bringJourneyWindowToFront();
}

void BinauralJourneyAudioProcessorEditor::closeJourneyWindow()
{
    if (journeyEditorWindow == nullptr)
        return;

   #if JUCE_MAC
    detachMacChildWindow (*journeyEditorWindow);
   #endif

    journeyEditorWindow->setVisible (false);
    journeyEditorWindow.reset();
}

void BinauralJourneyAudioProcessorEditor::updateOutputMeters()
{
    leftOutputMeterValue = juce::jlimit (
        0.0, 1.0,
        static_cast<double> (audioProcessor.getLeftOutputPeak()));
    rightOutputMeterValue = juce::jlimit (
        0.0, 1.0,
        static_cast<double> (audioProcessor.getRightOutputPeak()));

    leftOutputMeter.repaint();
    rightOutputMeter.repaint();

    const auto eventCount = audioProcessor.getPeakGuardEventCount();

    if (eventCount != lastPeakGuardEventCount)
    {
        lastPeakGuardEventCount = eventCount;
        peakGuardStatusTicks = 18;
    }
    else if (peakGuardStatusTicks > 0)
    {
        --peakGuardStatusTicks;
    }

    const bool recentlyLimited = peakGuardStatusTicks > 0;
    peakGuardStatusLabel.setText (
        recentlyLimited
            ? "Peak guard active - lower Master output if this repeats."
            : "Peak guard ready",
        juce::dontSendNotification);
    peakGuardStatusLabel.setColour (
        juce::Label::textColourId,
        recentlyLimited ? juce::Colours::darkorange : bodyColour);
}

void BinauralJourneyAudioProcessorEditor::setParameterValue (
    const juce::String& parameterID,
    float value)
{
    if (auto* parameter =
            audioProcessor.getParameterState().getParameter (parameterID))
    {
        parameter->setValueNotifyingHost (
            parameter->convertTo0to1 (value));
    }
}

double BinauralJourneyAudioProcessorEditor::getEffectiveAnchorFrequency() const
{
    if (anchorModeBox.getSelectedId() != 2)
        return anchorSlider.getValue();

    const int noteIndex = juce::jmax (0, anchorNoteBox.getSelectedId() - 1);
    const int octave = juce::jlimit (2, 6, anchorOctaveBox.getSelectedId() + 1);

    return BinauralJourneyAudioProcessor::calculateMusicalNoteFrequency (
        noteIndex, octave, tuningReferenceSlider.getValue());
}

double BinauralJourneyAudioProcessorEditor::getSelectedPresetFrequency() const
{
    switch (beatPresetBox.getSelectedId())
    {
        case 2: return 2.0;
        case 3: return 6.0;
        case 4: return 7.83;
        case 5: return 10.0;
        case 6: return 20.0;
        case 7: return 40.0;
        case 1:
        default: return -1.0;
    }
}

void BinauralJourneyAudioProcessorEditor::applySelectedBeatPreset()
{
    const double presetFrequency = getSelectedPresetFrequency();

    if (presetFrequency <= 0.0)
        return;

    const juce::ScopedValueSetter<bool> presetGuard (applyingBeatPreset, true);
    beatSlider.setValue (presetFrequency, juce::sendNotificationSync);
}

void BinauralJourneyAudioProcessorEditor::updateFrequencyReadout()
{
    const double anchor = getEffectiveAnchorFrequency();
    const double beat = journeyModeBox.getSelectedId() == 2
        ? audioProcessor.getCurrentBeatFrequency()
        : beatSlider.getValue();
    const bool rightIsAnchor = anchorEarBox.getSelectedId() == 2;

    const double left = rightIsAnchor ? anchor + beat : anchor;
    const double right = rightIsAnchor ? anchor : anchor + beat;

    leftFrequencyLabel.setText (
        "Left: " + juce::String (left, 2) + " Hz",
        juce::dontSendNotification);

    rightFrequencyLabel.setText (
        "Right: " + juce::String (right, 2) + " Hz",
        juce::dontSendNotification);
}

void BinauralJourneyAudioProcessorEditor::applySelectedSoundPreset()
{
    const int preset = soundPresetBox.getSelectedId();

    if (preset > 1)
    {
        userPresetBox.setSelectedId (1, juce::dontSendNotification);
        updateUserPresetButtons();
    }

    if (preset <= 1)
    {
        presetHintLabel.setText (
            "Choose a factory sound preset to apply it automatically.",
            juce::dontSendNotification);
        return;
    }

    outputEnabledButton.setToggleState (true, juce::sendNotificationSync);
    toneEnabledButton.setToggleState (true, juce::sendNotificationSync);
    journeyModeBox.setSelectedId (1, juce::sendNotificationSync);
    playbackSourceBox.setSelectedId (1, juce::sendNotificationSync);

    anchorModeBox.setSelectedId (2, juce::sendNotificationSync);
    anchorNoteBox.setSelectedId (10, juce::sendNotificationSync); // A
    anchorOctaveBox.setSelectedId (2, juce::sendNotificationSync); // Octave 3
    tuningReferenceSlider.setValue (440.0, juce::sendNotificationSync);

    waveformBox.setSelectedId (1, juce::sendNotificationSync);
    anchorEarBox.setSelectedId (1, juce::sendNotificationSync);
    toneVolumeSlider.setValue (10.0, juce::sendNotificationSync);
    leftVolumeSlider.setValue (100.0, juce::sendNotificationSync);
    rightVolumeSlider.setValue (100.0, juce::sendNotificationSync);
    masterVolumeSlider.setValue (100.0, juce::sendNotificationSync);
    fadeInSlider.setValue (2.0, juce::sendNotificationSync);
    fadeOutSlider.setValue (2.0, juce::sendNotificationSync);

    // Factory sound presets are tone presets. Ambient noise stays off.
    noiseTypeBox.setSelectedId (1, juce::sendNotificationSync);
    noiseVolumeSlider.setValue (8.0, juce::sendNotificationSync);

    int beatPresetId = 5;

    switch (preset)
    {
        case 2: beatPresetId = 2; break;
        case 3: beatPresetId = 3; break;
        case 4: beatPresetId = 4; break;
        case 5: beatPresetId = 5; break;
        case 6: beatPresetId = 6; break;
        case 7: beatPresetId = 7; break;
        default: break;
    }

    beatPresetBox.setSelectedId (beatPresetId, juce::sendNotificationSync);

    presetHintLabel.setText (
        "Preset applied. Adjust any control to personalize it.",
        juce::dontSendNotification);

    // Keep the applied factory preset selected so the user can see
    // which preset is currently being used.
    updateAnchorModeControls();
    updateNoiseControls();
    updateToneControls();
    updateJourneyControls();
    updateFrequencyReadout();
}

void BinauralJourneyAudioProcessorEditor::restoreDefaultSettings()
{
    outputEnabledButton.setToggleState (true, juce::sendNotificationSync);
    toneEnabledButton.setToggleState (true, juce::sendNotificationSync);
    journeyModeBox.setSelectedId (1, juce::sendNotificationSync);

    anchorModeBox.setSelectedId (1, juce::sendNotificationSync);
    anchorSlider.setValue (200.0, juce::sendNotificationSync);
    anchorNoteBox.setSelectedId (10, juce::sendNotificationSync);
    anchorOctaveBox.setSelectedId (3, juce::sendNotificationSync);
    tuningReferenceSlider.setValue (440.0, juce::sendNotificationSync);

    beatPresetBox.setSelectedId (5, juce::sendNotificationSync);
    waveformBox.setSelectedId (1, juce::sendNotificationSync);
    anchorEarBox.setSelectedId (1, juce::sendNotificationSync);

    noiseTypeBox.setSelectedId (1, juce::sendNotificationSync);
    noiseVolumeSlider.setValue (8.0, juce::sendNotificationSync);

    toneVolumeSlider.setValue (10.0, juce::sendNotificationSync);
    leftVolumeSlider.setValue (100.0, juce::sendNotificationSync);
    rightVolumeSlider.setValue (100.0, juce::sendNotificationSync);
    masterVolumeSlider.setValue (100.0, juce::sendNotificationSync);
    fadeInSlider.setValue (0.25, juce::sendNotificationSync);
    fadeOutSlider.setValue (0.25, juce::sendNotificationSync);
    sessionDurationSlider.setValue (10.0, juce::sendNotificationSync);

    setParameterValue ("stage1BeatPreset", 4.0f); // Alpha
    setParameterValue ("stage1Beat", 10.0f);
    setParameterValue ("stage1HoldMinutes", 5.0f);
    setParameterValue ("stage1TransitionMinutes", 1.0f);
    setParameterValue ("stage2BeatPreset", 2.0f); // Theta
    setParameterValue ("stage2Beat", 6.0f);
    setParameterValue ("stage2HoldMinutes", 10.0f);
    setParameterValue ("stage2TransitionMinutes", 2.0f);
    setParameterValue ("journeyStageCount", 1.0f); // Three stages by default
    setParameterValue ("stage3BeatPreset", 1.0f); // Delta
    setParameterValue ("stage3Beat", 2.0f);
    setParameterValue ("stage3HoldMinutes", 15.0f);
    setParameterValue ("stage3TransitionMinutes", 1.0f);
    setParameterValue ("stage4BeatPreset", 4.0f); // Alpha
    setParameterValue ("stage4Beat", 10.0f);
    setParameterValue ("stage4HoldMinutes", 5.0f);
    setParameterValue ("stage4TransitionMinutes", 1.0f);
    setParameterValue ("stage5BeatPreset", 2.0f); // Theta
    setParameterValue ("stage5Beat", 6.0f);
    setParameterValue ("stage5HoldMinutes", 5.0f);
    setParameterValue ("stage5TransitionMinutes", 1.0f);
    setParameterValue ("stage6BeatPreset", 1.0f); // Delta
    setParameterValue ("stage6Beat", 2.0f);
    setParameterValue ("stage6HoldMinutes", 5.0f);

    audioProcessor.restartSession();

    soundPresetBox.setSelectedId (1, juce::dontSendNotification);
    userPresetBox.setSelectedId (1, juce::dontSendNotification);
    updateUserPresetButtons();
    userPresetHintLabel.setText (
        "Defaults are active. Choose Save as... to keep a customized setup.",
        juce::dontSendNotification);
    presetHintLabel.setText (
        "Defaults restored. Ambient noise is set to None.",
        juce::dontSendNotification);

    updateAnchorModeControls();
    updateNoiseControls();
    updateToneControls();
    updateJourneyControls();
    updatePlaybackSourceControls();
    updateFrequencyReadout();
}


juce::File BinauralJourneyAudioProcessorEditor::getUserPresetFile() const
{
    auto directory = juce::File::getSpecialLocation (
        juce::File::userApplicationDataDirectory)
        .getChildFile ("BinauralJourney");

    (void) directory.createDirectory();
    return directory.getChildFile ("UserPresets.xml");
}

std::unique_ptr<juce::XmlElement>
BinauralJourneyAudioProcessorEditor::loadUserPresetDocument() const
{
    const auto file = getUserPresetFile();

    if (file.existsAsFile())
    {
        auto document = juce::XmlDocument::parse (file);

        if (document != nullptr && document->hasTagName (userPresetRootTag))
            return document;
    }

    return std::make_unique<juce::XmlElement> (userPresetRootTag);
}

bool BinauralJourneyAudioProcessorEditor::writeUserPresetDocument (
    const juce::XmlElement& document) const
{
    return getUserPresetFile().replaceWithText (document.toString());
}

void BinauralJourneyAudioProcessorEditor::refreshUserPresetList (
    const juce::String& selectedName)
{
    const juce::ScopedValueSetter<bool> refreshGuard (
        refreshingUserPresetList, true);

    userPresetBox.clear (juce::dontSendNotification);
    userPresetBox.addItem ("Choose a saved preset...", 1);

    const auto document = loadUserPresetDocument();
    juce::StringArray names;

    for (auto* child = document->getFirstChildElement(); child != nullptr;
         child = child->getNextElement())
    {
        if (child->hasTagName (userPresetTag))
        {
            const auto name = child->getStringAttribute ("name").trim();

            if (name.isNotEmpty())
                names.addIfNotAlreadyThere (name, true);
        }
    }

    names.sort (true);

    for (int index = 0; index < names.size(); ++index)
        userPresetBox.addItem (names[index], index + 2);

    int selectedId = 1;

    if (selectedName.isNotEmpty())
    {
        for (int index = 0; index < names.size(); ++index)
        {
            if (names[index].equalsIgnoreCase (selectedName))
            {
                selectedId = index + 2;
                break;
            }
        }
    }

    userPresetBox.setSelectedId (selectedId, juce::dontSendNotification);

    if (names.isEmpty())
    {
        userPresetHintLabel.setText (
            "No custom presets saved yet. Save either a single tone or a journey.",
            juce::dontSendNotification);
    }

    updateUserPresetButtons();
}

void BinauralJourneyAudioProcessorEditor::updateUserPresetButtons()
{
    const bool hasSelection = userPresetBox.getSelectedId() > 1;
    renameUserPresetButton.setEnabled (hasSelection);
    deleteUserPresetButton.setEnabled (hasSelection);
    renameUserPresetButton.setAlpha (hasSelection ? 1.0f : 0.45f);
    deleteUserPresetButton.setAlpha (hasSelection ? 1.0f : 0.45f);
}

void BinauralJourneyAudioProcessorEditor::saveCurrentUserPreset()
{
    const auto suggestedName = userPresetBox.getSelectedId() > 1
        ? userPresetBox.getText()
        : juce::String();

    presetDialog = std::make_unique<juce::AlertWindow> (
        "Save custom preset",
        "Enter a name. Using an existing name replaces that preset.",
        juce::MessageBoxIconType::NoIcon,
        this);

    presetDialog->addTextEditor (
        "presetName", suggestedName, "Preset name");
    presetDialog->addButton (
        "Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton (
        "Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeThis (this);

    presetDialog->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [safeThis] (int result)
            {
                if (safeThis == nullptr)
                    return;

                auto dialog = std::move (safeThis->presetDialog);

                if (dialog == nullptr || result != 1)
                    return;

                const auto name = dialog->getTextEditorContents (
                    "presetName").trim();

                if (name.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "Preset name required",
                        "Please enter a name before saving.",
                        "OK",
                        safeThis.getComponent());
                    return;
                }

                safeThis->saveCurrentUserPresetNamed (name);
            }));
}

void BinauralJourneyAudioProcessorEditor::saveCurrentUserPresetNamed (
    const juce::String& name)
{
    auto document = loadUserPresetDocument();
    auto* preset = findUserPresetByName (*document, name);

    if (preset == nullptr)
    {
        preset = new juce::XmlElement (userPresetTag);
        document->addChildElement (preset);
    }

    preset->setAttribute ("name", name.trim());
    preset->deleteAllChildElements();

    auto stateXml = audioProcessor.getParameterState().copyState().createXml();

    if (stateXml == nullptr)
        return;

    preset->addChildElement (stateXml.release());

    if (! writeUserPresetDocument (*document))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Preset could not be saved",
            "The preset file could not be written to your user settings folder.",
            "OK",
            this);
        return;
    }

    refreshUserPresetList (name);
    userPresetHintLabel.setText (
        "Saved " + name + ". It includes the current tone, mix, and journey settings.",
        juce::dontSendNotification);
}

void BinauralJourneyAudioProcessorEditor::loadSelectedUserPreset()
{
    if (userPresetBox.getSelectedId() <= 1)
        return;

    const auto selectedName = userPresetBox.getText().trim();
    auto document = loadUserPresetDocument();
    auto* preset = findUserPresetByName (*document, selectedName);

    if (preset == nullptr)
    {
        refreshUserPresetList();
        return;
    }

    auto* stateXml = preset->getFirstChildElement();

    if (stateXml == nullptr
        || ! stateXml->hasTagName (
            audioProcessor.getParameterState().state.getType()))
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Preset could not be loaded",
            "The saved preset does not contain valid Binaural Journey settings.",
            "OK",
            this);
        return;
    }

    const auto state = juce::ValueTree::fromXml (*stateXml);

    if (! state.isValid())
        return;

    const bool hasFlexibleStageParameters = stateContainsParameter (
        state, "journeyStageCount");
    const bool hasPlaybackSource = stateContainsParameter (
        state, "playbackSource");

    audioProcessor.getParameterState().replaceState (state);

    if (! hasPlaybackSource)
        setParameterValue ("playbackSource", 0.0f);

    if (! hasFlexibleStageParameters)
    {
        // Presets saved by the earlier three-stage build remain compatible.
        // They reopen as three-stage journeys, while the new stages receive
        // sensible defaults and stay hidden until the stage count is increased.
        setParameterValue ("journeyStageCount", 1.0f);
        setParameterValue ("stage3TransitionMinutes", 1.0f);
        setParameterValue ("stage4BeatPreset", 4.0f);
        setParameterValue ("stage4Beat", 10.0f);
        setParameterValue ("stage4HoldMinutes", 5.0f);
        setParameterValue ("stage4TransitionMinutes", 1.0f);
        setParameterValue ("stage5BeatPreset", 2.0f);
        setParameterValue ("stage5Beat", 6.0f);
        setParameterValue ("stage5HoldMinutes", 5.0f);
        setParameterValue ("stage5TransitionMinutes", 1.0f);
        setParameterValue ("stage6BeatPreset", 1.0f);
        setParameterValue ("stage6Beat", 2.0f);
        setParameterValue ("stage6HoldMinutes", 5.0f);
    }

    audioProcessor.restartSession();
    soundPresetBox.setSelectedId (1, juce::dontSendNotification);

    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeThis (this);
    juce::MessageManager::callAsync (
        [safeThis, selectedName]
        {
            if (safeThis == nullptr)
                return;

            safeThis->updateAnchorModeControls();
            safeThis->updateNoiseControls();
            safeThis->updateToneControls();
            safeThis->updateJourneyControls();
            safeThis->updatePlaybackSourceControls();
            safeThis->updateFrequencyReadout();
            safeThis->updateSessionDisplay();
            safeThis->userPresetHintLabel.setText (
                "Loaded " + selectedName + ". All controls remain editable.",
                juce::dontSendNotification);
        });
}

void BinauralJourneyAudioProcessorEditor::renameSelectedUserPreset()
{
    if (userPresetBox.getSelectedId() <= 1)
        return;

    const auto oldName = userPresetBox.getText().trim();
    presetDialog = std::make_unique<juce::AlertWindow> (
        "Rename custom preset",
        "Enter a new name for " + oldName + ".",
        juce::MessageBoxIconType::NoIcon,
        this);

    presetDialog->addTextEditor ("presetName", oldName, "Preset name");
    presetDialog->addButton (
        "Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton (
        "Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeThis (this);

    presetDialog->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [safeThis, oldName] (int result)
            {
                if (safeThis == nullptr)
                    return;

                auto dialog = std::move (safeThis->presetDialog);

                if (dialog == nullptr || result != 1)
                    return;

                const auto newName = dialog->getTextEditorContents (
                    "presetName").trim();

                if (newName.isEmpty())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "Preset name required",
                        "Please enter a name before renaming.",
                        "OK",
                        safeThis.getComponent());
                    return;
                }

                safeThis->renameSelectedUserPresetTo (oldName, newName);
            }));
}

void BinauralJourneyAudioProcessorEditor::renameSelectedUserPresetTo (
    const juce::String& oldName,
    const juce::String& newName)
{
    auto document = loadUserPresetDocument();
    auto* preset = findUserPresetByName (*document, oldName);

    if (preset == nullptr)
        return;

    auto* duplicate = findUserPresetByName (*document, newName);

    if (duplicate != nullptr && duplicate != preset)
    {
        juce::AlertWindow::showMessageBoxAsync (
            juce::MessageBoxIconType::WarningIcon,
            "Name already used",
            "Another custom preset already uses that name.",
            "OK",
            this);
        return;
    }

    preset->setAttribute ("name", newName.trim());

    if (! writeUserPresetDocument (*document))
        return;

    refreshUserPresetList (newName);
    userPresetHintLabel.setText (
        "Renamed " + oldName + " to " + newName + ".",
        juce::dontSendNotification);
}

void BinauralJourneyAudioProcessorEditor::deleteSelectedUserPreset()
{
    if (userPresetBox.getSelectedId() <= 1)
        return;

    const auto selectedName = userPresetBox.getText().trim();
    presetDialog = std::make_unique<juce::AlertWindow> (
        "Delete custom preset?",
        "Delete " + selectedName + "? This cannot be undone.",
        juce::MessageBoxIconType::QuestionIcon,
        this);

    presetDialog->addButton ("Delete", 1);
    presetDialog->addButton (
        "Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<BinauralJourneyAudioProcessorEditor> safeThis (this);

    presetDialog->enterModalState (
        true,
        juce::ModalCallbackFunction::create (
            [safeThis, selectedName] (int result)
            {
                if (safeThis == nullptr)
                    return;

                auto dialog = std::move (safeThis->presetDialog);

                if (dialog == nullptr || result != 1)
                    return;

                auto document = safeThis->loadUserPresetDocument();
                auto* preset = findUserPresetByName (*document, selectedName);

                if (preset == nullptr)
                    return;

                document->removeChildElement (preset, true);

                if (! safeThis->writeUserPresetDocument (*document))
                    return;

                safeThis->refreshUserPresetList();
                safeThis->userPresetHintLabel.setText (
                    "Deleted " + selectedName + ".",
                    juce::dontSendNotification);
            }));
}

//==============================================================================
void BinauralJourneyAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (pageBackground);

    const auto layout = calculateEditorLayout (getLocalBounds());
    const auto cardFloat = layout.contentCard.toFloat();

    g.setColour (juce::Colour { 0x14000000 });
    g.fillRoundedRectangle (cardFloat.translated (0.0f, 2.0f), 11.0f);

    g.setColour (panelBackground);
    g.fillRoundedRectangle (cardFloat, 11.0f);

    g.setColour (borderColour);
    g.drawRoundedRectangle (cardFloat, 11.0f, 1.0f);

    auto contentCard = layout.contentCard;
    auto titleArea = contentCard.removeFromTop (38).reduced (16, 0);
    g.setColour (accentColour);
    g.fillRoundedRectangle (
        juce::Rectangle<float> (static_cast<float> (titleArea.getX()),
                                static_cast<float> (titleArea.getCentreY() - 8),
                                4.0f,
                                16.0f),
        2.0f);

    const juce::String pageTitle = activePage == 0
        ? "Setup and playback"
        : (activePage == 1 ? "Tone and pitch" : "Mix and output");

    g.setColour (headingColour);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText (pageTitle, titleArea.withTrimmedLeft (12),
                juce::Justification::centredLeft, false);
}

void BinauralJourneyAudioProcessorEditor::resized()
{
    const auto layout = calculateEditorLayout (getLocalBounds());

    auto header = layout.header;
    auto toggles = header.removeFromRight (270);
    auto helpArea = header.removeFromRight (42);
    auto titleArea = header;

    titleLabel.setBounds (titleArea.removeFromTop (28));
    subtitleLabel.setBounds (titleArea.removeFromTop (18));

    helpAboutButton.setBounds (helpArea.reduced (5, 7));
    outputEnabledButton.setBounds (toggles.removeFromLeft (138).reduced (3, 9));
    toneEnabledButton.setBounds (toggles.reduced (3, 9));

    auto tabs = layout.tabs;
    constexpr int tabGap = 8;
    const int tabWidth = (tabs.getWidth() - tabGap * 2) / 3;
    setupTabButton.setBounds (tabs.removeFromLeft (tabWidth));
    tabs.removeFromLeft (tabGap);
    toneTabButton.setBounds (tabs.removeFromLeft (tabWidth));
    tabs.removeFromLeft (tabGap);
    mixTabButton.setBounds (tabs);

    auto placeLabeledControl = [] (juce::Label& label,
                                   juce::Component& control,
                                   juce::Rectangle<int> area)
    {
        label.setBounds (area.removeFromTop (20));
        control.setBounds (area.reduced (0, 2));
    };

    auto placeAlignedButton = [] (juce::TextButton& button,
                                  juce::Rectangle<int> area,
                                  int horizontalInset)
    {
        area.removeFromTop (20);
        button.setBounds (area.reduced (horizontalInset, 2));
    };

    auto content = layout.contentCard.reduced (16);
    content.removeFromTop (38);

    if (activePage == 0)
    {
        auto presetRow = content.removeFromTop (50);
        auto presetArea = presetRow.removeFromLeft (500);
        presetRow.removeFromLeft (12);
        auto resetArea = presetRow.removeFromLeft (150);
        placeLabeledControl (soundPresetLabel, soundPresetBox, presetArea);
        placeAlignedButton (resetButton, resetArea, 6);
        presetHintLabel.setBounds (content.removeFromTop (18));

        content.removeFromTop (5);
        auto userPresetRow = content.removeFromTop (50);
        auto userPresetArea = userPresetRow.removeFromLeft (360);
        userPresetRow.removeFromLeft (8);
        auto savePresetArea = userPresetRow.removeFromLeft (108);
        userPresetRow.removeFromLeft (6);
        auto renamePresetArea = userPresetRow.removeFromLeft (92);
        userPresetRow.removeFromLeft (6);
        auto deletePresetArea = userPresetRow.removeFromLeft (82);
        placeLabeledControl (userPresetLabel, userPresetBox, userPresetArea);
        placeAlignedButton (saveUserPresetButton, savePresetArea, 3);
        placeAlignedButton (renameUserPresetButton, renamePresetArea, 3);
        placeAlignedButton (deleteUserPresetButton, deletePresetArea, 3);
        userPresetHintLabel.setBounds (content.removeFromTop (18));

        content.removeFromTop (7);
        auto journeyRow = content.removeFromTop (56);
        auto journeyModeArea = journeyRow.removeFromLeft (250);
        journeyRow.removeFromLeft (10);
        auto journeyEditArea = journeyRow.removeFromLeft (132);
        journeyRow.removeFromLeft (10);
        placeLabeledControl (journeyModeLabel, journeyModeBox, journeyModeArea);
        placeAlignedButton (editJourneyButton, journeyEditArea, 4);
        journeySummaryLabel.setBounds (journeyRow.reduced (2, 14));

        content.removeFromTop (7);
        auto sessionRow = content.removeFromTop (62);
        auto sourceArea = sessionRow.removeFromLeft (190);
        sessionRow.removeFromLeft (8);
        auto durationArea = sessionRow.removeFromLeft (180);
        sessionRow.removeFromLeft (8);
        auto playArea = sessionRow.removeFromLeft (62);
        auto restartArea = sessionRow.removeFromLeft (70);
        auto stopArea = sessionRow.removeFromLeft (58);
        sessionRow.removeFromLeft (8);
        placeLabeledControl (playbackSourceLabel, playbackSourceBox, sourceArea);
        placeLabeledControl (sessionDurationLabel, sessionDurationSlider, durationArea);
        placeAlignedButton (playPauseButton, playArea, 2);
        placeAlignedButton (restartSessionButton, restartArea, 2);
        placeAlignedButton (stopSessionButton, stopArea, 2);

        auto sessionStatusArea = sessionRow;
        sessionStatusArea.removeFromTop (20);
        sessionTimeLabel.setBounds (sessionStatusArea.reduced (0, 2));
    }
    else if (activePage == 1)
    {
        auto sourceRow = content.removeFromTop (56);
        auto sourceLeft = sourceRow.removeFromLeft ((sourceRow.getWidth() - 10) / 2);
        sourceRow.removeFromLeft (10);
        placeLabeledControl (anchorModeLabel, anchorModeBox, sourceLeft);
        placeLabeledControl (anchorEarLabel, anchorEarBox, sourceRow);

        content.removeFromTop (6);
        placeLabeledControl (anchorLabel, anchorSlider, content.removeFromTop (56));

        content.removeFromTop (6);
        auto noteRow = content.removeFromTop (60);
        auto noteArea = noteRow.removeFromLeft (noteRow.getWidth() * 45 / 100);
        noteRow.removeFromLeft (8);
        auto octaveArea = noteRow.removeFromLeft (82);
        noteRow.removeFromLeft (8);
        placeLabeledControl (anchorNoteLabel, anchorNoteBox, noteArea);
        placeLabeledControl (anchorOctaveLabel, anchorOctaveBox, octaveArea);
        placeLabeledControl (tuningReferenceLabel, tuningReferenceSlider, noteRow);

        content.removeFromTop (7);
        auto beatRow = content.removeFromTop (60);
        auto beatPresetArea = beatRow.removeFromLeft (beatRow.getWidth() * 40 / 100);
        beatRow.removeFromLeft (10);
        placeLabeledControl (beatPresetLabel, beatPresetBox, beatPresetArea);
        placeLabeledControl (beatLabel, beatSlider, beatRow);

        content.removeFromTop (8);
        auto waveformRow = content.removeFromTop (56);
        auto waveformArea = waveformRow.removeFromLeft (210);
        waveformRow.removeFromLeft (10);
        placeLabeledControl (waveformLabel, waveformBox, waveformArea);
        waveformHintLabel.setBounds (waveformRow.reduced (3, 15));

        content.removeFromTop (12);
        auto readout = content.removeFromTop (36);
        constexpr int readoutGap = 10;
        auto leftReadout = readout.removeFromLeft ((readout.getWidth() - readoutGap) / 2);
        readout.removeFromLeft (readoutGap);
        leftFrequencyLabel.setBounds (leftReadout);
        rightFrequencyLabel.setBounds (readout);
    }
    else
    {
        auto noiseRow = content.removeFromTop (56);
        auto noiseTypeArea = noiseRow.removeFromLeft (noiseRow.getWidth() * 40 / 100);
        noiseRow.removeFromLeft (10);
        placeLabeledControl (noiseTypeLabel, noiseTypeBox, noiseTypeArea);
        placeLabeledControl (noiseVolumeLabel, noiseVolumeSlider, noiseRow);

        noiseHintLabel.setBounds (content.removeFromTop (24));
        content.removeFromTop (5);

        placeLabeledControl (toneVolumeLabel, toneVolumeSlider,
                             content.removeFromTop (56));
        content.removeFromTop (6);

        auto channelRow = content.removeFromTop (56);
        auto leftArea = channelRow.removeFromLeft ((channelRow.getWidth() - 10) / 2);
        channelRow.removeFromLeft (10);
        placeLabeledControl (leftVolumeLabel, leftVolumeSlider, leftArea);
        placeLabeledControl (rightVolumeLabel, rightVolumeSlider, channelRow);

        content.removeFromTop (6);
        placeLabeledControl (masterVolumeLabel, masterVolumeSlider,
                             content.removeFromTop (56));
        content.removeFromTop (6);

        auto fadeRow = content.removeFromTop (56);
        auto fadeInArea = fadeRow.removeFromLeft ((fadeRow.getWidth() - 10) / 2);
        fadeRow.removeFromLeft (10);
        placeLabeledControl (fadeInLabel, fadeInSlider, fadeInArea);
        placeLabeledControl (fadeOutLabel, fadeOutSlider, fadeRow);

        content.removeFromTop (7);
        outputHintLabel.setBounds (content.removeFromTop (22));

        content.removeFromTop (4);
        outputMeterLabel.setBounds (content.removeFromTop (20));

        auto meterRow = content.removeFromTop (48);
        auto leftMeterArea = meterRow.removeFromLeft (
            (meterRow.getWidth() - 12) / 2);
        meterRow.removeFromLeft (12);

        auto placeMeter = [] (juce::Label& label,
                              juce::ProgressBar& meter,
                              juce::Rectangle<int> area)
        {
            label.setBounds (area.removeFromLeft (24));
            meter.setBounds (area.reduced (0, 10));
        };

        placeMeter (leftOutputMeterLabel, leftOutputMeter, leftMeterArea);
        placeMeter (rightOutputMeterLabel, rightOutputMeter, meterRow);
        peakGuardStatusLabel.setBounds (content.removeFromTop (24));
    }

    auto footer = layout.footer;
    versionLabel.setBounds (footer.removeFromRight (105));
    safetyLabel.setBounds (footer);

    if (helpAboutOverlay != nullptr)
    {
        helpAboutOverlay->setBounds (getLocalBounds());

        if (helpAboutOverlay->isVisible())
            helpAboutOverlay->toFront (false);
    }
}
