#pragma once

#include "DemiurgeLook.h"
#include "plugin/PluginProcessor.h"

// The selected cell's active take, for fine tuning by hand:
// double-click to add or delete a note, drag a note to move it, drag its right edge to
// resize it. Times snap to 1/16. Every edit can be undone.
class PianoRoll : public juce::Component
{
public:
    PianoRoll (DemiurgeProcessor& p, DemiurgeLook& look) : processor (p), lnf (look) {}

    void refresh(); // re-reads the selected take
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr int kKeys = 46, kRuler = 18;
    static constexpr double kSnap = 0.25;

private:
    juce::Rectangle<float> grid() const;
    float xFor (double beat) const;
    double beatAt (float x) const;
    float yFor (int pitch) const;
    int pitchAt (float y) const;
    juce::Rectangle<float> noteRect (const dm::Note& n) const;
    int noteAt (juce::Point<float> p, bool& onEdge) const;
    void commit (const juce::String& what);
    bool isDrums() const;

    DemiurgeProcessor& processor;
    DemiurgeLook& lnf;

    dm::Notes notes;
    double length = 16.0;
    int lo = 48, hi = 72;
    juce::StringArray chords;

    int dragging = -1;
    bool resizing = false;
    dm::Note dragOrigin;
    juce::Point<float> dragStart;
    bool changed = false;
};
