#pragma once

// The preset browser: a panel over the editor's body, opened by a click on the preset name in the
// top bar. Folders at the left (Favourites, All presets, Factory and its categories, User and its
// subfolders on disk), the presets of the selected folder at the right with their stars, tags and MIDI
// program numbers, a search box above (any case, in the names, the folder names and the tags) and
// the tags of the list under it: click tags to show only the presets that have them all.
//
// A click on a preset loads it, a double-click loads it and closes the browser. Keys: Up / Down
// (Page Up / Page Down) select, Enter loads, Left / Right change folder while the search box is
// empty, Esc clears the search and then closes, typing searches, F2 renames and Delete deletes a
// user preset. Drag a user preset onto a user folder to move it there. Right-click a preset or a
// user folder for the rest (favourite, tags, rename, move, delete, new folder, show the file).
// A plugin that can audition presets (onPreview) gets a play button after each star.

#include "Presets.h"
#include "../BidooLook.h"
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace bidoo::kit
{
class PresetBrowser : public juce::Component
{
public:
    explicit PresetBrowser (PresetManager& presets);
    ~PresetBrowser() override;

    // ---- what the editor does for it ----
    std::function<void (int index)> onLoad;                  // load the preset at `index` of the list
    std::function<void()> onClose;                           // hide the browser
    std::function<void (const juce::String& folder)> onSave; // the save dialog, offering `folder`
    std::function<void()> onPresetsChanged;                  // presets renamed, moved, deleted: new list
    // A preset's key changed (renamed or moved): the editor follows its current preset.
    std::function<void (const juce::String& oldKey, const juce::String& newKey)> onKeyChanged;
    // Audition (optional): a play button after each star plays the preset at `index` without
    // loading it. None: no button.
    std::function<void (int index)> onPreview;

    void applyTheme (const bidoo::look::Palette& p);
    // The panel's left edge (under the preset name), in the browser's coordinates.
    void setAnchorX (int x);
    // After the preset list was read again: keeps the folder, the search and the selection.
    void rebuild();
    // The preset loaded now (-1: none in the list): highlighted, and selected when `select`.
    void setCurrent (int index, bool select);
    void focusSearch();

    // The folder a save goes to: the selected user folder, else the selected user preset's.
    juce::String getSaveFolder() const;

    // Selects a folder by its label ("Favourites", "All presets", "Factory", a category, "User",
    // a user folder "a/b"). False when there is none.
    bool selectFolder (const juce::String& folderOrLabel);
    void setSearchText (const juce::String& text);

    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& key) override;
    void visibilityChanged() override;

    static constexpr int kPanelWidth = 780, kPanelHeight = 540;

private:
    enum class NodeKind
    {
        favourites,
        all,
        factory,
        category,
        user,
        userFolder
    };

    struct Node
    {
        NodeKind kind;
        juce::String label, folder;
        int depth = 0;
        int count = 0;
    };

    // A scrolling list of rows drawn by its owner (folders, presets).
    class RowList : public juce::Component
    {
    public:
        explicit RowList (PresetBrowser& owner, bool presetRows);
        void paint (juce::Graphics& g) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseDoubleClick (const juce::MouseEvent& e) override;
        int rowAt (int y) const;
        int hoverRow = -1;
        bool hoverStar = false, hoverPlay = false;
        static constexpr int kRowHeight = 26;

    private:
        PresetBrowser& browser;
        const bool presets;
    };

    // The search box: keys for the browser first.
    struct SearchBox : juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onKey;
        bool keyPressed (const juce::KeyPress& key) override { return (onKey && onKey (key)) || juce::TextEditor::keyPressed (key); }
    };

    struct CloseButton : juce::Button
    {
        CloseButton() : juce::Button ("Close") {}
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override;
    };

    // The tags of the list as chips: a click toggles one (the list shows the presets with every
    // tag lit).
    class TagStrip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit TagStrip (PresetBrowser& owner) : browser (owner) {}
        int layoutFor (int width); // places the chips; returns the height they need
        void paint (juce::Graphics& g) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void mouseDown (const juce::MouseEvent& e) override;
        static constexpr int kChipHeight = 22, kGap = 6, kMaxRows = 3;

    private:
        int chipAt (juce::Point<int> p) const;
        PresetBrowser& browser;
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> chips;
        int hover = -1;
    };

    void buildNodes();
    void filter();
    bool inNode (const PresetManager::Entry& e, const Node& n) const;
    juce::String rowFolderText (const PresetManager::Entry& e) const;
    void selectNode (int node);
    void selectRow (int row, bool scroll = true);
    void scrollToRow (int row);
    void loadRow (int row, bool close);
    void updateButtons();
    void layoutLists();

    void paintNodeRow (juce::Graphics& g, int row, juce::Rectangle<int> r, bool hover);
    void paintPresetRow (juce::Graphics& g, int row, juce::Rectangle<int> r, bool hover, bool starHover, bool playHover);
    static juce::Path starPath (juce::Rectangle<float> r);
    static juce::Path folderPath (juce::Rectangle<float> r);
    static juce::Rectangle<int> starArea (juce::Rectangle<int> row) { return row.withWidth (28); }
    // The audition button, right of the star (only with onPreview).
    static juce::Rectangle<int> playArea (juce::Rectangle<int> row) { return row.withTrimmedLeft (28).withWidth (24); }
    bool inPlayArea (int x) const { return onPreview != nullptr && playArea ({ 0, 0, presetList.getWidth(), RowList::kRowHeight }).getHorizontalRange().contains (x); }

    void presetClicked (int row, const juce::MouseEvent& e);
    // Dragging a user preset onto a user folder at the left moves it there.
    void presetDragged (const juce::MouseEvent& e);
    bool presetDropped();
    void presetMenu (int row);
    void nodeClicked (int row, const juce::MouseEvent& e);
    void nodeMenu (int row);

    void toggleFavourite (int entryIndex);
    void toggleTag (const juce::String& tag);
    void editTags (int entryIndex);
    void renamePreset (int entryIndex);
    void deletePreset (int entryIndex);
    void movePreset (int entryIndex, const juce::String& folder);
    void newFolder (const juce::String& parent);
    void renameFolder (const juce::String& folder);
    void deleteFolder (const juce::String& folder);
    void presetsChanged (const juce::String& selectKey, const juce::String& folderToShow);
    void showProblem (const juce::String& title, const juce::String& message);
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  const juce::String& okText, std::function<void (const juce::String&)> done);

    juce::Rectangle<int> panelBounds() const;

    PresetManager& presets;
    const bidoo::look::Palette* palette = &bidoo::look::dark();
    int anchorX = 0;

    std::vector<Node> nodes;
    std::vector<int> rows; // entry indices of the selected folder that match the search
    int selectedNode = 1;  // All presets
    int selectedRow = -1;
    int currentEntry = -1;
    juce::String query;
    juce::StringArray allTags, selectedTags; // the tags of the list, the lit ones
    int dragEntry = -1, dropNode = -1; // a user preset being dragged, the folder under it
    juce::Point<int> dragPos;

    SearchBox search;
    juce::TextButton newFolderButton { "NEW FOLDER" }, saveButton { "SAVE" }, renameButton { "RENAME" }, deleteButton { "DELETE" };
    CloseButton closeButton;
    RowList nodeList { *this, false }, presetList { *this, true };
    TagStrip tagStrip { *this };
    juce::Viewport nodeView, presetView;
    juce::Rectangle<int> nodeWell, presetWell, presetHeader, footer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetBrowser)
};
} // namespace bidoo::kit
