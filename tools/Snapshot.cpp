// Renders the editor to PNG files without a host, for checking the UI.
// Usage: ui_snapshot <output-folder>
//        ui_snapshot --api [key]   sends a song plan request and prints what comes back

#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

using namespace dm;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 1 && juce::String (argv[1]) == "--api")
    {
        auto tree = Song::createDefault();
        Song song (tree);
        composer::ModelSettings ms;
        ms.effort = "low";
        AiClient client;
        bool done = false;
        client.send (composer::planRequest (song, "A short punk song, 4 parts", ms),
                     argc > 2 ? juce::String (argv[2]) : juce::String ("invalid"),
                     [&] (int status, const juce::String& body)
                     {
                         juce::var answer;
                         const auto error = composer::extractAnswer (body, status, answer);
                         std::printf ("HTTP %d\n%s\n", status, (error.isNotEmpty() ? error : juce::JSON::toString (answer)).toRawUTF8());
                         done = true;
                         juce::MessageManager::getInstance()->stopDispatchLoop();
                     });
        juce::MessageManager::getInstance()->runDispatchLoop();
        return done ? 0 : 1;
    }
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    DemiurgeProcessor processor;
    auto s = processor.song();

    // A small song with a few takes so every view has something to show.
    s.tree.setProperty (ids::title, "Neon Rain", nullptr);
    s.tree.setProperty (ids::idea, "Dark synthwave at 100 BPM, LinnDrum beat, pulsing bass, a breathy vocal hook in the chorus.", nullptr);
    s.tree.setProperty (ids::style, "80s synthwave, gated reverb drums, analog bass", nullptr);
    s.tree.setProperty (ids::bpm, 100.0, nullptr);
    s.addSection ("Intro", 4, "Am | F | C | G", 0);
    s.addSection ("Chorus 2", 8, "F | G | Am | Am | F | G | E | E");
    s.addPart ("Strings", Role::strings);
    s.addPart ("Guitar", Role::guitar);

    juce::Random r (7);
    for (int i = 0; i < s.numSections(); ++i)
    {
        auto sec = s.section (i);
        const auto sid = sec[ids::id].toString();
        const double len = s.sectionLengthBeats (sec);
        Notes drums, bass, chords, melody;
        for (double b = 0; b < len; b += 0.5)
        {
            drums.push_back ({ b, 0.2, 42, 70 + r.nextInt (30) });
            if (std::fmod (b, 2.0) == 0.0) drums.push_back ({ b, 0.2, 36, 110 });
            if (std::fmod (b, 2.0) == 1.0) drums.push_back ({ b, 0.2, 38, 105 });
            bass.push_back ({ b, 0.45, 33 + (int) (b / 4) % 4 * 2, 90 + r.nextInt (20) });
        }
        for (double b = 0; b < len; b += 4)
            for (int k : { 57, 60, 64 })
                chords.push_back ({ b, 3.9, k + (int) (b / 4) % 3, 80 });
        for (double b = 0; b < len; b += 1.5)
            melody.push_back ({ b, 1.0, 64 + r.nextInt (8), 95 });
        s.addTake (s.part (0), sid, drums, {}, "claude-opus-5-5");
        s.addTake (s.part (1), sid, bass, "pumping eighths", "claude-opus-5-5");
        if (i > 0) s.addTake (s.part (1), sid, bass, "busier", "claude-opus-5-5");
        s.addTake (s.part (2), sid, chords, {}, "claude-opus-5-5");
        if (i != 0) s.addTake (s.part (3), sid, melody, {}, "claude-opus-5-5");
    }
    processor.setSelection (s.part (1)[ids::id].toString(), s.section (2)[ids::id].toString());
    processor.setStatus ("Writing Bass for the whole song: 412 notes. Locked to the kick, octave jumps into the chorus.");
    processor.livePosition = 50.0;

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    auto* ed = dynamic_cast<DemiurgeEditor*> (editor.get());
    editor->setSize (DemiurgeEditor::kWidth, DemiurgeEditor::kHeight);

    auto snap = [&] (const juce::String& name)
    {
        ed->refreshForTesting();
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream stream (out.getChildFile (name + ".png"));
        stream.setPosition (0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };

    ed->applyTheme (true);
    snap ("light");
    ed->applyTheme (false);
    snap ("dark");
    return 0;
}
