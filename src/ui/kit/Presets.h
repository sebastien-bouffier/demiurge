#pragma once

// The preset list: the factory presets (from the processor), then the user presets, files holding
// the parameter state under <user data>/Presets/, in subfolders if the user made some.
//
// The order of the list is the MIDI program order, counted from 0 (bank * 128 + program, see
// programLabel), the order of the preset menu and of the previous / next arrows:
//  - the factory presets in the plugin's order ("Init" first), whatever their categories (the
//    browser's folders under Factory), so categories never move a program number;
//  - the user presets at the top of Presets/, by name, then each subfolder by name, its own
//    presets by name before its subfolders (depth first).
// A preset's number changes only when presets are added, renamed, moved or deleted before it.
//
// Tags: a factory preset's come from the plugin (FactoryPreset::tags), a user preset's are the
// "presetTags" attribute of its file ("Bass, Dark"), given when it is saved and editable after.
// The browser filters by them, and the search looks in them.
//
// Also here: favourites (stars, shared by every instance, in <user data>/Favourites.xml), the
// user's default (Save as default: what new instances start with and what the first factory
// preset, "Init", loads; Restore built-in default forgets it) and copy / paste of the settings
// through the clipboard, between instances of the same plugin. Everything a preset load does is
// one undo step, and leaves alone what the plugin keeps out of the sound (bypass, view settings).

#include "SoundState.h"
#include <algorithm>
#include <set>

