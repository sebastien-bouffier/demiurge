#include "TopBar.h"

namespace bidoo::kit
{
namespace
{
constexpr int kLogoGap = 10;  // logo | divider | name
constexpr int kCpuWidth = 52; // "CPU 100 %"
} // namespace

TopBar::TopBar (SoundState& s, PresetManager& pm, Options o) : sound (s), presets (pm), options (std::move (o))
{
    presetCombo.setTextWhenNothingSelected ("Select preset...");
    presetCombo.onChange = [this] { presetChosen(); };
    presetCombo.beforePopup = [this] { rebuildPresetList (presets.getCurrentName()); };
    presetCombo.onNameClick = [this] { showPresetBrowser (! isPresetBrowserOpen()); };
    presetPrev.onClick = [this] { stepPreset (-1); };
    presetNext.onClick = [this] { stepPreset (+1); };
    presetSave.setTooltip ("Save the current settings as a user preset (name, folder and tags)");
    presetSave.onClick = [this]
    {
        // the browser's folder, else the current user preset's
        juce::String folder;
        if (isPresetBrowserOpen())
            folder = browser->getSaveFolder();
        else if (const int idx = currentPresetIndex(); idx >= 0 && ! presets.getEntries()[(size_t) idx].factory)
            folder = presets.getEntries()[(size_t) idx].folder;
        showSaveDialog (folder);
    };
    for (auto* c : std::initializer_list<juce::Component*> { &presetPrev, &presetCombo, &presetNext, &presetSave })
        addAndMakeVisible (*c);
    rebuildPresetList (presets.getCurrentName());
    lastPresetName = presets.getCurrentName();

    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    addAndMakeVisible (undoButton);
    addAndMakeVisible (redoButton);
    abButton.onClick = [this]
    {
        sound.switchAb();
        showCurrentPreset();
    };
    abCopy.onClick = [this] { sound.copyAbToOther(); };
    addChildComponent (abButton);
    addChildComponent (abCopy);
    abButton.setVisible (options.abCompare);
    abCopy.setVisible (options.abCompare);
    updateHistoryButtons();

    themeToggle.setTooltip ("Dark or light theme, for every instance");
    themeToggle.onClick = [this]
    {
        if (onThemeClicked)
            onThemeClicked (! light);
    };
    addAndMakeVisible (themeToggle);

    startTimerHz (30);
}

TopBar::~TopBar()
{
    stopTimer();
    if (host != nullptr)
    {
        host->removeKeyListener (&historyKeys);
        host->removeMouseListener (&pointerWatcher);
        if (browser != nullptr)
            host->removeChildComponent (browser.get());
    }
}

void TopBar::attachTo (juce::Component& editor)
{
    jassert (host == nullptr);
    host = &editor;
    host->addMouseListener (&pointerWatcher, true);
    host->addKeyListener (&historyKeys);
    host->setWantsKeyboardFocus (true);
}

void TopBar::setBrowserArea (juce::Rectangle<int> area)
{
    browserArea = area;
    if (browser != nullptr)
    {
        browser->setBounds (area);
        browser->setAnchorX (getX() + presetPrev.getX() - area.getX());
    }
}

void TopBar::applyTheme (const bidoo::look::Palette& p, bool isLight)
{
    pal = &p;
    light = isLight;
    if (browser != nullptr)
        browser->applyTheme (p);
    const int modified = shownModified;
    shownModified = -1;
    showPresetModified (modified == 1);
    themeToggle.setButtonText (bidoo::look::themeButtonText (light));
    repaint();
}

// ---------------------------------------------------------------------------------------------
//  Layout and painting
// ---------------------------------------------------------------------------------------------
juce::Rectangle<int> TopBar::logoArea() const
{
    const int w = juce::roundToInt (bidoo::look::kLogoBarHeight * bidoo::look::kLogoAspect);
    return { bidoo::look::kBarMargin, (getHeight() - bidoo::look::kLogoBarHeight) / 2, w, bidoo::look::kLogoBarHeight };
}

juce::Rectangle<int> TopBar::titleArea() const
{
    const int x = logoArea().getRight() + 2 * kLogoGap + 1;
    if (! showTitle)
        return { logoArea().getRight(), 0, 0, getHeight() };
    const int w = drawTitle != nullptr && titleWidth > 0 ? titleWidth : bidoo::look::titleWidth (options.title);
    return { x, 0, w, getHeight() };
}

// What a narrow window gives up when the row is too short for everything (the extras' width
// first, see Options::extrasWidth): the version, then some of the preset list, then the name
// beside the logo, then more of the list, then the CPU meter.
void TopBar::fitToWidth()
{
    using namespace bidoo::look;
    showVersion = options.version.isNotEmpty();
    showTitle = true;
    showCpu = options.cpuLoad != nullptr;
    listWidth = kPresetListWidth;

    auto needed = [this]
    {
        const int left = titleArea().getRight() - kBarMargin + kBarGroupGap
                         + kArrowWidth + kBarGap + listWidth + kBarGap + kArrowWidth + 2 * kBarGap + kSaveWidth + kBarGroupGap
                         + kHistoryWidth + kBarGap + kHistoryWidth
                         + (options.abCompare ? 6 + kAbWidth + kBarGap + kAbCopyWidth + kBarGroupGap : kBarGroupGap);
        const int right = kThemeWidth + kBarGap + (showVersion ? kVersionWidth : 0)
                          + (showCpu ? kCpuWidth + kBarGroupGap : kBarGroupGap - kBarGap);
        return left + right + options.extrasWidth;
    };
    const int available = getWidth() - 2 * kBarMargin;
    auto shrinkList = [&] (int minimum)
    {
        if (const int over = needed() - available; over > 0)
            listWidth = juce::jmax (minimum, listWidth - over);
    };
    if (needed() > available)
        showVersion = false;
    shrinkList (150);
    if (needed() > available)
    {
        showTitle = false;
        listWidth = juce::jmin (kPresetListWidth, listWidth + juce::jmax (0, available - needed())); // the room back
    }
    shrinkList (110);
    if (needed() > available)
        showCpu = false;
}

void TopBar::resized()
{
    using namespace bidoo::look;
    fitToWidth();
    auto row = getLocalBounds().reduced (kBarMargin, 0);
    row.removeFromLeft (titleArea().getRight() - kBarMargin + kBarGroupGap);
    presetPrev.setBounds (barSlot (row, kArrowWidth));
    presetCombo.setBounds (barSlot (row, listWidth));
    presetNext.setBounds (barSlot (row, kArrowWidth, false, 2 * kBarGap));
    presetSave.setBounds (barSlot (row, kSaveWidth, false, kBarGroupGap));
    undoButton.setBounds (barSlot (row, kHistoryWidth));
    redoButton.setBounds (barSlot (row, kHistoryWidth, false, options.abCompare ? 6 : kBarGroupGap));
    if (options.abCompare)
    {
        abButton.setBounds (barSlot (row, kAbWidth));
        abCopy.setBounds (barSlot (row, kAbCopyWidth, false, kBarGroupGap));
    }
    themeToggle.setBounds (barSlot (row, kThemeWidth, true, kBarGap));
    versionArea = showVersion ? barSlot (row, kVersionWidth, true, 0) : juce::Rectangle<int>();
    cpuArea = showCpu ? barSlot (row, kCpuWidth, true, kBarGroupGap) : juce::Rectangle<int>();
    if (! showCpu)
        row.removeFromRight (kBarGroupGap - kBarGap);
    if (layoutExtras)
    {
        // the extras take their slots in the editor's coordinates (the bar sits at its top left)
        auto extras = row.translated (getX(), getY());
        layoutExtras (extras);
    }
    if (browser != nullptr)
        browser->setAnchorX (getX() + presetPrev.getX() - browserArea.getX());
}

void TopBar::paint (juce::Graphics& g)
{
    using namespace bidoo::look;
    const auto& p = *pal;
    drawLogo (g, logoArea().toFloat(), p, p.background);
    if (showTitle)
    {
        g.setColour (p.textDim.withAlpha (0.45f));
        g.fillRect (logoArea().getRight() + kLogoGap, getHeight() / 2 - 9, 1, 18);
        const auto title = titleArea();
        if (drawTitle != nullptr)
            drawTitle (g, title);
        else
            bidoo::look::drawTitle (g, options.title, title.getX(), getHeight() / 2, p);
    }

    g.setColour (p.textDim);
    g.setFont (font (10.0f));
    if (showVersion)
        g.drawText ("v" + options.version, versionArea.withTrimmedRight (6), juce::Justification::centredRight, false);
    if (! cpuArea.isEmpty() && shownCpu >= 0.0f)
        g.drawText ("CPU " + juce::String (juce::roundToInt (shownCpu * 100.0f)) + " %", cpuArea, juce::Justification::centredRight, false);
}

void TopBar::timerCallback()
{
    const auto name = presets.getCurrentName();
    if (name != lastPresetName)
    {
        // a name changed elsewhere (undo, a host restoring a state, a program change)
        lastPresetName = name;
        selectInList (name);
        if (browser != nullptr)
            browser->setCurrent (currentPresetIndex(), true);
        if (onPresetLoaded)
            onPresetLoaded();
    }
    showPresetModified (name.isNotEmpty() && sound.isPresetModified());
    keepBrowserOnTop();

    sound.syncUndo();
    updateHistoryButtons();

    if (options.cpuLoad != nullptr && ++cpuTicks >= 8) // about 4 times a second
    {
        cpuTicks = 0;
        const float load = juce::jmax (0.0f, options.cpuLoad());
        smoothedCpu = shownCpu < 0.0f ? load : smoothedCpu + 0.5f * (load - smoothedCpu);
        if (shownCpu < 0.0f || juce::roundToInt (smoothedCpu * 100.0f) != juce::roundToInt (shownCpu * 100.0f))
        {
            shownCpu = smoothedCpu;
            repaint (cpuArea);
        }
    }
}

// ---------------------------------------------------------------------------------------------
//  Undo, A/B
// ---------------------------------------------------------------------------------------------
void TopBar::undo()
{
    sound.undo();
    updateHistoryButtons();
    if (onUndoRedo)
        onUndoRedo();
}

void TopBar::redo()
{
    sound.redo();
    updateHistoryButtons();
    if (onUndoRedo)
        onUndoRedo();
}

void TopBar::updateHistoryButtons()
{
    undoButton.setEnabled (sound.canUndo());
    redoButton.setEnabled (sound.canRedo());
    if (options.abCompare && shownAb != sound.getActiveAb())
    {
        shownAb = sound.getActiveAb();
        const juce::String active = shownAb == 0 ? "A" : "B", other = shownAb == 0 ? "B" : "A";
        abButton.setTooltip ("Compare: settings " + active + " are active, click to switch to " + other);
        abCopy.setTooltip ("Copy settings " + active + " to " + other);
        abButton.repaint();
        abCopy.repaint();
    }
}

bool TopBar::HistoryKeys::keyPressed (const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    if (! mods.isCommandDown() || mods.isAltDown())
        return false;
    const auto code = juce::CharacterFunctions::toLowerCase ((juce::juce_wchar) key.getKeyCode());
    if (code == 'z')
    {
        if (mods.isShiftDown())
            bar.redo();
        else
            bar.undo();
    }
    else if (code == 'y' && ! mods.isShiftDown())
        bar.redo();
    else
        return false;
    return true;
}

TopBar::AbButton::AbButton (SoundState& s) : juce::Button ("A/B"), sound (s) {}

void TopBar::AbButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);
    auto r = getLocalBounds().reduced (3);
    const int active = sound.getActiveAb();
    g.setFont (bidoo::look::buttonFont ((float) getHeight()));
    for (int i = 0; i < 2; ++i)
    {
        const auto half = i == 0 ? r.removeFromLeft (r.getWidth() / 2) : r;
        if (i == active)
        {
            g.setColour (findColour (juce::TextButton::buttonOnColourId));
            g.fillRoundedRectangle (half.toFloat(), 4.0f);
        }
        g.setColour (findColour (i == active ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)
                         .withMultipliedAlpha (i == active ? 1.0f : 0.55f));
        g.drawText (i == 0 ? "A" : "B", half, juce::Justification::centred, false);
    }
}

