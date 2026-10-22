#pragma once

// The sound state of a plugin, beside its own processor class: undo / redo, A/B compare, the
// preset reference (has the sound changed since the preset was loaded or saved?) and the user's
// default (what new instances start with). The processor owns one, after its parameter tree, and
// passes itself as the SoundClient that says which parameters are the sound and hears of loads.
//
// The processor's getStateInformation adds writeState's children to its state XML, and its
// setStateInformation calls readState before replacing the tree, then stateReplaced after it.
// A plugin with an undo history of its own (edits outside the parameters, recorded as JUCE
// undoable actions) passes its UndoManager: the kit records the parameters into it, so undo /
// redo goes through both in the order they were made.
//
// Undo, message thread only. Changes of the sound parameters are collected by syncUndo() (the top
// bar calls it 30 times a second, and at the end of every gesture) and recorded only while the
// user is editing: during a mouse press on the editor, a gesture, a ScopedUndoStep, or just after
// one. Anything else (automation, a host restoring a state) is taken as the new reference without
// entering the history. One undo step is: a mouse press on the editor (a whole drag, whatever the
// control does with gestures meanwhile), a gesture begun without a press (a wheel turn, a typed
// value, a menu item; wheel turns in a row and gestures in one burst merge), or a ScopedUndoStep
// (a preset load, an A/B switch).

