#pragma once

#include "PluginProcessor.h"
#include "UserData.h"

#include <algorithm>
#include <vector>

// Presets are whole songs: the parameters plus the SONG tree. Factory presets are a list of
// "parameter id -> value" applied after resetting every parameter to its default, with an
// empty song. User presets are the plugin state in <user data>/Presets/<name>.dmsong.
namespace presets_detail
{
struct FactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> list { { "Init", {} } };
    return list;
}
} // namespace presets_detail

class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    explicit PresetManager (DemiurgeProcessor& p) : processor (p), apvts (p.apvts) { refresh(); }

    static juce::File getUserDirectory() { return getUserDataDirectory().getChildFile ("Presets"); }

    void refresh()
    {
        entries.clear();
        const auto& factory = presets_detail::factoryPresets();
        for (size_t i = 0; i < factory.size(); ++i)
            entries.push_back ({ factory[i].name, true, (int) i, {} });

        auto files = getUserDirectory().findChildFiles (juce::File::findFiles, false, "*.dmsong");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });
        for (const auto& f : files)
            entries.push_back ({ f.getFileNameWithoutExtension(), false, -1, f });
    }

    const std::vector<Entry>& getEntries() const { return entries; }
    juce::String getCurrentName() const { return apvts.state.getProperty ("presetName").toString(); }

    int indexOfName (const juce::String& name) const
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == name)
                return (int) i;
        return -1;
    }

    bool isFactoryName (const juce::String& name) const
    {
        for (const auto& p : presets_detail::factoryPresets())
            if (p.name.equalsIgnoreCase (name))
                return true;
        return false;
    }

    void load (int index)
    {
        if (index < 0 || index >= (int) entries.size())
            return;
        const auto& e = entries[(size_t) index];

        if (e.factory)
        {
            resetToDefaults();
            for (const auto& pv : presets_detail::factoryPresets()[(size_t) e.factoryIndex].values)
                setParam (pv.first, pv.second);
            processor.resetSong();
        }
        else
        {
            auto xml = juce::XmlDocument::parse (e.file);
            if (xml == nullptr)
                return;
            resetToDefaults();
            for (auto* child : xml->getChildIterator())
                if (child->hasTagName ("PARAM") && child->hasAttribute ("id"))
                    setParam (child->getStringAttribute ("id"), (float) child->getDoubleAttribute ("value"));
            if (auto* songXml = xml->getChildByName (dm::ids::SONG.toString()))
                processor.loadSong (juce::ValueTree::fromXml (*songXml));
        }
        apvts.state.setProperty ("presetName", e.name, nullptr);
    }

    bool saveUser (const juce::String& rawName)
    {
        auto name = rawName.trim();
        if (name.isEmpty())
            return false;
        if (isFactoryName (name))
            name += " (user)";

        auto dir = getUserDirectory();
        if (! dir.createDirectory().wasOk())
            return false;

        apvts.state.setProperty ("presetName", name, nullptr);
        auto state = apvts.copyState();
        state.appendChild (processor.songTree.createCopy(), nullptr);
        auto xml = state.createXml();
        const auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".dmsong");
        if (xml == nullptr || ! xml->writeTo (file))
            return false;
        refresh();
        return true;
    }

    bool deleteUser (int index)
    {
        if (index < 0 || index >= (int) entries.size() || entries[(size_t) index].factory)
            return false;
        const bool ok = entries[(size_t) index].file.deleteFile();
        refresh();
        return ok;
    }

private:
    void resetToDefaults()
    {
        for (auto* p : apvts.processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
    }

    void setParam (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }

    DemiurgeProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
};
