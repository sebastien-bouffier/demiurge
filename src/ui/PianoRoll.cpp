#include "PianoRoll.h"

using namespace dm;

namespace
{
juce::String noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[pitch % 12] + juce::String (pitch / 12 - 1);
}

juce::String drumName (int pitch)
{
    switch (pitch)
    {
        case 35: return "Kick 2";   case 36: return "Kick";     case 37: return "Stick";   case 38: return "Snare";
        case 39: return "Clap";     case 40: return "Snare 2";  case 41: return "Tom 6";   case 42: return "HH cl";
        case 43: return "Tom 5";    case 44: return "HH ped";   case 45: return "Tom 4";   case 46: return "HH op";
        case 47: return "Tom 3";    case 48: return "Tom 2";    case 49: return "Crash";   case 50: return "Tom 1";
        case 51: return "Ride";     case 52: return "China";    case 53: return "Bell";    case 54: return "Tamb";
        case 55: return "Splash";   case 56: return "Cowbell";  case 57: return "Crash 2"; case 59: return "Ride 2";
        case 60: return "Bongo h";  case 61: return "Bongo l";  case 62: return "Conga m"; case 63: return "Conga h";
        case 64: return "Conga l";  case 69: return "Cabasa";   case 70: return "Maracas"; case 75: return "Claves";
        default: return noteName (pitch);
    }
}
} // namespace

bool PianoRoll::isDrums() const
{
    auto pt = processor.song().partById (processor.selectedPart);
    const auto r = roleFromName (pt[ids::role].toString());
    return r == Role::drums || r == Role::percussion;
}

void PianoRoll::refresh()
{
    if (dragging >= 0)
        return; // don't fight an edit in progress
    auto s = processor.song();
    auto sec = s.sectionById (processor.selectedSection);
    auto pt = s.partById (processor.selectedPart);
    notes = (sec.isValid() && pt.isValid()) ? s.activeNotes (pt, processor.selectedSection) : Notes {};
    length = sec.isValid() ? s.sectionLengthBeats (sec) : 16.0;
    chords.clear();
    chords.addTokens (sec[ids::chords].toString(), "|", {});
    chords.trim();

    // Pitch range: the notes, at least two octaves, centred on a sensible register.
    if (notes.empty())
    {
        const auto r = roleFromName (pt[ids::role].toString());
        const int centre = (r == Role::drums || r == Role::percussion) ? 44 : r == Role::bass ? 40 : 64;
        lo = centre - 12;
        hi = centre + 12;
    }
    else
    {
        lo = 127;
        hi = 0;
        for (auto& n : notes)
        {
            lo = juce::jmin (lo, n.pitch);
            hi = juce::jmax (hi, n.pitch);
        }
        lo -= 2;
        hi += 2;
        while (hi - lo < 24)
        {
            --lo;
            ++hi;
        }
        lo = juce::jmax (0, lo);
        hi = juce::jmin (127, hi);
    }
    repaint();
}

juce::Rectangle<float> PianoRoll::grid() const
{
    return getLocalBounds().toFloat().withTrimmedLeft ((float) kKeys).withTrimmedTop ((float) kRuler);
}

float PianoRoll::xFor (double beat) const
{
    const auto g = grid();
    return g.getX() + (float) (beat / juce::jmax (1.0, length)) * g.getWidth();
}

double PianoRoll::beatAt (float x) const
{
    const auto g = grid();
    return (x - g.getX()) / juce::jmax (1.0f, g.getWidth()) * length;
}

float PianoRoll::yFor (int pitch) const
{
    const auto g = grid();
    const float row = g.getHeight() / (float) (hi - lo + 1);
    return g.getBottom() - (float) (pitch - lo + 1) * row;
}

int PianoRoll::pitchAt (float y) const
{
    const auto g = grid();
    const float row = g.getHeight() / (float) (hi - lo + 1);
    return juce::jlimit (0, 127, lo + (int) std::floor ((g.getBottom() - y) / row));
}