#include "KitCommon.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace bidoo::kit
{
// A factory preset: parameter id -> plain value (choice index, 0/1 for switches). Loading one
// first resets every parameter to its default, so a preset names only what differs. The category
// is optional: the preset browser shows it as a folder under Factory ({ name, values } alone
// leaves the preset at the top of Factory). Tags show in the browser, where they filter the list.
struct FactoryPreset
{
    FactoryPreset() = default;
    FactoryPreset (juce::String presetName, std::vector<std::pair<juce::String, float>> presetValues = {},
                   juce::String presetCategory = {}, juce::StringArray presetTags = {})
        : name (std::move (presetName)), values (std::move (presetValues)), category (std::move (presetCategory)),
          tags (std::move (presetTags))
    {
    }

    juce::String name;
    std::vector<std::pair<juce::String, float>> values;
    juce::String category;
    juce::StringArray tags;
};

// What the processor tells the kit about its sound. Every function has a default.
class SoundClient
{
public:
    virtual ~SoundClient() = default;

    // "Init" first: the first factory preset loads the user's default when there is one.
    virtual std::vector<FactoryPreset> getFactoryPresets() const { return { { "Init", {} } }; }

    // The folders of the factory presets in the browser: { category, { preset names } }, in the
    // browser's order. A preset named in none keeps its own category. Never changes program numbers.
    using FactoryCategories = std::vector<std::pair<juce::String, juce::StringArray>>;
    virtual FactoryCategories getFactoryCategories() const { return {}; }

    // Parameters that are part of the sound: undo / redo, A/B, presets, copy / paste and the
    // default. Session or view state (bypass, and whatever a plugin marks) is left out of all.
    virtual bool isSoundParameter (const juce::String& paramId) const { return paramId != "bypass"; }

    // Message thread, after a state, a preset or an A/B slot was loaded.
    virtual void stateRestored() {}
    // Message thread, around a preset load (before stateRestored); the XML of a user preset, a
    // pasted or opened one (null for a factory preset).
    virtual void presetLoading (const juce::XmlElement* /*presetXml*/) {}
    virtual void presetLoaded (const juce::XmlElement* /*presetXml*/) {}
    // What a preset holds besides the parameters and the state tree (a file path, a routing...):
    // add it to `presetXml` as children or attributes; presetLoaded reads it back.
    virtual void addToPreset (juce::XmlElement& /*presetXml*/) const {}

    // Whether parameter changes now come from outside the user's hands though they arrive as
    // gestures (a mapped MIDI controller): no undo step.
    virtual bool isApplyingExternalChange() const { return false; }

    // The sound beyond the parameters (a modulation routing, macro names...), for undo / redo,
    // A/B and the preset reference: a snapshot (null: none), and setting one back. The revision
    // changes whenever that state changes: the kit takes a snapshot only then. Message thread.
    virtual std::unique_ptr<juce::XmlElement> createExtraState() const { return nullptr; }
    virtual void restoreExtraState (const juce::XmlElement& /*snapshot*/) {}
    virtual int getExtraStateRevision() const { return 0; }
};

class SoundState : private juce::AudioProcessorParameter::Listener
{
public:
    SoundState (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& state, SoundClient& c,
                juce::UndoManager* sharedUndoManager = nullptr)
        : undoManager (sharedUndoManager != nullptr ? *sharedUndoManager : ownUndoManager), processor (p), apvts (state), client (c)
    {
        for (auto* param : processor.getParameters())
            param->addListener (this);
    }

    ~SoundState() override
    {
        for (auto* param : processor.getParameters())
            param->removeListener (this);
    }

    juce::AudioProcessor& getProcessor() { return processor; }
    juce::AudioProcessorValueTreeState& getState() { return apvts; }
    SoundClient& getClient() { return client; }
    bool isSoundParameter (const juce::String& id) const { return client.isSoundParameter (id); }

    // ---- the preset name (a property of the state tree, saved with it) ----
    juce::String getPresetName() const { return apvts.state.getProperty ("presetName").toString(); }
    void setPresetName (const juce::String& name) { apvts.state.setProperty ("presetName", name, nullptr); }

    // =========================================================================================
    //  The user's default (Save as default in the preset list): <user data>/Default.<ext>, a preset
    //  holding only what presets hold. A new instance starts with it (applyUserDefault, from the
    //  processor's constructor), and loading the first factory preset ("Init") loads it.
    // =========================================================================================
    static juce::File getUserDefaultFile() { return getUserDataDirectory().getChildFile ("Default." BIDOO_KIT_EXT); }

    void applyUserDefault()
    {
        const auto xml = juce::XmlDocument::parse (getUserDefaultFile());
        if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
            return;
        for (auto* child : xml->getChildWithTagNameIterator ("PARAM"))
            if (auto* p = apvts.getParameter (child->getStringAttribute ("id"));
                p != nullptr && child->hasAttribute ("value") && isSoundParameter (child->getStringAttribute ("id")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) child->getDoubleAttribute ("value")));
    }

    // =========================================================================================
    //  The saved state: the A/B slots and the preset reference, as children of the processor's
    //  state XML (the tag names come from Fab, so its saved sessions read).
    // =========================================================================================
    void writeState (juce::XmlElement& xml) const
    {
        xml.addChildElement (createAbXml().release());
        const juce::ScopedLock sl (abLock);
        if (presetRef.valid)
            xml.addChildElement (createRefXml (presetRef, kPresetRefTag).release());
    }

    // Reads (and removes) what writeState added, before the processor replaces its tree.
    void readState (juce::XmlElement& xml)
    {
        if (auto* ab = xml.getChildByName (kAbTag))
        {
            restoreAbXml (*ab);
            xml.removeChildElement (ab, true);
        }
        else
        {
            const juce::ScopedLock sl (abLock);
            abActive = 0;
            abSlots[0] = abSlots[1] = {};
        }

        // the preset reference (none in older sessions, or never asked for: the settings the
        // editor first sees are taken as their preset's, see isPresetModified)
        const juce::ScopedLock sl (abLock);
        presetRef = {};
        if (auto* ref = xml.getChildByName (kPresetRefTag))
        {
            presetRef = readRefXml (*ref);
            xml.removeChildElement (ref, true);
        }
        ++presetRefRevision;
    }

    // After the processor replaced its tree (any thread): a fresh history, then stateRestored.
    void stateReplaced()
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<SoundState> (this)]
                                         {
                                             if (safe != nullptr)
                                             {
                                                 safe->syncUndo (false);
                                                 safe->undoManager.clearUndoHistory();
                                                 safe->client.stateRestored();
                                             }
                                         });
    }

    // =========================================================================================
    //  The preset reference: the sound parameters as the last preset load or save left them, so
    //  the editor can show that the settings were changed since (isPresetModified). It follows
    //  undo / redo, A/B and the saved session. Message thread.
    // =========================================================================================
    struct PresetRef
    {
        bool valid = false;
        std::vector<std::pair<juce::String, float>> values; // id, normalised
        juce::String extra; // the client's extra state (createExtraState), as text

        bool sameAs (const PresetRef& other) const
        {
            if (valid != other.valid || values.size() != other.values.size() || extra != other.extra)
                return false;
            for (size_t i = 0; i < values.size(); ++i)
                if (values[i].first != other.values[i].first || ! juce::exactlyEqual (values[i].second, other.values[i].second))
                    return false;
            return true;
        }
    };

    // Takes the current settings as the preset's (the presets call it after a load or a save).
    void setPresetReference()
    {
        auto values = captureNormalised();
        auto extra = extraNow();
        const juce::ScopedLock sl (abLock);
        presetRef = { true, std::move (values), std::move (extra) };
        ++presetRefRevision;
    }

    // Whether a sound parameter differs from the preset reference.
    bool isPresetModified()
    {
        if (! isMessageThread())
            return false;
        if (! hasPresetRef())
            setPresetReference(); // a new instance: its settings are its preset's
        if (refCacheRevision != presetRefRevision.load())
        {
            const juce::ScopedLock sl (abLock);
            refCacheRevision = presetRefRevision.load();
            refCache.clear();
            for (const auto& v : presetRef.values)
                if (auto* p = apvts.getParameter (v.first); p != nullptr && isSoundParameter (v.first))
                    refCache.emplace_back (p, v.second);
        }
        for (const auto& c : refCache)
            if (std::abs (c.first->getValue() - c.second) > 1.0e-5f)
                return true;
        // the extra state, compared again only when it or the reference changed
        if (const int rev = client.getExtraStateRevision(); rev != refExtraRevision || refExtraFor != presetRefRevision.load())
        {
            refExtraRevision = rev;
            refExtraFor = presetRefRevision.load();
            const auto now = extraNow();
            const juce::ScopedLock sl (abLock);
            refExtraDiffers = now != presetRef.extra;
        }
        return refExtraDiffers;
    }

    // =========================================================================================
    //  Undo (see the top of the file).
    // =========================================================================================
