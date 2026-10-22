#include "PluginProcessor.h"
#include "UserData.h"
#include "ui/PluginEditor.h"

using namespace dm;

DemiurgeProcessor::BusesProperties DemiurgeProcessor::busesProperties()
{
    if (JucePlugin_IsMidiEffect)
        return BusesProperties();
    return BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

juce::AudioProcessorValueTreeState::ParameterLayout DemiurgeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "loop", 1 }, "Loop section", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "thru", 1 }, "MIDI thru", true));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "output", 1 }, "Output part", 0, 32, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
            return v == 0 ? juce::String ("All parts") : "Part " + juce::String (v); })));
    layout.add (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { "channel", 1 }, "Output channel", 0, 16, 0,
        juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) {
            return v == 0 ? juce::String ("Part's own") : "Ch " + juce::String (v); })));
    return layout;
}

DemiurgeProcessor::DemiurgeProcessor()
    : AudioProcessor (busesProperties()),
      apvts (*this, nullptr, "PARAMS", createLayout()),
      songTree (Song::createDefault())
{
    loopParam = apvts.getParameter ("loop");
    thruParam = apvts.getParameter ("thru");
    outputParam = apvts.getParameter ("output");
    channelParam = apvts.getParameter ("channel");

    songTree.addListener (this);
    playingModel = new PlaybackModel (PlaybackModel::build (song()));
    modelDirty = false;
    setSelection (song().part (0)[ids::id].toString(), song().section (0)[ids::id].toString());
    startTimerHz (30);
    sound.applyUserDefault(); // what new instances start with (Save as default)
}

DemiurgeProcessor::~DemiurgeProcessor()
{
    stopTimer();
    songTree.removeListener (this);
    delete pendingModel.exchange (nullptr);
    delete retiredModel.exchange (nullptr);
    delete playingModel;
}

bool DemiurgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (JucePlugin_IsMidiEffect)
        return true;
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out || layouts.getMainInputChannelSet().isDisabled();
}

void DemiurgeProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr;
}

//==============================================================================
void DemiurgeProcessor::processBlock (juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    const bidoo::kit::CpuLoad::Scope cpu (cpuLoad, *this, audio.getNumSamples());
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = audio.getNumSamples();

    // Pick up a new playback model.
    if (retiredModel.load (std::memory_order_acquire) == nullptr)
        if (auto* m = pendingModel.exchange (nullptr, std::memory_order_acq_rel))
        {
            retiredModel.store (playingModel, std::memory_order_release);
            playingModel = m;
        }

    BlockInfo block;
    block.numSamples = numSamples;
    double bpm = playingModel != nullptr ? playingModel->bpm : 120.0;
    bool hostPlaying = false;

    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();
            if (hostPlaying)
            {
                bpm = pos->getBpm().orFallback (bpm);
                block.ppqStart = pos->getPpqPosition().orFallback (0.0);
            }
        }

    if (hostPlaying)
    {
        block.playing = true;
        internalWasPlaying = false;
    }
    else if (internalPlay.load())
    {
        // Audition without the host: our own clock at the song's tempo.
        if (! internalWasPlaying)
            internalPpq = 0.0;
        internalWasPlaying = true;
        block.playing = true;
        block.ppqStart = internalPpq;
    }
    else
        internalWasPlaying = false;

    block.samplesPerBeat = sampleRate * 60.0 / juce::jmax (1.0, bpm);
    hostBpm.store (bpm, std::memory_order_relaxed);
    if (! hostPlaying && block.playing)
        internalPpq += numSamples / block.samplesPerBeat;

    const bool loop = loopParam->getValue() >= 0.5f;
    const int sec = loopSectionIndex.load();
    if (playingModel != nullptr && (loop || (! hostPlaying && block.playing))
        && sec >= 0 && sec + 1 < (int) playingModel->sectionStarts.size())
    {
        // LOOP, and auditioning with the internal clock, play the selected section over and over.
        block.loop = true;
        block.loopStart = playingModel->sectionStarts[(size_t) sec];
        block.loopEnd = playingModel->sectionStarts[(size_t) sec + 1];
    }
    block.onlyPart = juce::roundToInt (outputParam->convertFrom0to1 (outputParam->getValue())) - 1;
    block.forceChannel = juce::roundToInt (channelParam->convertFrom0to1 (channelParam->getValue()));

    // Capture what the track plays into us.
    if (capturing.load() && block.playing)
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff())
                continue;
            const double ppq = block.ppqStart + meta.samplePosition / block.samplesPerBeat;
            int s1 = 0, n1 = 0, s2 = 0, n2 = 0;
            captureFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0)
            {
                captureBuffer[(size_t) s1] = { Player::songBeatFor (ppq, block), m.isNoteOn(), m.getChannel(),
                                               m.getNoteNumber(), (int) m.getVelocity() };
                captureFifo.finishedWrite (1);
            }
        }

    if (thruParam->getValue() < 0.5f)
        midi.clear();

    player.process (playingModel, block, events);
    for (const auto& e : events)
        midi.addEvent (e.type == MidiOut::Type::noteOn
                           ? juce::MidiMessage::noteOn (e.channel, e.pitch, (juce::uint8) e.velocity)
                           : juce::MidiMessage::noteOff (e.channel, e.pitch),
                       e.sample);

    livePosition.store (block.playing ? player.songPosition() : -1.0, std::memory_order_relaxed);
}