juce::Rectangle<float> PianoRoll::noteRect (const Note& n) const
{
    const auto g = grid();
    const float row = g.getHeight() / (float) (hi - lo + 1);
    const float x0 = xFor (n.start), x1 = juce::jmin (g.getRight(), xFor (n.start + n.length));
    return { x0, yFor (n.pitch), juce::jmax (3.0f, x1 - x0), row };
}

int PianoRoll::noteAt (juce::Point<float> p, bool& onEdge) const
{
    for (int i = (int) notes.size(); --i >= 0;)
    {
        const auto r = noteRect (notes[(size_t) i]).expanded (0.0f, 1.0f);
        if (r.contains (p))
        {
            onEdge = p.x > r.getRight() - juce::jmin (6.0f, r.getWidth() * 0.35f);
            return i;
        }
    }
    onEdge = false;
    return -1;
}

void PianoRoll::paint (juce::Graphics& g)
{
    const auto& pal = lnf.palette();
    const auto gr = grid();
    const bool drums = isDrums();
    auto s = processor.song();
    const double beatsPerBar = s.beatsPerBar();

    g.setColour (pal.screen);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    // Rows: black keys darker, C rows labelled
    const float row = gr.getHeight() / (float) (hi - lo + 1);
    for (int p = lo; p <= hi; ++p)
    {
        const int pc = p % 12;
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        const float y = yFor (p);
        if (black && ! drums)
        {
            g.setColour (pal.well.withAlpha (0.45f));
            g.fillRect (gr.getX(), y, gr.getWidth(), row);
        }
        if ((drums || pc == 0) && row >= 7.0f)
        {
            g.setColour (pal.textDim);
            g.setFont (bidoo::look::font (juce::jmin (10.5f, row + 1.0f)));
            g.drawText (drums ? drumName (p) : noteName (p), juce::Rectangle<float> (3.0f, y, (float) kKeys - 5.0f, row),
                        juce::Justification::centredLeft, false);
        }
        if (pc == 0 && ! drums)
        {
            g.setColour (pal.outline.withAlpha (0.6f));
            g.drawHorizontalLine (juce::roundToInt (y + row), gr.getX(), gr.getRight());
        }
    }

    // Columns: bars, beats, sixteenths
    for (double b = 0.0; b < length + 1.0e-6; b += kSnap)
    {
        const bool bar = std::fmod (b + 1.0e-6, beatsPerBar) < 2.0e-6;
        const bool beat = std::fmod (b + 1.0e-6, 1.0) < 2.0e-6;
        if (! bar && ! beat && gr.getWidth() / (length / kSnap) < 5.0)
            continue;
        g.setColour (bar ? pal.outline : beat ? pal.well : pal.well.withAlpha (0.4f));
        g.drawVerticalLine (juce::roundToInt (xFor (b)), gr.getY(), gr.getBottom());
    }

    // Ruler: bar numbers and chords
    const int bars = juce::roundToInt (length / beatsPerBar);
    g.setFont (bidoo::look::font (10.5f, true));
    for (int bar = 0; bar < bars; ++bar)
    {
        const float x0 = xFor (bar * beatsPerBar), x1 = xFor ((bar + 1) * beatsPerBar);
        auto r = juce::Rectangle<float> (x0 + 3.0f, 1.0f, x1 - x0 - 4.0f, (float) kRuler - 2.0f);
        g.setColour (pal.textDim);
        g.drawText (juce::String (bar + 1), r, juce::Justification::centredLeft, false);
        if (bar < chords.size())
        {
            g.setColour (pal.text);
            g.drawText (chords[bar], r.withTrimmedLeft (16.0f), juce::Justification::centredLeft, true);
        }
    }

    // Notes, coloured by the part's role, brighter when louder
    auto pt = s.partById (processor.selectedPart);
    const auto colour = lnf.roleColour (roleFromName (pt[ids::role].toString()));
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto r = noteRect (notes[i]).reduced (0.0f, 0.5f);
        g.setColour (colour.withAlpha (0.45f + 0.55f * (float) notes[i].velocity / 127.0f));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour ((int) i == dragging ? pal.text : colour.darker (0.4f));
        g.drawRoundedRectangle (r, 2.0f, (int) i == dragging ? 1.5f : 0.8f);
    }

    // Playhead inside this section
    const double pos = processor.livePosition.load();
    const int secIndex = s.sectionIndex (processor.selectedSection);
    if (pos >= 0.0 && secIndex >= 0)
    {
        const double local = pos - s.sectionStartBeat (secIndex);
        if (local >= 0.0 && local < length)
        {
            g.setColour (pal.accent);
            g.fillRect (xFor (local) - 1.0f, gr.getY(), 2.0f, gr.getHeight());
        }
    }

    if (notes.empty())
    {
        g.setColour (pal.textDim);
        g.setFont (bidoo::look::font (13.0f));
        g.drawText (pt.isValid() ? "No notes yet. GENERATE asks Claude; double-click to draw notes by hand."
                                 : "Select a cell in the arrangement.",
                    gr, juce::Justification::centred);
    }
}

