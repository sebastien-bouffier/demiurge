#include "PluginEditor.h"
#include "plugin/Exchange.h"
#include "plugin/UserData.h"

using namespace dm;


//==============================================================================
DemiurgeEditor::DemiurgeEditor (DemiurgeProcessor& p)
    : AudioProcessorEditor (&p), processor (p), presets (p), arrangement (p, lnf), roll (p, lnf)
{
    setLookAndFeel (&lnf);

    // ---- top bar ----
    presetCombo.setTextWhenNothingSelected ("Select song...");
    presetCombo.onChange = [this] { presetChosen(); };
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (+1); };
    presetSave.onClick = [this] { showSaveDialog(); };
    presetDelete.onClick = [this] { confirmDelete(); };
    undoButton.onClick = [this] { processor.undo.undo(); refreshAll(); };
    redoButton.onClick = [this] { processor.undo.redo(); refreshAll(); };
    keyButton.onClick = [this] { showSettings(); };
    themeToggle.onClick = [this]
    {
        const bool light = ! lnf.light();
        applyTheme (light);
        settings::set ("theme", light ? "light" : "dark");
    };
    for (auto* c : std::initializer_list<juce::Component*> { &presetCombo, &presetPrev, &presetNext, &presetSave,
                                                             &presetDelete, &undoButton, &redoButton, &keyButton, &themeToggle })
        addAndMakeVisible (*c);
    rebuildPresetList (presets.getCurrentName());

    auto label = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (l);
    };
    auto editor = [this] (juce::TextEditor& e, bool multiLine, const juce::String& hint)
    {
        e.setMultiLine (multiLine, true);
        e.setReturnKeyStartsNewLine (multiLine);
        e.setTextToShowWhenEmpty (hint, lnf.palette().textDim);
        e.setFont (bidoo::look::font (13.0f));
        e.setIndents (6, multiLine ? 5 : 4);
        addAndMakeVisible (e);
    };

    // ---- song panel ----
    label (titleLabel, "Title");
    label (ideaLabel, "Idea");
    label (keyLabel, "Key");
    label (bpmLabel, "Tempo");
    label (tsLabel, "Time");
    label (styleLabel, "Style");
    editor (titleEditor, false, "Untitled");
    editor (ideaEditor, true, "Describe the song: genre, mood, references, instruments, structure... "
                              "then PLAN SONG writes the arrangement and the parts list.");
    editor (styleEditor, true, "Genre, feel and production notes (the plan fills this in).");
    titleEditor.onTextChange = [this] { processor.songTree.setProperty (ids::title, titleEditor.getText(), nullptr); };
    ideaEditor.onTextChange = [this] { processor.songTree.setProperty (ids::idea, ideaEditor.getText(), nullptr); };
    styleEditor.onTextChange = [this] { processor.songTree.setProperty (ids::style, styleEditor.getText(), nullptr); };

    keyCombo.addItemList (keyNames(), 1);
    scaleCombo.addItemList (scaleNames(), 1);
    tsCombo.addItemList (timeSignatures(), 1);
    keyCombo.onChange = [this] { setSongProperty (ids::key, keyCombo.getSelectedItemIndex(), "Key"); };
    scaleCombo.onChange = [this] { setSongProperty (ids::scale, scaleCombo.getText(), "Scale"); };
    tsCombo.onChange = [this]
    {
        const auto ts = tsCombo.getText();
        processor.undo.beginNewTransaction ("Time signature");
        processor.songTree.setProperty (ids::tsNum, ts.upToFirstOccurrenceOf ("/", false, false).getIntValue(), &processor.undo);
        processor.songTree.setProperty (ids::tsDen, ts.fromFirstOccurrenceOf ("/", false, false).getIntValue(), &processor.undo);
    };
    bpmSlider.setRange (30.0, 300.0, 1.0);
    bpmSlider.setTextValueSuffix (" BPM");
    bpmSlider.onDragStart = [this] { processor.undo.beginNewTransaction ("Tempo"); };
    bpmSlider.onValueChange = [this] { processor.songTree.setProperty (ids::bpm, bpmSlider.getValue(), &processor.undo); };
    planButton.onClick = [this] { planSong(); };
    composeAllButton.onClick = [this] { composeAll(); };
    for (auto* c : std::initializer_list<juce::Component*> { &keyCombo, &scaleCombo, &tsCombo, &bpmSlider, &planButton,
                                                             &composeAllButton })
        addAndMakeVisible (*c);

    // ---- part panel ----
    label (partNameLabel, "Name");
    label (roleLabel, "Role");
    label (channelLabel, "Channel");
    label (programLabel, "Sound");
    label (descriptionLabel, "What it plays");
    editor (partName, false, "Part name");
    editor (partDescription, true, "How this part should sound and behave, in your words. "
                                   "Claude reads it every time it writes this part.");
    partName.onTextChange = [this] { setPartProperty (ids::name, partName.getText(), {}); };
    partDescription.onTextChange = [this] { setPartProperty (ids::description, partDescription.getText(), {}); };
    roleCombo.addItemList (roleNames(), 1);
    for (int ch = 1; ch <= 16; ++ch)
        channelCombo.addItem ("Ch " + juce::String (ch) + (ch == 10 ? " (drums)" : ""), ch);
    for (int prog = 0; prog < 128; ++prog)
        programCombo.addItem (juce::String (prog + 1) + " " + gmProgramName (prog), prog + 1);
    roleCombo.onChange = [this]
    {
        const auto role = roleFromName (roleCombo.getText());
        auto pt = processor.song().partById (processor.selectedPart);
        if (! pt.isValid() || roleFromName (pt[ids::role].toString()) == role)
            return;
        processor.undo.beginNewTransaction ("Role");
        pt.setProperty (ids::role, roleName (role), &processor.undo);
        pt.setProperty (ids::program, defaultProgram (role), &processor.undo);
        if (role == Role::drums || role == Role::percussion)
            pt.setProperty (ids::channel, 10, &processor.undo);
    };
    channelCombo.onChange = [this] { setPartProperty (ids::channel, channelCombo.getSelectedId(), "Channel"); };
    programCombo.onChange = [this] { setPartProperty (ids::program, programCombo.getSelectedId() - 1, "Sound"); };
    for (auto* c : std::initializer_list<juce::Component*> { &roleCombo, &channelCombo, &programCombo })
        addAndMakeVisible (*c);

    // ---- arrangement ----
    addAndMakeVisible (arrangement);
    arrangement.onSelectionChanged = [this] { refreshAll(); };
    arrangement.onEditSection = [this] (const juce::String& id) { editSection (id); };
    arrangement.onGenerate = [this] (const juce::String& part, const juce::String& sec, bool vary) { generate (part, sec, vary); };
    arrangement.onDragOut = [this] (const juce::String& part, const juce::String& sec) { dragOut (part, sec); };
    addSectionButton.onClick = [this] { addSection(); };
    addPartButton.onClick = [this] { addPart(); };
    addAndMakeVisible (addSectionButton);
    addAndMakeVisible (addPartButton);

    // ---- cell panel ----
    addAndMakeVisible (roll);
    label (promptLabel, "Prompt");
    editor (promptEditor, true, "What to write or change, e.g. \"syncopated, octave jumps, busier in the last bar\". "
                                "Leave empty to let Claude decide.");
    promptEditor.onTextChange = [this] { setPartProperty (ids::prompt, promptEditor.getText(), {}); };
    generateButton.onClick = [this] { generate (processor.selectedPart, processor.selectedSection, false); };
    varyButton.onClick = [this] { generate (processor.selectedPart, processor.selectedSection, true); };
    wholePartButton.onClick = [this] { generate (processor.selectedPart, {}, false); };
    wholeSectionButton.onClick = [this] { generate ({}, processor.selectedSection, false); };
    takePrev.onClick = [this] { stepTake (-1); };
    takeNext.onClick = [this] { stepTake (+1); };
    deleteTakeButton.onClick = [this]
    {
        auto s = processor.song();
        processor.undo.beginNewTransaction ("Delete take");
        s.deleteActiveTake (s.partById (processor.selectedPart), processor.selectedSection);
    };
    quantButton.onClick = [this]
    {
        const bool drums = roleFromName (processor.song().partById (processor.selectedPart)[ids::role].toString()) == Role::drums;
        transformTake ("Quantize", [drums] (Notes& notes)
        {
            for (auto& n : notes)
            {
                const double end = n.start + n.length;
                n.start = std::round (n.start / 0.25) * 0.25;
                if (! drums)
                    n.length = juce::jmax (0.25, std::round (end / 0.25) * 0.25 - n.start);
            }
        });
    };
    humanButton.onClick = [this]
    {
        transformTake ("Humanize", [] (Notes& notes)
        {
            juce::Random r;
            for (auto& n : notes)
            {
                n.start = juce::jmax (0.0, n.start + (r.nextDouble() - 0.5) * 0.03);
                n.velocity = juce::jlimit (1, 127, n.velocity + r.nextInt ({ -10, 11 }));
            }
        });
    };
    octDownButton.onClick = [this] { transformTake ("Octave down", [] (Notes& n) { for (auto& x : n) x.pitch -= 12; }); };
    octUpButton.onClick = [this] { transformTake ("Octave up", [] (Notes& n) { for (auto& x : n) x.pitch += 12; }); };
    captureButton.setClickingTogglesState (false);
    captureButton.onClick = [this] { processor.setCapture (! processor.isCapturing()); };
    dragButton.onDragOut = [this] { dragOut (processor.selectedPart, processor.selectedSection); };
    dragButton.onClick = [this]
    {
        const auto f = exchange::writeMidiFile (processor.song(), { processor.selectedPart, processor.selectedSection });
        processor.setStatus ("Drag the DRAG MIDI button onto a track, or a cell, part name or section header. "
                             "Last file: " + f.getFullPathName());
    };
    fromReaperButton.onClick = [this] { importFromReaper(); };
    toReaperButton.onClick = [this] { sendToReaper(); };
    for (auto* c : std::initializer_list<juce::Component*> { &generateButton, &varyButton, &wholePartButton, &wholeSectionButton,
                                                             &takePrev, &takeNext, &deleteTakeButton, &quantButton, &humanButton,
                                                             &octDownButton, &octUpButton, &captureButton, &dragButton,
                                                             &fromReaperButton, &toReaperButton })
        addAndMakeVisible (*c);

    // ---- bottom bar ----
    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (statusLabel);
    cancelButton.onClick = [this] { processor.cancelJobs(); };
    addChildComponent (cancelButton);
    playButton.setClickingTogglesState (true);
    playButton.onClick = [this] { processor.internalPlay = playButton.getToggleState(); };
    loopButton.setClickingTogglesState (true);
    thruButton.setClickingTogglesState (true);
    loopAttachment = std::make_unique<APVTS::ButtonAttachment> (processor.apvts, "loop", loopButton);
    thruAttachment = std::make_unique<APVTS::ButtonAttachment> (processor.apvts, "thru", thruButton);
    outputCombo.onChange = [this]
    {
        auto* prm = processor.apvts.getParameter ("output");
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jmax (0, outputCombo.getSelectedId() - 1)));
    };
    channelOutCombo.addItem ("Own channels", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channelOutCombo.addItem ("All on ch " + juce::String (ch), ch + 1);
    channelOutCombo.onChange = [this]
    {
        auto* prm = processor.apvts.getParameter ("channel");
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) juce::jmax (0, channelOutCombo.getSelectedId() - 1)));
    };
    for (auto* c : std::initializer_list<juce::Component*> { &playButton, &loopButton, &thruButton, &outputCombo, &channelOutCombo })
        addAndMakeVisible (*c);

    processor.songTree.addListener (this);
    applyTheme (settings::get ("theme", bidoo::look::kDefaultLight ? "light" : "dark") != "dark");

    // Resizable like the other Bidoo plugins: drawn scaled from 60 % to 200 %, size remembered.
    setResizable (true, true);
    setResizeLimits (kWidth * 6 / 10, kHeight * 6 / 10, kWidth * 2, kHeight * 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) kWidth / (double) kHeight);
    const double scale = juce::jlimit (0.6, 2.0, settings::get ("editorScale", "1.0").getDoubleValue());
    setSize (juce::roundToInt (kWidth * (scale > 0.0 ? scale : 1.0)), juce::roundToInt (kHeight * (scale > 0.0 ? scale : 1.0)));

    refreshAll();
    startTimerHz (30);
}