//==============================================================================
void DemiurgeProcessor::timerCallback()
{
    delete retiredModel.exchange (nullptr, std::memory_order_acq_rel);

    if (modelDirty && pendingModel.load() == nullptr)
    {
        modelDirty = false;
        pendingModel.store (new PlaybackModel (PlaybackModel::build (song())), std::memory_order_release);
        loopSectionIndex = song().sectionIndex (selectedSection);
    }

    if (capturing.load())
        drainCapture (false);
}

void DemiurgeProcessor::setSelection (const juce::String& part, const juce::String& section)
{
    selectedPart = part;
    selectedSection = section;
    loopSectionIndex = song().sectionIndex (section);
}

void DemiurgeProcessor::resetSong()
{
    loadSong (Song::createDefault());
}

void DemiurgeProcessor::loadSong (const juce::ValueTree& newSong)
{
    if (! newSong.hasType (ids::SONG))
        return;
    songTree.copyPropertiesAndChildrenFrom (newSong, nullptr);
    undo.clearUndoHistory();
    modelDirty = true;
    auto s = song();
    if (! s.partById (selectedPart).isValid())
        selectedPart = s.part (0)[ids::id].toString();
    if (! s.sectionById (selectedSection).isValid())
        selectedSection = s.section (0)[ids::id].toString();
    setSelection (selectedPart, selectedSection);
}

//==============================================================================
//  Capture
//==============================================================================
void DemiurgeProcessor::setCapture (bool on)
{
    if (on == capturing.load())
        return;
    if (on)
    {
        captureFifo.reset();
        captureOpen.clear();
        captured.clear();
        capturing = true;
        setStatus ("Capturing: play the track (or play your keyboard) with the transport running, then press CAPTURE again.");
    }
    else
    {
        capturing = false;
        drainCapture (true);
    }
}