void PianoRoll::mouseMove (const juce::MouseEvent& e)
{
    bool edge = false;
    const int i = noteAt (e.position, edge);
    setMouseCursor (i < 0 ? juce::MouseCursor::NormalCursor
                          : edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
}

void PianoRoll::mouseDown (const juce::MouseEvent& e)
{
    bool edge = false;
    dragging = noteAt (e.position, edge);
    resizing = edge;
    changed = false;
    if (dragging >= 0)
    {
        dragOrigin = notes[(size_t) dragging];
        dragStart = e.position;
        if (e.mods.isPopupMenu())
        {
            notes.erase (notes.begin() + dragging);
            dragging = -1;
            commit ("Delete note");
        }
    }
    repaint();
}

void PianoRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    auto& n = notes[(size_t) dragging];
    const double dBeat = beatAt (e.position.x) - beatAt (dragStart.x);
    if (resizing)
        n.length = juce::jlimit (kSnap, juce::jmax (kSnap, length - n.start), std::round ((dragOrigin.length + dBeat) / kSnap) * kSnap);
    else
    {
        n.start = juce::jlimit (0.0, length - kSnap, std::round ((dragOrigin.start + dBeat) / kSnap) * kSnap);
        n.pitch = juce::jlimit (0, 127, dragOrigin.pitch + pitchAt (e.position.y) - pitchAt (dragStart.y));
    }
    changed = changed || ! (n == dragOrigin);
    repaint();
}

void PianoRoll::mouseUp (const juce::MouseEvent&)
{
    const bool wasDragging = dragging >= 0;
    dragging = -1;
    if (wasDragging && changed)
        commit (resizing ? "Resize note" : "Move note");
    repaint();
}

void PianoRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! grid().contains (e.position) || ! processor.song().partById (processor.selectedPart).isValid())
        return;
    bool edge = false;
    const int i = noteAt (e.position, edge);
    if (i >= 0)
    {
        notes.erase (notes.begin() + i);
        commit ("Delete note");
        return;
    }
    Note n;
    n.start = juce::jlimit (0.0, length - kSnap, std::floor (beatAt (e.position.x) / kSnap) * kSnap);
    n.length = isDrums() ? kSnap : 0.5;
    n.pitch = pitchAt (e.position.y);
    n.velocity = 100;
    notes.push_back (n);
    commit ("Add note");
}

void PianoRoll::commit (const juce::String& what)
{
    auto s = processor.song();
    auto pt = s.partById (processor.selectedPart);
    if (! pt.isValid() || ! s.sectionById (processor.selectedSection).isValid())
        return;
    sanitise (notes, length);
    processor.undo.beginNewTransaction (what);
    s.setActiveNotes (pt, processor.selectedSection, notes, "hand");
    refresh();
}
