// Engine tests: song model, player timing, Claude requests and answers, Reaper exchange.
// Run: engine_tests (returns non-zero on failure).

#include "engine/Composer.h"
#include "engine/Player.h"
#include "engine/Song.h"
#include "plugin/Exchange.h"

#include <cstdio>

using namespace dm;

static int failures = 0, checks = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        ++checks;                                                                \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static bool near (double a, double b, double eps = 1.0e-6) { return std::abs (a - b) < eps; }

static void testNotes()
{
    Notes n { { 0.0, 1.0, 36, 110 }, { 1.5, 0.25, 42, 80 } };
    const auto s = notesToString (n);
    CHECK (s == "0 1 36 110; 1.5 0.25 42 80");
    CHECK (notesFromString (s) == n);

    auto messy = notesFromString ("0 1 36 110,\n 2 0.5 38; junk; 3 0.3333 200 90; -1 1 40 90; 5 1 40 90");
    CHECK (messy.size() == 5);
    CHECK (messy[1].velocity == 100);
    sanitise (messy, 4.0);
    CHECK (messy.size() == 2); // pitch 200, start -1 and start 5 dropped
    CHECK (messy[0].pitch == 36 && messy[1].pitch == 38);
}

static void testSong()
{
    juce::UndoManager um;
    auto tree = Song::createDefault();
    Song s (tree, &um);
    CHECK (s.numSections() == 2 && s.numParts() == 4);
    CHECK (near (s.beatsPerBar(), 4.0));
    CHECK (near (s.lengthBeats(), 64.0));
    CHECK (s.sectionAt (33.0) == 1);
    CHECK ((int) s.part (0)[ids::channel] == 10); // drums
    CHECK ((int) s.part (1)[ids::channel] == 1);

    auto bass = s.part (1);
    const auto verse = s.section (0)[ids::id].toString();
    um.beginNewTransaction();
    s.addTake (bass, verse, { { 0, 1, 40, 100 } }, "first", "test");
    um.beginNewTransaction();
    s.addTake (bass, verse, { { 0, 2, 45, 100 } }, "second", "test");
    CHECK (s.numTakes (bass, verse) == 2);
    CHECK (s.activeNotes (bass, verse)[0].pitch == 45);
    um.beginNewTransaction();
    s.setActiveTake (bass, verse, 0);
    CHECK (s.activeNotes (bass, verse)[0].pitch == 40);
    um.undo();
    CHECK (s.activeNotes (bass, verse)[0].pitch == 45);
    um.undo();
    CHECK (s.numTakes (bass, verse) == 1);
    um.redo();
    um.redo();
    CHECK (s.numTakes (bass, verse) == 2 && s.activeTakeIndex (bass, verse) == 0);
    um.undo();
    um.undo();
    um.redo();
    CHECK (s.numTakes (bass, verse) == 2);

    // A capture across both sections lands in two takes.
    Notes abs { { 2.0, 1.0, 50, 90 }, { 34.0, 1.0, 52, 90 } };
    CHECK (s.importAbsolute (bass, abs, "capture") == 2);
    const auto chorus = s.section (1)[ids::id].toString();
    CHECK (near (s.activeNotes (bass, chorus)[0].start, 2.0));

    auto flat = s.flattenPart (bass);
    CHECK (flat.size() == 2 && near (flat[1].start, 34.0));

    // Duplicating a section copies its takes.
    s.duplicateSection (1);
    CHECK (s.numSections() == 3);
    CHECK (s.activeNotes (bass, s.section (2)[ids::id].toString()).size() == 1);
    s.removeSection (2);
    CHECK (s.numSections() == 2);
}