namespace bidoo::kit
{
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        bool factory = true;
        int factoryIndex = -1;
        juce::File file;
        // Factory: the category (empty: none). User: the subfolder of Presets/, "a/b" (empty: the top).
        juce::String folder;
        juce::StringArray tags;
    };

    // How deep user subfolders are read (deeper ones are left out of the list).
    static constexpr int kMaxFolderDepth = 8;

    explicit PresetManager (SoundState& s)
        : sound (s), apvts (s.getState()), factory (categorised (s.getClient()))
    {
        // the browser's category order: the plugin's list, then any other in the order it first appears
        for (const auto& c : s.getClient().getFactoryCategories())
            for (const auto& f : factory)
                if (f.category == c.first && ! categories.contains (c.first))
                    categories.add (c.first);
        for (const auto& f : factory)
            if (f.category.isNotEmpty() && ! categories.contains (f.category))
                categories.add (f.category);
        refresh();
    }

    SoundState& getSound() { return sound; }

    static juce::File getUserDirectory() { return getUserDataDirectory().getChildFile ("Presets"); }
    static juce::String getExtension() { return "." BIDOO_KIT_EXT; }

    // Re-reads the user presets and folders from disk (and the favourites).
    void refresh()
    {
        entries.clear();
        userFolders.clear();

        // factory: in the plugin's order (the program numbers), whatever their categories
        for (size_t i = 0; i < factory.size(); ++i)
            entries.push_back ({ factory[i].name, true, (int) i, {}, factory[i].category, factory[i].tags });

        scanUserFolder (getUserDirectory(), {}, 0);
        favourites.reload();
    }

    const std::vector<Entry>& getEntries() const { return entries; }
    // The factory categories, in the browser's order.
    const juce::StringArray& getFactoryCategories() const { return categories; }
    // Every user subfolder ("a", "a/b"...), in list order (empty folders too).
    const juce::StringArray& getUserFolders() const { return userFolders; }

    // A folder's place on disk: "" is Presets/ itself.
    static juce::File userFolderFile (const juce::String& folder)
    {
        return folder.isEmpty() ? getUserDirectory() : getUserDirectory().getChildFile (folder);
    }

    // What the browser shows as a preset's folder: "Factory", the category, "User", "User / a / b".
    static juce::String folderLabel (const Entry& e)
    {
        if (e.factory)
            return e.folder.isEmpty() ? juce::String ("Factory") : e.folder;
        return e.folder.isEmpty() ? juce::String ("User") : "User / " + e.folder.replace ("/", " / ");
    }

    // A stable name for a preset, for the favourites: "factory/<name>" or "user/<folder>/<name>".
    static juce::String keyOf (const Entry& e)
    {
        if (e.factory)
            return "factory/" + e.name;
        return "user/" + (e.folder.isEmpty() ? e.name : e.folder + "/" + e.name);
    }

    int indexOfKey (const juce::String& key) const
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (keyOf (entries[i]) == key)
                return (int) i;
        return -1;
    }

    // Search: every word of `query` is in the name, the folder or a tag (any case). Empty matches all.
    static bool matches (const Entry& e, const juce::String& query)
    {
        const auto words = juce::StringArray::fromTokens (query, " ", "");
        const auto where = folderLabel (e);
        for (const auto& w : words)
        {
            if (w.isEmpty() || e.name.containsIgnoreCase (w) || where.containsIgnoreCase (w))
                continue;
            if (! std::any_of (e.tags.begin(), e.tags.end(), [&w] (const juce::String& t) { return t.containsIgnoreCase (w); }))
                return false;
        }
        return true;
    }

    // Whether the preset has every one of `tags` (any case).
    static bool hasTags (const Entry& e, const juce::StringArray& tags)
    {
        for (const auto& t : tags)
            if (! e.tags.contains (t, true))
                return false;
        return true;
    }

    // Every tag of the list: the factory presets' in their order, then the user presets' new ones.
    juce::StringArray getAllTags() const
    {
        juce::StringArray all;
        for (const auto& e : entries)
            for (const auto& t : e.tags)
                if (! all.contains (t, true))
                    all.add (t);
        return all;
    }

    // The MIDI program that loads the preset at `index` of the list: "12", or "1:5" past the first
    // bank of 128 (Bank Select CC 0 = 1, then Program Change 5).
    static juce::String programLabel (int index)
    {
        const int bank = index / 128, program = index % 128;
        return bank > 0 ? juce::String (bank) + ":" + juce::String (program) : juce::String (program);
    }

    juce::String getCurrentName() const { return sound.getPresetName(); }

    int indexOfName (const juce::String& name) const
    {
        for (size_t i = 0; i < entries.size(); ++i)
            if (entries[i].name == name)
                return (int) i;
        return -1;
    }

    bool isFactoryName (const juce::String& name) const
    {
        for (const auto& p : factory)
            if (p.name.equalsIgnoreCase (name))
                return true;
        return false;
    }

    // Message thread only. The first factory preset loads the user's default when there is one.
    void load (int index)
    {
        if (index < 0 || index >= (int) entries.size())
            return;
        const auto& e = entries[(size_t) index];

        std::unique_ptr<juce::XmlElement> xml;
        if (! e.factory)
        {
            xml = juce::XmlDocument::parse (e.file);
            if (xml == nullptr)
                return;
        }
        else if (e.factoryIndex == 0)
        {
            xml = readUserDefault(); // null: the built-in one
        }

        auto& client = sound.getClient();
        SoundState::ScopedUndoStep undoStep (sound); // the whole load is one undo step
        client.presetLoading (xml.get());
        resetToDefaults();
        if (xml != nullptr)
            applyParams (*xml);
        else
            for (const auto& pv : factory[(size_t) e.factoryIndex].values)
                setParam (pv.first, pv.second);

        sound.setPresetName (e.name);
        client.presetLoaded (xml.get());
        client.stateRestored();
        sound.setPresetReference(); // within the undo step: undo brings the previous one back
    }

    // Whether saving `rawName` into `folder` would replace a user preset.
    bool userPresetExists (const juce::String& rawName, const juce::String& folder = {}) const
    {
        return userPresetFile (finalSaveName (rawName), folder).existsAsFile();
    }

    // The name a save under `rawName` takes: trimmed, " (user)" added to a factory name.
    juce::String finalSaveName (const juce::String& rawName) const
    {
        auto name = rawName.trim();
        if (name.isNotEmpty() && isFactoryName (name))
            name += " (user)";
        return name;
    }

    // Saves the current settings as a user preset in `folder` (a subfolder of Presets/, made if
    // needed), with its tags. Saving under a factory name appends " (user)".
    bool saveUser (const juce::String& rawName, const juce::String& folder = {}, const juce::StringArray& tags = {})
    {
        const auto name = finalSaveName (rawName);
        if (name.isEmpty() || ! isSafeFolder (folder))
            return false;

        const auto dir = userFolderFile (folder);
        if (! dir.createDirectory().wasOk())
            return false;

        sound.setPresetName (name);
        auto xml = apvts.copyState().createXml();
        if (xml == nullptr)
            return false;
        if (! tags.isEmpty())
            xml->setAttribute (kTagsAttribute, joinTags (tags));
        sound.getClient().addToPreset (*xml);
        if (! xml->writeTo (userPresetFile (name, folder)))
            return false;

        sound.setPresetReference();
        refresh();
        return true;
    }

    // The tags of a user preset, written into its file.
    bool setUserTags (int index, const juce::StringArray& tags)
    {
        if (! isUser (index))
            return false;
        const auto file = entries[(size_t) index].file;
        auto xml = juce::XmlDocument::parse (file);
        if (xml == nullptr)
            return false;
        if (tags.isEmpty())
            xml->removeAttribute (kTagsAttribute);
        else
            xml->setAttribute (kTagsAttribute, joinTags (tags));
        if (! xml->writeTo (file))
            return false;
        refresh();
        return true;
    }

    // Deletes a user preset: to the system's trash (Recycle Bin) when there is one.
    bool deleteUser (int index, bool toTrash = true)
    {
        if (! isUser (index))
            return false;
        const auto key = keyOf (entries[(size_t) index]);
        const auto& file = entries[(size_t) index].file;
        const bool ok = (toTrash && file.moveToTrash()) || file.deleteFile();
        if (ok)
            favourites.set (key, false);
        refresh();
        return ok;
    }

    // Renames a user preset (its file). The current preset's name follows. False when the name is
    // empty or taken in that folder.
    bool renameUser (int index, const juce::String& rawName)
    {
        if (! isUser (index))
            return false;
        const auto e = entries[(size_t) index];
        const auto name = finalSaveName (rawName);
        if (name.isEmpty() || name == e.name)
            return name == e.name;
        const auto target = userPresetFile (name, e.folder);
        if (target.exists() && target != e.file)
            return false;
        if (! e.file.moveFileTo (target))
            return false;
        auto renamed = e;
        renamed.name = name;
        favourites.rename (keyOf (e), keyOf (renamed));
        if (getCurrentName() == e.name)
            sound.setPresetName (name);
        refresh();
        return true;
    }

    // Moves a user preset into another folder ("": the top). False when one of that name is there.
    bool moveUser (int index, const juce::String& folder)
    {
        if (! isUser (index) || ! isSafeFolder (folder))
            return false;
        const auto e = entries[(size_t) index];
        if (e.folder == folder)
            return true;
        const auto target = userPresetFile (e.name, folder);
        if (target.exists() || ! target.getParentDirectory().createDirectory().wasOk() || ! e.file.moveFileTo (target))
            return false;
        auto moved = e;
        moved.folder = folder;
        favourites.rename (keyOf (e), keyOf (moved));
        refresh();
        return true;
    }

    // Makes a subfolder `name` in `parent` ("": the top). Returns the new folder ("a/b"), or empty.
    juce::String createFolder (const juce::String& parent, const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (name.isEmpty() || name.startsWithChar ('.') || ! isSafeFolder (parent) || folderDepth (parent) >= kMaxFolderDepth)
            return {};
        const auto folder = parent.isEmpty() ? name : parent + "/" + name;
        const auto dir = userFolderFile (folder);
        if (dir.exists() || ! dir.createDirectory().wasOk())
            return {};
        refresh();
        return folder;
    }

    // Renames a user folder (its presets keep their stars). Returns the new folder, or empty.
    juce::String renameFolder (const juce::String& folder, const juce::String& rawName)
    {
        const auto name = juce::File::createLegalFileName (rawName.trim());
        if (folder.isEmpty() || ! isSafeFolder (folder) || name.isEmpty() || name.startsWithChar ('.'))
            return {};
        const auto parent = folder.upToLastOccurrenceOf ("/", false, false);
        const auto renamed = folder.containsChar ('/') ? parent + "/" + name : name;
        if (renamed == folder)
            return folder;
        const auto dir = userFolderFile (folder), target = userFolderFile (renamed);
        if (! dir.isDirectory() || target.exists() || ! dir.moveFileTo (target))
            return {};
        favourites.renamePrefix ("user/" + folder + "/", "user/" + renamed + "/");
        refresh();
        return renamed;
    }

    // Deletes a user folder with everything in it: to the system's trash when there is one.
    bool deleteFolder (const juce::String& folder, bool toTrash = true)
    {
        if (folder.isEmpty() || ! isSafeFolder (folder))
            return false;
        const auto dir = userFolderFile (folder);
        const bool ok = dir.isDirectory() && ((toTrash && dir.moveToTrash()) || dir.deleteRecursively());
        if (ok)
            favourites.renamePrefix ("user/" + folder + "/", {});
        refresh();
        return ok;
    }

    // How many presets a user folder holds, its subfolders included.
    int countInFolder (const juce::String& folder) const
    {
        int n = 0;
        for (const auto& e : entries)
            if (! e.factory && isInFolder (e.folder, folder))
                ++n;
        return n;
    }

    // Whether `folder` is `parent` or one of its subfolders.
    static bool isInFolder (const juce::String& folder, const juce::String& parent)
    {
        return parent.isEmpty() || folder == parent || folder.startsWith (parent + "/");
    }

    // A user preset file of that name from anywhere in Presets/ (the top first, then the list
    // order), for plugin code that reads more from the current preset's file. None: a File().
    static juce::File findUserPresetFile (const juce::String& name)
    {
        const auto fileName = juce::File::createLegalFileName (name) + getExtension();
        const auto top = getUserDirectory().getChildFile (fileName);
        if (top.existsAsFile())
            return top;
        auto found = getUserDirectory().findChildFiles (juce::File::findFiles, true, fileName);
        std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFullPathName().compareIgnoreCase (b.getFullPathName()) < 0; });
        return found.isEmpty() ? juce::File() : found.getFirst();
    }

    // ---- favourites ----------------------------------------------------------------------------
    static juce::File getFavouritesFile() { return getUserDataDirectory().getChildFile ("Favourites.xml"); }

    bool isFavourite (const Entry& e) const { return favourites.contains (keyOf (e)); }
    bool isFavourite (int index) const { return index >= 0 && index < (int) entries.size() && isFavourite (entries[(size_t) index]); }

    void setFavourite (int index, bool on)
    {
        if (index >= 0 && index < (int) entries.size())
            favourites.set (keyOf (entries[(size_t) index]), on);
    }

    int countFavourites() const
    {
        int n = 0;
        for (const auto& e : entries)
            n += isFavourite (e) ? 1 : 0;
        return n;
    }

    // Re-reads the favourites when another instance changed them.
    void reloadFavourites() { favourites.reload(); }

    // ---- the user's default -------------------------------------------------------------------
    static bool hasUserDefault() { return SoundState::getUserDefaultFile().existsAsFile(); }

    // Saves the current settings (what a preset holds) as the default of new instances and of "Init".
    bool saveAsDefault()
    {
        auto xml = createSettingsXml();
        const auto file = SoundState::getUserDefaultFile();
        if (xml == nullptr || ! file.getParentDirectory().createDirectory().wasOk())
            return false;
        xml->removeAttribute ("presetName");
        return xml->writeTo (file);
    }

    // Back to the plugin's own default: new instances and "Init" use the built-in values again.
    static bool restoreBuiltInDefault()
    {
        const auto file = SoundState::getUserDefaultFile();
        return ! file.exists() || file.deleteFile();
    }

    // ---- copy / paste --------------------------------------------------------------------------
    // The settings as a preset holds them (the sound parameters, the preset name, whatever the
    // plugin keeps in its state tree or adds to presets), marked with the plugin they come from.
    std::unique_ptr<juce::XmlElement> createSettingsXml() const
    {
        auto xml = apvts.copyState().createXml();
        if (xml == nullptr)
            return nullptr;
        // session parameters stay out (bypass, view settings...)
        for (auto* child = xml->getFirstChildElement(); child != nullptr;)
        {
            auto* next = child->getNextElement();
            if (child->hasTagName ("PARAM") && ! sound.isSoundParameter (child->getStringAttribute ("id")))
                xml->removeChildElement (child, true);
            child = next;
        }
        sound.getClient().addToPreset (*xml);
        xml->setAttribute (kPluginAttribute, BIDOO_KIT_FOLDER);
        return xml;
    }

    void copyToClipboard() const
    {
        if (auto xml = createSettingsXml())
            juce::SystemClipboard::copyTextToClipboard (xml->toString());
    }

    // Settings of this plugin in `text` (from copyToClipboard, or a user preset file of it), or null.
    std::unique_ptr<juce::XmlElement> parseSettings (const juce::String& text) const
    {
        auto xml = juce::XmlDocument::parse (text);
        if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
            return nullptr;
        if (xml->hasAttribute (kPluginAttribute))
            return xml->getStringAttribute (kPluginAttribute) == BIDOO_KIT_FOLDER ? std::move (xml) : nullptr;
        // no mark: only when every parameter it names is one of ours
        int known = 0;
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
        {
            if (apvts.getParameter (child->getStringAttribute ("id")) == nullptr)
                return nullptr;
            ++known;
        }
        return known > 0 ? std::move (xml) : nullptr;
    }

    bool clipboardHasSettings() const { return parseSettings (juce::SystemClipboard::getTextFromClipboard()) != nullptr; }

    // Loads the settings on the clipboard like a user preset (one undo step). False when it holds
    // none of this plugin.
    bool pasteFromClipboard() { return loadSettings (juce::SystemClipboard::getTextFromClipboard()); }

    bool loadSettings (const juce::String& text)
    {
        const auto xml = parseSettings (text);
        return xml != nullptr && loadXml (*xml, xml->getStringAttribute ("presetName"));
    }

    // A preset file of this plugin from anywhere (Open preset file...), named after the file. False
    // when it is not one.
    bool loadFile (const juce::File& file)
    {
        const auto xml = file.existsAsFile() ? parseSettings (file.loadFileAsString()) : nullptr;
        return xml != nullptr && loadXml (*xml, file.getFileNameWithoutExtension());
    }

