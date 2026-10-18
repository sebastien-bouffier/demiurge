#pragma once

#include "DemiurgeLook.h"
#include "plugin/PluginProcessor.h"

#include <map>

// The song at a glance: sections across, parts down. Each cell shows its active take.
// Click to select, drag a cell (or a part name, or a section header) out to the DAW to drop
// it as a MIDI clip, right-click for more.
class ArrangementView : public juce::Component
{
public:
    ArrangementView (DemiurgeProcessor& p, DemiurgeLook& look) : processor (p), lnf (look) {}

    std::function<void()> onSelectionChanged;
    std::function<void (const juce::String& sectionId)> onEditSection;
    std::function<void (const juce::String& partId, const juce::String& sectionId, bool vary)> onGenerate;
    std::function<void (const juce::String& partId, const juce::String& sectionId)> onDragOut; // empty id = all

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static constexpr int kHeader = 46, kGutter = 168;

private:
    struct Hit
    {
        enum class Where { none, header, gutter, mute, solo, cell } where = Where::none;
        int part = -1, section = -1;
    };
    Hit hitTest (juce::Point<int> p) const;
    juce::Rectangle<int> cellBounds (int part, int section) const;
    juce::Rectangle<int> rowBounds (int part) const;
    juce::Rectangle<int> columnBounds (int section) const;
    int rowHeight() const;
    void showSectionMenu (int section);
    void showPartMenu (int part);
    void showCellMenu (int part, int section);
    void select (int part, int section);
    const dm::Notes& notesOf (const juce::ValueTree& take); // parsed notes, cached by take id

    std::map<juce::String, std::pair<juce::String, dm::Notes>> cache;

    DemiurgeProcessor& processor;
    DemiurgeLook& lnf;
    Hit pressed;
    bool dragStarted = false;
};
