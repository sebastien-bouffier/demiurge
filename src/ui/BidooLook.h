#pragma once

// The look shared by the Bidoo plugins: Bourriho, Chourmo, FadaBox, Peneque and Quichar.
//
// One palette per theme (dark and light), one knob, one button. Keep this file identical in
// every repository. Chourmo draws with NanoVG rather than JUCE, so its ui/Theme.cpp and
// EditorView::drawKnob mirror these values and shapes by hand: update them together.
//
// Plugin-specific functional colours (EQ bands, tracks, delay lines, cable kinds) stay in each
// plugin. Everything else (surfaces, text, the accent, knobs, buttons and the Inter typeface)
// comes from here.
//
// Inter (SIL Open Font License, fonts/OFL.txt) is compiled into each plugin by CMake:
//   juce_add_binary_data(<Target>Fonts HEADER_NAME BidooFonts.h NAMESPACE BidooFonts
//                        SOURCES fonts/Inter-Medium.otf fonts/Inter-SemiBold.otf)

#include <juce_gui_basics/juce_gui_basics.h>

#include "BidooFonts.h"

namespace bidoo::look
{
struct Palette
{
    bool light;
    juce::Colour background; // window
    juce::Colour panel;      // panels and groups on the background
    juce::Colour well;       // knob track, inset areas, slider grooves
    juce::Colour button;     // button and combo box face
    juce::Colour outline;    // button, combo box and knob outlines
    juce::Colour screen;     // displays, text fields
    juce::Colour text, textDim;
    juce::Colour accent;   // the one accent colour: knob values, active buttons, focus
    juce::Colour onAccent; // text and marks drawn on the accent
};

inline const Palette& dark()
{
    static const Palette p { false,
                             juce::Colour (0xff16181d), juce::Colour (0xff1e2128), juce::Colour (0xff2b2f39),
                             juce::Colour (0xff252932), juce::Colour (0xff333844), juce::Colour (0xff0f1115),
                             juce::Colour (0xffe8eaf0), juce::Colour (0xff7f8696),
                             juce::Colour (0xffff8a3d), juce::Colour (0xff0f1115) };
    return p;
}

inline const Palette& light()
{
    static const Palette p { true,
                             juce::Colour (0xffe4e6eb), juce::Colour (0xfff6f7f9), juce::Colour (0xffdcdfe5),
                             juce::Colour (0xfffbfbfc), juce::Colour (0xffd2d6de), juce::Colour (0xffffffff),
                             juce::Colour (0xff222733), juce::Colour (0xff7c8393),
                             juce::Colour (0xffe2601f), juce::Colour (0xffffffff) };
    return p;
}

inline const Palette& palette (bool isLight) { return isLight ? light() : dark(); }

// A fresh install opens in the light theme.
constexpr bool kDefaultLight = true;

// The theme button names the theme it switches to.
inline juce::String themeButtonText (bool isLight) { return isLight ? "DARK" : "LIGHT"; }

constexpr float kCorner = 6.0f; // buttons, combo boxes, toggles

// ---------------------------------------------------------------------------------------------
//  Typeface: Inter Medium for regular text, Inter SemiBold for bold.
// ---------------------------------------------------------------------------------------------
inline juce::Typeface::Ptr inter (bool semiBold)
{
    static const auto medium = juce::Typeface::createSystemTypefaceFor (BidooFonts::InterMedium_otf,
                                                                        (size_t) BidooFonts::InterMedium_otfSize);
    static const auto bold = juce::Typeface::createSystemTypefaceFor (BidooFonts::InterSemiBold_otf,
                                                                      (size_t) BidooFonts::InterSemiBold_otfSize);
    return semiBold ? bold : medium;
}

// Inter at a pixel height.
inline juce::Font font (float height, bool semiBold = false)
{
    return juce::Font (juce::FontOptions (inter (semiBold)).withHeight (height));
}

// JUCE looks up the typeface of every font that doesn't name one (juce::FontOptions (12.0f),
// juce::Font::bold, ...) through the *default* LookAndFeel, not the component's. This one answers
// Inter for those, Medium or SemiBold depending on the bold flag.
class InterLookAndFeel : public juce::LookAndFeel_V4
{
public:
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
    {
        if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
            return inter (f.isBold());
        return LookAndFeel_V4::getTypefaceForFont (f);
    }
};

// Make it the plugin's default while an editor is open: hold one as the editor's first member.
// Several editors may be open at once; the last one to close restores JUCE's default.
class ScopedInterDefault
{
public:
    ScopedInterDefault()
    {
        if (users()++ == 0)
        {
            juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel());
            juce::Typeface::clearTypefaceCache();
        }
    }