private:
    static constexpr const char* kPluginAttribute = "bidooPlugin";
    static constexpr const char* kTagsAttribute = "presetTags";

    static std::vector<FactoryPreset> categorised (const SoundClient& client)
    {
        auto presets = client.getFactoryPresets();
        for (const auto& [category, names] : client.getFactoryCategories())
            for (auto& p : presets)
                if (names.contains (p.name))
                    p.category = category;
        return presets;
    }

    // The stars, by preset key, in Favourites.xml: written at each change and read again before
    // each change and each refresh, so every instance of the plugin shares them.
    class Favourites
    {
    public:
        bool contains (const juce::String& key) const { return keys.count (key) > 0; }

        // A small file: read each time (a modification time may not tell two quick writes apart).
        void reload()
        {
            const auto file = getFavouritesFile();
            keys.clear();
            if (auto xml = juce::XmlDocument::parse (file))
                for (auto* e : xml->getChildWithTagNameIterator ("PRESET"))
                    if (e->getStringAttribute ("key").isNotEmpty())
                        keys.insert (e->getStringAttribute ("key"));
        }

        void set (const juce::String& key, bool on)
        {
            reload(); // another instance's changes first
            if (on == contains (key))
                return;
            if (on)
                keys.insert (key);
            else
                keys.erase (key);
            write();
        }

        void rename (const juce::String& from, const juce::String& to)
        {
            reload();
            if (keys.erase (from) == 0)
                return;
            keys.insert (to);
            write();
        }

        // Every key starting with `from` gets `to` instead (empty `to`: removed).
        void renamePrefix (const juce::String& from, const juce::String& to)
        {
            reload();
            std::vector<juce::String> moved;
            for (const auto& k : keys)
                if (k.startsWith (from))
                    moved.push_back (k);
            if (moved.empty())
                return;
            for (const auto& k : moved)
            {
                keys.erase (k);
                if (to.isNotEmpty())
                    keys.insert (to + k.substring (from.length()));
            }
            write();
        }

    private:
        void write()
        {
            juce::XmlElement xml ("FAVOURITES");
            for (const auto& k : keys)
                xml.createNewChildElement ("PRESET")->setAttribute ("key", k);
            const auto file = getFavouritesFile();
            if (file.getParentDirectory().createDirectory().wasOk())
                xml.writeTo (file);
        }

        std::set<juce::String> keys;
    };

    bool isUser (int index) const { return index >= 0 && index < (int) entries.size() && ! entries[(size_t) index].factory; }

    // A folder given by the user interface: relative, no "..", nothing hidden.
    static bool isSafeFolder (const juce::String& folder)
    {
        if (folder.isEmpty())
            return true;
        if (folder.startsWithChar ('/') || folder.containsChar ('\\') || folder.containsChar (':'))
            return false;
        for (const auto& part : juce::StringArray::fromTokens (folder, "/", ""))
            if (part.isEmpty() || part.startsWithChar ('.'))
                return false;
        return true;
    }

    static int folderDepth (const juce::String& folder)
    {
        return folder.isEmpty() ? 0 : 1 + folder.retainCharacters ("/").length();
    }

    static juce::File userPresetFile (const juce::String& name, const juce::String& folder)
    {
        return userFolderFile (folder).getChildFile (juce::File::createLegalFileName (name) + getExtension());
    }

    // The tags in a preset file's first element (the rest of the file is not read).
    static juce::StringArray readTags (const juce::File& f)
    {
        juce::XmlDocument doc (f);
        if (auto root = doc.getDocumentElement (true))
            return parseTags (root->getStringAttribute (kTagsAttribute));
        return {};
    }

    // A folder's presets by name, then its subfolders by name, each before the next (depth first).
    void scanUserFolder (const juce::File& dir, const juce::String& folder, int depth)
    {
        if (! dir.isDirectory())
            return;
        auto byName = [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; };
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*" + getExtension());
        std::sort (files.begin(), files.end(), byName);
        for (const auto& f : files)
            entries.push_back ({ f.getFileNameWithoutExtension(), false, -1, f, folder, readTags (f) });

        if (depth >= kMaxFolderDepth)
            return;
        auto dirs = dir.findChildFiles (juce::File::findDirectories, false);
        std::sort (dirs.begin(), dirs.end(), byName);
        for (const auto& d : dirs)
        {
            const auto name = d.getFileName();
            if (name.startsWithChar ('.') || d.isSymbolicLink())
                continue;
            const auto sub = folder.isEmpty() ? name : folder + "/" + name;
            userFolders.add (sub);
            scanUserFolder (d, sub, depth + 1);
        }
    }

    // Loads settings like a user preset: one undo step.
    bool loadXml (const juce::XmlElement& xml, const juce::String& name)
    {
        auto& client = sound.getClient();
        SoundState::ScopedUndoStep undoStep (sound);
        client.presetLoading (&xml);
        resetToDefaults();
        applyParams (xml);
        sound.setPresetName (name);
        client.presetLoaded (&xml);
        client.stateRestored();
        sound.setPresetReference();
        return true;
    }

    std::unique_ptr<juce::XmlElement> readUserDefault() const
    {
        auto xml = juce::XmlDocument::parse (SoundState::getUserDefaultFile());
        return xml != nullptr && xml->hasTagName (apvts.state.getType()) ? std::move (xml) : nullptr;
    }

    void applyParams (const juce::XmlElement& xml)
    {
        for (auto* child : xml.getChildWithTagNameIterator ("PARAM"))
            if (child->hasAttribute ("id") && child->hasAttribute ("value"))
                setParam (child->getStringAttribute ("id"), (float) child->getDoubleAttribute ("value"));
    }

    void resetToDefaults()
    {
        for (auto* p : sound.getProcessor().getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && ! isKept (rp->getParameterID()))
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (rp->getDefaultValue());
                rp->endChangeGesture();
            }
    }

    // What the plugin keeps out of the sound (bypass, view settings) is session state, not part
    // of a sound: loading a preset leaves it alone.
    bool isKept (const juce::String& id) const { return ! sound.isSoundParameter (id); }

    void setParam (const juce::String& id, float value)
    {
        if (isKept (id))
            return;
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    }

    SoundState& sound;
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<FactoryPreset> factory;
    std::vector<Entry> entries;
    juce::StringArray categories, userFolders;
    Favourites favourites;
};
} // namespace bidoo::kit
