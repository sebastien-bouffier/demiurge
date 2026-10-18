#pragma once

// Everything Demiurge says to Claude and reads back: the request bodies for the Messages API
// and the parsing of its answers. No networking here (see plugin/AiClient), so it can be
// tested without a key.

#include "Song.h"

namespace dm::composer
{
struct ModelSettings
{
    juce::String model = "claude-opus-5-5";
    juce::String effort = "medium"; // low, medium, high
    int maxTokens = 32000;
};

// Models offered in the settings, best first: { id, label }.
std::vector<std::pair<juce::String, juce::String>> models();

//==============================================================================
// 1. Song plan: title, tempo, key, sections with chords, and the parts to write.
juce::var planRequest (const Song& song, const juce::String& idea, const ModelSettings& settings);
// Applies a plan to the song (through its UndoManager). Sections and parts whose names match
// the old ones keep their takes. Returns an error message, or empty on success.
juce::String applyPlan (Song& song, const juce::var& plan, juce::String& summary);

//==============================================================================
// 2. Notes for some cells.
struct Target
{
    struct Cell { juce::String part, section; };
    std::vector<Cell> cells;
    juce::String instruction;  // what the user asked for, may be empty
    bool vary = false;         // rework the current take instead of starting over
};

juce::var composeRequest (const Song& song, const Target& target, const ModelSettings& settings);

struct CellNotes
{
    juce::String part, section;
    Notes notes;
};

struct ComposeResult
{
    std::vector<CellNotes> cells;
    juce::String comment;
};

// Reads the "cells" answer. Notes outside their section are dropped. Returns an error message,
// or empty on success.
juce::String parseCompose (const Song& song, const Target& target, const juce::var& answer, ComposeResult& result);

//==============================================================================
// The JSON the model wrote, out of a Messages API response body. Returns an error message
// (API error, refusal, truncated answer) or empty on success.
juce::String extractAnswer (const juce::String& responseBody, int httpStatus, juce::var& answer);

// The song as the model sees it (also used in tests and for debugging).
juce::String describeSong (const Song& song);

} // namespace dm::composer