DemiurgeEditor::~DemiurgeEditor()
{
    processor.songTree.removeListener (this);
    settings::set ("editorScale", (double) getWidth() / (double) kWidth);
    setLookAndFeel (nullptr);
}

//==============================================================================
//  Refresh
//==============================================================================
juce::String DemiurgeEditor::partLabel (const juce::String& id)
{
    return processor.song().partById (id)[ids::name].toString();
}

juce::String DemiurgeEditor::sectionLabel (const juce::String& id)
{
    return processor.song().sectionById (id)[ids::name].toString();
}

void DemiurgeEditor::setSongProperty (const juce::Identifier& id, const juce::var& v, const juce::String& undoName)
{
    if (processor.songTree[id] == v)
        return;
    processor.undo.beginNewTransaction (undoName);
    processor.songTree.setProperty (id, v, &processor.undo);
}

void DemiurgeEditor::setPartProperty (const juce::Identifier& id, const juce::var& v, const juce::String& undoName)
{
    auto pt = processor.song().partById (processor.selectedPart);
    if (! pt.isValid() || pt[id] == v)
        return;
    if (undoName.isNotEmpty())
        processor.undo.beginNewTransaction (undoName);
    pt.setProperty (id, v, undoName.isNotEmpty() ? &processor.undo : nullptr);
}

