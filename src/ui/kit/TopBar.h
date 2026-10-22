#pragma once

// The top bar every Bidoo plugin shares, one row on one centre line:
//
//   [oo] | NAME   < preset >  SAVE   undo redo   A|B A>B   ...plugin controls...   CPU 3 %  v1.2.0  [DARK]
//
// The logo, the plugin's name (or its own wordmark), the preset name between the previous / next
// arrows (a click on the name opens the preset browser over the editor's body, the arrow at its
// right the quick list: copy / paste, open a file, the default, every preset), SAVE (name, folder
// and tags), undo / redo (Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z, Ctrl+Y), A/B compare and copy (optional),
// the plugin's own controls, the CPU meter (optional), the version and the theme button, last.
// Deleting, renaming, moving and tagging presets is in the browser.
//
// A window too narrow for all of it gives up the version, then some of the preset list, then the
// name beside the logo, then the CPU meter (Options::extrasWidth: the room the plugin's own
// controls need).
//
// The editor makes one, places it at the top left of its design area (so the bar's coordinates
// are the editor's: extras laid out in the bar's row land where they should), calls attachTo
// (itself) once, gives the browser its area with setBrowserArea in its layout, and applies the
// theme when onThemeClicked asks. The bar runs the undo collection (30 times a second).

#include "PresetBrowser.h"
#include "../BidooLook.h"
#include <functional>
#include <memory>

namespace bidoo::kit
{
class TopBar : public juce::Component, private juce::Timer
{
public:
    struct Options
    {
        juce::String title;               // the plugin's name, in capitals ("XPRESSO")
        juce::String version;             // "1.2.0" (JucePlugin_VersionString)
        bool abCompare = true;            // A/B and copy
        std::function<float()> cpuLoad;   // 0..1 (CpuLoad::getLoad); none: no CPU meter
        int extrasWidth = 0;              // what the plugin's controls in the bar need (layoutExtras)
    };

    TopBar (SoundState& sound, PresetManager& presets, Options options);
    ~TopBar() override;

    static constexpr int kHeight = 48;

    // ---- what the editor hears of, or adds ----
    std::function<void()> onPresetLoaded;                 // a preset, A/B slot or pasted settings is in: refresh the plugin's view
    std::function<void()> onUndoRedo;                     // after an undo or a redo (buttons or keys)
    std::function<void (bool light)> onThemeClicked;     // the theme button: apply that theme (then call applyTheme)
    std::function<void (juce::Rectangle<int>& row)> layoutExtras; // the plugin's controls in the bar (barSlot from the left or the right)
    std::function<void (juce::PopupMenu&)> addPresetActions;      // the plugin's items in the quick list, after the shared ones
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> drawTitle; // a wordmark instead of the name, in the given area
    int titleWidth = 0;                                    // with drawTitle: its width (0: the name's)
    std::function<void (juce::Component& browser)> onBrowserCreated; // e.g. give it the editor's scale transform

    // Installs the undo watch (mouse and keys) on `editor`, where the browser opens too.
    void attachTo (juce::Component& editor);
    // The browser's area: the editor's body, in the editor's coordinates.
    void setBrowserArea (juce::Rectangle<int> area);

    void applyTheme (const bidoo::look::Palette& p, bool light);
    const bidoo::look::Palette& palette() const { return *pal; }

    void showPresetBrowser (bool show);
    bool isPresetBrowserOpen() const { return browser != nullptr && browser->isVisible(); }
    PresetBrowser* getPresetBrowser() { return browser.get(); }
    // A plugin that brings its own panels to the front: the open browser stays above them.
    void keepBrowserOnTop();
    // After the plugin changed presets itself (a program change...): the list and the name again.
    void refreshPresets();

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildPresetList (const juce::String& selectName);
    void addPresetItems (juce::PopupMenu& menu, const juce::String& folder, int& next);
    void addSharedActions (juce::PopupMenu& menu);
    void presetChosen();
    void loadPreset (int index);
    int currentPresetIndex() const;
    void presetListChanged();
    void stepPreset (int delta);
    void showSaveDialog (const juce::String& folder);
    void saveUserPreset (const juce::String& name, const juce::String& folder, const juce::StringArray& tags);
    void showCurrentPreset();
    void updateHistoryButtons();
    void pasteSettings();
    void openPresetFile();
    void settingsLoaded();
    void selectInList (const juce::String& presetName);
    void showPresetModified (bool modified);
    void undo();
    void redo();
    juce::Rectangle<int> logoArea() const;
    juce::Rectangle<int> titleArea() const;
    void fitToWidth();

    // Every press, release and wheel turn on the editor or its children, for the undo steps.
    struct PointerWatcher : juce::MouseListener
    {
        explicit PointerWatcher (SoundState& s) : sound (s) {}
        void mouseDown (const juce::MouseEvent&) override { sound.notePointerDown(); }
        void mouseUp (const juce::MouseEvent&) override { sound.notePointerUp(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { sound.noteWheel(); }
        SoundState& sound;
    };

    // Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z and Ctrl+Y, from whichever child has the focus.
    struct HistoryKeys : juce::KeyListener
    {
        explicit HistoryKeys (TopBar& b) : bar (b) {}
        bool keyPressed (const juce::KeyPress& key, juce::Component*) override;
        TopBar& bar;
    };

    // A/B: both letters, the active one lit; a click switches to the other.
    struct AbButton : juce::Button
    {
        explicit AbButton (SoundState& s);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        SoundState& sound;
    };

    // Copies the active settings into the other slot: "A > B" or "B > A".
    struct AbCopyButton : juce::Button
    {
        explicit AbCopyButton (SoundState& s);
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        SoundState& sound;
    };

    // The preset name: a click opens the browser (onNameClick), the arrow at the right or a right
    // click the quick list, rebuilt as it opens (user presets saved meanwhile, the clipboard, the default).
    struct PresetBox : juce::ComboBox
    {
        static constexpr int kArrowZone = 24;
        std::function<void()> beforePopup, onNameClick;
        void showPopup() override;
        void mouseDown (const juce::MouseEvent& e) override;
        void paintOverChildren (juce::Graphics& g) override;
    };

    SoundState& sound;
    PresetManager& presets;
    const Options options;
    const bidoo::look::Palette* pal = &bidoo::look::palette (bidoo::look::kDefaultLight);
    bool light = bidoo::look::kDefaultLight;
    juce::Component* host = nullptr;

    PresetBox presetCombo;
    bidoo::look::ArrowButton presetPrev { false, "Previous preset" }, presetNext { true, "Next preset" };
    juce::TextButton presetSave { "SAVE" };
    bidoo::look::HistoryButton undoButton { false, "Undo (Ctrl+Z)" }, redoButton { true, "Redo (Ctrl+Shift+Z)" };
    AbButton abButton { sound };
    AbCopyButton abCopy { sound };
    juce::TextButton themeToggle;
    PointerWatcher pointerWatcher { sound };
    HistoryKeys historyKeys { *this };
    int shownAb = -1;

    std::unique_ptr<PresetBrowser> browser; // made on first use
    juce::Rectangle<int> browserArea;
    juce::String currentKey; // the preset last loaded or saved from the list (PresetManager::keyOf)
    juce::String lastPresetName;
    int shownModified = -1;  // the preset name dimmed: settings changed since the preset was loaded or saved
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Rectangle<int> cpuArea, versionArea;
    bool showVersion = true, showTitle = true, showCpu = true; // what fits (fitToWidth)
    int listWidth = bidoo::look::kPresetListWidth;
    float shownCpu = -1.0f, smoothedCpu = 0.0f;
    int cpuTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};
} // namespace bidoo::kit