TopBar::AbCopyButton::AbCopyButton (SoundState& s) : juce::Button ("Copy A/B"), sound (s) {}

void TopBar::AbCopyButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);
    const bool fromA = sound.getActiveAb() == 0;
    const auto c = getLocalBounds().toFloat().getCentre();
    g.setColour (findColour (juce::TextButton::textColourOffId));
    g.setFont (bidoo::look::buttonFont ((float) getHeight()));
    const auto box = juce::Rectangle<float> (12.0f, (float) getHeight());
    g.drawText (fromA ? "A" : "B", box.withCentre ({ c.x - 10.0f, c.y }), juce::Justification::centred, false);
    g.drawText (fromA ? "B" : "A", box.withCentre ({ c.x + 10.0f, c.y }), juce::Justification::centred, false);
    juce::Path arrow;
    arrow.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
    g.fillPath (arrow);
}

// After an A/B switch: the preset list shows the slot's preset, and the plugin refreshes as after a load.
void TopBar::showCurrentPreset()
{
    lastPresetName = presets.getCurrentName();
    selectInList (lastPresetName);
    updateHistoryButtons();
    if (browser != nullptr)
        browser->setCurrent (currentPresetIndex(), true);
    if (onPresetLoaded)
        onPresetLoaded();
}

// ---------------------------------------------------------------------------------------------
//  The preset name
// ---------------------------------------------------------------------------------------------
void TopBar::PresetBox::showPopup()
{
    if (beforePopup)
        beforePopup();
    juce::ComboBox::showPopup();
}

