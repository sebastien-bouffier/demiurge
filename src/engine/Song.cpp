#include "Song.h"

#include <algorithm>
#include <cmath>

namespace dm
{
//==============================================================================
//  Notes
//==============================================================================
static juce::String num (double v)
{
    // Up to 4 decimals, no trailing zeros: "1", "0.25", "3.3333"
    auto s = juce::String (std::round (v * 10000.0) / 10000.0, 4);
    if (s.containsChar ('.'))
    {
        s = s.trimCharactersAtEnd ("0");
        if (s.endsWithChar ('.'))
            s = s.dropLastCharacters (1);
    }
    return s == "-0" ? "0" : s;
}

juce::String notesToString (const Notes& notes)
{
    juce::String out;
    out.preallocateBytes (notes.size() * 18);
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        if (i > 0)
            out << "; ";
        out << num (n.start) << ' ' << num (n.length) << ' ' << n.pitch << ' ' << n.velocity;
    }
    return out;
}

Notes notesFromString (const juce::String& text)
{
    Notes notes;
    juce::StringArray entries;
    entries.addTokens (text.replaceCharacters (",\n\r|", ";;;;"), ";", {});
    notes.reserve ((size_t) entries.size());

    for (auto& e : entries)
    {
        juce::StringArray f;
        f.addTokens (e.trim(), " \t", {});
        f.removeEmptyStrings();
        if (f.size() < 3)
            continue;

        auto isNumber = [] (const juce::String& s) { return s.containsOnly ("0123456789.-+eE") && s.isNotEmpty(); };
        if (! isNumber (f[0]) || ! isNumber (f[1]) || ! isNumber (f[2]))
            continue;

        Note n;
        n.start = f[0].getDoubleValue();
        n.length = f[1].getDoubleValue();
        n.pitch = f[2].getIntValue();
        n.velocity = f.size() > 3 && isNumber (f[3]) ? f[3].getIntValue() : 100;
        notes.push_back (n);
    }
    return notes;
}

void sanitise (Notes& notes, double maxBeats)
{
    notes.erase (std::remove_if (notes.begin(), notes.end(), [maxBeats] (const Note& n)
                                 {
                                     return ! std::isfinite (n.start) || ! std::isfinite (n.length)
                                         || n.start < -1.0e-6 || (maxBeats > 0.0 && n.start >= maxBeats - 1.0e-6)
                                         || n.pitch < 0 || n.pitch > 127;
                                 }),
                 notes.end());

    for (auto& n : notes)
    {
        n.start = juce::jmax (0.0, n.start);
        n.length = juce::jlimit (1.0 / 64.0, 64.0, n.length);
        n.velocity = juce::jlimit (1, 127, n.velocity);
    }

    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                      { return a.start < b.start - 1.0e-9 || (std::abs (a.start - b.start) <= 1.0e-9 && a.pitch < b.pitch); });

    // The same pitch twice at the same time is one note.
    notes.erase (std::unique (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
                              { return a.pitch == b.pitch && std::abs (a.start - b.start) < 1.0e-4; }),
                 notes.end());
}

//==============================================================================
//  Roles, programs, keys
//==============================================================================
static const char* kRoleNames[] = { "drums", "percussion", "bass", "keys", "pad", "strings", "brass", "woodwinds",
                                    "choir", "lead", "arp", "guitar", "pluck", "melody", "fx" };
static_assert (sizeof (kRoleNames) / sizeof (kRoleNames[0]) == (size_t) Role::count);

juce::String roleName (Role r) { return kRoleNames[juce::jlimit (0, (int) Role::count - 1, (int) r)]; }

Role roleFromName (const juce::String& s)
{
    for (int i = 0; i < (int) Role::count; ++i)
        if (s.trim().equalsIgnoreCase (kRoleNames[i]))
            return (Role) i;
    return Role::keys;
}

juce::StringArray roleNames()
{
    juce::StringArray a;
    for (auto* n : kRoleNames)
        a.add (n);
    return a;
}

int defaultProgram (Role r)
{
    switch (r)
    {
        case Role::drums:      return 0;
        case Role::percussion: return 0;
        case Role::bass:       return 33;  // Electric Bass (finger)
        case Role::keys:       return 0;   // Acoustic Grand Piano
        case Role::pad:        return 89;  // Pad 2 (warm)
        case Role::strings:    return 48;  // String Ensemble 1
        case Role::brass:      return 61;  // Brass Section
        case Role::woodwinds:  return 73;  // Flute
        case Role::choir:      return 52;  // Choir Aahs
        case Role::lead:       return 81;  // Lead 2 (sawtooth)
        case Role::arp:        return 80;  // Lead 1 (square)
        case Role::guitar:     return 27;  // Electric Guitar (clean)
        case Role::pluck:      return 45;  // Pizzicato Strings
        case Role::melody:     return 53;  // Voice Oohs (a guide for a singer)
        case Role::fx:         return 98;  // FX 3 (crystal)
        case Role::count:      break;
    }
    return 0;
}

