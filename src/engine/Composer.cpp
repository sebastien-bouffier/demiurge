#include "Composer.h"

#include <set>

namespace dm::composer
{
namespace
{
struct Obj
{
    juce::DynamicObject::Ptr o { new juce::DynamicObject() };
    Obj& set (const juce::Identifier& k, const juce::var& v) { o->setProperty (k, v); return *this; }
    operator juce::var() const { return juce::var (o.get()); }
};

juce::var arr (std::initializer_list<juce::var> items)
{
    juce::Array<juce::var> a;
    for (auto& i : items)
        a.add (i);
    return a;
}

juce::var strings (const juce::StringArray& s)
{
    juce::Array<juce::var> a;
    for (auto& x : s)
        a.add (x);
    return a;
}

juce::var str() { return Obj().set ("type", "string"); }
juce::var integer() { return Obj().set ("type", "integer"); }
juce::var number() { return Obj().set ("type", "number"); }
juce::var enumOf (const juce::StringArray& values) { return Obj().set ("type", "string").set ("enum", strings (values)); }

juce::var object (std::initializer_list<std::pair<const char*, juce::var>> props)
{
    Obj properties;
    juce::StringArray required;
    for (auto& p : props)
    {
        properties.set (p.first, p.second);
        required.add (p.first);
    }
    return Obj().set ("type", "object").set ("properties", properties).set ("required", strings (required))
                .set ("additionalProperties", false);
}

juce::var arrayOf (const juce::var& items) { return Obj().set ("type", "array").set ("items", items); }

const char* kSystem = R"(You are Demiurge, a composer, arranger and session musician living inside a DAW plugin.
You write MIDI parts for every kind of instrument: drums and percussion, bass, keys, pads, orchestral sections
(strings, brass, woodwinds, choir), synths, guitars, arpeggios, and melody lines for a singer.

You always answer with the JSON the request asks for, nothing else.

NOTE FORMAT
Each part of each section is a list of notes written as "start length pitch velocity", separated by "; ".
- start and length are in beats (quarter notes) from the start of the section: 0 is the downbeat of its first bar,
  0.5 an eighth note later, 0.25 a sixteenth, 0.3333 a triplet eighth. A 4/4 bar is 4 beats, a 6/8 bar is 3 beats.
- Every start must be inside the section (0 <= start < section length). Notes may ring to the section end, not past it.
- pitch is the MIDI note number (60 = middle C = C4, 69 = A4). velocity is 1-127.
Example: "0 1 36 110; 1 1 38 100; 2 0.5 36 105; 2.5 0.5 36 90; 3 1 38 104"

WRITING GOOD PARTS
- Follow the chords given for each bar, the key, the tempo and the style. Make every part fit the other parts you are
  shown: lock the bass with the kick, leave space for the melody, avoid every part playing the same rhythm.
- Sound like a skilled human player of that instrument: idiomatic ranges and voicings, dynamics through velocity,
  slight variation between repeats, fills and transitions at section ends, a clear role for each part.
- Drums and percussion use the General MIDI drum map (on channel 10): 36 kick, 35 acoustic kick, 38 snare, 40 electric
  snare, 37 side stick, 39 clap, 42 closed hi-hat, 44 pedal hi-hat, 46 open hi-hat, 41 43 45 47 48 50 toms low to high,
  49 and 57 crash, 51 and 59 ride, 53 ride bell, 54 tambourine, 56 cowbell, 69 cabasa, 70 maracas, 75 claves,
  60 61 bongos, 62 63 64 congas. Drum note lengths are short (0.1 to 0.25).
- Bass mostly between E1 (28) and G3 (55). Keys and pads voice-lead smoothly. Guitars use playable voicings; strums are
  chord notes a few hundredths of a beat apart.
- Orchestral sections are written as the section plays (for example one "strings" part holding the whole string
  voicing), with long notes, swells through velocity and correct registers.
- A "melody" part is a line for a singer: one note at a time, never overlapping, singable steps and leaps, a range of
  about an octave and a half, phrases with room to breathe, rhythm that could carry lyrics, a memorable hook in the
  chorus.
- Sections that share a name (Chorus, Chorus 2) should be recognisably the same, with small variations.)";

juce::String describeNotesShort (const Notes& n) { return n.empty() ? juce::String ("(empty)") : notesToString (n); }

void addCommon (Obj& body, const ModelSettings& s, const juce::var& schema, const juce::String& userText)
{
    body.set ("model", s.model)
        .set ("max_tokens", s.maxTokens)
        .set ("system", arr ({ Obj().set ("type", "text").set ("text", kSystem)
                                    .set ("cache_control", Obj().set ("type", "ephemeral")) }))
        .set ("messages", arr ({ Obj().set ("role", "user").set ("content", userText) }));

    Obj format;
    format.set ("type", "json_schema").set ("schema", schema);
    Obj config;
    config.set ("format", format);
    if (s.effort.isNotEmpty() && ! s.model.startsWith ("claude-haiku"))
        config.set ("effort", s.effort);
    body.set ("output_config", config);

    // Server-side fallback when a safety classifier declines (AiClient adds its beta header).
    // Haiku has no server-side fallback.
    if (! s.model.startsWith ("claude-haiku"))
        body.set ("fallbacks", "default");
}
} // namespace