void TopBar::PresetBox::mouseDown (const juce::MouseEvent& e)
{
    const auto local = e.getEventRelativeTo (this);
    if (e.mods.isPopupMenu())
        showPopup();
    else if (onNameClick && isEnabled() && local.x < getWidth() - kArrowZone)
        onNameClick();
    else
        juce::ComboBox::mouseDown (e);
}

void TopBar::PresetBox::paintOverChildren (juce::Graphics& g)
{
    // the line between the name (the browser) and the arrow (the quick list)
    g.setColour (findColour (juce::ComboBox::outlineColourId).withMultipliedAlpha (0.8f));
    g.fillRect (getWidth() - kArrowZone, 6, 1, getHeight() - 12);
}

// The current preset's place in the list: the one last loaded or saved from it while its name is
// still the current one (two folders may hold the same name), else the first of that name.
int TopBar::currentPresetIndex() const
{
    const auto name = presets.getCurrentName();
    if (name.isEmpty())
        return -1;
    const int byKey = presets.indexOfKey (currentKey);
    if (byKey >= 0 && presets.getEntries()[(size_t) byKey].name == name)
        return byKey;
    return presets.indexOfName (name);
}

// The preset in the list, or its name alone when the list doesn't have it (a file opened from
// elsewhere, settings pasted from a preset saved on another computer).
void TopBar::selectInList (const juce::String& presetName)
{
    const int idx = presetName == presets.getCurrentName() ? currentPresetIndex() : presets.indexOfName (presetName);
    if (idx >= 0 || presetName.isEmpty())
        presetCombo.setSelectedId (idx + 1, juce::dontSendNotification);
    else
        presetCombo.setText (presetName, juce::dontSendNotification);
}