juce::String gmProgramName (int program)
{
    static const char* names[128] = {
        "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1",
        "Electric Piano 2", "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba",
        "Xylophone", "Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ",
        "Reed Organ", "Accordion", "Harmonica", "Tango Accordion", "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)",
        "Electric Guitar (jazz)", "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar",
        "Distortion Guitar", "Guitar Harmonics", "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)",
        "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello",
        "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Orchestral Harp", "Timpani", "String Ensemble 1",
        "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice",
        "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1",
        "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon",
        "Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
        "Lead 1 (square)", "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)",
        "Lead 6 (voice)", "Lead 7 (fifths)", "Lead 8 (bass + lead)", "Pad 1 (new age)", "Pad 2 (warm)",
        "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)", "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)",
        "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)", "FX 5 (brightness)",
        "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba",
        "Bagpipe", "Fiddle", "Shanai", "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum",
        "Melodic Tom", "Synth Drum", "Reverse Cymbal", "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet",
        "Telephone Ring", "Helicopter", "Applause", "Gunshot"
    };
    return names[juce::jlimit (0, 127, program)];
}

juce::StringArray keyNames() { return { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" }; }

juce::StringArray scaleNames()
{
    return { "major", "minor", "harmonic minor", "melodic minor", "dorian", "phrygian", "lydian", "mixolydian",
             "locrian", "major pentatonic", "minor pentatonic", "blues", "chromatic" };
}

juce::StringArray timeSignatures() { return { "4/4", "3/4", "2/4", "5/4", "6/8", "7/8", "9/8", "12/8" }; }

//==============================================================================
//  Song
//==============================================================================
juce::ValueTree Song::createDefault()
{
    juce::ValueTree t (ids::SONG);
    t.setProperty (ids::title, "Untitled", nullptr);
    t.setProperty (ids::key, 9, nullptr); // A
    t.setProperty (ids::scale, "minor", nullptr);
    t.setProperty (ids::bpm, 120.0, nullptr);
    t.setProperty (ids::tsNum, 4, nullptr);
    t.setProperty (ids::tsDen, 4, nullptr);
    t.setProperty (ids::style, "", nullptr);
    t.setProperty (ids::idea, "", nullptr);
    t.setProperty (ids::nextId, 1, nullptr);
    t.appendChild (juce::ValueTree (ids::SECTIONS), nullptr);
    t.appendChild (juce::ValueTree (ids::PARTS), nullptr);

    Song s (t);
    s.addSection ("Verse", 8, "Am | F | C | G | Am | F | C | G");
    s.addSection ("Chorus", 8, "F | G | Am | Am | F | G | E | E");
    s.addPart ("Drums", Role::drums);
    s.addPart ("Bass", Role::bass);
    s.addPart ("Chords", Role::keys);
    s.addPart ("Melody", Role::melody);
    return t;
}

double Song::beatsPerBar() const
{
    const int n = juce::jlimit (1, 32, (int) tree.getProperty (ids::tsNum, 4));
    const int d = juce::jlimit (1, 32, (int) tree.getProperty (ids::tsDen, 4));
    return n * 4.0 / d;
}

juce::ValueTree Song::sectionById (const juce::String& id) const { return sections().getChildWithProperty (ids::id, id); }
juce::ValueTree Song::partById (const juce::String& id) const { return parts().getChildWithProperty (ids::id, id); }
int Song::sectionIndex (const juce::String& id) const { return sections().indexOf (sectionById (id)); }
int Song::partIndex (const juce::String& id) const { return parts().indexOf (partById (id)); }

double Song::sectionLengthBeats (const juce::ValueTree& s) const
{
    return juce::jlimit (1, 256, (int) s.getProperty (ids::bars, 4)) * beatsPerBar();
}

double Song::sectionStartBeat (int index) const
{
    double b = 0.0;
    for (int i = 0; i < juce::jmin (index, numSections()); ++i)
        b += sectionLengthBeats (section (i));
    return b;
}

double Song::lengthBeats() const { return sectionStartBeat (numSections()); }

int Song::sectionAt (double beat) const
{
    double b = 0.0;
    for (int i = 0; i < numSections(); ++i)
    {
        const double len = sectionLengthBeats (section (i));
        if (beat >= b && beat < b + len)
            return i;
        b += len;
    }
    return -1;
}

juce::String Song::makeId (const juce::String& prefix)
{
    const int n = juce::jmax (1, (int) tree.getProperty (ids::nextId, 1));
    tree.setProperty (ids::nextId, n + 1, undo);
    return prefix + juce::String (n);
}

juce::ValueTree Song::addSection (const juce::String& name, int bars, const juce::String& chords, int insertAt)
{
    juce::ValueTree s (ids::SECTION);
    s.setProperty (ids::id, makeId ("s"), nullptr);
    s.setProperty (ids::name, name, nullptr);
    s.setProperty (ids::bars, juce::jlimit (1, 256, bars), nullptr);
    s.setProperty (ids::chords, chords, nullptr);
    sections().addChild (s, insertAt, undo);
    return s;
}

juce::ValueTree Song::duplicateSection (int index)
{
    auto src = section (index);
    if (! src.isValid())
        return {};

    auto copy = addSection (src[ids::name].toString(), (int) src[ids::bars], src[ids::chords].toString(), index + 1);
    const auto srcId = src[ids::id].toString(), newId = copy[ids::id].toString();
    for (int p = 0; p < numParts(); ++p)
    {
        auto pt = part (p);
        if (auto take = activeTake (pt, srcId); take.isValid())
            addTake (pt, newId, notesFromString (take[ids::notes].toString()), take[ids::prompt].toString(), "copy");
    }
    return copy;
}

void Song::removeSection (int index)
{
    auto s = section (index);
    if (! s.isValid())
        return;
    const auto id = s[ids::id].toString();
    for (int p = 0; p < numParts(); ++p)
    {
        auto pt = part (p);
        auto c = pt.getChildWithProperty (ids::section, id);
        if (c.isValid())
            pt.removeChild (c, undo);
    }
    sections().removeChild (s, undo);
}

void Song::moveSection (int index, int newIndex)
{
    if (index >= 0 && index < numSections() && newIndex >= 0 && newIndex < numSections())
        sections().moveChild (index, newIndex, undo);
}

int Song::freeChannelFor (Role role) const
{
    if (role == Role::drums || role == Role::percussion)
        return 10;
    for (int ch = 1; ch <= 16; ++ch)
    {
        if (ch == 10)
            continue;
        bool used = false;
        for (int p = 0; p < numParts(); ++p)
            used = used || (int) part (p)[ids::channel] == ch;
        if (! used)
            return ch;
    }
    return 1;
}

juce::ValueTree Song::addPart (const juce::String& name, Role role, int insertAt)
{
    juce::ValueTree p (ids::PART);
    p.setProperty (ids::id, makeId ("p"), nullptr);
    p.setProperty (ids::name, name, nullptr);
    p.setProperty (ids::role, roleName (role), nullptr);
    p.setProperty (ids::channel, freeChannelFor (role), nullptr);
    p.setProperty (ids::program, defaultProgram (role), nullptr);
    p.setProperty (ids::mute, false, nullptr);
    p.setProperty (ids::solo, false, nullptr);
    p.setProperty (ids::description, "", nullptr);
    parts().addChild (p, insertAt, undo);
    return p;
}

void Song::removePart (int index)
{
    auto p = part (index);
    if (p.isValid())
        parts().removeChild (p, undo);
}

void Song::movePart (int index, int newIndex)
{
    if (index >= 0 && index < numParts() && newIndex >= 0 && newIndex < numParts())
        parts().moveChild (index, newIndex, undo);
}

juce::ValueTree Song::cell (const juce::ValueTree& partTree, const juce::String& sectionId, bool create)
{
    auto pt = partTree;
    auto c = pt.getChildWithProperty (ids::section, sectionId);
    if (! c.isValid() && create)
    {
        c = juce::ValueTree (ids::CELL);
        c.setProperty (ids::section, sectionId, nullptr);
        c.setProperty (ids::active, -1, nullptr);
        pt.appendChild (c, undo);
    }
    return c;
}

juce::ValueTree Song::activeTake (const juce::ValueTree& partTree, const juce::String& sectionId) const
{
    auto c = partTree.getChildWithProperty (ids::section, sectionId);
    if (! c.isValid() || c.getNumChildren() == 0)
        return {};
    const int a = juce::jlimit (0, c.getNumChildren() - 1, (int) c.getProperty (ids::active, c.getNumChildren() - 1));
    return c.getChild (a);
}

Notes Song::activeNotes (const juce::ValueTree& partTree, const juce::String& sectionId) const
{
    auto t = activeTake (partTree, sectionId);
    return t.isValid() ? notesFromString (t[ids::notes].toString()) : Notes {};
}

int Song::numTakes (const juce::ValueTree& partTree, const juce::String& sectionId) const
{
    auto c = partTree.getChildWithProperty (ids::section, sectionId);
    return c.isValid() ? c.getNumChildren() : 0;
}

int Song::activeTakeIndex (const juce::ValueTree& partTree, const juce::String& sectionId) const
{
    auto c = partTree.getChildWithProperty (ids::section, sectionId);
    if (! c.isValid() || c.getNumChildren() == 0)
        return -1;
    return juce::jlimit (0, c.getNumChildren() - 1, (int) c.getProperty (ids::active, c.getNumChildren() - 1));
}

juce::ValueTree Song::addTake (juce::ValueTree partTree, const juce::String& sectionId, const Notes& notes,
                               const juce::String& prompt, const juce::String& source)
{
    auto c = cell (partTree, sectionId, true);
    juce::ValueTree t (ids::TAKE);
    t.setProperty (ids::id, makeId ("t"), nullptr);
    t.setProperty (ids::prompt, prompt, nullptr);
    t.setProperty (ids::source, source, nullptr);
    t.setProperty (ids::notes, notesToString (notes), nullptr);
    c.appendChild (t, undo);
    c.setProperty (ids::active, c.getNumChildren() - 1, undo);
    return t;
}

void Song::setActiveTake (juce::ValueTree partTree, const juce::String& sectionId, int index)
{
    auto c = cell (partTree, sectionId, false);
    if (c.isValid() && c.getNumChildren() > 0)
        c.setProperty (ids::active, juce::jlimit (0, c.getNumChildren() - 1, index), undo);
}

void Song::deleteActiveTake (juce::ValueTree partTree, const juce::String& sectionId)
{
    auto c = cell (partTree, sectionId, false);
    if (! c.isValid() || c.getNumChildren() == 0)
        return;
    const int a = activeTakeIndex (partTree, sectionId);
    c.removeChild (a, undo);
    if (c.getNumChildren() == 0)
        partTree.removeChild (c, undo);
    else
        c.setProperty (ids::active, juce::jmin (a, c.getNumChildren() - 1), undo);
}

void Song::setActiveNotes (juce::ValueTree partTree, const juce::String& sectionId, const Notes& notes,
                           const juce::String& source)
{
    auto t = activeTake (partTree, sectionId);
    if (t.isValid())
        t.setProperty (ids::notes, notesToString (notes), undo);
    else
        addTake (partTree, sectionId, notes, {}, source);
}

int Song::importAbsolute (juce::ValueTree partTree, const Notes& absolute, const juce::String& source)
{
    int added = 0;
    double start = 0.0;
    for (int i = 0; i < numSections(); ++i)
    {
        auto s = section (i);
        const double len = sectionLengthBeats (s);
        Notes local;
        for (const auto& n : absolute)
            if (n.start >= start - 1.0e-6 && n.start < start + len - 1.0e-6)
            {
                auto m = n;
                m.start -= start;
                local.push_back (m);
            }
        if (! local.empty())
        {
            sanitise (local, len);
            addTake (partTree, s[ids::id].toString(), local, {}, source);
            ++added;
        }
        start += len;
    }
    return added;
}

Notes Song::flattenPart (const juce::ValueTree& partTree) const
{
    Notes out;
    double start = 0.0;
    for (int i = 0; i < numSections(); ++i)
    {
        auto s = section (i);
        for (auto n : activeNotes (partTree, s[ids::id].toString()))
        {
            n.start += start;
            out.push_back (n);
        }
        start += sectionLengthBeats (s);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Note& a, const Note& b) { return a.start < b.start; });
    return out;
}

//==============================================================================
PlaybackModel PlaybackModel::build (const Song& song)
{
    PlaybackModel m;
    m.bpm = song.bpm();

    bool anySolo = false;
    for (int p = 0; p < song.numParts(); ++p)
        anySolo = anySolo || (bool) song.part (p)[ids::solo];

    for (int p = 0; p < song.numParts(); ++p)
    {
        auto pt = song.part (p);
        Part part;
        part.channel = juce::jlimit (1, 16, (int) pt.getProperty (ids::channel, 1));
        part.audible = ! (bool) pt[ids::mute] && (! anySolo || (bool) pt[ids::solo]);
        part.notes = song.flattenPart (pt);
        m.parts.push_back (std::move (part));
    }

    double start = 0.0;
    for (int i = 0; i < song.numSections(); ++i)
    {
        m.sectionStarts.push_back (start);
        start += song.sectionLengthBeats (song.section (i));
    }
    m.sectionStarts.push_back (start);
    m.lengthBeats = start;
    return m;
}

} // namespace dm
