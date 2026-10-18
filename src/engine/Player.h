#pragma once

// Plays a PlaybackModel in sync with the host transport. Runs on the audio thread: no
// allocation, no locks. Notes end on time even when the model is swapped under them.

#include "Song.h"

#include <array>
#include <cstdint>
#include <vector>

namespace dm
{
struct MidiOut
{
    enum class Type : std::uint8_t { noteOn, noteOff };
    Type type = Type::noteOn;
    int sample = 0;   // offset in the block
    int channel = 1;  // 1-16
    int pitch = 60;
    int velocity = 100;
};

struct BlockInfo
{
    bool playing = false;
    double ppqStart = 0.0;     // host position (quarter notes) at the first sample
    int numSamples = 0;
    double samplesPerBeat = 22050.0;

    // Loop one range of the song (a section) whatever the host position is.
    bool loop = false;
    double loopStart = 0.0, loopEnd = 0.0;

    // One part only (-1 = every part), optionally forced onto one channel (0 = the part's own).
    int onlyPart = -1;
    int forceChannel = 0;
};

class Player
{
public:
    Player() { out.reserve (2048); }

    // Fills `events` (cleared first) with what to send during this block.
    void process (const PlaybackModel* model, const BlockInfo& block, std::vector<MidiOut>& events);

    // Sends note-offs for everything that is sounding, at sample 0 (stop, seek, bypass).
    void allNotesOff (std::vector<MidiOut>& events, int sample = 0);

    // Song position (beats) at the start of the last block, -1 when stopped. For the UI.
    double songPosition() const { return lastSongBeat; }

    // Song beat for a host position, the way process() maps it.
    static double songBeatFor (double ppq, const BlockInfo& block);

private:
    struct Active
    {
        int channel = 0, pitch = 0;
        double endPpq = 0.0; // host position of the note-off
    };

    void startNote (int channel, int pitch, int velocity, double onPpq, double lengthBeats, int sample,
                    std::vector<MidiOut>& events);
    // Note-offs for notes ending before `ppq` (or at it, when inclusive).
    void endNotesBefore (double ppq, const BlockInfo& block, std::vector<MidiOut>& events, bool inclusive = false);
    void playRange (const PlaybackModel& model, const BlockInfo& block, double songFrom, double songTo,
                    double hostFrom, std::vector<MidiOut>& events);
    static int sampleFor (double ppq, const BlockInfo& block);

    static constexpr int kMaxActive = 512;
    std::array<Active, kMaxActive> active {};
    int numActive = 0;

    bool wasPlaying = false;
    double expectedPpq = -1.0;
    double lastSongBeat = -1.0;
    std::vector<MidiOut> out;
};

} // namespace dm