void TopBar::showPresetModified (bool modified)
{
    if ((modified ? 1 : 0) == shownModified)
        return;
    shownModified = modified ? 1 : 0;
    presetCombo.setColour (juce::ComboBox::textColourId, modified ? pal->textDim : pal->text);
    presetCombo.setTooltip (juce::String (modified ? "Changed since the preset was loaded or saved. " : "")
                            + "Click the name to browse the presets (folders, tags, search, favourites); the arrow opens "
                              "the quick list, with copy / paste, open a preset file and the default settings at the top");
}

// ---------------------------------------------------------------------------------------------
//  The quick list
// ---------------------------------------------------------------------------------------------
void TopBar::rebuildPresetList (const juce::String& selectName)
{
    presets.refresh();
    presetCombo.clear (juce::dontSendNotification);
    auto& menu = *presetCombo.getRootMenu();
    juce::Component::SafePointer<TopBar> safe (this);
    menu.addItem ("Browse presets...", [safe]
                  {
                      if (safe != nullptr)
                          safe->showPresetBrowser (true);
                  });
    addSharedActions (menu);
    if (addPresetActions)
        addPresetActions (menu);
    menu.addSeparator();

    // each preset with its MIDI program number at the right (counted from 0, in list order)
    const auto& entries = presets.getEntries();
    auto item = [&entries] (size_t i)
    {
        juce::PopupMenu::Item it (entries[i].name);
        it.itemID = (int) i + 1;
        it.shortcutKeyDescription = PresetManager::programLabel ((int) i);
        return it;
    };
    // factory: the presets without a category, then each category (the browser's order)
    juce::StringArray groups { juce::String() };
    groups.addArray (presets.getFactoryCategories());
    for (const auto& category : groups)
    {
        bool headed = false;
        for (size_t i = 0; i < entries.size() && entries[i].factory; ++i)
            if (entries[i].folder == category)
            {
                if (! headed)
                    menu.addSectionHeader (category.isEmpty() ? juce::String ("Factory") : "Factory / " + category);
                headed = true;
                menu.addItem (item (i));
            }
    }

    // user presets: the top folder's in the list, each subfolder as a submenu
    int next = 0;
    while (next < (int) entries.size() && entries[(size_t) next].factory)
        ++next;
    if (next < (int) entries.size() || ! presets.getUserFolders().isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader ("User");
        addPresetItems (menu, {}, next);
    }

    // the starred ones again, last (the list finds a preset by its first item)
    juce::PopupMenu favourites;
    for (size_t i = 0; i < entries.size(); ++i)
        if (presets.isFavourite (entries[i]))
            favourites.addItem (item (i));
    if (favourites.getNumItems() > 0)
    {
        menu.addSeparator();
        menu.addSubMenu ("Favourites", favourites);
    }
    selectInList (selectName);
    if (browser != nullptr)
    {
        browser->rebuild();
        browser->setCurrent (currentPresetIndex(), false);
    }
}