std::vector<std::pair<juce::String, juce::String>> models()
{
    return { { "claude-opus-5-5", "Opus 5.5" }, { "claude-sonnet-5-5", "Sonnet 5.5" }, { "claude-haiku-5-5", "Haiku 5.5" } };
}

//==============================================================================
juce::String describeSong (const Song& song)
{
    const auto& t = song.tree;
    juce::String s;
    const int key = juce::jlimit (0, 11, (int) t[ids::key]);
    s << "SONG\n"
      << "title: " << t[ids::title].toString() << "\n"
      << "key: " << keyNames()[key] << " " << t[ids::scale].toString()
      << "   tempo: " << juce::String (song.bpm(), 1) << " BPM"
      << "   time signature: " << (int) t[ids::tsNum] << "/" << (int) t[ids::tsDen]
      << " (" << juce::String (song.beatsPerBar(), 3).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") << " beats per bar)\n";
    if (t[ids::style].toString().isNotEmpty())
        s << "style: " << t[ids::style].toString() << "\n";
    if (t[ids::idea].toString().isNotEmpty())
        s << "original idea: " << t[ids::idea].toString() << "\n";

    s << "\nARRANGEMENT (in playing order)\n";
    for (int i = 0; i < song.numSections(); ++i)
    {
        auto sec = song.section (i);
        s << (i + 1) << ". [" << sec[ids::id].toString() << "] " << sec[ids::name].toString() << ": "
          << (int) sec[ids::bars] << " bars = " << juce::String (song.sectionLengthBeats (sec), 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
          << " beats; chords by bar: " << (sec[ids::chords].toString().isNotEmpty() ? sec[ids::chords].toString() : "(free)") << "\n";
    }

    s << "\nPARTS\n";
    for (int p = 0; p < song.numParts(); ++p)
    {
        auto pt = song.part (p);
        const int program = (int) pt[ids::program];
        s << "- [" << pt[ids::id].toString() << "] " << pt[ids::name].toString() << ": role " << pt[ids::role].toString()
          << ", MIDI channel " << (int) pt[ids::channel];
        if (roleFromName (pt[ids::role].toString()) != Role::drums && roleFromName (pt[ids::role].toString()) != Role::percussion)
            s << ", sound " << gmProgramName (program);
        if (pt[ids::description].toString().isNotEmpty())
            s << ". " << pt[ids::description].toString();
        s << "\n";
    }
    return s;
}

//==============================================================================
juce::var planRequest (const Song& song, const juce::String& idea, const ModelSettings& settings)
{
    auto schema = object ({
        { "title", str() },
        { "bpm", number() },
        { "key", enumOf (keyNames()) },
        { "scale", enumOf (scaleNames()) },
        { "timeSignature", enumOf (timeSignatures()) },
        { "style", str() },
        { "sections", arrayOf (object ({ { "name", str() }, { "bars", integer() }, { "chords", str() } })) },
        { "parts", arrayOf (object ({ { "name", str() }, { "role", enumOf (roleNames()) },
                                      { "program", integer() }, { "description", str() } })) },
        { "comment", str() },
    });

    juce::String text;
    text << "Plan a song from this idea:\n" << idea.trim() << "\n\n"
         << "Return the song's title, tempo, key, scale and time signature, a one or two sentence style description "
            "(genre, feel, production references), the arrangement as a list of sections in playing order "
            "(name, number of bars, chords with bars separated by \" | \" and several chords in a bar separated by spaces, "
            "for example \"Am | F | C G | E\", with exactly one entry per bar), and the parts to write "
            "(a short name, a role, a General MIDI program 0-127 as a sound hint, and a description of what the part "
            "plays and how). Cover the instruments the idea needs and no more. "
            "Put a one sentence note for the musician in comment.\n\n"
         << "Current song (keep what still fits, the names of sections and parts that you keep are matched):\n"
         << describeSong (song);

    Obj body;
    addCommon (body, settings, schema, text);
    return body;
}

