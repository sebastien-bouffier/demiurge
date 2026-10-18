#pragma once

#include "BidooLook.h"
#include "engine/Song.h"

#include <juce_gui_basics/juce_gui_basics.h>

// Knobs, buttons, combo boxes and text fields in the look shared by the Bidoo plugins.
class DemiurgeLook : public juce::LookAndFeel_V4
{
public:
    DemiurgeLook() { setLight (bidoo::look::kDefaultLight); }

    void setLight (bool light)
    {
        isLight = light;
        bidoo::look::applyColours (*this, palette());
    }

    const bidoo::look::Palette& palette() const { return bidoo::look::palette (isLight); }
    bool light() const { return isLight; }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float start, float end,
                           juce::Slider& s) override
    {
        bidoo::look::drawRotarySlider (g, x, y, width, height, pos, start, end, s, palette());
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bg, bool hi, bool down) override
    {
        bidoo::look::drawButtonBackground (g, b, bg, hi, down, palette());
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        bidoo::look::drawButtonText (g, b, bidoo::look::buttonFont ((float) b.getHeight()));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return bidoo::look::comboFont(); }
    juce::Font getPopupMenuFont() override { return bidoo::look::comboFont(); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        bidoo::look::drawComboBox (g, w, h, box, palette());
    }

    void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed) override
    {
        g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
        g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h), bidoo::look::kCorner);
    }

    void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed) override
    {
        g.setColour (ed.hasKeyboardFocus (true) ? palette().accent : palette().outline);
        g.drawRoundedRectangle (juce::Rectangle<float> (0.5f, 0.5f, (float) w - 1.0f, (float) h - 1.0f),
                                bidoo::look::kCorner, 1.0f);
    }

    juce::Font getLabelFont (juce::Label&) override { return bidoo::look::font (13.0f); }

    // A colour per instrument role (drawn on top of the palette).
    juce::Colour roleColour (dm::Role r) const
    {
        static const juce::uint32 darkC[] = { 0xffff7043, 0xffffa726, 0xff42a5f5, 0xffab47bc, 0xff26c6da, 0xff66bb6a,
                                              0xffffca28, 0xff9ccc65, 0xffec407a, 0xffef5350, 0xff7e57c2, 0xff8d6e63,
                                              0xff29b6f6, 0xffffee58, 0xff78909c };
        static const juce::uint32 lightC[] = { 0xffe0603a, 0xffd98a1e, 0xff3477c4, 0xff9150ac, 0xff1f9fb0, 0xff4f9a53,
                                               0xffc99a12, 0xff7aa344, 0xffc93a6a, 0xffd04545, 0xff6a4cb0, 0xff7d5c50,
                                               0xff2a8fc6, 0xffb9a41a, 0xff667c88 };
        const int i = juce::jlimit (0, (int) dm::Role::count - 1, (int) r);
        return juce::Colour (isLight ? lightC[i] : darkC[i]);
    }

private:
    bool isLight = true;
};