// The user presets of `folder` from entry `next` on (the list order: a folder's presets, then its
// subfolders), each subfolder as a submenu. `next` ends past the folder's last preset.
void TopBar::addPresetItems (juce::PopupMenu& menu, const juce::String& folder, int& next)
{
    const auto& entries = presets.getEntries();
    while (next < (int) entries.size() && entries[(size_t) next].folder == folder)
    {
        juce::PopupMenu::Item it (entries[(size_t) next].name);
        it.itemID = next + 1;
        it.shortcutKeyDescription = PresetManager::programLabel (next);
        menu.addItem (std::move (it));
        ++next;
    }
    for (const auto& sub : presets.getUserFolders())
    {
        const bool child = sub.startsWith (folder.isEmpty() ? juce::String() : folder + "/")
                           && ! sub.substring (folder.isEmpty() ? 0 : folder.length() + 1).containsChar ('/');
        if (! child)
            continue;
        juce::PopupMenu subMenu; // the subfolder's presets come next in the list
        addPresetItems (subMenu, sub, next);
        if (subMenu.getNumItems() == 0)
            subMenu.addItem ("(empty)", false, false, [] {});
        menu.addSubMenu (sub.fromLastOccurrenceOf ("/", false, false), subMenu);
    }
}

// The actions above the presets (items without an id: the list's selection stays as it is).
void TopBar::addSharedActions (juce::PopupMenu& menu)
{
    juce::Component::SafePointer<TopBar> safe (this);
    menu.addItem ("Copy settings", [safe]
                  {
                      if (safe != nullptr)
                          safe->presets.copyToClipboard();
                  });
    menu.addItem ("Paste settings", presets.clipboardHasSettings(), false, [safe]
                  {
                      if (safe != nullptr)
                          safe->pasteSettings();
                  });
    menu.addItem ("Open preset file...", [safe]
                  {
                      if (safe != nullptr)
                          safe->openPresetFile();
                  });
    menu.addItem ("Save as default", [safe]
                  {
                      if (safe == nullptr)
                          return;
                      if (! safe->presets.saveAsDefault())
                          juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                            .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                            .withTitle ("Save as default")
                                                            .withMessage ("The default could not be saved.\n"
                                                                          + SoundState::getUserDefaultFile().getFullPathName())
                                                            .withButton ("OK"),
                                                        [] (int) {});
                  });
    menu.addItem ("Restore built-in default", PresetManager::hasUserDefault(), false, [] { PresetManager::restoreBuiltInDefault(); });
}