juce::String applyPlan (Song& song, const juce::var& plan, juce::String& summary)
{
    if (! plan.isObject() || ! plan["sections"].isArray() || ! plan["parts"].isArray())
        return "The plan is missing its sections or parts.";

    auto& t = song.tree;
    auto* um = song.undo;
    if (plan["title"].toString().isNotEmpty())
        t.setProperty (ids::title, plan["title"].toString(), um);
    if ((double) plan["bpm"] >= 20.0 && (double) plan["bpm"] <= 400.0)
        t.setProperty (ids::bpm, (double) plan["bpm"], um);
    if (const int k = keyNames().indexOf (plan["key"].toString()); k >= 0)
        t.setProperty (ids::key, k, um);
    if (scaleNames().contains (plan["scale"].toString()))
        t.setProperty (ids::scale, plan["scale"].toString(), um);
    if (const auto ts = plan["timeSignature"].toString(); timeSignatures().contains (ts))
    {
        t.setProperty (ids::tsNum, ts.upToFirstOccurrenceOf ("/", false, false).getIntValue(), um);
        t.setProperty (ids::tsDen, ts.fromFirstOccurrenceOf ("/", false, false).getIntValue(), um);
    }
    t.setProperty (ids::style, plan["style"].toString(), um);

    // Sections: reuse the ones with the same name (each old one once), in the new order.
    {
        auto secs = song.sections();
        std::set<juce::String> reused;
        juce::Array<juce::ValueTree> order;
        for (auto& s : *plan["sections"].getArray())
        {
            const auto name = s["name"].toString().trim();
            if (name.isEmpty())
                continue;
            juce::ValueTree match;
            for (int i = 0; i < secs.getNumChildren() && ! match.isValid(); ++i)
            {
                auto old = secs.getChild (i);
                if (old[ids::name].toString().equalsIgnoreCase (name) && reused.count (old[ids::id].toString()) == 0)
                    match = old;
            }
            if (! match.isValid())
                match = song.addSection (name, juce::jlimit (1, 256, (int) s["bars"]), s["chords"].toString());
            else
            {
                match.setProperty (ids::bars, juce::jlimit (1, 256, (int) s["bars"]), um);
                match.setProperty (ids::chords, s["chords"].toString(), um);
            }
            reused.insert (match[ids::id].toString());
            order.add (match);
        }
        if (order.isEmpty())
            return "The plan has no sections.";

        for (int i = secs.getNumChildren(); --i >= 0;)
            if (! order.contains (secs.getChild (i)))
                song.removeSection (i);
        for (int i = 0; i < order.size(); ++i)
            song.moveSection (secs.indexOf (order[i]), i);
    }

    // Parts: same idea, matched by name.
    {
        auto pts = song.parts();
        juce::Array<juce::ValueTree> order;
        for (auto& p : *plan["parts"].getArray())
        {
            const auto name = p["name"].toString().trim();
            if (name.isEmpty())
                continue;
            const auto role = roleFromName (p["role"].toString());
            juce::ValueTree match;
            for (int i = 0; i < pts.getNumChildren() && ! match.isValid(); ++i)
                if (pts.getChild (i)[ids::name].toString().equalsIgnoreCase (name) && ! order.contains (pts.getChild (i)))
                    match = pts.getChild (i);
            if (! match.isValid())
                match = song.addPart (name, role);
            else if (roleFromName (match[ids::role].toString()) != role)
            {
                match.setProperty (ids::role, roleName (role), um);
                if (role == Role::drums || role == Role::percussion)
                    match.setProperty (ids::channel, 10, um);
            }
            match.setProperty (ids::program, juce::jlimit (0, 127, (int) p["program"]), um);
            match.setProperty (ids::description, p["description"].toString(), um);
            order.add (match);
        }
        for (int i = pts.getNumChildren(); --i >= 0;)
            if (! order.contains (pts.getChild (i)))
                song.removePart (i);
        for (int i = 0; i < order.size(); ++i)
            song.movePart (pts.indexOf (order[i]), i);
    }

    summary = plan["comment"].toString();
    return {};
}