static void testPlayer()
{
    auto tree = Song::createDefault();
    Song s (tree);
    auto bass = s.part (1);
    s.addTake (bass, s.section (0)[ids::id].toString(), { { 0.0, 1.0, 40, 100 }, { 1.0, 0.5, 43, 90 } }, {}, "test");
    auto model = PlaybackModel::build (s);

    Player player;
    std::vector<MidiOut> ev;
    BlockInfo b;
    b.playing = true;
    b.samplesPerBeat = 1000.0;
    b.numSamples = 500;

    b.ppqStart = 0.0; // [0, 0.5)
    player.process (&model, b, ev);
    CHECK (ev.size() == 1 && ev[0].type == MidiOut::Type::noteOn && ev[0].sample == 0 && ev[0].pitch == 40 && ev[0].channel == 1);

    b.ppqStart = 0.5; // [0.5, 1.0): nothing
    player.process (&model, b, ev);
    CHECK (ev.empty());

    b.ppqStart = 1.0; // [1, 1.5): off 40 at 0, on 43 at 0
    player.process (&model, b, ev);
    CHECK (ev.size() == 2);
    CHECK (ev[0].type == MidiOut::Type::noteOff && ev[0].pitch == 40);
    CHECK (ev[1].type == MidiOut::Type::noteOn && ev[1].pitch == 43);

    b.ppqStart = 1.5; // [1.5, 2): 43 ends exactly at the start of this block
    player.process (&model, b, ev);
    CHECK (ev.size() == 1 && ev[0].type == MidiOut::Type::noteOff && ev[0].sample == 0);

    // Sample accuracy inside a block (pre-roll before the song start)
    Player p2;
    b.ppqStart = -0.25;
    p2.process (&model, b, ev);
    CHECK (ev.size() == 1 && ev[0].sample == 250);

    // Stop releases held notes
    b.playing = false;
    p2.process (&model, b, ev);
    CHECK (ev.size() == 1 && ev[0].type == MidiOut::Type::noteOff);

    // A seek releases held notes
    Player p3;
    b.playing = true;
    b.ppqStart = 0.0;
    p3.process (&model, b, ev);
    b.ppqStart = 10.0;
    p3.process (&model, b, ev);
    CHECK (! ev.empty() && ev[0].type == MidiOut::Type::noteOff && ev[0].pitch == 40);

    // Loop the verse (32 beats): host position 32 plays the verse's first note again.
    Player p4;
    b.loop = true;
    b.loopStart = 0.0;
    b.loopEnd = 32.0;
    b.ppqStart = 31.75; // [31.75, 32.25): wraps at sample 250
    p4.process (&model, b, ev);
    CHECK (ev.size() == 1 && ev[0].type == MidiOut::Type::noteOn && ev[0].pitch == 40 && ev[0].sample == 250);
    CHECK (near (Player::songBeatFor (40.0, b), 8.0));

    // One part only, forced channel
    Player p5;
    b.loop = false;
    b.ppqStart = 0.0;
    b.onlyPart = 1;
    b.forceChannel = 5;
    p5.process (&model, b, ev);
    CHECK (ev.size() == 1 && ev[0].channel == 5);
    b.onlyPart = 0; // drums: empty
    Player p6;
    p6.process (&model, b, ev);
    CHECK (ev.empty());

    // Mute
    bass.setProperty (ids::mute, true, nullptr);
    auto muted = PlaybackModel::build (s);
    Player p7;
    b.onlyPart = -1;
    p7.process (&muted, b, ev);
    CHECK (ev.empty());
}