void TopBar::pasteSettings()
{
    if (presets.pasteFromClipboard())
        settingsLoaded();
}

void TopBar::settingsLoaded()
{
    lastPresetName = presets.getCurrentName();
    selectInList (lastPresetName);
    updateHistoryButtons();
    if (onPresetLoaded)
        onPresetLoaded();
}

// A preset file of this plugin from any folder (one sent by someone, a backup...): loaded like a
// user preset, named after the file.
void TopBar::openPresetFile()
{
    auto folder = PresetManager::getUserDirectory();
    if (! folder.isDirectory())
        folder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    fileChooser = std::make_unique<juce::FileChooser> ("Open preset file", folder, "*" + PresetManager::getExtension());
    juce::Component::SafePointer<TopBar> safe (this);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (safe == nullptr || file == juce::File())
                                      return;
                                  if (safe->presets.loadFile (file))
                                  {
                                      safe->settingsLoaded();
                                      return;
                                  }
                                  juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                                    .withTitle ("Open preset file")
                                                                    .withMessage ("This file is not a preset of this plugin.\n"
                                                                                  + file.getFullPathName())
                                                                    .withButton ("OK")
                                                                    .withAssociatedComponent (safe.getComponent()),
                                                                [] (int) {});
                              });
}

// ---------------------------------------------------------------------------------------------
//  Loading, saving
// ---------------------------------------------------------------------------------------------
void TopBar::refreshPresets() { rebuildPresetList (presets.getCurrentName()); }

void TopBar::presetChosen()
{
    const int idx = presetCombo.getSelectedId() - 1;
    if (idx >= 0)
        loadPreset (idx);
}

// From the list, the arrows or the browser: one undo step.
void TopBar::loadPreset (int index)
{
    const auto& entries = presets.getEntries();
    if (index < 0 || index >= (int) entries.size())
        return;
    currentKey = PresetManager::keyOf (entries[(size_t) index]);
    presets.load (index);
    lastPresetName = presets.getCurrentName();
    selectInList (lastPresetName);
    if (browser != nullptr)
        browser->setCurrent (currentPresetIndex(), true);
    if (onPresetLoaded)
        onPresetLoaded();
    keepBrowserOnTop();
}

void TopBar::presetListChanged() { rebuildPresetList (presets.getCurrentName()); }

void TopBar::stepPreset (int delta)
{
    const int n = (int) presets.getEntries().size();
    if (n == 0)
        return;
    const int current = presetCombo.getSelectedId() - 1;
    int next = current < 0 ? (delta > 0 ? 0 : n - 1) : current + delta;
    next = (next % n + n) % n;
    presetCombo.setSelectedId (next + 1, juce::sendNotificationSync);
}

// The name, the user folder to save into (the one given first) and the tags (the current preset's).
void TopBar::showSaveDialog (const juce::String& folder)
{
    auto* w = new juce::AlertWindow ("Save preset", "Preset name:", juce::MessageBoxIconType::NoIcon, host != nullptr ? host : this);
    w->addTextEditor ("name", presets.getCurrentName());
    juce::StringArray folderNames { "User" };
    const auto folders = presets.getUserFolders();
    for (const auto& f : folders)
        folderNames.add ("User / " + f.replace ("/", " / "));
    w->addComboBox ("folder", folderNames, "Folder:");
    w->getComboBoxComponent ("folder")->setSelectedItemIndex (folders.indexOf (folder) + 1, juce::dontSendNotification);
    juce::StringArray currentTags;
    if (const int idx = currentPresetIndex(); idx >= 0)
        currentTags = presets.getEntries()[(size_t) idx].tags;
    w->addTextEditor ("tags", joinTags (currentTags), "Tags (separated by commas):");
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TopBar> safeThis (this);
    w->enterModalState (true,
                        juce::ModalCallbackFunction::create ([safeThis, w, folders] (int result)
                        {
                            if (result != 1 || safeThis == nullptr)
                                return;
                            const auto name = w->getTextEditorContents ("name").trim();
                            const auto tags = parseTags (w->getTextEditorContents ("tags"));
                            const int choice = w->getComboBoxComponent ("folder")->getSelectedItemIndex();
                            const auto target = choice > 0 ? folders[choice - 1] : juce::String();
                            if (name.isEmpty())
                                return;
                            if (! safeThis->presets.userPresetExists (name, target))
                            {
                                safeThis->saveUserPreset (name, target, tags);
                                return;
                            }
                            juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (
                                                              juce::MessageBoxIconType::QuestionIcon, "Save preset",
                                                              "There is already a preset called \"" + safeThis->presets.finalSaveName (name)
                                                                  + "\" in this folder. Replace it?",
                                                              "Replace", "Cancel", safeThis.getComponent()),
                                                          [safeThis, name, target, tags] (int replace)
                                                          {
                                                              if (replace == 1 && safeThis != nullptr)
                                                                  safeThis->saveUserPreset (name, target, tags);
                                                          });
                        }),
                        true);
}