void DemiurgeProcessor::drainCapture (bool commit)
{
    const int n = captureFifo.getNumReady();
    if (n > 0)
    {
        const auto scope = captureFifo.read (n);
        auto take = [this] (int start, int size)
        {
            for (int i = start; i < start + size; ++i)
            {
                const auto& e = captureBuffer[(size_t) i];
                const int key = e.channel << 8 | e.pitch;
                if (auto it = captureOpen.find (key); it != captureOpen.end())
                {
                    it->second.length = juce::jmax (1.0 / 64.0, e.beat - it->second.start);
                    if (e.beat > it->second.start) // a loop wrap would give a negative length: drop it
                        captured.push_back (it->second);
                    captureOpen.erase (it);
                }
                if (e.on)
                    captureOpen[key] = { e.beat, 0.25, e.pitch, e.velocity };
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    if (! commit)
        return;

    for (auto& [key, note] : captureOpen)
        captured.push_back (note); // still held: keep its default length
    captureOpen.clear();

    auto s = song();
    auto part = s.partById (selectedPart);
    if (captured.empty() || ! part.isValid())
    {
        setStatus ("Capture stopped: no notes came in.");
        return;
    }
    undo.beginNewTransaction ("Capture");
    const int takes = s.importAbsolute (part, captured, "capture");
    setStatus ("Captured " + juce::String ((int) captured.size()) + " notes into " + part[ids::name].toString() + " ("
               + juce::String (takes) + (takes == 1 ? " section)." : " sections)."));
    captured.clear();
}

//==============================================================================
//  AI jobs
//==============================================================================
composer::ModelSettings DemiurgeProcessor::modelSettings() const
{
    composer::ModelSettings m;
    m.model = settings::get ("model", m.model);
    m.effort = settings::get ("effort", m.effort);
    return m;
}

void DemiurgeProcessor::setStatus (const juce::String& text, bool error)
{
    status = text;
    statusIsError = error;
    ++statusSerial;
}

bool DemiurgeProcessor::isWorking() const
{
    return std::any_of (running.begin(), running.end(), [] (auto& r) { return r.has_value(); });
}

juce::String DemiurgeProcessor::currentJobLabel() const
{
    juce::StringArray labels;
    for (auto& r : running)
        if (r)
            labels.add (r->label);
    // "Writing Drums, Bass" rather than "Writing Drums, Writing Bass"
    for (int i = 1; i < labels.size(); ++i)
        if (labels[0].startsWith ("Writing ") && labels[i].startsWith ("Writing "))
            labels.set (i, labels[i].fromFirstOccurrenceOf ("Writing ", false, false));
    return labels.joinIntoString (", ");
}

int DemiurgeProcessor::currentJobChars() const
{
    int chars = 0;
    for (size_t i = 0; i < kSlots; ++i)
        if (running[i])
            chars += ai[i].receivedChars();
    return chars;
}

double DemiurgeProcessor::currentJobSeconds() const
{
    return isWorking() ? (juce::Time::getMillisecondCounterHiRes() - jobStarted) / 1000.0 : 0.0;
}

void DemiurgeProcessor::enqueue (const Job& job)
{
    jobs.push_back (job);
    jobs.back().group = nextGroup++;
    startNextJob();
}

void DemiurgeProcessor::cancelJobs()
{
    jobs.clear();
    for (size_t i = 0; i < kSlots; ++i)
        if (running[i])
            ai[i].cancel();
}

void DemiurgeProcessor::startNextJob()
{
    if (jobs.empty())
        return;

    const auto key = settings::apiKey();
    if (key.isEmpty())
    {
        jobs.clear();
        setStatus ("Add your Anthropic API key first (KEY button, bottom right).", true);
        return;
    }

    // COMPOSE ALL becomes one job per part, expanded when it comes up so it follows a plan
    // queued before it. The parts of a wave share a group and run at the same time.
    if (jobs.front().kind == Job::Kind::composeAll)
    {
        if (isWorking())
            return;
        jobs.pop_front();
        auto s = song();
        std::vector<Job> perPart;
        for (int wave = 0; wave < 3; ++wave)
        {
            const int group = nextGroup++;
            for (int p = 0; p < s.numParts(); ++p)
            {
                auto pt = s.part (p);
                if (composer::composeWave (roleFromName (pt[ids::role].toString())) != wave)
                    continue;
                Job j;
                for (int i = 0; i < s.numSections(); ++i)
                    j.target.cells.push_back ({ pt[ids::id].toString(), s.section (i)[ids::id].toString() });
                j.target.instruction = pt[ids::prompt].toString().trim();
                j.label = "Writing " + pt[ids::name].toString();
                j.group = group;
                perPart.push_back (j);
            }
        }
        for (auto it = perPart.rbegin(); it != perPart.rend(); ++it)
            jobs.push_front (*it);
        startNextJob();
        return;
    }

    // Start every queued job of the running group (or of the next group once nothing runs).
    for (size_t slot = 0; slot < kSlots && ! jobs.empty(); ++slot)
    {
        if (running[slot])
            continue;
        const auto group = jobs.front().group;
        const bool busyWithOtherGroup = std::any_of (running.begin(), running.end(),
                                                     [group] (auto& r) { return r.has_value() && r->group != group; });
        if (busyWithOtherGroup || jobs.front().kind == Job::Kind::composeAll)
            return;

        auto job = jobs.front();
        jobs.pop_front();
        if (! isWorking())
            jobStarted = juce::Time::getMillisecondCounterHiRes();

        // The request is built now, so a job sees what the groups before it wrote.
        const auto settingsNow = modelSettings();
        auto s = song();
        juce::var body;
        if (job.kind == Job::Kind::plan)
            body = composer::planRequest (s, job.idea, settingsNow);
        else
        {
            // Drop cells whose part or section disappeared meanwhile.
            auto& cells = job.target.cells;
            cells.erase (std::remove_if (cells.begin(), cells.end(), [&s] (const composer::Target::Cell& c)
                                         { return ! s.partById (c.part).isValid() || ! s.sectionById (c.section).isValid(); }),
                         cells.end());
            if (cells.empty())
                continue;
            body = composer::composeRequest (s, job.target, settingsNow);
        }

        running[slot] = job;
        ai[slot].send (body, key, [this, slot] (int code, const juce::String& response) { jobFinished (slot, code, response); });
    }
}

void DemiurgeProcessor::jobFinished (size_t slot, int httpStatus, const juce::String& body)
{
    if (! running[slot])
        return;
    const auto job = *running[slot];
    running[slot].reset();

    if (httpStatus == -1)
    {
        setStatus ("Cancelled.");
        jobs.clear();
        return;
    }

    juce::var answer;
    auto error = composer::extractAnswer (body, httpStatus, answer);
    auto s = song();

    if (error.isEmpty() && job.kind == Job::Kind::plan)
    {
        undo.beginNewTransaction ("Plan song");
        juce::String summary;
        error = composer::applyPlan (s, answer, summary);
        if (error.isEmpty())
        {
            s.tree.setProperty (ids::idea, job.idea, &undo);
            setSelection (s.partById (selectedPart).isValid() ? selectedPart : s.part (0)[ids::id].toString(),
                          s.sectionById (selectedSection).isValid() ? selectedSection : s.section (0)[ids::id].toString());
            setStatus ("Song planned: " + juce::String (s.numSections()) + " sections, " + juce::String (s.numParts())
                       + " parts. " + summary);
        }
    }
    else if (error.isEmpty())
    {
        composer::ComposeResult result;
        error = composer::parseCompose (s, job.target, answer, result);
        if (error.isEmpty())
        {
            undo.beginNewTransaction (job.label);
            const auto label = job.target.instruction.isNotEmpty() ? job.target.instruction : juce::String ("(no prompt)");
            int notes = 0;
            for (auto& c : result.cells)
            {
                s.addTake (s.partById (c.part), c.section, c.notes, label, modelSettings().model);
                notes += (int) c.notes.size();
            }
            setStatus (job.label + ": " + juce::String (notes) + " notes. " + result.comment);
        }
    }

    if (error.isNotEmpty())
    {
        setStatus (job.label + " failed. " + error, true);
        jobs.clear();
        return;
    }
    startNextJob();
}

//==============================================================================
void DemiurgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("selectedPart", selectedPart, nullptr);
    state.setProperty ("selectedSection", selectedSection, nullptr);
    state.removeChild (state.getChildWithName (ids::SONG), nullptr);
    state.appendChild (songTree.createCopy(), nullptr);
    if (auto xml = state.createXml())
    {
        sound.writeState (*xml); // the preset reference
        copyXmlToBinary (*xml, destData);
    }
}

void DemiurgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    sound.readState (*xml);
    auto state = juce::ValueTree::fromXml (*xml);
    auto songChild = state.getChildWithName (ids::SONG);
    state.removeChild (songChild, nullptr);
    selectedPart = state["selectedPart"].toString();
    selectedSection = state["selectedSection"].toString();
    apvts.replaceState (state);
    if (songChild.isValid())
        loadSong (songChild);
    sound.stateReplaced();
}

// A preset is a whole song: the parameters (the kit's) and the SONG tree.
void DemiurgeProcessor::addToPreset (juce::XmlElement& presetXml) const
{
    if (auto* old = presetXml.getChildByName (ids::SONG.toString()))
        presetXml.removeChildElement (old, true);
    if (auto song = songTree.createXml())
        presetXml.addChildElement (song.release());
}

void DemiurgeProcessor::presetLoaded (const juce::XmlElement* presetXml)
{
    if (presetXml != nullptr)
        if (auto* songXml = presetXml->getChildByName (ids::SONG.toString()))
        {
            loadSong (juce::ValueTree::fromXml (*songXml));
            return;
        }
    resetSong(); // a factory preset: an empty song
}

juce::AudioProcessorEditor* DemiurgeProcessor::createEditor() { return new DemiurgeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DemiurgeProcessor(); }
