#pragma once

// The song: one juce::ValueTree, edited through a juce::UndoManager so every change
// (an AI take, a capture, a note edit, a new section) can be undone and redone.
//
//   SONG { title, key, scale, bpm, tsNum, tsDen, style, idea, nextId }
//     SECTIONS
//       SECTION { id, name, bars, chords, prompt } -- in arrangement order; prompt = direction for the section
//     PARTS
//       PART { id, name, role, channel, program, mute, solo, description, prompt }
//         CELL { section, active }                -- one per section that has takes
//           TAKE { id, prompt, source, notes }    -- every version, oldest first
//
// Note times are in beats (quarter notes) from the start of the section.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <vector>

namespace dm
{
namespace ids
{
#define DM_ID(name) inline const juce::Identifier name { #name };
DM_ID(SONG) DM_ID(SECTIONS) DM_ID(SECTION) DM_ID(PARTS) DM_ID(PART) DM_ID(CELL) DM_ID(TAKE)
DM_ID(title) DM_ID(key) DM_ID(scale) DM_ID(bpm) DM_ID(tsNum) DM_ID(tsDen) DM_ID(style) DM_ID(idea)
DM_ID(nextId) DM_ID(id) DM_ID(name) DM_ID(bars) DM_ID(chords) DM_ID(role) DM_ID(channel)
DM_ID(program) DM_ID(mute) DM_ID(solo) DM_ID(description) DM_ID(section) DM_ID(active)
DM_ID(prompt) DM_ID(source) DM_ID(notes)
#undef DM_ID
} // namespace ids

struct Note
{
    double start = 0.0;  // beats
    double length = 0.25; // beats
    int pitch = 60;
    int velocity = 100;

    bool operator== (const Note& o) const
    {
        return pitch == o.pitch && velocity == o.velocity
            && std::abs (start - o.start) < 1.0e-6 && std::abs (length - o.length) < 1.0e-6;
    }
};

using Notes = std::vector<Note>;

// "start length pitch velocity; ..." -- the format stored in TAKE.notes and used with the AI.
juce::String notesToString (const Notes& notes);
// Tolerant: accepts ';', ',' or new lines between notes. Invalid entries are skipped.
Notes notesFromString (const juce::String& text);
// Sorts, clamps pitch / velocity, drops notes outside [0, maxBeats) and gives every note a
// minimum length. maxBeats <= 0 keeps every start time >= 0.
void sanitise (Notes& notes, double maxBeats);

// Instrument roles. The role picks the default MIDI channel, GM program and what the AI is told.
enum class Role { drums, percussion, bass, keys, pad, strings, brass, woodwinds, choir, lead,
                  arp, guitar, pluck, melody, fx, count };
juce::String roleName (Role r);          // "drums", "bass", ... (also the AI's enum)
Role roleFromName (const juce::String& s);
juce::StringArray roleNames();
int defaultProgram (Role r);              // GM program 0-127
juce::String gmProgramName (int program); // "Acoustic Grand Piano", ...

juce::StringArray keyNames();             // C, C#, D, ... B
juce::StringArray scaleNames();           // major, minor, dorian, ...
juce::StringArray timeSignatures();       // 4/4, 3/4, ...

//==============================================================================
class Song
{
public:
    static juce::ValueTree createDefault();

    // Wraps a SONG tree (does not own it).
    explicit Song (juce::ValueTree songTree, juce::UndoManager* um = nullptr) : tree (songTree), undo (um) {}

    juce::ValueTree tree;
    juce::UndoManager* undo = nullptr;

    double beatsPerBar() const;
    double bpm() const { return (double) tree.getProperty (ids::bpm, 120.0); }

    juce::ValueTree sections() const { return tree.getChildWithName (ids::SECTIONS); }
    juce::ValueTree parts() const { return tree.getChildWithName (ids::PARTS); }
    int numSections() const { return sections().getNumChildren(); }
    int numParts() const { return parts().getNumChildren(); }
    juce::ValueTree section (int i) const { return sections().getChild (i); }
    juce::ValueTree part (int i) const { return parts().getChild (i); }
    juce::ValueTree sectionById (const juce::String& id) const;
    juce::ValueTree partById (const juce::String& id) const;
    int sectionIndex (const juce::String& id) const;
    int partIndex (const juce::String& id) const;

    double sectionLengthBeats (const juce::ValueTree& section) const;
    double sectionStartBeat (int index) const;
    double lengthBeats() const;
    // Section that holds the song position `beat` (-1 when past the end).
    int sectionAt (double beat) const;

    juce::String makeId (const juce::String& prefix);

    juce::ValueTree addSection (const juce::String& name, int bars, const juce::String& chords, int insertAt = -1);
    juce::ValueTree duplicateSection (int index);   // copies every part's active take
    void removeSection (int index);
    void moveSection (int index, int newIndex);

    juce::ValueTree addPart (const juce::String& name, Role role, int insertAt = -1);
    void removePart (int index);
    void movePart (int index, int newIndex);
    int freeChannelFor (Role role) const;

    // Cells and takes
    juce::ValueTree cell (const juce::ValueTree& part, const juce::String& sectionId, bool create);
    juce::ValueTree activeTake (const juce::ValueTree& part, const juce::String& sectionId) const;
    Notes activeNotes (const juce::ValueTree& part, const juce::String& sectionId) const;
    int numTakes (const juce::ValueTree& part, const juce::String& sectionId) const;
    int activeTakeIndex (const juce::ValueTree& part, const juce::String& sectionId) const;
    juce::ValueTree addTake (juce::ValueTree part, const juce::String& sectionId, const Notes& notes,
                             const juce::String& prompt, const juce::String& source);
    void setActiveTake (juce::ValueTree part, const juce::String& sectionId, int index);
    void deleteActiveTake (juce::ValueTree part, const juce::String& sectionId);
    // Replaces the notes of the active take (piano roll edits). Creates a take if there is none.
    void setActiveNotes (juce::ValueTree part, const juce::String& sectionId, const Notes& notes,
                         const juce::String& source);

    // Notes given in song beats (a capture, a Reaper item) are split across the sections they
    // fall in, and each of those sections gets a new take. Returns how many takes were added.
    int importAbsolute (juce::ValueTree part, const Notes& absolute, const juce::String& source);

    // Every audible note of one part, in song beats.
    Notes flattenPart (const juce::ValueTree& part) const;

private:
    template <typename T> void set (juce::ValueTree t, const juce::Identifier& id, const T& v) { t.setProperty (id, v, undo); }
};

//==============================================================================
// What the audio thread plays: every part flattened to song beats. Built on the message
// thread and handed over whole.
struct PlaybackModel
{
    struct Part
    {
        int channel = 1; // 1-16
        bool audible = true;
        Notes notes;     // song beats, sorted by start
    };

    std::vector<Part> parts;
    std::vector<double> sectionStarts; // song beats; one more entry than sections (the end)
    double lengthBeats = 0.0;
    double bpm = 120.0;

    static PlaybackModel build (const Song& song);
};

} // namespace dm
