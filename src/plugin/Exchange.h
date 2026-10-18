#pragma once

// Getting MIDI out of Demiurge and into the DAW, and back:
//  - Standard MIDI files for drag and drop (a cell, a part, a section or the whole song).
//  - Text files shared with the Reaper scripts in reaper/, which read and replace MIDI items.

#include "engine/Song.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace exchange
{
juce::File clipsFolder();
juce::File exchangeFolder();
juce::File toReaperFile();    // written by Demiurge, read by "Demiurge - Replace selected item.lua"
juce::File fromReaperFile();  // written by "Demiurge - Send selected item.lua", read by Demiurge

// What to export. Empty part / section ids mean every part / every section.
struct Scope
{
    juce::String part, section;
};

// Writes a type 1 MIDI file: tempo and time signature, then one named track per part.
// Positions start at 0 at the beginning of the scope (the section, or the song).
juce::File writeMidiFile (const dm::Song& song, const Scope& scope);

// The scope's notes in song beats (quarter notes from the song start), with their channels.
struct AbsoluteNote
{
    dm::Note note;
    int channel = 1;
};
std::vector<AbsoluteNote> absoluteNotes (const dm::Song& song, const Scope& scope, double& rangeStart, double& rangeEnd);

// Demiurge -> Reaper. Returns false if the file could not be written.
bool writeToReaper (const dm::Song& song, const Scope& scope, juce::String& message);
// Reaper -> Demiurge: notes in song beats. Returns an error message, or empty.
juce::String readFromReaper (dm::Notes& notes, juce::String& itemName);

// Parses the exchange text format (also used by tests).
dm::Notes parseExchange (const juce::String& text, juce::String& itemName);
} // namespace exchange
