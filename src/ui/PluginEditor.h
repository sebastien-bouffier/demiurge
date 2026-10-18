#pragma once

#include "ArrangementView.h"
#include "DemiurgeLook.h"
#include "PianoRoll.h"
#include "plugin/PluginProcessor.h"
#include "plugin/Presets.h"

// A button that, dragged out of the plugin, drops a MIDI file into the DAW. A plain click
// still clicks.
class DragOutButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void()> onDragOut;

    void mouseDown (const juce::MouseEvent& e) override { started = false; juce::TextButton::mouseDown (e); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! started && e.getDistanceFromDragStart() > 8 && onDragOut)
        {
            started = true;
            setState (juce::Button::buttonNormal);
            onDragOut();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! started)
            juce::TextButton::mouseUp (e);
    }

private:
    bool started = false;
};

class DemiurgeEditor : public juce::AudioProcessorEditor,
                       public juce::DragAndDropContainer,
                       private juce::ValueTree::Listener,
                       private juce::Timer
{
public:
    explicit DemiurgeEditor (DemiurgeProcessor&);
    ~DemiurgeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void layout(); // positions every control at kWidth x kHeight; resized() scales them to the window

    static constexpr int kWidth = 1280, kHeight = 820; // design size; the window is resizable
    static constexpr int kTopBar = 48;

    // For the snapshot tool
    void applyTheme (bool light);
    void refreshForTesting() { refreshAll(); timerCallback(); }

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { songChanged = true; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { songChanged = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { songChanged = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { songChanged = true; }
    void valueTreeRedirected (juce::ValueTree&) override { songChanged = true; }
    void timerCallback() override;

    void refreshAll();       // every widget from the song
    void refreshSongPanel();
    void refreshPartPanel();
    void refreshCellPanel();
    void refreshUndoButtons();

    // Actions
    void planSong();
    void composeAll();
    void generate (const juce::String& part, const juce::String& section, bool vary);
    void transformTake (const juce::String& what, std::function<void (dm::Notes&)> fn);
    void dragOut (const juce::String& part, const juce::String& section);
    void editSection (const juce::String& sectionId);
    void addSection();
    void addPart();
    void showSettings();
    void sendToReaper();
    void importFromReaper();
    void stepTake (int delta);

    // Presets (the Bidoo top bar)
    void rebuildPresetList (const juce::String& selectName);
    void presetChosen();
    void stepPreset (int delta);
    void updatePresetButtons();
    void showSaveDialog();
    void confirmDelete();

    juce::String partLabel (const juce::String& partId);
    juce::String sectionLabel (const juce::String& sectionId);
    void setSongProperty (const juce::Identifier& id, const juce::var& v, const juce::String& undoName);
    void setPartProperty (const juce::Identifier& id, const juce::var& v, const juce::String& undoName);

    bidoo::look::ScopedInterDefault interFonts; // Inter for every font that doesn't name a typeface
    DemiurgeLook lnf;                           // declared early so it is destroyed late
    DemiurgeProcessor& processor;
    PresetManager presets;

    ArrangementView arrangement;
    PianoRoll roll;

    // Top bar
    juce::ComboBox presetCombo;
    bidoo::look::ArrowButton presetPrev { false, "Previous preset" }, presetNext { true, "Next preset" };
    juce::TextButton presetSave { "SAVE" }, presetDelete { "DELETE" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" }, keyButton { "KEY" };
    juce::TextButton themeToggle;

    // Song panel
    juce::TextEditor ideaEditor, styleEditor, titleEditor;
    juce::TextButton planButton { "PLAN SONG" }, composeAllButton { "COMPOSE ALL" };
    juce::ComboBox keyCombo, scaleCombo, tsCombo;
    juce::Slider bpmSlider { juce::Slider::LinearBar, juce::Slider::TextBoxLeft };
    juce::Label ideaLabel, styleLabel, titleLabel, keyLabel, bpmLabel, tsLabel;

    // Part panel
    juce::TextEditor partName, partDescription;
    juce::ComboBox roleCombo, channelCombo, programCombo;
    juce::Label partNameLabel, roleLabel, channelLabel, programLabel, descriptionLabel;

    // Arrangement panel
    juce::TextButton addSectionButton { "+ SECTION" }, addPartButton { "+ PART" };

    // Cell panel
    juce::TextEditor promptEditor;
    juce::TextButton generateButton { "GENERATE" }, varyButton { "VARY" };
    juce::TextButton wholePartButton { "WHOLE PART" }, wholeSectionButton { "WHOLE SECTION" };
    bidoo::look::ArrowButton takePrev { false, "Previous take" }, takeNext { true, "Next take" };
    juce::TextButton deleteTakeButton { "DELETE TAKE" };
    juce::TextButton quantButton { "QUANT" }, humanButton { "HUMAN" }, octDownButton { "OCT -" }, octUpButton { "OCT +" };
    juce::TextButton captureButton { "CAPTURE" };
    DragOutButton dragButton { "DRAG MIDI" };
    juce::TextButton fromReaperButton { "FROM REAPER" }, toReaperButton { "TO REAPER" };
    juce::Label promptLabel;

    // Bottom bar
    juce::Label statusLabel;
    juce::TextButton cancelButton { "CANCEL" };
    juce::TextButton playButton { "PLAY" }, loopButton { "LOOP" }, thruButton { "THRU" };
    juce::ComboBox outputCombo, channelOutCombo;
    std::unique_ptr<APVTS::ButtonAttachment> loopAttachment, thruAttachment;

    juce::String cellTitle, cellInfo;
    bool songChanged = true;
    int shownStatus = -1;
    double lastPosition = -2.0;
    juce::Rectangle<int> songPanel, partPanel, arrangementPanel, cellPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DemiurgeEditor)
};