void DemiurgeEditor::refreshAll()
{
    refreshSongPanel();
    refreshPartPanel();
    refreshCellPanel();
    refreshUndoButtons();

    // Output part list follows the parts.
    auto s = processor.song();
    outputCombo.clear (juce::dontSendNotification);
    outputCombo.addItem ("All parts", 1);
    for (int p = 0; p < juce::jmin (32, s.numParts()); ++p)
        outputCombo.addItem ("Only " + juce::String (p + 1) + " " + s.part (p)[ids::name].toString(), p + 2);
    auto* out = processor.apvts.getParameter ("output");
    outputCombo.setSelectedId (juce::roundToInt (out->convertFrom0to1 (out->getValue())) + 1, juce::dontSendNotification);
    auto* ch = processor.apvts.getParameter ("channel");
    channelOutCombo.setSelectedId (juce::roundToInt (ch->convertFrom0to1 (ch->getValue())) + 1, juce::dontSendNotification);

    arrangement.repaint();
    repaint();
}

static void setTextIfIdle (juce::TextEditor& e, const juce::String& text)
{
    if (! e.hasKeyboardFocus (true) && e.getText() != text)
        e.setText (text, juce::dontSendNotification);
}

void DemiurgeEditor::refreshSongPanel()
{
    const auto& t = processor.songTree;
    setTextIfIdle (titleEditor, t[ids::title].toString());
    setTextIfIdle (ideaEditor, t[ids::idea].toString());
    setTextIfIdle (styleEditor, t[ids::style].toString());
    keyCombo.setSelectedItemIndex (juce::jlimit (0, 11, (int) t[ids::key]), juce::dontSendNotification);
    scaleCombo.setSelectedItemIndex (scaleNames().indexOf (t[ids::scale].toString()), juce::dontSendNotification);
    tsCombo.setSelectedItemIndex (timeSignatures().indexOf (juce::String ((int) t[ids::tsNum]) + "/" + juce::String ((int) t[ids::tsDen])),
                                  juce::dontSendNotification);
    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue ((double) t[ids::bpm], juce::dontSendNotification);
}