    ~ScopedInterDefault()
    {
        if (--users() == 0)
        {
            juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
            juce::Typeface::clearTypefaceCache();
        }
    }

    ScopedInterDefault (const ScopedInterDefault&) = delete;
    ScopedInterDefault& operator= (const ScopedInterDefault&) = delete;

private:
    static int& users()
    {
        static int n = 0;
        return n;
    }

    static InterLookAndFeel& lookAndFeel()
    {
        static InterLookAndFeel lf;
        return lf;
    }
};

// Hover and press feedback that also shows on near-white faces.
inline juce::Colour interactive (juce::Colour c, const Palette& p, bool highlighted, bool down)
{
    if (down)
        return p.light ? c.darker (0.10f) : c.brighter (0.15f);
    if (highlighted)
        return p.light ? c.darker (0.04f) : c.brighter (0.06f);
    return c;
}

// ---------------------------------------------------------------------------------------------
//  Top bar: every plugin's first row reads the same way, all on one centre line:
//    NAME   [<] [ preset list ] [>]  SAVE  DELETE   ...plugin controls...   OUTPUT   [DARK/LIGHT]
//  The theme button is always the last one on the right. With Inter, JUCE centres the capitals
//  of a text box on the box's centre, so a title and buttons laid out with the same centre line
//  line up exactly.
// ---------------------------------------------------------------------------------------------
constexpr int kBarControlHeight = 28; // buttons, preset list, output bar
constexpr int kBarGap = 4;            // between the controls of a group
constexpr int kBarGroupGap = 16;      // between groups
constexpr int kBarMargin = 16;        // left and right edges of the window
constexpr float kTitleHeight = 22.0f;
constexpr int kArrowWidth = 28, kPresetListWidth = 220, kSaveWidth = 60, kDeleteWidth = 68, kThemeWidth = 68;

inline juce::Font titleFont() { return font (kTitleHeight, true); }

inline int titleWidth (const juce::String& name)
{
    return juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (titleFont(), name)));
}

// The plugin name in the text colour, its capitals centred on centreY, starting at x.
inline void drawTitle (juce::Graphics& g, const juce::String& name, int x, int centreY, const Palette& p)
{
    g.setColour (p.text);
    g.setFont (titleFont());
    g.drawText (name, x, centreY - kBarControlHeight / 2, titleWidth (name) + 4, kBarControlHeight,
                juce::Justification::centredLeft, false);
}

// Takes `width` from the left (or right) of a bar row and returns a control-height slot centred
// on the row, so everything in the row shares one centre line.
inline juce::Rectangle<int> barSlot (juce::Rectangle<int>& row, int width, bool fromRight = false, int gapAfter = kBarGap)
{
    auto r = fromRight ? row.removeFromRight (width) : row.removeFromLeft (width);
    if (fromRight)
        row.removeFromRight (gapAfter);
    else
        row.removeFromLeft (gapAfter);
    return r.withSizeKeepingCentre (width, kBarControlHeight);
}