private:
    juce::UndoManager ownUndoManager { 30000, 100 };

public:
    juce::UndoManager& undoManager; // the kit's own, or the plugin's (see the constructor)

    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    bool undo()
    {
        if (! isMessageThread())
            return false;
        syncUndo();
        const juce::ScopedValueSetter<bool> svs (applyingUndo, true);
        return undoManager.undo();
    }

    bool redo()
    {
        if (! isMessageThread())
            return false;
        syncUndo();
        const juce::ScopedValueSetter<bool> svs (applyingUndo, true);
        return undoManager.redo();
    }

    // Groups every change made while it exists into one undo step (the preset name and reference
    // included).
    class ScopedUndoStep
    {
    public:
        explicit ScopedUndoStep (SoundState& s) : owner (s)
        {
            if (owner.stepDepth++ == 0)
            {
                owner.startTransaction();
                nameBefore = owner.getPresetName();
                refBefore = owner.getPresetRef();
            }
        }

        ~ScopedUndoStep()
        {
            if (--owner.stepDepth == 0)
            {
                owner.syncUndo (true);
                const auto nameAfter = owner.getPresetName();
                auto refAfter = owner.getPresetRef();
                if (nameAfter != nameBefore || ! refAfter.sameAs (refBefore))
                    owner.undoManager.perform (new NameAction (owner, nameBefore, nameAfter, std::move (refBefore), std::move (refAfter)));
                owner.lastUserEvent = now();
            }
        }

    private:
        SoundState& owner;
        juce::String nameBefore;
        PresetRef refBefore;
        JUCE_DECLARE_NON_COPYABLE (ScopedUndoStep)
    };

    // Collects the changes made since the last call; records them when the user is editing or
    // `force` is set, otherwise only takes them as the new reference.
    void syncUndo (bool force = false)
    {
        if (! isMessageThread() || applyingUndo)
            return;
        ensureTracked();

        if (gesturePress && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            gesturePress = false;

        const bool record = force || isUserEditing();
        std::vector<ParamChange> changes;
        for (size_t i = 0; i < tracked.size(); ++i)
        {
            const float v = tracked[i].param->getValue();
            if (! juce::exactlyEqual (v, tracked[i].committed))
            {
                if (record)
                    changes.push_back ({ i, tracked[i].committed, v });
                tracked[i].committed = v;
            }
        }
        if (! changes.empty())
            undoManager.perform (new ParamsAction (*this, std::move (changes)));

        // the client's extra state: a snapshot when its revision moved, a step when it changed
        if (const int rev = client.getExtraStateRevision(); rev != extraRevision)
        {
            extraRevision = rev;
            auto now = extraNow();
            if (now != extraCommitted)
            {
                if (record)
                    undoManager.perform (new ExtraAction (*this, extraCommitted, now));
                extraCommitted = std::move (now);
            }
        }
    }

    // From the editor (the top bar's pointer watcher): a mouse button went down / up anywhere on
    // it, or the wheel turned.
    void notePointerDown()
    {
        if (! pointerDown)
        {
            // No collection first: what is pending now was most likely set by this very press
            // (a control reacting to mouse-down before the editor hears of it).
            pointerDown = true;
            if (! gesturePress) // a control already began this press's step
            {
                undoManager.beginNewTransaction();
                lastTransactionStart = now();
            }
            gesturePress = false;
        }
        lastUserEvent = now();
    }

    void notePointerUp()
    {
        syncUndo (true);
        pointerDown = false;
        lastUserEvent = now();
    }

    void noteWheel()
    {
        // A control that changes values on the wheel without a gesture: the first turn of a
        // series still starts its own step (unless a gesture of this turn already did).
        const double t = now();
        if (! pointerDown && t - lastWheel > kWheelMergeMs && t - lastTransactionStart > 20.0)
            undoManager.beginNewTransaction();
        lastWheel = lastUserEvent = t;
    }

    // A value the user didn't set (a mapped MIDI controller): the new undo reference, silently.
    void keepAsUndoReference (juce::RangedAudioParameter& p)
    {
        if (trackedReady)
            for (auto& t : tracked)
                if (t.param == &p)
                    t.committed = p.getValue();
    }

    // =========================================================================================
    //  A/B compare: two complete settings of the sound parameters. The active one lives in the
    //  parameters; the other waits in its slot. Message thread only.
    // =========================================================================================
    int getActiveAb() const { return abActive.load(); }

    // Saves the current settings into the active slot and loads the other one (an empty slot
    // starts as a copy of the current settings). One undo step.
    void switchAb()
    {
        if (! isMessageThread())
            return;
        ScopedUndoStep step (*this);
        auto before = getAbState();
        {
            const juce::ScopedLock sl (abLock);
            abSlots[abActive] = captureSlot();
            const int target = 1 - abActive;
            if (! abSlots[target].valid)
                abSlots[target] = abSlots[abActive];
            abActive = target;
        }
        applySlot (abSlots[abActive]);
        undoManager.perform (new AbAction (*this, std::move (before), getAbState()));
        client.stateRestored();
    }

    // Copies the current settings into the other slot. One undo step.
    void copyAbToOther()
    {
        if (! isMessageThread())
            return;
        ScopedUndoStep step (*this);
        auto before = getAbState();
        {
            const juce::ScopedLock sl (abLock);
            abSlots[1 - abActive] = captureSlot();
        }
        undoManager.perform (new AbAction (*this, std::move (before), getAbState()));
    }

private:
    static constexpr const char* kAbTag = "FAB_AB";
    static constexpr const char* kPresetRefTag = "FAB_PRESET_REF";
    static constexpr double kWheelMergeMs = 600.0; // wheel turns closer than this make one step
    // Gestures this close together without a press are one batch of plugin code (a menu item,
    // an imported file setting many parameters): one step.
    static constexpr double kBatchMergeMs = 40.0;

    static double now() { return juce::Time::getMillisecondCounterHiRes(); }
    static bool isMessageThread() { return juce::MessageManager::existsAndIsCurrentThread(); }

    bool isUserEditing() const
    {
        return pointerDown || gesturePress || stepDepth > 0 || now() - lastUserEvent < 250.0;
    }

    void startTransaction()
    {
        syncUndo();
        undoManager.beginNewTransaction();
        lastTransactionStart = now();
    }

    void parameterValueChanged (int, float) override {}

    void parameterGestureChanged (int, bool starting) override
    {
        if (applyingUndo || stepDepth > 0 || client.isApplyingExternalChange() || ! isMessageThread())
            return;

        if (starting)
        {
            if (pointerDown || gesturePress)
            {
                // part of the current press: the press is the step
            }
            else if (juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            {
                // a press the editor hears of after its control (or never, outside the editor)
                startTransaction();
                gesturePress = true;
            }
            else if (now() - lastWheel > kWheelMergeMs && now() - lastGesture > kBatchMergeMs)
            {
                startTransaction();
            }
        }
        else
        {
            syncUndo (true);
        }
        lastUserEvent = lastGesture = now();
    }

    // ---- tracked parameters ----
    struct Tracked
    {
        juce::RangedAudioParameter* param;
        float committed;
    };
    std::vector<Tracked> tracked;
    bool trackedReady = false;

    // Built on first use: isSoundParameter() is the client's, not callable while it is built.
    void ensureTracked()
    {
        if (trackedReady)
            return;
        trackedReady = true;
        for (auto* p : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && isSoundParameter (rp->getParameterID()))
                tracked.push_back ({ rp, rp->getValue() });
        extraRevision = client.getExtraStateRevision();
        extraCommitted = extraNow();
    }

    // The client's extra state as text ("" for none).
    juce::String extraNow() const
    {
        const auto xml = client.createExtraState();
        return xml != nullptr ? xml->toString (juce::XmlElement::TextFormat().singleLine().withoutHeader()) : juce::String();
    }

    // Sets the client's extra state back (an undo, an A/B switch), keeping it as the reference.
    void restoreExtra (const juce::String& text)
    {
        if (text.isEmpty())
            return;
        if (const auto xml = juce::XmlDocument::parse (text))
        {
            const juce::ScopedValueSetter<bool> svs (applyingUndo, true);
            client.restoreExtraState (*xml);
        }
        extraCommitted = text;
        extraRevision = client.getExtraStateRevision();
    }

    // A change of the client's extra state, done when it is recorded.
    struct ExtraAction : juce::UndoableAction
    {
        ExtraAction (SoundState& s, juce::String b, juce::String a) : owner (s), before (std::move (b)), after (std::move (a)) {}
        bool perform() override
        {
            if (! done)
                owner.restoreExtra (after);
            done = true;
            return true;
        }
        bool undo() override
        {
            owner.restoreExtra (before);
            done = false;
            return true;
        }
        int getSizeInUnits() override { return 1 + (before.length() + after.length()) / 256; }

        SoundState& owner;
        juce::String before, after;
        bool done = true;
    };

    // Sets a parameter from an undo or an A/B switch, keeping it as the reference.
    void applyValue (size_t index, float normalised)
    {
        auto& t = tracked[index];
        t.committed = normalised;
        if (! juce::exactlyEqual (t.param->getValue(), normalised))
        {
            const juce::ScopedValueSetter<bool> svs (applyingUndo, true);
            t.param->beginChangeGesture();
            t.param->setValueNotifyingHost (normalised);
            t.param->endChangeGesture();
            t.committed = t.param->getValue();
        }
    }

    struct ParamChange
    {
        size_t index;
        float before, after;
    };

    // Parameter changes of one step. The values are already set when it is recorded.
    struct ParamsAction : juce::UndoableAction
    {
        ParamsAction (SoundState& s, std::vector<ParamChange> c) : owner (s), changes (std::move (c)) {}

        bool perform() override
        {
            if (! done)
                for (const auto& c : changes)
                    owner.applyValue (c.index, c.after);
            done = true;
            return true;
        }

        bool undo() override
        {
            for (auto it = changes.rbegin(); it != changes.rend(); ++it)
                owner.applyValue (it->index, it->before);
            done = false;
            return true;
        }

        int getSizeInUnits() override { return 1 + (int) changes.size(); }

        // Successive collections of one step merge: one entry per parameter, first before, last after.
        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
        {
            auto* other = dynamic_cast<ParamsAction*> (next);
            if (other == nullptr)
                return nullptr;
            auto merged = changes;
            for (const auto& c : other->changes)
            {
                auto it = std::find_if (merged.begin(), merged.end(), [&c] (const ParamChange& m) { return m.index == c.index; });
                if (it != merged.end())
                    it->after = c.after;
                else
                    merged.push_back (c);
            }
            auto* a = new ParamsAction (owner, std::move (merged));
            a->done = true;
            return a;
        }

        SoundState& owner;
        std::vector<ParamChange> changes;
        bool done = true;
    };

    // The preset name and reference of a step (a preset load, an A/B switch).
    struct NameAction : juce::UndoableAction
    {
        NameAction (SoundState& s, juce::String b, juce::String a, PresetRef rb, PresetRef ra)
            : owner (s), before (std::move (b)), after (std::move (a)), refBefore (std::move (rb)), refAfter (std::move (ra))
        {
        }
        bool perform() override { return set (after, refAfter); }
        bool undo() override { return set (before, refBefore); }
        bool set (const juce::String& name, const PresetRef& ref)
        {
            owner.setPresetName (name);
            owner.setPresetRef (ref);
            return true;
        }
        int getSizeInUnits() override { return 2 + (int) (refBefore.values.size() + refAfter.values.size()) / 8; }

        SoundState& owner;
        juce::String before, after;
        PresetRef refBefore, refAfter;
    };

    bool hasPresetRef() const
    {
        const juce::ScopedLock sl (abLock);
        return presetRef.valid;
    }

    PresetRef getPresetRef() const
    {
        const juce::ScopedLock sl (abLock);
        return presetRef;
    }

    void setPresetRef (const PresetRef& ref)
    {
        const juce::ScopedLock sl (abLock);
        presetRef = ref;
        ++presetRefRevision;
    }

    // The sound parameters, normalised.
    std::vector<std::pair<juce::String, float>> captureNormalised() const
    {
        std::vector<std::pair<juce::String, float>> values;
        for (auto* p : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && isSoundParameter (rp->getParameterID()))
                values.emplace_back (rp->getParameterID(), rp->getValue());
        return values;
    }

    static std::unique_ptr<juce::XmlElement> createRefXml (const PresetRef& ref, const char* tag)
    {
        auto xml = std::make_unique<juce::XmlElement> (tag);
        for (const auto& v : ref.values)
        {
            auto* pe = xml->createNewChildElement ("PARAM");
            pe->setAttribute ("id", v.first);
            pe->setAttribute ("value", (double) v.second);
        }
        if (ref.extra.isNotEmpty())
            xml->setAttribute ("extra", ref.extra);
        return xml;
    }

    PresetRef readRefXml (const juce::XmlElement& xml) const
    {
        PresetRef ref;
        ref.valid = true;
        for (auto* pe : xml.getChildWithTagNameIterator ("PARAM"))
            if (isSoundParameter (pe->getStringAttribute ("id")))
                ref.values.emplace_back (pe->getStringAttribute ("id"), (float) pe->getDoubleAttribute ("value"));
        ref.extra = xml.getStringAttribute ("extra");
        return ref;
    }

    // ---- A/B ----
    struct AbSlot
    {
        bool valid = false;
        juce::String presetName;
        std::vector<std::pair<juce::String, float>> values; // id, plain value
        juce::String extra; // the client's extra state
        PresetRef reference; // of its preset
    };

    struct AbState
    {
        int active = 0;
        AbSlot slots[2];
    };

    AbSlot captureSlot() const
    {
        AbSlot s;
        s.valid = true;
        s.presetName = getPresetName();
        {
            const juce::ScopedLock sl (abLock);
            s.reference = presetRef;
        }
        for (auto* p : processor.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p); rp != nullptr && isSoundParameter (rp->getParameterID()))
                s.values.emplace_back (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
        s.extra = extraNow();
        return s;
    }

    void applySlot (const AbSlot& s)
    {
        ensureTracked();
        for (const auto& v : s.values)
            for (size_t i = 0; i < tracked.size(); ++i)
                if (tracked[i].param->getParameterID() == v.first)
                {
                    tracked[i].committed = tracked[i].param->getValue(); // record the change itself
                    const juce::ScopedValueSetter<bool> svs (applyingUndo, true);
                    auto* p = tracked[i].param;
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (v.second));
                    p->endChangeGesture();
                    break;
                }
        if (s.extra.isNotEmpty() && s.extra != extraCommitted)
        {
            const auto before = extraCommitted;
            restoreExtra (s.extra);
            undoManager.perform (new ExtraAction (*this, before, s.extra));
        }
        setPresetName (s.presetName);
        if (s.reference.valid)
            setPresetRef (s.reference);
        else
            setPresetReference(); // a slot without one: its settings are its preset's
    }

    AbState getAbState() const
    {
        const juce::ScopedLock sl (abLock);
        AbState st;
        st.active = abActive;
        st.slots[0] = abSlots[0];
        st.slots[1] = abSlots[1];
        return st;
    }

    void setAbState (const AbState& st)
    {
        const juce::ScopedLock sl (abLock);
        abActive = st.active;
        abSlots[0] = st.slots[0];
        abSlots[1] = st.slots[1];
    }

    // Which slot is active and what each holds (parameter values are undone by their own action).
    struct AbAction : juce::UndoableAction
    {
        AbAction (SoundState& s, AbState b, AbState a) : owner (s), before (std::move (b)), after (std::move (a)) {}
        bool perform() override { owner.setAbState (after); return true; }
        bool undo() override { owner.setAbState (before); return true; }
        int getSizeInUnits() override { return 10 + (int) (before.slots[0].values.size() + before.slots[1].values.size()); }

        SoundState& owner;
        AbState before, after;
    };

    std::unique_ptr<juce::XmlElement> createAbXml() const
    {
        auto xml = std::make_unique<juce::XmlElement> (kAbTag);
        const juce::ScopedLock sl (abLock);
        xml->setAttribute ("active", abActive.load());
        for (int i = 0; i < 2; ++i)
        {
            // the active slot is the current settings
            const auto slot = i == abActive ? captureSlot() : abSlots[i];
            if (! slot.valid)
                continue;
            auto* e = xml->createNewChildElement ("SLOT");
            e->setAttribute ("index", i);
            e->setAttribute ("presetName", slot.presetName);
            if (slot.extra.isNotEmpty())
                e->setAttribute ("extra", slot.extra);
            for (const auto& v : slot.values)
            {
                auto* pe = e->createNewChildElement ("PARAM");
                pe->setAttribute ("id", v.first);
                pe->setAttribute ("value", (double) v.second);
            }
            if (slot.reference.valid)
                e->addChildElement (createRefXml (slot.reference, kPresetRefTag).release());
        }
        return xml;
    }

    void restoreAbXml (const juce::XmlElement& xml)
    {
        const juce::ScopedLock sl (abLock);
        abActive = juce::jlimit (0, 1, xml.getIntAttribute ("active", 0));
        abSlots[0] = abSlots[1] = {};
        for (auto* e : xml.getChildWithTagNameIterator ("SLOT"))
        {
            const int i = e->getIntAttribute ("index", -1);
            if (i < 0 || i > 1)
                continue;
            auto& s = abSlots[i];
            s.valid = true;
            s.presetName = e->getStringAttribute ("presetName");
            s.extra = e->getStringAttribute ("extra");
            for (auto* pe : e->getChildWithTagNameIterator ("PARAM"))
                if (isSoundParameter (pe->getStringAttribute ("id")))
                    s.values.emplace_back (pe->getStringAttribute ("id"), (float) pe->getDoubleAttribute ("value"));
            if (auto* ref = e->getChildByName (kPresetRefTag))
                s.reference = readRefXml (*ref);
        }
    }

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& apvts;
    SoundClient& client;

    juce::CriticalSection abLock; // the slots and the preset reference are also read by getStateInformation on any thread
    std::atomic<int> abActive { 0 };
    AbSlot abSlots[2];

    PresetRef presetRef;
    std::atomic<int> presetRefRevision { 0 };
    // the reference's parameters, for isPresetModified (message thread)
    int refCacheRevision = -1;
    std::vector<std::pair<juce::RangedAudioParameter*, float>> refCache;
    int refExtraRevision = -1, refExtraFor = -1;
    bool refExtraDiffers = false;

    // the client's extra state as the undo history last took it
    juce::String extraCommitted;
    int extraRevision = 0;

    bool pointerDown = false;  // between the editor's mouse-down and mouse-up
    bool gesturePress = false; // a gesture began while a button was down, before (or without) the editor's mouse-down
    bool applyingUndo = false;
    int stepDepth = 0;
    double lastUserEvent = 0.0, lastWheel = 0.0, lastTransactionStart = 0.0, lastGesture = 0.0;

    JUCE_DECLARE_WEAK_REFERENCEABLE (SoundState)
    JUCE_DECLARE_NON_COPYABLE (SoundState)
};
} // namespace bidoo::kit