void DemiurgeEditor::refreshPartPanel()
{
    auto pt = processor.song().partById (processor.selectedPart);
    const bool valid = pt.isValid();
    for (auto* c : std::initializer_list<juce::Component*> { &partName, &partDescription, &roleCombo, &channelCombo, &programCombo, &promptEditor })
        c->setEnabled (valid);
    setTextIfIdle (partName, pt[ids::name].toString());
    setTextIfIdle (partDescription, pt[ids::description].toString());
    setTextIfIdle (promptEditor, pt[ids::prompt].toString());
    roleCombo.setSelectedItemIndex (roleNames().indexOf (pt[ids::role].toString()), juce::dontSendNotification);
    channelCombo.setSelectedId (valid ? (int) pt[ids::channel] : 0, juce::dontSendNotification);
    programCombo.setSelectedId (valid ? (int) pt[ids::program] + 1 : 0, juce::dontSendNotification);
}

void DemiurgeEditor::refreshCellPanel()
{
    auto s = processor.song();
    auto pt = s.partById (processor.selectedPart);
    auto sec = s.sectionById (processor.selectedSection);
    const auto sid = processor.selectedSection;
    const int takes = pt.isValid() && sec.isValid() ? s.numTakes (pt, sid) : 0;
    const int active = takes > 0 ? s.activeTakeIndex (pt, sid) : -1;

    cellTitle = pt.isValid() && sec.isValid() ? (pt[ids::name].toString() + "  /  " + sec[ids::name].toString()).toUpperCase()
                                              : juce::String ("CELL");
    if (takes == 0)
        cellInfo = "No take yet";
    else
    {
        auto take = s.activeTake (pt, sid);
        cellInfo = "Take " + juce::String (active + 1) + " of " + juce::String (takes) + "  (" + take[ids::source].toString() + ")";
        if (take[ids::prompt].toString().isNotEmpty() && take[ids::prompt].toString() != "(no prompt)")
            cellInfo << "  \"" << take[ids::prompt].toString().substring (0, 60) << "\"";
    }
    takePrev.setEnabled (takes > 1);
    takeNext.setEnabled (takes > 1);
    deleteTakeButton.setEnabled (takes > 0);
    varyButton.setEnabled (takes > 0);
    const bool drums = roleFromName (pt[ids::role].toString()) == Role::drums || roleFromName (pt[ids::role].toString()) == Role::percussion;
    octDownButton.setEnabled (takes > 0 && ! drums);
    octUpButton.setEnabled (takes > 0 && ! drums);
    quantButton.setEnabled (takes > 0);
    humanButton.setEnabled (takes > 0);
    roll.refresh();
}

void DemiurgeEditor::refreshUndoButtons()
{
    undoButton.setEnabled (processor.undo.canUndo());
    redoButton.setEnabled (processor.undo.canRedo());
    undoButton.setTooltip (processor.undo.getUndoDescription());
    redoButton.setTooltip (processor.undo.getRedoDescription());
}

void DemiurgeEditor::timerCallback()
{
    if (songChanged)
    {
        songChanged = false;
        refreshAll();
    }

    // Status bar
    const bool working = processor.isWorking();
    cancelButton.setVisible (working);
    if (working)
    {
        juce::String text = processor.currentJobLabel() + "...  " + juce::String ((int) processor.currentJobSeconds()) + " s";
        if (processor.jobsWaiting() > 0)
            text << "  (" << processor.jobsWaiting() << " more waiting)";
        statusLabel.setText (text, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, lnf.palette().accent);
        shownStatus = -1;
    }
    else if (shownStatus != processor.statusSerial)
    {
        shownStatus = processor.statusSerial;
        statusLabel.setText (processor.status.isNotEmpty() ? processor.status
                                 : juce::String ("Ready. Describe a song and press PLAN SONG, or select a cell and press GENERATE."),
                             juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, processor.statusIsError ? juce::Colour (0xffe53935) : lnf.palette().textDim);
    }
    for (auto* b : { &planButton, &composeAllButton, &generateButton, &varyButton, &wholePartButton, &wholeSectionButton })
        b->setAlpha (working ? 0.6f : 1.0f);

    captureButton.setToggleState (processor.isCapturing(), juce::dontSendNotification);
    playButton.setToggleState (processor.internalPlay.load(), juce::dontSendNotification);

    // Playhead
    const double pos = processor.livePosition.load();
    if (std::abs (pos - lastPosition) > 1.0e-9)
    {
        lastPosition = pos;
        arrangement.repaint();
        roll.repaint();
    }
}

//==============================================================================
//  Actions
//==============================================================================
void DemiurgeEditor::planSong()
{
    const auto idea = ideaEditor.getText().trim();
    if (idea.isEmpty())
    {
        processor.setStatus ("Write the song idea first (genre, mood, instruments...).", true);
        return;
    }
    DemiurgeProcessor::Job job;
    job.kind = DemiurgeProcessor::Job::Kind::plan;
    job.idea = idea;
    job.label = "Planning the song";
    processor.enqueue (job);
}

void DemiurgeEditor::composeAll()
{
    DemiurgeProcessor::Job job;
    job.kind = DemiurgeProcessor::Job::Kind::composeAll;
    job.label = "Writing every part";
    processor.enqueue (job);
}