// Previous / next preset: the button face with a small triangle.
class ArrowButton : public juce::Button
{
public:
    ArrowButton (bool forwardDirection, const juce::String& tooltip)
        : juce::Button (forwardDirection ? "Next" : "Previous"), forward (forwardDirection)
    {
        setTooltip (tooltip);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId),
                                               highlighted && isEnabled(), down);
        const auto c = getLocalBounds().toFloat().getCentre();
        const float d = forward ? 1.0f : -1.0f;
        juce::Path p;
        p.addTriangle (c.x - 2.5f * d, c.y - 4.5f, c.x - 2.5f * d, c.y + 4.5f, c.x + 3.0f * d, c.y);
        g.setColour (findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (isEnabled() ? 1.0f : 0.4f));
        g.fillPath (p);
    }

private:
    bool forward;
};

// The text of a combo box and of a ListButton.
inline juce::Font comboFont() { return font (13.0f); }

// A button that reads like a combo box (text on the left, a small down arrow on the right), for
// preset names that open a browser rather than a plain menu.
class ListButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId),
                                               highlighted && isEnabled(), down);
        const float alpha = isEnabled() ? 1.0f : 0.4f;
        g.setFont (comboFont());
        g.setColour (findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (alpha));
        g.drawFittedText (getButtonText(), getLocalBounds().reduced (9, 0).withTrimmedRight (16),
                          juce::Justification::centredLeft, 1, 0.8f);
        juce::Path arrow;
        const float ax = (float) getWidth() - 13.0f, ay = (float) getHeight() * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (alpha));
        g.fillPath (arrow);
    }
};

// ---------------------------------------------------------------------------------------------
//  Knob: a thin track, the value arc (from the centre for bipolar ranges) with a dot at the value,
//  around a small top-lit disc with a short pointer line.
// ---------------------------------------------------------------------------------------------
struct KnobGeometry
{
    juce::Point<float> centre;
    float radius;     // outer radius of the cell
    float arcRadius;  // track and value arc
    float discRadius; // the body
    float lineWidth;  // track and value arc
    float angle;      // value
    float zeroAngle;  // where the value arc starts
};

inline KnobGeometry knobGeometry (juce::Rectangle<float> area, float sliderPos, float startAngle, float endAngle,
                                  juce::Slider& s)
{
    KnobGeometry k;
    const auto b = area.reduced (4.0f);
    k.radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    k.centre = b.getCentre();
    k.lineWidth = juce::jmax (2.0f, k.radius * 0.055f);
    k.arcRadius = k.radius - k.lineWidth * 0.75f;
    k.discRadius = k.radius * 0.76f;
    k.angle = startAngle + sliderPos * (endAngle - startAngle);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;
    k.zeroAngle = bipolar ? startAngle + (float) s.valueToProportionOfLength (0.0) * (endAngle - startAngle) : startAngle;
    return k;
}

// `fill` is the value colour (usually the accent, or a functional colour such as a band's).
// With `withPointer` false the caller draws its own marks on the disc (a stepped knob's label).
inline void drawKnob (juce::Graphics& g, const KnobGeometry& k, float startAngle, float endAngle, juce::Colour fill,
                      bool active, const Palette& p, bool withPointer = true)
{
    const auto c = k.centre;
    const auto disc = juce::Rectangle<float> (k.discRadius * 2.0f, k.discRadius * 2.0f).withCentre (c);

    // body
    g.setGradientFill (juce::ColourGradient (p.well.brighter (0.06f), c.x, disc.getY(), p.panel, c.x, disc.getBottom(), false));
    g.fillEllipse (disc);
    g.setColour (p.outline);
    g.drawEllipse (disc, 1.0f);

    // track
    const juce::PathStrokeType stroke (k.lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path track;
    track.addCentredArc (c.x, c.y, k.arcRadius, k.arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (p.well);
    g.strokePath (track, stroke);

    // value and its dot
    const bool on = active && fill.getAlpha() > 0;
    if (on)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, k.arcRadius, k.arcRadius, 0.0f, juce::jmin (k.zeroAngle, k.angle),
                             juce::jmax (k.zeroAngle, k.angle), true);
        g.setColour (fill);
        g.strokePath (value, stroke);
        const float dot = k.lineWidth * 3.0f;
        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre (c.getPointOnCircumference (k.arcRadius, k.angle)));
    }

    if (withPointer)
    {
        g.setColour (on ? p.text : p.textDim.withAlpha (0.5f));
        g.drawLine ({ c.getPointOnCircumference (k.discRadius * 0.36f, k.angle),
                      c.getPointOnCircumference (k.discRadius * 0.84f, k.angle) },
                    juce::jmax (1.6f, k.lineWidth * 0.8f));
    }
}