void TopBar::saveUserPreset (const juce::String& name, const juce::String& folder, const juce::StringArray& tags)
{
    if (presets.saveUser (name, folder, tags))
    {
        PresetManager::Entry saved;
        saved.name = presets.getCurrentName();
        saved.factory = false;
        saved.folder = folder;
        currentKey = PresetManager::keyOf (saved);
        lastPresetName = presets.getCurrentName();
        presetListChanged();
        if (browser != nullptr)
            browser->setCurrent (currentPresetIndex(), true);
        return;
    }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle ("Save preset")
                                      .withMessage ("The preset could not be saved.\n"
                                                    + PresetManager::userFolderFile (folder).getFullPathName())
                                      .withButton ("OK"),
                                  [] (int) {});
}

// ---------------------------------------------------------------------------------------------
//  The browser
// ---------------------------------------------------------------------------------------------
// A plugin may bring its own panels to the front at any time (a band selected by a preset load,
// a value box): the open browser stays above everything but the editor's resize corner.
void TopBar::keepBrowserOnTop()
{
    if (! isPresetBrowserOpen() || host == nullptr)
        return;
    const auto& children = host->getChildren();
    for (int i = children.indexOf (browser.get()) + 1; i < children.size(); ++i)
        if (children[i]->isVisible() && dynamic_cast<juce::ResizableCornerComponent*> (children[i]) == nullptr)
        {
            browser->toFront (false);
            for (auto* c : host->getChildren())
                if (dynamic_cast<juce::ResizableCornerComponent*> (c) != nullptr)
                    c->toFront (false);
            return;
        }
}

void TopBar::showPresetBrowser (bool show)
{
    if (host == nullptr)
        return;
    if (show && browser == nullptr)
    {
        browser = std::make_unique<PresetBrowser> (presets);
        browser->onLoad = [this] (int index) { loadPreset (index); };
        browser->onClose = [this] { showPresetBrowser (false); };
        browser->onSave = [this] (const juce::String& folder) { showSaveDialog (folder); };
        browser->onPresetsChanged = [this] { presetListChanged(); };
        browser->onKeyChanged = [this] (const juce::String& oldKey, const juce::String& newKey)
        {
            // a renamed or moved preset, or folder ("user/a/"), that may be the current one
            if (currentKey == oldKey)
                currentKey = newKey;
            else if (oldKey.endsWithChar ('/') && currentKey.startsWith (oldKey))
                currentKey = newKey + currentKey.substring (oldKey.length());
        };
        browser->applyTheme (*pal);
        host->addChildComponent (*browser);
        setBrowserArea (browserArea);
        if (onBrowserCreated)
            onBrowserCreated (*browser);
    }
    if (browser == nullptr)
        return;
    if (show)
    {
        rebuildPresetList (presets.getCurrentName()); // presets and stars saved meanwhile (other instances too)
        browser->setCurrent (currentPresetIndex(), true);
        browser->setVisible (true);
        browser->toFront (false);
        keepBrowserOnTop();
        browser->focusSearch();
    }
    else if (browser->isVisible())
    {
        browser->setVisible (false);
        if (host->isShowing())
            host->grabKeyboardFocus();
    }
}
} // namespace bidoo::kit