//==============================================================================
juce::var composeRequest (const Song& song, const Target& target, const ModelSettings& settings)
{
    auto schema = object ({
        { "cells", arrayOf (object ({ { "part", str() }, { "section", str() }, { "notes", str() } })) },
        { "comment", str() },
    });

    std::set<juce::String> targetParts, targetSections;
    for (auto& c : target.cells)
    {
        targetParts.insert (c.part);
        targetSections.insert (c.section);
    }

    juce::String text = describeSong (song);

    // What is already written: every part in the target sections, and the target parts in
    // every section (to keep motifs consistent across the song).
    juce::String existing;
    constexpr int kMaxContextChars = 150000;
    for (int i = 0; i < song.numSections(); ++i)
    {
        auto sec = song.section (i);
        const auto sid = sec[ids::id].toString();
        juce::String block;
        for (int p = 0; p < song.numParts(); ++p)
        {
            auto pt = song.part (p);
            const auto pid = pt[ids::id].toString();
            const bool isTarget = targetParts.count (pid) && targetSections.count (sid);
            if (isTarget && ! target.vary)
                continue; // being rewritten from scratch
            if (! targetSections.count (sid) && ! targetParts.count (pid))
                continue;
            const auto notes = song.activeNotes (pt, sid);
            if (notes.empty())
                continue;
            block << "  " << pt[ids::name].toString() << " [" << pid << "]" << (isTarget ? " (current version, to rework)" : "")
                  << ": " << describeNotesShort (notes) << "\n";
        }
        if (block.isNotEmpty() && existing.length() + block.length() < kMaxContextChars)
            existing << sec[ids::name].toString() << " [" << sid << "]\n" << block;
    }
    if (existing.isNotEmpty())
        text << "\nNOTES ALREADY WRITTEN (beats from each section's start)\n" << existing;

    text << "\nTASK\n";
    if (target.vary)
        text << "Rework the parts below as asked, keeping what works in their current version.\n";
    else
        text << "Write the parts below.\n";
    if (target.instruction.trim().isNotEmpty())
        text << "Instructions: " << target.instruction.trim() << "\n";
    text << "Return one entry in cells for each of these (use the ids exactly):\n";
    for (auto& c : target.cells)
    {
        auto pt = song.partById (c.part);
        auto sec = song.sectionById (c.section);
        text << "- part " << c.part << " (" << pt[ids::name].toString() << ") in section " << c.section << " ("
             << sec[ids::name].toString() << ", " << juce::String (song.sectionLengthBeats (sec), 2).trimCharactersAtEnd ("0").trimCharactersAtEnd (".")
             << " beats)\n";
    }
    text << "Put a one sentence note on what you wrote in comment.";

    Obj body;
    addCommon (body, settings, schema, text);
    return body;
}

juce::String parseCompose (const Song& song, const Target& target, const juce::var& answer, ComposeResult& result)
{
    if (! answer["cells"].isArray())
        return "The answer has no cells.";

    for (auto& c : *answer["cells"].getArray())
    {
        CellNotes cn;
        cn.part = c["part"].toString().trim();
        cn.section = c["section"].toString().trim();
        auto sec = song.sectionById (cn.section);
        if (! sec.isValid() || ! song.partById (cn.part).isValid())
            continue;

        bool wanted = false;
        for (auto& t : target.cells)
            wanted = wanted || (t.part == cn.part && t.section == cn.section);
        if (! wanted)
            continue;

        cn.notes = notesFromString (c["notes"].toString());
        const double len = song.sectionLengthBeats (sec);
        sanitise (cn.notes, len);
        for (auto& n : cn.notes)
            n.length = juce::jmin (n.length, juce::jmax (1.0 / 64.0, len - n.start));
        result.cells.push_back (std::move (cn));
    }
    result.comment = answer["comment"].toString();
    return result.cells.empty() ? "The answer had no usable notes." : juce::String();
}

//==============================================================================
juce::String extractAnswer (const juce::String& responseBody, int httpStatus, juce::var& answer)
{
    const auto json = juce::JSON::parse (responseBody);

    if (httpStatus != 200 || json["type"].toString() == "error")
    {
        auto msg = json["error"]["message"].toString();
        if (msg.isEmpty())
            msg = responseBody.substring (0, 300);
        if (httpStatus == 401)
            return "The API key was refused (" + msg + ").";
        if (httpStatus == 429 || httpStatus == 529)
            return "Claude is busy or rate limited, try again in a moment (" + msg + ").";
        return "Claude API error " + juce::String (httpStatus) + ": " + msg;
    }

    const auto stop = json["stop_reason"].toString();
    if (stop == "refusal")
        return "Claude declined this request.";

    juce::String text;
    if (auto* content = json["content"].getArray())
        for (auto& block : *content)
            if (block["type"].toString() == "text")
                text << block["text"].toString();

    if (text.isEmpty())
        return "Claude returned no answer" + (stop.isNotEmpty() ? " (" + stop + ")." : juce::String ("."));

    if (juce::JSON::parse (text, answer).failed() || ! answer.isObject())
        return stop == "max_tokens" ? "The answer was too long and got cut. Ask for fewer sections or parts at once."
                                    : "Claude's answer was not valid JSON.";
    return {};
}

} // namespace dm::composer