void DemiurgeEditor::generate (const juce::String& part, const juce::String& section, bool vary)
{
    auto s = processor.song();
    DemiurgeProcessor::Job job;
    job.kind = DemiurgeProcessor::Job::Kind::compose;
    job.target.vary = vary;

    auto pt = s.partById (part.isNotEmpty() ? part : processor.selectedPart);
    job.target.instruction = pt[ids::prompt].toString().trim();

    if (part.isNotEmpty() && section.isNotEmpty())
    {
        job.target.cells.push_back ({ part, section });
        job.label = (vary ? "Reworking " : "Writing ") + partLabel (part) + " in " + sectionLabel (section);
    }
    else if (part.isNotEmpty())
    {
        for (int i = 0; i < s.numSections(); ++i)
            job.target.cells.push_back ({ part, s.section (i)[ids::id].toString() });
        job.label = "Writing " + partLabel (part) + " for the whole song";
    }
    else
    {
        for (int p = 0; p < s.numParts(); ++p)
            job.target.cells.push_back ({ s.part (p)[ids::id].toString(), section });
        job.label = "Writing every part of " + sectionLabel (section);
    }
    if (job.target.cells.empty())
        return;
    processor.enqueue (job);
}

void DemiurgeEditor::transformTake (const juce::String& what, std::function<void (Notes&)> fn)
{
    auto s = processor.song();
    auto pt = s.partById (processor.selectedPart);
    auto sec = s.sectionById (processor.selectedSection);
    if (! pt.isValid() || ! sec.isValid() || s.numTakes (pt, processor.selectedSection) == 0)
        return;
    auto notes = s.activeNotes (pt, processor.selectedSection);
    fn (notes);
    sanitise (notes, s.sectionLengthBeats (sec));
    processor.undo.beginNewTransaction (what);
    s.setActiveNotes (pt, processor.selectedSection, notes, "hand");
}

void DemiurgeEditor::stepTake (int delta)
{
    auto s = processor.song();
    auto pt = s.partById (processor.selectedPart);
    const int n = s.numTakes (pt, processor.selectedSection);
    if (n < 2)
        return;
    const int next = ((s.activeTakeIndex (pt, processor.selectedSection) + delta) % n + n) % n;
    processor.undo.beginNewTransaction ("Choose take");
    s.setActiveTake (pt, processor.selectedSection, next);
}

void DemiurgeEditor::dragOut (const juce::String& part, const juce::String& section)
{
    const auto f = exchange::writeMidiFile (processor.song(), { part, section });
    if (f.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void DemiurgeEditor::editSection (const juce::String& sectionId)
{
    auto sec = processor.song().sectionById (sectionId);
    if (! sec.isValid())
        return;
    auto* w = new juce::AlertWindow ("Section", "Name, length and chords (one entry per bar, bars separated by |).",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", sec[ids::name].toString(), "Name");
    w->addTextEditor ("bars", juce::String ((int) sec[ids::bars]), "Bars");
    w->addTextEditor ("chords", sec[ids::chords].toString(), "Chords");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DemiurgeEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, sectionId] (int r)
    {
        if (r != 1 || safe == nullptr)
            return;
        auto target = safe->processor.song().sectionById (sectionId);
        if (! target.isValid())
            return;
        auto* um = &safe->processor.undo;
        um->beginNewTransaction ("Edit section");
        target.setProperty (ids::name, w->getTextEditorContents ("name").trim(), um);
        target.setProperty (ids::bars, juce::jlimit (1, 256, w->getTextEditorContents ("bars").getIntValue()), um);
        target.setProperty (ids::chords, w->getTextEditorContents ("chords").trim(), um);
    }), true);
}

void DemiurgeEditor::addSection()
{
    auto s = processor.song();
    processor.undo.beginNewTransaction ("Add section");
    const int at = s.sectionIndex (processor.selectedSection);
    auto sec = s.addSection ("Section " + juce::String (s.numSections() + 1), 8, {}, at >= 0 ? at + 1 : -1);
    processor.setSelection (processor.selectedPart, sec[ids::id].toString());
    editSection (sec[ids::id].toString());
}

void DemiurgeEditor::addPart()
{
    auto* w = new juce::AlertWindow ("Add part", "A name and a role. Describe it in the PART panel afterwards.",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", "New part", "Name");
    w->addComboBox ("role", roleNames(), "Role");
    w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DemiurgeEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
    {
        if (r != 1 || safe == nullptr)
            return;
        auto s = safe->processor.song();
        safe->processor.undo.beginNewTransaction ("Add part");
        const auto role = roleFromName (w->getComboBoxComponent ("role")->getText());
        auto name = w->getTextEditorContents ("name").trim();
        auto pt = s.addPart (name.isNotEmpty() ? name : roleName (role), role);
        safe->processor.setSelection (pt[ids::id].toString(), safe->processor.selectedSection);
        safe->refreshAll();
    }), true);
}

