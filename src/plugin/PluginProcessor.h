#pragma once

#include "ui/kit/CpuLoad.h"
#include "ui/kit/SoundState.h"
#include "AiClient.h"
#include "engine/Composer.h"
#include "engine/Player.h"
#include "engine/Song.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <deque>
#include <map>
#include <optional>

// Demiurge: an FX with audio pass-through that writes and plays MIDI (like MetroMidi, the
// shape Reaper, Bitwig, Cubase and most hosts expect from a MIDI generator).
class DemiurgeProcessor final : public juce::AudioProcessor,
                                public bidoo::kit::SoundClient,
                                private juce::ValueTree::Listener,
                                private juce::Timer
{
public:
    DemiurgeProcessor();
    ~DemiurgeProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return JucePlugin_IsMidiEffect != 0; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // ---- the song (message thread) ----
    juce::UndoManager undo;
    juce::ValueTree songTree;

    // The Bidoo kit: presets (whole songs), the preset reference, the user's default; its undo
    // goes into `undo` with the song's edits. No A/B: a song is too much to swap.
    bidoo::kit::SoundState sound { *this, apvts, *this, &undo };
    bidoo::kit::CpuLoad cpuLoad; // the top bar's CPU meter
    void addToPreset (juce::XmlElement& presetXml) const override;   // the song
    void presetLoaded (const juce::XmlElement* presetXml) override;  // the song back, or an empty one
    dm::Song song() { return dm::Song (songTree, &undo); }
    void resetSong();
    void loadSong (const juce::ValueTree& newSong); // replaces the song, clears undo history

    // The editor's selection, kept with the session. The selected section is the one that
    // LOOP plays.
    juce::String selectedPart, selectedSection;
    void setSelection (const juce::String& part, const juce::String& section);

    // ---- AI ----
    struct Job
    {
        enum class Kind { plan, compose, composeAll } kind = Kind::compose;
        juce::String idea;          // plan
        dm::composer::Target target; // compose
        juce::String label;         // shown while it runs
        int group = 0;              // jobs of one group run at the same time (a COMPOSE ALL wave)
    };
    void enqueue (const Job& job);
    void cancelJobs();
    bool isWorking() const;
    int jobsWaiting() const { return (int) jobs.size(); }
    juce::String currentJobLabel() const;
    int currentJobChars() const;
    double currentJobSeconds() const;
    dm::composer::ModelSettings modelSettings() const;

    // Last message for the status bar (results and errors).
    juce::String status;
    bool statusIsError = false;
    int statusSerial = 0; // bumps whenever status changes
    void setStatus (const juce::String& text, bool error = false);

    // ---- capture: records the MIDI coming in from the track into the selected part ----
    void setCapture (bool on);
    bool isCapturing() const { return capturing.load(); }

    // ---- internal transport for auditioning when the host is stopped ----
    std::atomic<bool> internalPlay { false };

    // ---- live info for the UI ----
    std::atomic<double> livePosition { -1.0 }; // song beats, -1 when stopped
    std::atomic<double> hostBpm { 120.0 };

private:
    static BusesProperties busesProperties();
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override { modelDirty = true; }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { modelDirty = true; }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { modelDirty = true; }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { modelDirty = true; }
    void valueTreeRedirected (juce::ValueTree&) override { modelDirty = true; }
    void timerCallback() override;

    void startNextJob();
    void jobFinished (size_t slot, int status, const juce::String& body);
    void drainCapture (bool commit);

    // Playback model handover (message thread builds, audio thread plays).
    bool modelDirty = true;
    std::atomic<dm::PlaybackModel*> pendingModel { nullptr }, retiredModel { nullptr };
    dm::PlaybackModel* playingModel = nullptr; // audio thread
    std::atomic<int> loopSectionIndex { 0 };

    dm::Player player;
    std::vector<dm::MidiOut> events;
    double sampleRate = 44100.0;
    double internalPpq = 0.0;
    bool internalWasPlaying = false;

    juce::RangedAudioParameter *loopParam = nullptr, *thruParam = nullptr, *outputParam = nullptr, *channelParam = nullptr;

    // Capture: audio thread -> message thread.
    struct CapturedEvent
    {
        double beat = 0.0;
        bool on = true;
        int channel = 1, pitch = 0, velocity = 0;
    };
    static constexpr int kCaptureSize = 4096;
    juce::AbstractFifo captureFifo { kCaptureSize };
    std::array<CapturedEvent, kCaptureSize> captureBuffer {};
    std::atomic<bool> capturing { false };
    std::map<int, dm::Note> captureOpen; // (channel << 8 | pitch) -> note being held
    dm::Notes captured;

    // Up to kSlots requests at once (the parts of a COMPOSE ALL wave).
    static constexpr size_t kSlots = 4;
    std::array<AiClient, kSlots> ai;
    std::array<std::optional<Job>, kSlots> running;
    std::deque<Job> jobs;
    double jobStarted = 0.0;
    int nextGroup = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DemiurgeProcessor)
};