static void testComposer()
{
    juce::UndoManager um;
    auto tree = Song::createDefault();
    Song s (tree, &um);

    composer::ModelSettings ms;
    auto plan = composer::planRequest (s, "Dark synthwave, 100 BPM", ms);
    CHECK (plan["model"].toString() == "claude-opus-5-5");
    CHECK (plan["output_config"]["format"]["type"].toString() == "json_schema");
    CHECK (plan["output_config"]["effort"].toString() == "medium");
    CHECK (plan["fallbacks"].toString() == "default");
    CHECK (plan["messages"][0]["content"].toString().contains ("Dark synthwave"));

    // Apply a plan: Verse is kept (same name, its takes stay), Chorus goes, Intro is new.
    auto drums = s.part (0);
    const auto verseId = s.section (0)[ids::id].toString();
    s.addTake (drums, verseId, { { 0, 0.25, 36, 100 } }, {}, "test");

    const auto planJson = juce::JSON::parse (R"({
      "title": "Neon Rain", "bpm": 100, "key": "F#", "scale": "minor", "timeSignature": "4/4",
      "style": "Dark synthwave", "comment": "ok",
      "sections": [ { "name": "Intro", "bars": 4, "chords": "F#m | D | A | E" },
                    { "name": "Verse", "bars": 16, "chords": "F#m | D/F# | A/E | E", "direction": "subtle inversions" } ],
      "parts": [ { "name": "Drums", "role": "drums", "program": 0, "description": "LinnDrum" },
                 { "name": "Synth Bass", "role": "bass", "program": 38, "description": "8th notes" },
                 { "name": "Vocal", "role": "melody", "program": 53, "description": "breathy" } ] })");
    juce::String summary;
    CHECK (composer::applyPlan (s, planJson, summary).isEmpty());
    CHECK (s.tree[ids::title].toString() == "Neon Rain");
    CHECK ((int) s.tree[ids::key] == 6);
    CHECK (s.numSections() == 2 && s.section (0)[ids::name].toString() == "Intro");
    CHECK (s.section (1)[ids::id].toString() == verseId && (int) s.section (1)[ids::bars] == 16);
    CHECK (s.numParts() == 3 && s.part (0) == drums);
    CHECK (s.numTakes (drums, verseId) == 1);
    CHECK ((int) s.part (1)[ids::channel] >= 1 && (int) s.part (2)[ids::program] == 53);
    um.undo();
    CHECK (s.tree[ids::title].toString() == "Untitled");
    um.redo();

    // Compose request and answer
    composer::Target t;
    const auto bassId = s.part (1)[ids::id].toString();
    t.cells.push_back ({ bassId, verseId });
    t.instruction = "pumping eighths";
    auto req = composer::composeRequest (s, t, ms);
    const auto text = req["messages"][0]["content"].toString();
    CHECK (text.contains ("pumping eighths") && text.contains (bassId) && text.contains ("64 beats"));
    CHECK (text.contains ("0 0.25 36 100")); // the drums already written in the verse are context
    CHECK (text.contains ("direction for every part in this section: subtle inversions"));
    CHECK (text.contains ("D/F#"));
    CHECK (req["system"][0]["text"].toString().contains ("subtle inversions"));

    // Writing a whole section: each part's own prompt goes with its cell.
    s.part (2).setProperty (ids::prompt, "breathy, low register", nullptr);
    composer::Target whole;
    for (int p = 0; p < s.numParts(); ++p)
        whole.cells.push_back ({ s.part (p)[ids::id].toString(), verseId });
    const auto wholeText = composer::composeRequest (s, whole, ms)["messages"][0]["content"].toString();
    CHECK (wholeText.contains ("(Vocal) in section " + verseId) && wholeText.contains ("instructions for this part: breathy, low register"));
    CHECK (! wholeText.contains ("Instructions:"));

    const auto answer = juce::JSON::parse (R"({ "comment": "done", "cells": [
        { "part": ")" + bassId + R"(", "section": ")" + verseId + R"(", "notes": "0 0.5 42 100; 0.5 0.5 42 90; 63.5 2 42 80; 70 1 42 80" },
        { "part": "nope", "section": "nope", "notes": "0 1 1 1" } ] })");
    composer::ComposeResult result;
    CHECK (composer::parseCompose (s, t, answer, result).isEmpty());
    CHECK (result.cells.size() == 1 && result.cells[0].notes.size() == 3);
    CHECK (near (result.cells[0].notes[2].length, 0.5)); // clipped to the section end

    // Messages API envelopes
    juce::var out;
    const auto ok = R"({"type":"message","stop_reason":"end_turn","content":[{"type":"thinking","thinking":""},{"type":"text","text":"{\"cells\":[],\"comment\":\"x\"}"}]})";
    CHECK (composer::extractAnswer (ok, 200, out).isEmpty() && out["comment"].toString() == "x");
    CHECK (composer::extractAnswer (R"({"type":"error","error":{"type":"authentication_error","message":"invalid x-api-key"}})", 401, out).contains ("refused"));
    CHECK (composer::extractAnswer (R"({"type":"message","stop_reason":"refusal","content":[]})", 200, out).contains ("declined"));
    CHECK (composer::extractAnswer (R"({"type":"message","stop_reason":"max_tokens","content":[{"type":"text","text":"{\"cells\":[{\"no"}]})", 200, out).contains ("too long"));

    // Haiku: no effort, no fallbacks
    ms.model = "claude-haiku-5-5";
    auto h = composer::composeRequest (s, t, ms);
    CHECK (! h.getDynamicObject()->hasProperty ("fallbacks"));
    CHECK (! h["output_config"].getDynamicObject()->hasProperty ("effort"));
}

static void testExchange()
{
    auto tree = Song::createDefault();
    Song s (tree);
    auto bass = s.part (1);
    s.addTake (bass, s.section (1)[ids::id].toString(), { { 1.0, 0.5, 40, 100 } }, {}, "test");

    double from = 0, to = 0;
    auto notes = exchange::absoluteNotes (s, { bass[ids::id].toString(), s.section (1)[ids::id].toString() }, from, to);
    CHECK (near (from, 32.0) && near (to, 64.0));
    CHECK (notes.size() == 1 && near (notes[0].note.start, 33.0) && notes[0].channel == 1);

    juce::String name;
    auto parsed = exchange::parseExchange ("demiurge 1\nname Bass item\nnote 33.5 0.5 41 99 1\nnote 32 1 40 100 1\n", name);
    CHECK (name == "Bass item" && parsed.size() == 2 && near (parsed[0].start, 32.0));
}

int main()
{
    testNotes();
    testSong();
    testPlayer();
    testComposer();
    testExchange();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