void DemiurgeEditor::showSettings()
{
    auto* w = new juce::AlertWindow ("Claude", "Your Anthropic API key (console.anthropic.com), the model and how hard it thinks.\n"
                                     "The key is kept in " + settings::file().getFullPathName(),
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("key", settings::get ("apiKey"), "API key", true);
    juce::StringArray modelLabels;
    int modelIndex = 0;
    const auto current = processor.modelSettings();
    for (auto& [id, name] : composer::models())
    {
        if (id == current.model)
            modelIndex = modelLabels.size();
        modelLabels.add (name);
    }
    w->addComboBox ("model", modelLabels, "Model");
    w->getComboBoxComponent ("model")->setSelectedItemIndex (modelIndex);
    w->addComboBox ("effort", { "Fast (low effort)", "Balanced (medium effort)", "Best (high effort)" }, "Thinking");
    const juce::StringArray efforts { "low", "medium", "high" };
    w->getComboBoxComponent ("effort")->setSelectedItemIndex (juce::jmax (0, efforts.indexOf (current.effort)));
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<DemiurgeEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, efforts] (int r)
    {
        if (r != 1 || safe == nullptr)
            return;
        settings::set ("apiKey", w->getTextEditorContents ("key").trim());
        const int m = w->getComboBoxComponent ("model")->getSelectedItemIndex();
        if (m >= 0 && m < (int) composer::models().size())
            settings::set ("model", composer::models()[(size_t) m].first);
        settings::set ("effort", efforts[juce::jmax (0, w->getComboBoxComponent ("effort")->getSelectedItemIndex())]);
        safe->processor.setStatus (settings::apiKey().isNotEmpty() ? "Settings saved." : "Settings saved, but there is no API key yet.",
                                   settings::apiKey().isEmpty());
    }), true);
}

void DemiurgeEditor::sendToReaper()
{
    juce::PopupMenu m;
    m.addItem (1, "This cell (" + partLabel (processor.selectedPart) + " in " + sectionLabel (processor.selectedSection) + ")");
    m.addItem (2, "This part, whole song");
    juce::Component::SafePointer<DemiurgeEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&toReaperButton), [safe] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& p = safe->processor;
        juce::String message;
        const bool ok = exchange::writeToReaper (p.song(), { p.selectedPart, r == 1 ? p.selectedSection : juce::String() }, message);
        p.setStatus (message, ! ok);
    });
}

void DemiurgeEditor::importFromReaper()
{
    Notes notes;
    juce::String name;
    const auto error = exchange::readFromReaper (notes, name);
    if (error.isNotEmpty())
    {
        processor.setStatus (error, true);
        return;
    }
    auto s = processor.song();
    auto pt = s.partById (processor.selectedPart);
    if (! pt.isValid())
        return;
    processor.undo.beginNewTransaction ("From Reaper");
    const int takes = s.importAbsolute (pt, notes, "reaper");
    processor.setStatus ("Imported " + juce::String ((int) notes.size()) + " notes from \"" + name + "\" into "
                         + pt[ids::name].toString() + " (" + juce::String (takes) + " sections).");
}

//==============================================================================
//  Presets
//==============================================================================
void DemiurgeEditor::rebuildPresetList (const juce::String& selectName)
{
    presets.refresh();
    presetCombo.clear (juce::dontSendNotification);
    const auto& entries = presets.getEntries();
    bool wroteUserHeading = false;
    presetCombo.addSectionHeading ("Factory");
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (! entries[i].factory && ! wroteUserHeading)
        {
            presetCombo.addSeparator();
            presetCombo.addSectionHeading ("User");
            wroteUserHeading = true;
        }
        presetCombo.addItem (entries[i].name, (int) i + 1);
    }
    const int idx = presets.indexOfName (selectName);
    presetCombo.setSelectedId (idx >= 0 ? idx + 1 : 0, juce::dontSendNotification);
    updatePresetButtons();
}

void DemiurgeEditor::presetChosen()
{
    const int idx = presetCombo.getSelectedId() - 1;
    if (idx < 0)
        return;
    presets.load (idx);
    updatePresetButtons();
    refreshAll();
}

void DemiurgeEditor::stepPreset (int delta)
{
    const int n = (int) presets.getEntries().size();
    if (n == 0)
        return;
    const int current = presetCombo.getSelectedId() - 1;
    int next = current < 0 ? (delta > 0 ? 0 : n - 1) : current + delta;
    next = (next % n + n) % n;
    presetCombo.setSelectedId (next + 1, juce::sendNotificationSync);
}

void DemiurgeEditor::updatePresetButtons()
{
    const int idx = presetCombo.getSelectedId() - 1;
    const auto& entries = presets.getEntries();
    presetDelete.setEnabled (idx >= 0 && idx < (int) entries.size() && ! entries[(size_t) idx].factory);
}

void DemiurgeEditor::showSaveDialog()
{
    auto* w = new juce::AlertWindow ("Save song", "Song name:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", presets.getCurrentName().isNotEmpty() ? presets.getCurrentName() : processor.songTree[ids::title].toString());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<DemiurgeEditor> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int result)
    {
        if (result != 1 || safe == nullptr)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        if (name.isEmpty())
            return;
        if (safe->presets.saveUser (name))
            safe->rebuildPresetList (safe->presets.getCurrentName());
        else
            juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                              .withIconType (juce::MessageBoxIconType::WarningIcon)
                                              .withTitle ("Save song")
                                              .withMessage ("The song could not be saved.\n" + PresetManager::getUserDirectory().getFullPathName())
                                              .withButton ("OK"),
                                          [] (int) {});
    }), true);
}

