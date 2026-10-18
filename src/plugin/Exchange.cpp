#include "Exchange.h"
#include "UserData.h"

namespace exchange
{
using namespace dm;

juce::File clipsFolder() { return getUserDataDirectory().getChildFile ("Clips"); }
juce::File exchangeFolder() { return getUserDataDirectory().getChildFile ("Exchange"); }
juce::File toReaperFile() { return exchangeFolder().getChildFile ("to_reaper.txt"); }
juce::File fromReaperFile() { return exchangeFolder().getChildFile ("from_reaper.txt"); }

namespace
{
struct Range
{
    int firstSection = 0, lastSection = -1; // inclusive
    std::vector<int> parts;
};

Range rangeFor (const Song& song, const Scope& scope)
{
    Range r;
    if (scope.section.isNotEmpty())
        r.firstSection = r.lastSection = song.sectionIndex (scope.section);
    else
        r.lastSection = song.numSections() - 1;

    for (int p = 0; p < song.numParts(); ++p)
        if (scope.part.isEmpty() || song.part (p)[ids::id].toString() == scope.part)
            r.parts.push_back (p);
    return r;
}

juce::String scopeName (const Song& song, const Scope& scope)
{
    juce::String name = "Demiurge - " + song.tree[ids::title].toString();
    if (scope.part.isNotEmpty())
        name << " - " << song.partById (scope.part)[ids::name].toString();
    if (scope.section.isNotEmpty())
        name << " - " << song.sectionById (scope.section)[ids::name].toString();
    return juce::File::createLegalFileName (name);
}
} // namespace

std::vector<AbsoluteNote> absoluteNotes (const Song& song, const Scope& scope, double& rangeStart, double& rangeEnd)
{
    std::vector<AbsoluteNote> out;
    const auto r = rangeFor (song, scope);
    rangeStart = song.sectionStartBeat (juce::jmax (0, r.firstSection));
    rangeEnd = song.sectionStartBeat (r.lastSection + 1);

    for (int s = r.firstSection; s <= r.lastSection && s >= 0; ++s)
    {
        auto sec = song.section (s);
        const double start = song.sectionStartBeat (s);
        for (int p : r.parts)
        {
            auto pt = song.part (p);
            for (auto n : song.activeNotes (pt, sec[ids::id].toString()))
            {
                n.start += start;
                out.push_back ({ n, juce::jlimit (1, 16, (int) pt[ids::channel]) });
            }
        }
    }
    return out;
}

juce::File writeMidiFile (const Song& song, const Scope& scope)
{
    constexpr int ppq = 960;
    const auto r = rangeFor (song, scope);
    const double origin = song.sectionStartBeat (juce::jmax (0, r.firstSection));

    juce::MidiFile file;
    file.setTicksPerQuarterNote (ppq);

    juce::MidiMessageSequence meta;
    meta.addEvent (juce::MidiMessage::tempoMetaEvent (juce::roundToInt (60000000.0 / juce::jmax (1.0, song.bpm()))), 0.0);
    meta.addEvent (juce::MidiMessage::timeSignatureMetaEvent ((int) song.tree[ids::tsNum], (int) song.tree[ids::tsDen]), 0.0);
    meta.addEvent (juce::MidiMessage::textMetaEvent (3, song.tree[ids::title].toString()), 0.0);
    file.addTrack (meta);

    for (int p : r.parts)
    {
        auto pt = song.part (p);
        const int ch = juce::jlimit (1, 16, (int) pt[ids::channel]);
        juce::MidiMessageSequence seq;
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, pt[ids::name].toString()), 0.0);
        if (ch != 10)
            seq.addEvent (juce::MidiMessage::programChange (ch, juce::jlimit (0, 127, (int) pt[ids::program])), 0.0);

        for (int s = r.firstSection; s <= r.lastSection && s >= 0; ++s)
        {
            auto sec = song.section (s);
            const double start = song.sectionStartBeat (s) - origin;
            for (const auto& n : song.activeNotes (pt, sec[ids::id].toString()))
            {
                const double on = (start + n.start) * ppq, off = (start + n.start + n.length) * ppq;
                seq.addEvent (juce::MidiMessage::noteOn (ch, n.pitch, (juce::uint8) n.velocity), on);
                seq.addEvent (juce::MidiMessage::noteOff (ch, n.pitch), off);
            }
        }
        seq.updateMatchedPairs();
        file.addTrack (seq);
    }

    auto folder = clipsFolder();
    folder.createDirectory();
    auto out = folder.getChildFile (scopeName (song, scope) + ".mid");
    out.deleteFile();
    juce::FileOutputStream stream (out);
    if (stream.openedOk())
        file.writeTo (stream, 1);
    return out;
}

bool writeToReaper (const Song& song, const Scope& scope, juce::String& message)
{
    double from = 0.0, to = 0.0;
    const auto notes = absoluteNotes (song, scope, from, to);

    juce::String text;
    text << "demiurge 1\n"
         << "range " << from << " " << to << "\n"
         << "name " << scopeName (song, scope) << "\n";
    for (const auto& a : notes)
        text << "note " << a.note.start << " " << a.note.length << " " << a.note.pitch << " " << a.note.velocity
             << " " << a.channel << "\n";

    exchangeFolder().createDirectory();
    if (! toReaperFile().replaceWithText (text))
    {
        message = "Could not write " + toReaperFile().getFullPathName();
        return false;
    }
    message = juce::String ((int) notes.size()) + " notes ready. In Reaper, select the MIDI item to replace and run "
              "\"Demiurge - Replace selected item\".";
    return true;
}

Notes parseExchange (const juce::String& text, juce::String& itemName)
{
    Notes notes;
    juce::StringArray lines;
    lines.addLines (text);
    for (auto& l : lines)
    {
        juce::StringArray f;
        f.addTokens (l.trim(), " \t", {});
        f.removeEmptyStrings();
        if (f[0] == "name")
            itemName = l.fromFirstOccurrenceOf ("name", false, false).trim();
        else if (f[0] == "note" && f.size() >= 5)
            notes.push_back ({ f[1].getDoubleValue(), f[2].getDoubleValue(), f[3].getIntValue(), f[4].getIntValue() });
    }
    sanitise (notes, 0.0);
    return notes;
}

juce::String readFromReaper (Notes& notes, juce::String& itemName)
{
    const auto f = fromReaperFile();
    if (! f.existsAsFile())
        return "Nothing from Reaper yet. In Reaper, select a MIDI item and run \"Demiurge - Send selected item\".";
    notes = parseExchange (f.loadFileAsString(), itemName);
    if (notes.empty())
        return "The item sent from Reaper has no notes.";
    return {};
}
} // namespace exchange