// The usual LookAndFeel::drawRotarySlider body.
inline void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                              juce::Slider& s, const Palette& p)
{
    const auto k = knobGeometry (juce::Rectangle<int> (x, y, w, h).toFloat(), pos, start, end, s);
    drawKnob (g, k, start, end, s.findColour (juce::Slider::rotarySliderFillColourId), s.isEnabled(), p);
}

// ---------------------------------------------------------------------------------------------
//  Buttons: a rounded face with a 1 px outline when off, filled with the "on" colour when on.
// ---------------------------------------------------------------------------------------------
inline void drawButtonFace (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour face, bool on,
                            bool highlighted, bool down, const Palette& p)
{
    const auto r = area.reduced (1.0f);
    g.setColour (interactive (face, p, highlighted, down));
    g.fillRoundedRectangle (r, kCorner);
    if (! on)
    {
        g.setColour (highlighted ? p.textDim.withAlpha (0.6f) : p.outline);
        g.drawRoundedRectangle (r.reduced (0.5f), kCorner, 1.0f);
    }
}

inline juce::Font buttonFont (float height) { return font (juce::jmin (12.5f, height * 0.45f), true); }

// LookAndFeel::drawButtonBackground for TextButtons.
inline void drawButtonBackground (juce::Graphics& g, juce::Button& b, juce::Colour background, bool highlighted,
                                  bool down, const Palette& p)
{
    const bool on = b.getToggleState();
    const auto face = on ? b.findColour (juce::TextButton::buttonOnColourId) : background;
    drawButtonFace (g, b.getLocalBounds().toFloat(), face, on, highlighted, down, p);
}

// LookAndFeel::drawButtonText for TextButtons.
inline void drawButtonText (juce::Graphics& g, juce::TextButton& b, juce::Font f)
{
    g.setFont (f);
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                     .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 1), juce::Justification::centred, 1);
}

// LookAndFeel::drawToggleButton: the same face as a TextButton, lit with the tick colour when on.
inline void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool down, const Palette& p,
                              juce::Font f)
{
    const bool on = b.getToggleState();
    const auto area = b.getLocalBounds().toFloat();
    drawButtonFace (g, area, on ? b.findColour (juce::ToggleButton::tickColourId) : p.button, on, highlighted, down, p);
    g.setFont (f);
    g.setColour ((on ? p.onAccent : p.text).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
    g.drawFittedText (b.getButtonText(), area.reduced (4.0f, 1.0f).toNearestInt(), juce::Justification::centred, 1);
}

// An output / volume level: a rounded well filled with the accent up to `proportion` (0..1). The
// caption and value are drawn over it by the caller (or by the slider's text box).
inline void drawLevelBar (juce::Graphics& g, juce::Rectangle<float> area, float proportion, const Palette& p)
{
    const auto r = area.reduced (1.0f);
    g.setColour (p.well);
    g.fillRoundedRectangle (r, kCorner);
    const float w = r.getWidth() * juce::jlimit (0.0f, 1.0f, proportion);
    if (w > 0.0f)
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (r.withWidth (w).getSmallestIntegerContainer());
        g.setColour (p.accent.withAlpha (0.35f));
        g.fillRoundedRectangle (r, kCorner);
    }
}