void DemiurgeEditor::confirmDelete()
{
    const int idx = presetCombo.getSelectedId() - 1;
    const auto& entries = presets.getEntries();
    if (idx < 0 || idx >= (int) entries.size() || entries[(size_t) idx].factory)
        return;
    const auto name = entries[(size_t) idx].name;
    juce::Component::SafePointer<DemiurgeEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete song",
                                                                                "Delete the song \"" + name + "\"?", "Delete", "Cancel", this),
                                  [safe, idx] (int result)
                                  {
                                      if (result != 1 || safe == nullptr)
                                          return;
                                      safe->presets.deleteUser (idx);
                                      safe->rebuildPresetList ({});
                                  });
}

//==============================================================================
//  Theme and layout
//==============================================================================
void DemiurgeEditor::applyTheme (bool light)
{
    lnf.setLight (light);
    const auto& pal = lnf.palette();
    themeToggle.setButtonText (bidoo::look::themeButtonText (light));

    for (auto* l : { &titleLabel, &ideaLabel, &keyLabel, &bpmLabel, &tsLabel, &styleLabel, &partNameLabel, &roleLabel,
                     &channelLabel, &programLabel, &descriptionLabel, &promptLabel })
    {
        l->setColour (juce::Label::textColourId, pal.textDim);
        l->setFont (bidoo::look::font (12.0f, true));
    }
    for (auto* e : { &titleEditor, &ideaEditor, &styleEditor, &partName, &partDescription, &promptEditor })
    {
        e->setColour (juce::TextEditor::backgroundColourId, pal.screen);
        e->setColour (juce::TextEditor::textColourId, pal.text);
        e->setColour (juce::TextEditor::outlineColourId, pal.outline);
        e->setColour (juce::TextEditor::focusedOutlineColourId, pal.accent);
        e->setTextToShowWhenEmpty (e->getTextToShowWhenEmpty(), pal.textDim);
        e->applyColourToAllText (pal.text, true);
    }
    bpmSlider.setColour (juce::Slider::trackColourId, pal.accent.withAlpha (0.18f));
    bpmSlider.setColour (juce::Slider::backgroundColourId, pal.screen);
    bpmSlider.setColour (juce::Slider::textBoxOutlineColourId, pal.outline);
    statusLabel.setFont (bidoo::look::font (13.0f));
    shownStatus = -1;

    sendLookAndFeelChange();
    repaint();
}

void DemiurgeEditor::paint (juce::Graphics& g)
{
    const auto& pal = lnf.palette();
    g.fillAll (pal.background);
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kWidth));

    bidoo::look::drawTitle (g, "DEMIURGE", bidoo::look::kBarMargin, kTopBar / 2, pal);

    auto panel = [&] (juce::Rectangle<int> r, const juce::String& heading)
    {
        g.setColour (pal.panel);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (pal.textDim);
        g.setFont (bidoo::look::font (12.0f, true));
        g.drawText (heading, r.reduced (12, 0).withHeight (30), juce::Justification::centredLeft);
    };
    panel (songPanel, "SONG");
    panel (partPanel, "PART");
    panel (arrangementPanel, "ARRANGEMENT");
    panel (cellPanel, cellTitle);

    // Take info next to the cell title
    g.setColour (pal.text);
    g.setFont (bidoo::look::font (12.5f));
    const int titleW = juce::GlyphArrangement::getStringWidthInt (bidoo::look::font (12.0f, true), cellTitle);
    g.drawText (cellInfo, cellPanel.getX() + 24 + titleW, cellPanel.getY(), 560, 30, juce::Justification::centredLeft, true);

    // Hints under the arrangement heading
    g.setColour (pal.textDim);
    g.setFont (bidoo::look::font (11.5f));
    g.drawText ("Click a cell to select it. Drag a cell, part name or section header onto a DAW track. Right-click for more.",
                arrangementPanel.getX() + 120, arrangementPanel.getY(), 620, 30, juce::Justification::centredLeft, true);
}

void DemiurgeEditor::resized()
{
    layout();
    const auto scale = juce::AffineTransform::scale ((float) getWidth() / (float) kWidth);
    for (auto* c : getChildren())
        if (c != resizableCorner.get())
            c->setTransform (scale);
}

