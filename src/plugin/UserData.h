#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

// <app data>/Bidoo/Demiurge: Settings.xml, Presets/, Clips/ (dragged MIDI files) and
// Exchange/ (files shared with the Reaper scripts).
inline juce::File getUserDataDirectory()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("Bidoo").getChildFile ("Demiurge");
}

// Settings shared by every instance (Settings.xml).
namespace settings
{
inline juce::File file() { return getUserDataDirectory().getChildFile ("Settings.xml"); }

inline juce::String get (const juce::String& key, const juce::String& fallback = {})
{
    juce::PropertiesFile props (file(), {});
    return props.getValue (key, fallback);
}

inline void set (const juce::String& key, const juce::var& value)
{
    file().getParentDirectory().createDirectory();
    juce::PropertiesFile props (file(), {});
    props.setValue (key, value);
    props.saveIfNeeded();
}

// The Anthropic API key: Settings.xml, else the ANTHROPIC_API_KEY environment variable.
inline juce::String apiKey()
{
    auto k = get ("apiKey").trim();
    if (k.isEmpty())
        k = juce::SystemStats::getEnvironmentVariable ("ANTHROPIC_API_KEY", {}).trim();
    return k;
}
} // namespace settings
