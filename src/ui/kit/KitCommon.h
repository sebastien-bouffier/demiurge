#pragma once

// The Bidoo kit: what every Bidoo plugin shares around its sound, whatever its editor and
// processor classes are: the sound state (undo / redo, A/B, the preset reference, the user's
// default), the presets (factory, user folders, tags, favourites), the preset browser, the top
// bar and the CPU meter. Its source is fab/shared/kit; every other repo holds a byte-for-byte copy.
//
// A plugin using it defines, for every source file that includes the kit (compile definitions):
//   BIDOO_KIT_FOLDER  its name, the folder of its files: <app data>/Bidoo/<folder> ("Xpresso")
//   BIDOO_KIT_EXT     the extension of its preset files, without the dot ("xpresso")

#include <juce_audio_processors/juce_audio_processors.h>

#ifndef BIDOO_KIT_FOLDER
 #error "Define BIDOO_KIT_FOLDER (the plugin's folder name) for the Bidoo kit"
#endif
#ifndef BIDOO_KIT_EXT
 #error "Define BIDOO_KIT_EXT (the preset file extension, no dot) for the Bidoo kit"
#endif

namespace bidoo::kit
{
// <app data>/Bidoo/<folder>: Settings.xml, Default.<ext>, Favourites.xml and Presets/.
inline juce::File getUserDataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("Bidoo").getChildFile (BIDOO_KIT_FOLDER);
}

inline juce::File getSettingsFile() { return getUserDataDirectory().getChildFile ("Settings.xml"); }

// Settings.xml, shared by every instance of the plugin (theme, window size...).
struct Settings
{
    static juce::String get (const juce::String& key, const juce::String& fallback)
    {
        juce::PropertiesFile props (getSettingsFile(), {});
        return props.getValue (key, fallback);
    }

    static double getDouble (const juce::String& key, double fallback)
    {
        juce::PropertiesFile props (getSettingsFile(), {});
        return props.getDoubleValue (key, fallback);
    }

    static void set (const juce::String& key, const juce::var& value)
    {
        getSettingsFile().getParentDirectory().createDirectory();
        juce::PropertiesFile props (getSettingsFile(), {});
        props.setValue (key, value);
        props.saveIfNeeded();
    }
};

// Tags as written in a file or typed: "Bass, Dark ,bass" -> { "Bass", "Dark" } (trimmed, the first
// spelling of each kept, any case).
inline juce::StringArray parseTags (const juce::String& text)
{
    juce::StringArray tags;
    for (auto t : juce::StringArray::fromTokens (text, ",", ""))
    {
        t = t.trim();
        if (t.isNotEmpty() && ! tags.contains (t, true))
            tags.add (t);
    }
    return tags;
}

inline juce::String joinTags (const juce::StringArray& tags) { return tags.joinIntoString (", "); }
} // namespace bidoo::kit