void DemiurgeEditor::layout()
{
    using namespace bidoo::look;
    const juce::Rectangle<int> bounds (kWidth, kHeight);

    // ---- top bar: DEMIURGE | < songs > SAVE DELETE | UNDO REDO ......... KEY | theme ----
    auto top = bounds.withHeight (kTopBar).reduced (kBarMargin, 0);
    top.removeFromLeft (titleWidth ("DEMIURGE") + kBarGroupGap);
    presetPrev.setBounds (barSlot (top, kArrowWidth));
    presetCombo.setBounds (barSlot (top, kPresetListWidth));
    presetNext.setBounds (barSlot (top, kArrowWidth, false, 2 * kBarGap));
    presetSave.setBounds (barSlot (top, kSaveWidth));
    presetDelete.setBounds (barSlot (top, kDeleteWidth, false, kBarGroupGap));
    undoButton.setBounds (barSlot (top, 60));
    redoButton.setBounds (barSlot (top, 60));
    themeToggle.setBounds (barSlot (top, kThemeWidth, true, kBarGroupGap));
    keyButton.setBounds (barSlot (top, 60, true));

    // ---- panels ----
    auto body = bounds.withTrimmedTop (kTopBar).reduced (12, 0).withTrimmedBottom (8);
    auto bottomBar = body.removeFromBottom (32);
    body.removeFromBottom (8);
    auto rowA = body.removeFromTop (392);
    body.removeFromTop (8);
    auto rowB = body;

    songPanel = rowA.removeFromLeft (300);
    rowA.removeFromLeft (8);
    arrangementPanel = rowA;
    partPanel = rowB.removeFromLeft (300);
    rowB.removeFromLeft (8);
    cellPanel = rowB;

    auto labelRow = [] (juce::Rectangle<int>& area, juce::Label& l, juce::Component& c, int h = 26, int labelW = 52)
    {
        auto r = area.removeFromTop (h);
        l.setBounds (r.removeFromLeft (labelW));
        c.setBounds (r);
        area.removeFromTop (6);
    };

    // Song panel
    {
        auto a = songPanel.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (12);
        labelRow (a, titleLabel, titleEditor);
        ideaLabel.setBounds (a.removeFromTop (18));
        ideaEditor.setBounds (a.removeFromTop (92));
        a.removeFromTop (6);
        auto buttons = a.removeFromTop (30);
        planButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
        buttons.removeFromLeft (6);
        composeAllButton.setBounds (buttons);
        a.removeFromTop (8);
        auto keyRow = a.removeFromTop (26);
        keyLabel.setBounds (keyRow.removeFromLeft (52));
        keyCombo.setBounds (keyRow.removeFromLeft (64));
        keyRow.removeFromLeft (6);
        scaleCombo.setBounds (keyRow);
        a.removeFromTop (6);
        auto tempoRow = a.removeFromTop (26);
        bpmLabel.setBounds (tempoRow.removeFromLeft (52));
        bpmSlider.setBounds (tempoRow.removeFromLeft (110));
        tempoRow.removeFromLeft (6);
        tsLabel.setBounds (tempoRow.removeFromLeft (40));
        tsCombo.setBounds (tempoRow);
        a.removeFromTop (6);
        styleLabel.setBounds (a.removeFromTop (18));
        styleEditor.setBounds (a);
    }

    // Part panel
    {
        auto a = partPanel.reduced (12, 0).withTrimmedTop (30).withTrimmedBottom (12);
        labelRow (a, partNameLabel, partName, 26, 60);
        auto roleRow = a.removeFromTop (26);
        roleLabel.setBounds (roleRow.removeFromLeft (60));
        roleCombo.setBounds (roleRow.removeFromLeft (100));
        roleRow.removeFromLeft (6);
        channelCombo.setBounds (roleRow);
        channelLabel.setBounds ({});
        a.removeFromTop (6);
        labelRow (a, programLabel, programCombo, 26, 60);
        descriptionLabel.setBounds (a.removeFromTop (18));
        partDescription.setBounds (a);
    }

    // Arrangement panel
    {
        auto a = arrangementPanel.reduced (10, 0).withTrimmedBottom (10);
        auto heading = a.removeFromTop (30);
        addPartButton.setBounds (heading.removeFromRight (74).reduced (0, 3));
        heading.removeFromRight (6);
        addSectionButton.setBounds (heading.removeFromRight (90).reduced (0, 3));
        arrangement.setBounds (a);
    }

    // Cell panel
    {
        auto a = cellPanel.reduced (10, 0).withTrimmedBottom (10);
        auto heading = a.removeFromTop (30);
        deleteTakeButton.setBounds (heading.removeFromRight (100).reduced (0, 3));
        heading.removeFromRight (6);
        takeNext.setBounds (heading.removeFromRight (kArrowWidth).reduced (0, 3));
        heading.removeFromRight (4);
        takePrev.setBounds (heading.removeFromRight (kArrowWidth).reduced (0, 3));

        auto side = a.removeFromRight (312);
        a.removeFromRight (10);
        roll.setBounds (a);

        auto rowOf = [&side] (std::initializer_list<juce::Component*> cs, int h = 28)
        {
            auto r = side.removeFromTop (h);
            const int n = (int) cs.size(), w = (r.getWidth() - (n - 1) * 6) / n;
            for (auto* c : cs)
            {
                c->setBounds (r.removeFromLeft (w));
                r.removeFromLeft (6);
            }
            side.removeFromTop (6);
        };
        promptLabel.setBounds (side.removeFromTop (18));
        promptEditor.setBounds (side.removeFromTop (70));
        side.removeFromTop (8);
        rowOf ({ &generateButton, &varyButton });
        rowOf ({ &wholePartButton, &wholeSectionButton });
        side.removeFromTop (4);
        rowOf ({ &quantButton, &humanButton, &octDownButton, &octUpButton }, 26);
        rowOf ({ &captureButton, &dragButton }, 26);
        rowOf ({ &fromReaperButton, &toReaperButton }, 26);
    }

    // Bottom bar: status ............ CANCEL | PLAY LOOP THRU | output | channel
    {
        auto b = bottomBar.reduced (0, 2);
        channelOutCombo.setBounds (b.removeFromRight (130));
        b.removeFromRight (6);
        outputCombo.setBounds (b.removeFromRight (170));
        b.removeFromRight (kBarGroupGap);
        thruButton.setBounds (b.removeFromRight (60));
        b.removeFromRight (4);
        loopButton.setBounds (b.removeFromRight (60));
        b.removeFromRight (4);
        playButton.setBounds (b.removeFromRight (60));
        b.removeFromRight (kBarGroupGap);
        cancelButton.setBounds (b.removeFromRight (76));
        b.removeFromRight (8);
        statusLabel.setBounds (b);
    }
}