// ---------------------------------------------------------------------------------------------
//  Spectrum displays (Peneque, Quichar): a log-frequency grid, level lines with their values in
//  a left gutter, and the analyser's IN (before the plugin) and OUT (after it) curves.
// ---------------------------------------------------------------------------------------------
constexpr float kSpectrumGutter = 34.0f; // left and right of the plot, for the level labels

// Vertical lines at 1..9 x each decade between fMin and fMax; 1, 2 and 5 stronger.
template <typename XForFreq>
void drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> plot, double fMin, double fMax, XForFreq xForFreq,
                        const Palette& p)
{
    for (double dec = 10.0; dec <= 10000.0; dec *= 10.0)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = dec * m;
            if (f < fMin || f > fMax)
                continue;
            g.setColour ((m == 1 || m == 2 || m == 5) ? p.outline : p.well);
            g.drawVerticalLine (juce::roundToInt (xForFreq (f)), plot.getY(), plot.getBottom());
        }
}

// 20, 50, 100, 200, 500, 1k, ... just above the bottom of the plot.
template <typename XForFreq>
void drawFrequencyLabels (juce::Graphics& g, juce::Rectangle<float> plot, double fMin, double fMax, XForFreq xForFreq,
                          const Palette& p)
{
    g.setFont (font (10.5f));
    g.setColour (p.textDim);
    for (double dec = 10.0; dec <= 10000.0; dec *= 10.0)
        for (int m : { 1, 2, 5 })
        {
            const double f = dec * m;
            if (f <= fMin || f >= fMax)
                continue;
            const auto s = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
            g.drawText (s, juce::Rectangle<float> ((float) xForFreq (f) + 3.0f, plot.getBottom() - 15.0f, 40.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
}

// The piano strip under a spectrum: each key on the same log-frequency axis, C keys labelled.
template <typename XForFreq>
void drawKeyStrip (juce::Graphics& g, juce::Rectangle<float> keys, double fMin, double fMax, XForFreq xForFreq,
                   const Palette& p)
{
    const auto white = juce::Colour (p.light ? 0xffffffff : 0xffaeb4bd), black = juce::Colour (p.light ? 0xff2b2e34 : 0xff15181c);
    const auto edge = juce::Colour (p.light ? 0xffb5b8bf : 0xff6c7480), label = juce::Colour (p.light ? 0xff7a7f89 : 0xff3a3f47);
    auto noteHz = [] (double n) { return 440.0 * std::pow (2.0, (n - 69.0) / 12.0); };
    auto isBlack = [] (int n) { const int pc = ((n % 12) + 12) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; };
    const int nLo = (int) std::floor (69.0 + 12.0 * std::log2 (fMin / 440.0));
    const int nHi = (int) std::ceil (69.0 + 12.0 * std::log2 (fMax / 440.0));

    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (keys.toNearestInt());
    g.setColour (white);
    g.fillRect (keys);
    // white-key borders: between two white keys at the half-semitone, otherwise under the black key's centre
    g.setColour (edge);
    for (int n = nLo; n <= nHi; ++n)
        if (! isBlack (n) && ! isBlack (n + 1))
            g.drawVerticalLine (juce::roundToInt ((float) xForFreq (noteHz (n + 0.5))), keys.getY(), keys.getBottom());
        else if (isBlack (n))
            g.drawVerticalLine (juce::roundToInt ((float) xForFreq (noteHz (n))), keys.getY(), keys.getBottom());
    g.setColour (black);
    for (int n = nLo; n <= nHi; ++n)
        if (isBlack (n))
        {
            const float x0 = (float) xForFreq (noteHz (n - 0.36)), x1 = (float) xForFreq (noteHz (n + 0.36));
            g.fillRect (juce::Rectangle<float> (x0, keys.getY(), x1 - x0, keys.getHeight() * 0.6f));
        }
    // octave labels on the C keys, where they fit
    const auto labelFont = font (8.0f);
    g.setColour (label);
    g.setFont (labelFont);
    for (int n = nLo; n <= nHi; ++n)
        if (((n % 12) + 12) % 12 == 0)
        {
            const float x0 = (float) xForFreq (noteHz (n - 0.5)) + 1.0f, x1 = (float) xForFreq (noteHz (n + 1.0)) - 1.0f;
            const auto name = "C" + juce::String (n / 12 - 1);
            if (juce::GlyphArrangement::getStringWidth (labelFont, name) <= x1 - x0)
                g.drawText (name, juce::Rectangle<float> (x0, keys.getBottom() - 9.0f, x1 - x0, 9.0f), juce::Justification::centred, false);
        }
    g.setColour (edge);
    g.drawHorizontalLine (juce::roundToInt (keys.getY()), keys.getX(), keys.getRight());
}

// A horizontal line at `y` with its value in the left gutter; `strong` for the reference line.
inline void drawLevelLine (juce::Graphics& g, juce::Rectangle<float> plot, float y, const juce::String& label,
                           bool strong, const Palette& p)
{
    g.setColour (strong ? p.outline : p.well);
    g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    if (label.isNotEmpty())
    {
        g.setFont (font (10.5f));
        g.setColour (p.textDim);
        g.drawText (label, juce::Rectangle<float> (plot.getX() - kSpectrumGutter + 2.0f, y - 7.0f, kSpectrumGutter - 6.0f, 14.0f),
                    juce::Justification::centredRight);
    }
}

// The IN / OUT legend: [line] IN   [line] OUT, from (x, y), 84 px wide and 12 high.
inline void drawAnalyzerLegend (juce::Graphics& g, float x, float y, const Palette& p)
{
    g.setFont (font (10.5f, true));
    for (int i = 0; i < 2; ++i)
    {
        const auto c = i == 0 ? p.textDim : p.accent;
        g.setColour (c);
        g.fillRoundedRectangle (juce::Rectangle<float> (x, y + 5.0f, 12.0f, 2.0f), 1.0f);
        g.drawText (i == 0 ? "IN" : "OUT", juce::Rectangle<float> (x + 16.0f, y, 30.0f, 12.0f), juce::Justification::centredLeft);
        x += 42.0f;
    }
}

// The analyser: `in` and `out` are polylines across the plot (left to right). IN is a quiet grey
// curve, OUT the accent one, both lightly filled down to the bottom; a small legend top right
// (legend false: none, the caller puts drawAnalyzerLegend() where it has room).
inline void drawAnalyzer (juce::Graphics& g, juce::Rectangle<float> plot, const juce::Path& in, const juce::Path& out,
                          const Palette& p, bool legend = true)
{
    auto filled = [&plot] (const juce::Path& line)
    {
        juce::Path area (line);
        const auto b = line.getBounds();
        area.lineTo (b.getRight(), plot.getBottom());
        area.lineTo (b.getX(), plot.getBottom());
        area.closeSubPath();
        return area;
    };
    const auto inColour = p.textDim, outColour = p.accent;
    if (! in.isEmpty())
    {
        g.setColour (inColour.withAlpha (p.light ? 0.10f : 0.12f));
        g.fillPath (filled (in));
        g.setColour (inColour.withAlpha (0.85f));
        g.strokePath (in, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (! out.isEmpty())
    {
        g.setColour (outColour.withAlpha (p.light ? 0.10f : 0.14f));
        g.fillPath (filled (out));
        g.setColour (outColour);
        g.strokePath (out, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (legend)
        drawAnalyzerLegend (g, plot.getRight() - 92.0f, plot.getY() + 8.0f, p);
}

// LookAndFeel::drawComboBox.
inline void drawComboBox (juce::Graphics& g, int w, int h, juce::ComboBox& box, const Palette& p)
{
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (1.0f);
    g.setColour (interactive (box.findColour (juce::ComboBox::backgroundColourId), p, box.isMouseOver (true), false));
    g.fillRoundedRectangle (r, kCorner);
    g.setColour (box.hasKeyboardFocus (true) ? p.accent : box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r.reduced (0.5f), kCorner, 1.0f);

    juce::Path arrow;
    const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId).withMultipliedAlpha (box.isEnabled() ? 1.0f : 0.4f));
    g.fillPath (arrow);
}

// The standard JUCE colour ids, from the palette. Call it after a theme switch, then set any
// plugin-specific colours on top.
inline void applyColours (juce::LookAndFeel_V4& lf, const Palette& p)
{
    // window, widget bg, menu bg, outline, default text, default fill, highlighted text, highlighted fill, menu text
    lf.setColourScheme ({ p.background, p.panel, p.panel, p.outline, p.text, p.button, p.onAccent, p.accent, p.text });

    lf.setColour (juce::ResizableWindow::backgroundColourId, p.background);
    lf.setColour (juce::Label::textColourId, p.text);

    lf.setColour (juce::TextButton::buttonColourId, p.button);
    lf.setColour (juce::TextButton::buttonOnColourId, p.accent);
    lf.setColour (juce::TextButton::textColourOffId, p.text);
    lf.setColour (juce::TextButton::textColourOnId, p.onAccent);
    lf.setColour (juce::ToggleButton::textColourId, p.text);
    lf.setColour (juce::ToggleButton::tickColourId, p.accent);
    lf.setColour (juce::ToggleButton::tickDisabledColourId, p.textDim);

    lf.setColour (juce::Slider::rotarySliderFillColourId, p.accent);
    lf.setColour (juce::Slider::rotarySliderOutlineColourId, p.well);
    lf.setColour (juce::Slider::thumbColourId, p.accent);
    lf.setColour (juce::Slider::trackColourId, p.accent);
    lf.setColour (juce::Slider::backgroundColourId, p.well);
    lf.setColour (juce::Slider::textBoxTextColourId, p.text);
    lf.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lf.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    lf.setColour (juce::Slider::textBoxHighlightColourId, p.accent.withAlpha (0.35f));

    lf.setColour (juce::ComboBox::backgroundColourId, p.button);
    lf.setColour (juce::ComboBox::buttonColourId, p.button);
    lf.setColour (juce::ComboBox::textColourId, p.text);
    lf.setColour (juce::ComboBox::outlineColourId, p.outline);
    lf.setColour (juce::ComboBox::arrowColourId, p.textDim);

    lf.setColour (juce::PopupMenu::backgroundColourId, p.panel);
    lf.setColour (juce::PopupMenu::textColourId, p.text);
    lf.setColour (juce::PopupMenu::headerTextColourId, p.textDim);
    lf.setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
    lf.setColour (juce::PopupMenu::highlightedTextColourId, p.onAccent);

    lf.setColour (juce::TextEditor::backgroundColourId, p.screen);
    lf.setColour (juce::TextEditor::textColourId, p.text);
    lf.setColour (juce::TextEditor::outlineColourId, p.outline);
    lf.setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    lf.setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.35f));
    lf.setColour (juce::TextEditor::highlightedTextColourId, p.text);
    lf.setColour (juce::CaretComponent::caretColourId, p.accent);

    lf.setColour (juce::AlertWindow::backgroundColourId, p.panel);
    lf.setColour (juce::AlertWindow::textColourId, p.text);
    lf.setColour (juce::AlertWindow::outlineColourId, p.outline);
    lf.setColour (juce::BubbleComponent::backgroundColourId, p.panel);
    lf.setColour (juce::BubbleComponent::outlineColourId, p.outline);
    lf.setColour (juce::TooltipWindow::backgroundColourId, p.panel);
    lf.setColour (juce::TooltipWindow::textColourId, p.text);
    lf.setColour (juce::TooltipWindow::outlineColourId, p.outline);
    lf.setColour (juce::ScrollBar::thumbColourId, p.well);
}
} // namespace bidoo::look
