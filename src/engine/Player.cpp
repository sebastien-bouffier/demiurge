#include "Player.h"

#include <algorithm>
#include <cmath>

namespace dm
{
int Player::sampleFor (double ppq, const BlockInfo& block)
{
    const int s = (int) std::floor ((ppq - block.ppqStart) * block.samplesPerBeat + 1.0e-6);
    return juce::jlimit (0, juce::jmax (0, block.numSamples - 1), s);
}

double Player::songBeatFor (double ppq, const BlockInfo& block)
{
    if (! block.loop || block.loopEnd <= block.loopStart)
        return ppq;
    const double len = block.loopEnd - block.loopStart;
    double offset = std::fmod (ppq, len);
    if (offset < 0.0)
        offset += len;
    return block.loopStart + offset;
}

void Player::allNotesOff (std::vector<MidiOut>& events, int sample)
{
    for (int i = 0; i < numActive; ++i)
        events.push_back ({ MidiOut::Type::noteOff, sample, active[(size_t) i].channel, active[(size_t) i].pitch, 0 });
    numActive = 0;
}

void Player::endNotesBefore (double ppq, const BlockInfo& block, std::vector<MidiOut>& events, bool inclusive)
{
    for (int i = 0; i < numActive;)
    {
        auto& a = active[(size_t) i];
        if (inclusive ? a.endPpq <= ppq + 1.0e-9 : a.endPpq < ppq - 1.0e-9)
        {
            events.push_back ({ MidiOut::Type::noteOff, sampleFor (a.endPpq, block), a.channel, a.pitch, 0 });
            a = active[(size_t) --numActive];
        }
        else
            ++i;
    }
}

void Player::startNote (int channel, int pitch, int velocity, double onPpq, double lengthBeats, int sample,
                        std::vector<MidiOut>& events)
{
    // The same key already down: release it first so the new note is heard.
    for (int i = 0; i < numActive; ++i)
        if (active[(size_t) i].channel == channel && active[(size_t) i].pitch == pitch)
        {
            events.push_back ({ MidiOut::Type::noteOff, sample, channel, pitch, 0 });
            active[(size_t) i] = active[(size_t) --numActive];
            break;
        }

    if (numActive >= kMaxActive)
        return;

    events.push_back ({ MidiOut::Type::noteOn, sample, channel, pitch, velocity });
    active[(size_t) numActive++] = { channel, pitch, onPpq + lengthBeats };
}

void Player::playRange (const PlaybackModel& model, const BlockInfo& block, double songFrom, double songTo,
                        double hostFrom, std::vector<MidiOut>& events)
{
    for (size_t p = 0; p < model.parts.size(); ++p)
    {
        const auto& part = model.parts[p];
        if (block.onlyPart >= 0 && (int) p != block.onlyPart)
            continue;
        if (block.onlyPart < 0 && ! part.audible)
            continue;

        const int channel = block.forceChannel > 0 ? block.forceChannel : part.channel;
        auto it = std::lower_bound (part.notes.begin(), part.notes.end(), songFrom - 1.0e-9,
                                    [] (const Note& n, double b) { return n.start < b; });
        for (; it != part.notes.end() && it->start < songTo - 1.0e-9; ++it)
        {
            const double onPpq = hostFrom + (it->start - songFrom);
            double length = it->length;
            if (block.loop) // a looped section never rings past its end
                length = juce::jmin (length, block.loopEnd - it->start);
            startNote (channel, it->pitch, it->velocity, onPpq, juce::jmax (1.0 / 128.0, length),
                       sampleFor (onPpq, block), events);
        }
    }
}

void Player::process (const PlaybackModel* model, const BlockInfo& block, std::vector<MidiOut>& events)
{
    events.clear();
    const double ppqEnd = block.ppqStart + block.numSamples / juce::jmax (1.0, block.samplesPerBeat);

    if (! block.playing)
    {
        if (wasPlaying)
            allNotesOff (events);
        wasPlaying = false;
        expectedPpq = -1.0;
        lastSongBeat = -1.0;
        return;
    }

    // A jump (seek, loop in the host, a new start): stop what is sounding.
    if (wasPlaying && std::abs (block.ppqStart - expectedPpq) > 1.0e-3)
        allNotesOff (events);
    wasPlaying = true;
    expectedPpq = ppqEnd;
    lastSongBeat = songBeatFor (block.ppqStart, block);

    if (model == nullptr || ppqEnd <= 0.0)
    {
        endNotesBefore (ppqEnd, block, events);
        return;
    }

    if (! block.loop || block.loopEnd <= block.loopStart)
    {
        endNotesBefore (ppqEnd, block, events);
        playRange (*model, block, juce::jmax (0.0, block.ppqStart), ppqEnd, juce::jmax (0.0, block.ppqStart), events);
        endNotesBefore (ppqEnd, block, events); // very short notes that start and end in this block
        return;
    }

    // Looping: split the block where the loop wraps.
    double host = block.ppqStart;
    int guard = 0;
    while (host < ppqEnd - 1.0e-12 && guard++ < 64)
    {
        const double song = songBeatFor (host, block);
        const double wrapPpq = host + (block.loopEnd - song);
        const double segEnd = juce::jmin (ppqEnd, wrapPpq);

        endNotesBefore (segEnd, block, events);
        playRange (*model, block, song, song + (segEnd - host), host, events);
        endNotesBefore (segEnd, block, events);

        if (wrapPpq <= ppqEnd + 1.0e-12)
        {
            // At the wrap, anything still sounding stops.
            for (int i = 0; i < numActive; ++i)
                active[(size_t) i].endPpq = juce::jmin (active[(size_t) i].endPpq, wrapPpq);
            endNotesBefore (wrapPpq, block, events, true);
        }
        host = segEnd;
    }
}

} // namespace dm
