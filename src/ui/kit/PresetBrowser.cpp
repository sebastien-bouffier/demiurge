#include "PresetBrowser.h"

namespace bidoo::kit
{
namespace
{
constexpr int kPad = 12, kHeaderH = 30, kFooterH = 28, kSideWidth = 210, kListHeaderH = 26;

juce::String countText (int n) { return n == 1 ? juce::String ("1 preset") : juce::String (n) + " presets"; }
} // namespace

// =============================================================================================
//  RowList
// =============================================================================================
PresetBrowser::RowList::RowList (PresetBrowser& owner, bool presetRows) : browser (owner), presets (presetRows)
{
    setWantsKeyboardFocus (false);
    setMouseClickGrabsKeyboardFocus (false);
}

int PresetBrowser::RowList::rowAt (int y) const
{
    const int n = presets ? (int) browser.rows.size() : (int) browser.nodes.size();
    const int row = y / kRowHeight;
    return y >= 0 && row < n ? row : -1;
}

void PresetBrowser::RowList::paint (juce::Graphics& g)
{
    const int n = presets ? (int) browser.rows.size() : (int) browser.nodes.size();
    const auto clip = g.getClipBounds();
    const int first = juce::jmax (0, clip.getY() / kRowHeight), last = juce::jmin (n - 1, clip.getBottom() / kRowHeight);
    for (int row = first; row <= last; ++row)
    {
        const juce::Rectangle<int> r (0, row * kRowHeight, getWidth(), kRowHeight);
        if (presets)
            browser.paintPresetRow (g, row, r, row == hoverRow, row == hoverRow && hoverStar, row == hoverRow && hoverPlay);
        else
            browser.paintNodeRow (g, row, r, row == hoverRow);
    }
}

void PresetBrowser::RowList::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    const bool star = presets && row >= 0 && starArea ({ 0, 0, getWidth(), kRowHeight }).contains (e.x, kRowHeight / 2);
    const bool play = presets && row >= 0 && browser.inPlayArea (e.x);
    if (row != hoverRow || star != hoverStar || play != hoverPlay)
    {
        hoverRow = row;
        hoverStar = star;
        hoverPlay = play;
        repaint();
    }
}

void PresetBrowser::RowList::mouseExit (const juce::MouseEvent&)
{
    hoverRow = -1;
    hoverStar = hoverPlay = false;
    repaint();
}

void PresetBrowser::RowList::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (presets)
        browser.presetClicked (row, e);
    else
        browser.nodeClicked (row, e);
}

void PresetBrowser::RowList::mouseUp (const juce::MouseEvent&)
{
    if (presets)
        browser.presetDropped();
    browser.focusSearch();
}

void PresetBrowser::RowList::mouseDrag (const juce::MouseEvent& e)
{
    if (presets)
        browser.presetDragged (e);
}

void PresetBrowser::RowList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (presets && row >= 0 && ! e.mods.isPopupMenu() && ! starArea ({ 0, 0, getWidth(), kRowHeight }).contains (e.x, kRowHeight / 2)
        && ! browser.inPlayArea (e.x))
        browser.loadRow (row, true);
}

// =============================================================================================
//  TagStrip
// =============================================================================================
int PresetBrowser::TagStrip::layoutFor (int width)
{
    chips.clear();
    const auto font = bidoo::look::font (12.0f, true);
    int x = 0, row = 0;
    for (const auto& t : browser.allTags)
    {
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, t)) + 20;
        if (x > 0 && x + w > width)
        {
            if (++row >= kMaxRows)
                break;
            x = 0;
        }
        chips.push_back ({ { x, row * (kChipHeight + kGap), juce::jmin (w, width), kChipHeight }, t });
        x += w + kGap;
    }
    return chips.empty() ? 0 : (row + 1 - (row >= kMaxRows ? 1 : 0)) * (kChipHeight + kGap) - kGap;
}

int PresetBrowser::TagStrip::chipAt (juce::Point<int> p) const
{
    for (size_t i = 0; i < chips.size(); ++i)
        if (chips[i].first.contains (p))
            return (int) i;
    return -1;
}

void PresetBrowser::TagStrip::paint (juce::Graphics& g)
{
    const auto& p = *browser.palette;
    g.setFont (bidoo::look::font (12.0f, true));
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const auto r = chips[i].first.toFloat().reduced (0.5f);
        const bool on = browser.selectedTags.contains (chips[i].second, true);
        g.setColour (on ? p.accent : (hover == (int) i ? p.well : p.screen));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (on ? p.accent : p.outline);
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (on ? p.onAccent : p.text);
        g.drawText (chips[i].second, chips[i].first.reduced (8, 0), juce::Justification::centred, true);
    }
}

void PresetBrowser::TagStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = chipAt (e.getPosition());
    if (h != hover)
    {
        hover = h;
        repaint();
    }
}

void PresetBrowser::TagStrip::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void PresetBrowser::TagStrip::mouseDown (const juce::MouseEvent& e)
{
    if (const int i = chipAt (e.getPosition()); i >= 0)
        browser.toggleTag (chips[(size_t) i].second);
    browser.focusSearch();
}

void PresetBrowser::CloseButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), highlighted, down);
    const auto c = getLocalBounds().toFloat().getCentre();
    const float s = 4.5f;
    g.setColour (findColour (juce::TextButton::textColourOffId));
    g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 1.6f);
    g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.6f);
}

// =============================================================================================
//  PresetBrowser
// =============================================================================================
PresetBrowser::PresetBrowser (PresetManager& manager) : presets (manager)
{
    setWantsKeyboardFocus (true);

    search.setTextToShowWhenEmpty ("Search presets", juce::Colours::grey);
    search.setFont (bidoo::look::font (14.0f));
    search.setIndents (28, 0);
    search.setJustification (juce::Justification::centredLeft);
    search.setTooltip ("Type to search the names, folders and tags. Up / Down select, Enter loads, Esc closes");
    search.onTextChange = [this]
    {
        query = search.getText().trim();
        filter();
    };
    search.onKey = [this] (const juce::KeyPress& key) { return keyPressed (key); };
    addAndMakeVisible (search);

    newFolderButton.setTooltip ("A new folder in the selected user folder");
    newFolderButton.onClick = [this]
    {
        const auto& n = nodes[(size_t) selectedNode];
        newFolder (n.kind == NodeKind::userFolder ? n.folder : juce::String());
    };
    saveButton.setTooltip ("Save the current settings as a user preset, in the selected user folder");
    saveButton.onClick = [this]
    {
        if (onSave)
            onSave (getSaveFolder());
    };
    renameButton.setTooltip ("Rename the selected user preset (F2)");
    renameButton.onClick = [this]
    {
        if (selectedRow >= 0)
            renamePreset (rows[(size_t) selectedRow]);
    };
    deleteButton.setTooltip ("Delete the selected user preset (Delete)");
    deleteButton.onClick = [this]
    {
        if (selectedRow >= 0)
            deletePreset (rows[(size_t) selectedRow]);
    };
    closeButton.setTooltip ("Close (Esc)");
    closeButton.onClick = [this]
    {
        if (onClose)
            onClose();
    };
    for (auto* b : std::initializer_list<juce::Component*> { &newFolderButton, &saveButton, &renameButton, &deleteButton, &closeButton })
    {
        b->setWantsKeyboardFocus (false);
        b->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }

    for (auto* v : { &nodeView, &presetView })
    {
        v->setWantsKeyboardFocus (false);
        v->setScrollBarsShown (true, false);
        v->setScrollBarThickness (8);
        addAndMakeVisible (*v);
    }
    nodeView.setViewedComponent (&nodeList, false);
    presetView.setViewedComponent (&presetList, false);
    tagStrip.setTooltip ("Tags: click to show only the presets that have every lit tag");
    tagStrip.setWantsKeyboardFocus (false);
    tagStrip.setMouseClickGrabsKeyboardFocus (false);
    addAndMakeVisible (tagStrip);

    rebuild();
}

PresetBrowser::~PresetBrowser()
{
    nodeView.setViewedComponent (nullptr, false);
    presetView.setViewedComponent (nullptr, false);
}

void PresetBrowser::applyTheme (const bidoo::look::Palette& p)
{
    palette = &p;
    search.setColour (juce::TextEditor::backgroundColourId, p.screen);
    search.setColour (juce::TextEditor::textColourId, p.text);
    search.setColour (juce::TextEditor::outlineColourId, p.outline);
    search.setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
    search.setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.35f));
    search.setColour (juce::CaretComponent::caretColourId, p.accent);
    search.setTextToShowWhenEmpty ("Search presets", p.textDim);
    search.applyColourToAllText (p.text);
    for (auto* v : { &nodeView, &presetView })
    {
        v->getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, p.outline);
        v->getVerticalScrollBar().setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    }
    repaint();
}

void PresetBrowser::setAnchorX (int x)
{
    anchorX = x;
    resized();
}

juce::Rectangle<int> PresetBrowser::panelBounds() const
{
    const int w = juce::jmin (kPanelWidth, getWidth() - 2 * kPad), h = juce::jmin (kPanelHeight, getHeight() - 2 * kPad);
    const int x = juce::jlimit (kPad, juce::jmax (kPad, getWidth() - kPad - w), anchorX);
    return { x, 6, w, h };
}

void PresetBrowser::resized()
{
    auto r = panelBounds().reduced (kPad);
    auto header = r.removeFromTop (kHeaderH);
    closeButton.setBounds (header.removeFromRight (kHeaderH));
    header.removeFromRight (bidoo::look::kBarGroupGap);
    saveButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (bidoo::look::kBarGap);
    newFolderButton.setBounds (header.removeFromRight (104));
    header.removeFromRight (bidoo::look::kBarGroupGap);
    search.setBounds (header);
    r.removeFromTop (10);
    if (const int tagsH = tagStrip.layoutFor (r.getWidth()); tagsH > 0)
    {
        tagStrip.setBounds (r.removeFromTop (tagsH));
        r.removeFromTop (10);
    }
    else
        tagStrip.setBounds ({});

    footer = r.removeFromBottom (kFooterH);
    auto buttons = footer;
    deleteButton.setBounds (buttons.removeFromRight (72));
    buttons.removeFromRight (bidoo::look::kBarGap);
    renameButton.setBounds (buttons.removeFromRight (76));
    r.removeFromBottom (10);

    nodeWell = r.removeFromLeft (kSideWidth);
    r.removeFromLeft (10);
    presetWell = r;
    nodeView.setBounds (nodeWell.reduced (1, 6));
    presetHeader = presetWell.withHeight (kListHeaderH);
    presetView.setBounds (presetWell.withTrimmedTop (kListHeaderH).reduced (1, 4));
    layoutLists();
}

void PresetBrowser::layoutLists()
{
    nodeList.setSize (nodeView.getMaximumVisibleWidth(), (int) nodes.size() * RowList::kRowHeight);
    // the scrollbar only when needed, without changing the rows' width
    const int presetH = (int) rows.size() * RowList::kRowHeight;
    presetList.setSize (presetView.getWidth() - (presetH > presetView.getHeight() ? presetView.getScrollBarThickness() : 0), presetH);
    nodeList.repaint();
    presetList.repaint();
}

void PresetBrowser::visibilityChanged()
{
    if (isVisible())
        focusSearch();
}

void PresetBrowser::focusSearch()
{
    if (isShowing())
        search.grabKeyboardFocus();
}

// ---------------------------------------------------------------------------------------------
//  Folders and rows
// ---------------------------------------------------------------------------------------------
void PresetBrowser::buildNodes()
{
    const auto& entries = presets.getEntries();
    int factoryCount = 0;
    for (const auto& e : entries)
        factoryCount += e.factory ? 1 : 0;

    nodes.clear();
    nodes.push_back ({ NodeKind::favourites, "Favourites", {}, 0, presets.countFavourites() });
    nodes.push_back ({ NodeKind::all, "All presets", {}, 0, (int) entries.size() });
    nodes.push_back ({ NodeKind::factory, "Factory", {}, 0, factoryCount });
    for (const auto& c : presets.getFactoryCategories())
    {
        int n = 0;
        for (const auto& e : entries)
            n += e.factory && e.folder == c ? 1 : 0;
        nodes.push_back ({ NodeKind::category, c, c, 1, n });
    }
    nodes.push_back ({ NodeKind::user, "User", {}, 0, (int) entries.size() - factoryCount });
    for (const auto& f : presets.getUserFolders())
    {
        const int depth = f.retainCharacters ("/").length() + 1;
        nodes.push_back ({ NodeKind::userFolder, f.fromLastOccurrenceOf ("/", false, false), f, depth, presets.countInFolder (f) });
    }
}

bool PresetBrowser::inNode (const PresetManager::Entry& e, const Node& n) const
{
    switch (n.kind)
    {
        case NodeKind::favourites: return presets.isFavourite (e);
        case NodeKind::all: return true;
        case NodeKind::factory: return e.factory;
        case NodeKind::category: return e.factory && e.folder == n.folder;
        case NodeKind::user: return ! e.factory;
        case NodeKind::userFolder: return ! e.factory && PresetManager::isInFolder (e.folder, n.folder);
    }
    return false;
}

// What a row shows of its folder: what the selected folder doesn't say already.
juce::String PresetBrowser::rowFolderText (const PresetManager::Entry& e) const
{
    const auto& n = nodes[(size_t) selectedNode];
    switch (n.kind)
    {
        case NodeKind::favourites:
        case NodeKind::all: return PresetManager::folderLabel (e);
        case NodeKind::factory: return e.folder;
        case NodeKind::category: return {};
        case NodeKind::user: return e.folder.replace ("/", " / ");
        case NodeKind::userFolder:
            return e.folder == n.folder ? juce::String() : e.folder.substring (n.folder.length() + 1).replace ("/", " / ");
    }
    return {};
}

void PresetBrowser::rebuild()
{
    // keep the folder (by what it is) and the selected preset (by key)
    const auto oldNode = selectedNode >= 0 && selectedNode < (int) nodes.size() ? nodes[(size_t) selectedNode] : Node { NodeKind::all, {}, {}, 0, 0 };
    const auto& entries = presets.getEntries();
    const auto oldKey = selectedRow >= 0 && selectedRow < (int) rows.size() && rows[(size_t) selectedRow] < (int) entries.size()
                            ? PresetManager::keyOf (entries[(size_t) rows[(size_t) selectedRow]])
                            : juce::String();
    buildNodes();
    allTags = presets.getAllTags();
    for (int i = selectedTags.size(); --i >= 0;)
        if (! allTags.contains (selectedTags[i], true))
            selectedTags.remove (i);
    resized();
    selectedNode = 1;
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].kind == oldNode.kind && nodes[i].folder == oldNode.folder)
            selectedNode = (int) i;
    filter();
    if (oldKey.isNotEmpty())
    {
        const int idx = presets.indexOfKey (oldKey);
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == idx)
                selectRow ((int) i, false);
    }
}

void PresetBrowser::filter()
{
    const auto& entries = presets.getEntries();
    const int keepEntry = selectedRow >= 0 && selectedRow < (int) rows.size() ? rows[(size_t) selectedRow] : currentEntry;
    rows.clear();
    const auto& n = nodes[(size_t) selectedNode];
    for (size_t i = 0; i < entries.size(); ++i)
        if (inNode (entries[i], n) && PresetManager::matches (entries[i], query) && PresetManager::hasTags (entries[i], selectedTags))
            rows.push_back ((int) i);

    // the same preset stays selected when it is still there; else the current one, else the first
    selectedRow = -1;
    for (int want : { keepEntry, currentEntry })
        for (size_t i = 0; i < rows.size() && selectedRow < 0; ++i)
            if (rows[i] == want && want >= 0)
                selectedRow = (int) i;
    if (selectedRow < 0 && query.isNotEmpty() && ! rows.empty())
        selectedRow = 0;
    layoutLists();
    if (selectedRow >= 0)
        scrollToRow (selectedRow);
    else
        presetView.setViewPosition (0, 0);
    updateButtons();
    repaint();
}

void PresetBrowser::selectNode (int node)
{
    if (node < 0 || node >= (int) nodes.size())
        return;
    selectedNode = node;
    const int top = node * RowList::kRowHeight;
    const auto pos = nodeView.getViewPosition();
    if (top < pos.y)
        nodeView.setViewPosition (0, top);
    else if (top + RowList::kRowHeight > pos.y + nodeView.getHeight())
        nodeView.setViewPosition (0, top + RowList::kRowHeight - nodeView.getHeight());
    filter();
}

bool PresetBrowser::selectFolder (const juce::String& folderOrLabel)
{
    for (size_t i = 0; i < nodes.size(); ++i)
        if ((nodes[i].kind == NodeKind::userFolder && nodes[i].folder == folderOrLabel)
            || (nodes[i].kind != NodeKind::userFolder && nodes[i].label == folderOrLabel))
        {
            selectNode ((int) i);
            return true;
        }
    return false;
}

void PresetBrowser::setSearchText (const juce::String& text)
{
    search.setText (text, false);
    query = text.trim();
    filter();
}

void PresetBrowser::selectRow (int row, bool scroll)
{
    selectedRow = rows.empty() ? -1 : juce::jlimit (0, (int) rows.size() - 1, row);
    if (scroll && selectedRow >= 0)
        scrollToRow (selectedRow);
    updateButtons();
    presetList.repaint();
}

void PresetBrowser::scrollToRow (int row)
{
    const int top = row * RowList::kRowHeight, h = presetView.getHeight();
    const auto pos = presetView.getViewPosition();
    if (top < pos.y)
        presetView.setViewPosition (0, top);
    else if (top + RowList::kRowHeight > pos.y + h)
        presetView.setViewPosition (0, top + RowList::kRowHeight - h);
}

void PresetBrowser::setCurrent (int index, bool select)
{
    currentEntry = index;
    if (select)
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == index)
                selectRow ((int) i);
    presetList.repaint();
}

void PresetBrowser::loadRow (int row, bool close)
{
    if (row < 0 || row >= (int) rows.size())
        return;
    selectRow (row);
    const int entry = rows[(size_t) row];
    // a double-click's first click loaded it already: no second undo step
    if (onLoad && ! (close && entry == currentEntry))
        onLoad (entry);
    currentEntry = entry;
    presetList.repaint();
    if (close && onClose)
        onClose();
}

void PresetBrowser::updateButtons()
{
    const auto& entries = presets.getEntries();
    const bool user = selectedRow >= 0 && ! entries[(size_t) rows[(size_t) selectedRow]].factory;
    renameButton.setEnabled (user);
    deleteButton.setEnabled (user);
}

juce::String PresetBrowser::getSaveFolder() const
{
    if (selectedNode >= 0 && selectedNode < (int) nodes.size())
    {
        const auto& n = nodes[(size_t) selectedNode];
        if (n.kind == NodeKind::userFolder)
            return n.folder;
        if (n.kind == NodeKind::user)
            return {};
    }
    const auto& entries = presets.getEntries();
    if (selectedRow >= 0 && selectedRow < (int) rows.size())
        if (const auto& e = entries[(size_t) rows[(size_t) selectedRow]]; ! e.factory)
            return e.folder;
    return {};
}

// ---------------------------------------------------------------------------------------------
//  Painting
// ---------------------------------------------------------------------------------------------
juce::Path PresetBrowser::starPath (juce::Rectangle<float> r)
{
    juce::Path star;
    const auto c = r.getCentre();
    const float outer = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, inner = outer * 0.45f;
    for (int i = 0; i < 10; ++i)
    {
        const float a = juce::MathConstants<float>::pi * (float) i / 5.0f;
        const float rad = (i % 2) == 0 ? outer : inner;
        const juce::Point<float> pt (c.x + rad * std::sin (a), c.y - rad * std::cos (a) + outer * 0.08f);
        if (i == 0)
            star.startNewSubPath (pt);
        else
            star.lineTo (pt);
    }
    star.closeSubPath();
    return star;
}

juce::Path PresetBrowser::folderPath (juce::Rectangle<float> r)
{
    juce::Path p;
    const float tabW = r.getWidth() * 0.42f, tabH = r.getHeight() * 0.2f;
    p.startNewSubPath (r.getX(), r.getY() + tabH);
    p.lineTo (r.getX(), r.getY());
    p.lineTo (r.getX() + tabW, r.getY());
    p.lineTo (r.getX() + tabW + tabH, r.getY() + tabH);
    p.lineTo (r.getRight(), r.getY() + tabH);
    p.lineTo (r.getRight(), r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.closeSubPath();
    return p;
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto& p = *palette;
    // the plugin stays visible, dimmed, around the panel
    g.fillAll (p.background.withAlpha (p.light ? 0.6f : 0.72f));

    const auto panel = panelBounds();
    juce::DropShadow (juce::Colours::black.withAlpha (p.light ? 0.18f : 0.5f), 18, { 0, 6 }).drawForRectangle (g, panel);
    g.setColour (p.panel);
    g.fillRoundedRectangle (panel.toFloat(), 8.0f);
    g.setColour (p.outline);
    g.drawRoundedRectangle (panel.toFloat().reduced (0.5f), 8.0f, 1.0f);

    for (const auto& well : { nodeWell, presetWell })
    {
        g.setColour (p.screen);
        g.fillRoundedRectangle (well.toFloat(), 6.0f);
        g.setColour (p.outline);
        g.drawRoundedRectangle (well.toFloat().reduced (0.5f), 6.0f, 1.0f);
    }

    // the list's heading: the folder, how many presets, the program column
    const auto& n = nodes[(size_t) selectedNode];
    auto head = presetHeader.reduced (12, 0);
    g.setFont (bidoo::look::font (11.0f, true));
    g.setColour (p.textDim);
    g.drawText ("PGM", head.removeFromRight (40), juce::Justification::centredRight, false);
    juce::String heading = n.kind == NodeKind::userFolder ? "User / " + n.folder.replace ("/", " / ") : n.label;
    const bool narrowed = query.isNotEmpty() || ! selectedTags.isEmpty();
    heading = heading.toUpperCase() + "   " + (narrowed ? juce::String ((int) rows.size()) + " of " + countText (n.count)
                                                         : countText ((int) rows.size()));
    g.drawText (heading, head.withTrimmedLeft (20), juce::Justification::centredLeft, true);
    g.setColour (p.outline);
    g.fillRect (presetHeader.getX() + 1, presetHeader.getBottom() - 1, presetHeader.getWidth() - 2, 1);

    // nothing to show: say why
    if (rows.empty())
    {
        juce::String why;
        if (query.isNotEmpty())
            why = "No preset matches \"" + query + "\" here.";
        else if (! selectedTags.isEmpty())
            why = "No preset here has every lit tag.";
        else if (n.kind == NodeKind::favourites)
            why = "No favourites yet: click the star of a preset to add it here.";
        else if (n.kind == NodeKind::user || n.kind == NodeKind::userFolder)
            why = "No presets in this folder yet: SAVE stores the current settings here.";
        g.setColour (p.textDim);
        g.setFont (bidoo::look::font (13.0f));
        g.drawFittedText (why, presetView.getBounds().reduced (24, 0).withHeight (60), juce::Justification::centred, 2);
    }

    // footer: the keys
    g.setColour (p.textDim);
    g.setFont (bidoo::look::font (11.5f));
    const juce::String keys (juce::CharPointer_UTF8 ("Click loads, double-click loads and closes  \xc2\xb7  \xe2\x86\x91\xe2\x86\x93 select  \xc2\xb7  "
                                                     "Enter load  \xc2\xb7  \xe2\x86\x90\xe2\x86\x92 folder  \xc2\xb7  drag to a folder to move  \xc2\xb7  right-click for tags"));
    g.drawText (onPreview != nullptr ? juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6 listens  \xc2\xb7  ")) + keys : keys,
                footer.withTrimmedRight (160), juce::Justification::centredLeft, true);
}

void PresetBrowser::paintOverChildren (juce::Graphics& g)
{
    // the magnifier in the search box
    const auto sb = search.getBounds().toFloat();
    const juce::Rectangle<float> lens (sb.getX() + 10.0f, sb.getCentreY() - 6.0f, 9.0f, 9.0f);
    g.setColour (palette->textDim);
    g.drawEllipse (lens, 1.5f);
    g.drawLine (lens.getRight() - 1.5f, lens.getBottom() - 1.5f, lens.getRight() + 3.0f, lens.getBottom() + 3.0f, 1.6f);

    // the preset being dragged, by the pointer
    if (dragEntry >= 0 && dragEntry < (int) presets.getEntries().size())
    {
        const auto& p = *palette;
        const auto text = presets.getEntries()[(size_t) dragEntry].name;
        const auto font = bidoo::look::font (12.5f, true);
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, text)) + 20;
        const auto tag = juce::Rectangle<int> (dragPos.x + 12, dragPos.y - 11, w, 22).toFloat();
        g.setColour (p.well.withAlpha (0.95f));
        g.fillRoundedRectangle (tag, 4.0f);
        g.setColour (dropNode >= 0 ? p.accent : p.outline);
        g.drawRoundedRectangle (tag.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (p.text);
        g.setFont (font);
        g.drawText (text, tag, juce::Justification::centred, true);
    }
}

void PresetBrowser::paintNodeRow (juce::Graphics& g, int row, juce::Rectangle<int> r, bool hover)
{
    const auto& p = *palette;
    const auto& n = nodes[(size_t) row];
    const bool selected = row == selectedNode;
    const bool group = n.kind == NodeKind::favourites || n.kind == NodeKind::all || n.kind == NodeKind::factory || n.kind == NodeKind::user;

    auto area = r.reduced (5, 1);
    if (selected || hover)
    {
        g.setColour (selected ? p.well : p.well.withAlpha (0.5f));
        g.fillRoundedRectangle (area.toFloat(), 4.0f);
    }
    if (selected)
    {
        g.setColour (p.accent);
        g.fillRoundedRectangle (area.toFloat().withWidth (3.0f).reduced (0.0f, 4.0f), 1.5f);
    }
    if (row == dropNode)
    {
        g.setColour (p.accent.withAlpha (0.18f));
        g.fillRoundedRectangle (area.toFloat(), 4.0f);
        g.setColour (p.accent);
        g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.2f);
    }

    area.removeFromLeft (10 + 14 * n.depth);
    const auto icon = area.removeFromLeft (14).toFloat().withSizeKeepingCentre (13.0f, 11.0f);
    if (n.kind == NodeKind::favourites)
    {
        g.setColour (p.accent);
        g.fillPath (starPath (icon.expanded (1.0f)));
    }
    else if (n.kind == NodeKind::all)
    {
        g.setColour (p.textDim);
        for (int i = 0; i < 3; ++i)
            g.fillRoundedRectangle (icon.getX(), icon.getY() + 1.0f + (float) i * 4.0f, icon.getWidth(), 1.6f, 0.8f);
    }
    else
    {
        g.setColour (p.textDim.withAlpha (group ? 1.0f : 0.8f));
        g.strokePath (folderPath (icon), juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    area.removeFromLeft (8);

    g.setFont (bidoo::look::font (11.5f));
    g.setColour (p.textDim);
    g.drawText (juce::String (n.count), area.removeFromRight (34), juce::Justification::centredRight, false);
    g.setFont (bidoo::look::font (13.0f, group));
    g.setColour (selected ? p.text : (group ? p.text : p.text.withAlpha (0.85f)));
    g.drawText (n.label, area, juce::Justification::centredLeft, true);
}

void PresetBrowser::paintPresetRow (juce::Graphics& g, int row, juce::Rectangle<int> r, bool hover, bool starHover, bool playHover)
{
    const auto& p = *palette;
    const int entry = rows[(size_t) row];
    const auto& e = presets.getEntries()[(size_t) entry];
    const bool selected = row == selectedRow, current = entry == currentEntry;

    auto area = r.reduced (5, 1);
    if (selected || hover)
    {
        g.setColour (selected ? p.well : p.well.withAlpha (0.5f));
        g.fillRoundedRectangle (area.toFloat(), 4.0f);
    }
    if (current)
    {
        g.setColour (p.accent);
        g.fillRoundedRectangle (area.toFloat().withWidth (3.0f).reduced (0.0f, 4.0f), 1.5f);
    }

    const bool fav = presets.isFavourite (e);
    const auto star = starArea (area).toFloat().withSizeKeepingCentre (13.0f, 13.0f).translated (3.0f, 0.0f);
    const auto sp = starPath (star);
    if (fav)
    {
        g.setColour (p.accent.withMultipliedAlpha (starHover ? 0.8f : 1.0f));
        g.fillPath (sp);
    }
    else
    {
        g.setColour (starHover ? p.accent : p.textDim.withAlpha (hover || selected ? 0.8f : 0.4f));
        g.strokePath (sp, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (onPreview != nullptr)
    {
        // the audition button: a small play triangle
        const auto box = playArea (area).toFloat().withSizeKeepingCentre (10.0f, 11.0f).translated (2.0f, 0.0f);
        juce::Path tri;
        tri.addTriangle (box.getX(), box.getY(), box.getRight(), box.getCentreY(), box.getX(), box.getBottom());
        g.setColour (playHover ? p.accent : p.textDim.withAlpha (hover || selected ? 0.8f : 0.4f));
        g.fillPath (tri);
        area.removeFromLeft (playArea (area).getRight() - area.getX());
    }
    else
        area.removeFromLeft (starArea (area).getWidth() + 6);

    g.setFont (bidoo::look::font (11.5f));
    g.setColour (p.textDim);
    g.drawText (PresetManager::programLabel (entry), area.removeFromRight (40).withTrimmedRight (7), juce::Justification::centredRight, false);

    auto folderText = rowFolderText (e);
    if (! e.tags.isEmpty())
        folderText = (folderText.isEmpty() ? juce::String() : folderText + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")))
                     + e.tags.joinIntoString (", ");
    const auto nameFont = bidoo::look::font (13.5f, current);
    if (folderText.isNotEmpty())
    {
        const int nameW = juce::roundToInt (juce::GlyphArrangement::getStringWidth (nameFont, e.name)) + 16;
        const int folderW = juce::jmin (area.getWidth() / 2, juce::jmax (0, area.getWidth() - nameW));
        g.setFont (bidoo::look::font (12.0f));
        g.setColour (p.textDim);
        g.drawText (folderText, area.removeFromRight (folderW).withTrimmedRight (10), juce::Justification::centredRight, true);
    }
    g.setFont (nameFont);
    g.setColour (current ? p.accent : p.text);
    g.drawText (e.name, area, juce::Justification::centredLeft, true);
}

// ---------------------------------------------------------------------------------------------
//  Mouse and keys
// ---------------------------------------------------------------------------------------------
void PresetBrowser::mouseDown (const juce::MouseEvent& e)
{
    // outside the panel: close
    if (! panelBounds().contains (e.getPosition()))
    {
        if (onClose)
            onClose();
        return;
    }
    focusSearch();
}

void PresetBrowser::presetClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0)
        return;
    const int entry = rows[(size_t) row];
    if (e.mods.isPopupMenu())
    {
        selectRow (row, false);
        presetMenu (row);
        return;
    }
    const auto local = e.getEventRelativeTo (&presetList).getPosition();
    if (starArea ({ 0, row * RowList::kRowHeight, presetList.getWidth(), RowList::kRowHeight }).contains (local.x, row * RowList::kRowHeight + 1))
    {
        toggleFavourite (entry);
        return;
    }
    if (inPlayArea (local.x))
    {
        selectRow (row, false);
        onPreview (entry);
        return;
    }
    if (e.getNumberOfClicks() > 1)
        return; // the double-click does the rest
    loadRow (row, false);
}

void PresetBrowser::presetDragged (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        return;
    if (dragEntry < 0)
    {
        const int row = presetList.rowAt (e.getMouseDownY());
        if (row < 0 || e.getDistanceFromDragStart() < 6 || presets.getEntries()[(size_t) rows[(size_t) row]].factory)
            return;
        dragEntry = rows[(size_t) row];
        presetList.setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    dragPos = e.getEventRelativeTo (this).getPosition();

    // over a user folder other than the preset's own
    int target = -1;
    if (nodeView.getBounds().contains (dragPos))
    {
        const int row = nodeList.rowAt (e.getEventRelativeTo (&nodeList).getPosition().y);
        if (row >= 0)
        {
            const auto& n = nodes[(size_t) row];
            const auto& from = presets.getEntries()[(size_t) dragEntry];
            if ((n.kind == NodeKind::user || n.kind == NodeKind::userFolder) && n.folder != from.folder)
                target = row;
        }
    }
    if (target != dropNode)
    {
        dropNode = target;
        nodeList.repaint();
    }
    repaint();
}

bool PresetBrowser::presetDropped()
{
    if (dragEntry < 0)
        return false;
    const int entry = std::exchange (dragEntry, -1), node = std::exchange (dropNode, -1);
    presetList.setMouseCursor (juce::MouseCursor::NormalCursor);
    nodeList.repaint();
    repaint();
    if (node >= 0)
        movePreset (entry, nodes[(size_t) node].folder);
    return true;
}

void PresetBrowser::nodeClicked (int row, const juce::MouseEvent& e)
{
    if (row < 0)
        return;
    selectNode (row);
    if (e.mods.isPopupMenu())
        nodeMenu (row);
}

bool PresetBrowser::keyPressed (const juce::KeyPress& key)
{
    const bool searchEmpty = search.getText().isEmpty();
    const int page = juce::jmax (1, presetView.getHeight() / RowList::kRowHeight - 1);
    if (key == juce::KeyPress::escapeKey)
    {
        if (! searchEmpty)
            search.clear();
        else if (onClose)
            onClose();
        query = search.getText().trim();
        filter();
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey || key == juce::KeyPress::pageUpKey
        || key == juce::KeyPress::pageDownKey)
    {
        if (rows.empty())
            return true;
        const int step = key == juce::KeyPress::upKey ? -1 : key == juce::KeyPress::downKey ? 1 : key == juce::KeyPress::pageUpKey ? -page : page;
        selectRow (selectedRow < 0 ? (step > 0 ? 0 : (int) rows.size() - 1) : selectedRow + step);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        loadRow (selectedRow, false);
        return true;
    }
    if (searchEmpty && (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey))
    {
        selectNode (selectedNode + (key == juce::KeyPress::leftKey ? -1 : 1));
        return true;
    }
    if (selectedRow >= 0 && ! presets.getEntries()[(size_t) rows[(size_t) selectedRow]].factory)
    {
        if (key == juce::KeyPress::F2Key)
        {
            renamePreset (rows[(size_t) selectedRow]);
            return true;
        }
        if (searchEmpty && key == juce::KeyPress::deleteKey)
        {
            deletePreset (rows[(size_t) selectedRow]);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
//  Menus and actions
// ---------------------------------------------------------------------------------------------
void PresetBrowser::presetMenu (int row)
{
    const int entry = rows[(size_t) row];
    const auto e = presets.getEntries()[(size_t) entry];
    juce::Component::SafePointer<PresetBrowser> safe (this);
    auto act = [safe] (std::function<void (PresetBrowser&)> fn)
    {
        return [safe, fn]
        {
            if (safe != nullptr)
                fn (*safe);
        };
    };

    juce::PopupMenu menu;
    menu.addSectionHeader (e.name);
    menu.addItem ("Load", act ([row] (PresetBrowser& b) { b.loadRow (row, false); }));
    menu.addItem (presets.isFavourite (e) ? "Remove from favourites" : "Add to favourites",
                  act ([entry] (PresetBrowser& b) { b.toggleFavourite (entry); }));
    if (! e.factory)
    {
        menu.addSeparator();
        menu.addItem ("Tags...", act ([entry] (PresetBrowser& b) { b.editTags (entry); }));
        menu.addItem ("Rename...", act ([entry] (PresetBrowser& b) { b.renamePreset (entry); }));
        juce::PopupMenu move;
        move.addItem ("User", e.folder.isNotEmpty(), e.folder.isEmpty(), act ([entry] (PresetBrowser& b) { b.movePreset (entry, {}); }));
        for (const auto& f : presets.getUserFolders())
            move.addItem ("User / " + f.replace ("/", " / "), f != e.folder, f == e.folder,
                          act ([entry, f] (PresetBrowser& b) { b.movePreset (entry, f); }));
        menu.addSubMenu ("Move to", move);
        menu.addItem ("Delete...", act ([entry] (PresetBrowser& b) { b.deletePreset (entry); }));
        menu.addSeparator();
        const auto file = e.file;
        menu.addItem ("Show preset file", [file] { file.revealToUser(); });
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetList).withMousePosition());
}

void PresetBrowser::nodeMenu (int row)
{
    const auto n = nodes[(size_t) row];
    if (n.kind != NodeKind::user && n.kind != NodeKind::userFolder)
        return;
    juce::Component::SafePointer<PresetBrowser> safe (this);
    const auto folder = n.folder;
    juce::PopupMenu menu;
    menu.addSectionHeader (n.kind == NodeKind::user ? juce::String ("User") : "User / " + folder.replace ("/", " / "));
    menu.addItem ("New folder...", [safe, folder]
                  {
                      if (safe != nullptr)
                          safe->newFolder (folder);
                  });
    if (n.kind == NodeKind::userFolder)
    {
        menu.addItem ("Rename folder...", [safe, folder]
                      {
                          if (safe != nullptr)
                              safe->renameFolder (folder);
                      });
        menu.addItem ("Delete folder...", [safe, folder]
                      {
                          if (safe != nullptr)
                              safe->deleteFolder (folder);
                      });
    }
    menu.addSeparator();
    menu.addItem ("Show in file browser", [folder]
                  {
                      const auto dir = PresetManager::userFolderFile (folder);
                      if (dir.createDirectory().wasOk())
                          dir.startAsProcess();
                  });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&nodeList).withMousePosition());
}

void PresetBrowser::toggleFavourite (int entryIndex)
{
    presets.setFavourite (entryIndex, ! presets.isFavourite (entryIndex));
    const int keep = selectedRow;
    const auto oldRows = rows;
    buildNodes();
    // in Favourites, an unstarred preset stays listed until the folder is shown again
    if (nodes[(size_t) selectedNode].kind != NodeKind::favourites)
        filter();
    else
    {
        rows = oldRows;
        selectedRow = keep;
    }
    repaint();
    nodeList.repaint();
    presetList.repaint();
}

void PresetBrowser::toggleTag (const juce::String& tag)
{
    if (selectedTags.contains (tag, true))
        selectedTags.removeString (tag, true);
    else
        selectedTags.add (tag);
    tagStrip.repaint();
    filter();
}

void PresetBrowser::editTags (int entryIndex)
{
    const auto e = presets.getEntries()[(size_t) entryIndex];
    if (e.factory)
        return;
    juce::Component::SafePointer<PresetBrowser> safe (this);
    askText ("Tags", "Tags of \"" + e.name + "\", separated by commas:", joinTags (e.tags), "OK",
             [safe, e] (const juce::String& text)
             {
                 if (safe == nullptr)
                     return;
                 const auto key = PresetManager::keyOf (e);
                 if (! safe->presets.setUserTags (safe->presets.indexOfKey (key), parseTags (text)))
                 {
                     safe->showProblem ("Tags", "The tags could not be written.\n" + e.file.getFullPathName());
                     return;
                 }
                 safe->presetsChanged (key, {});
             });
}

void PresetBrowser::presetsChanged (const juce::String& selectKey, const juce::String& folderToShow)
{
    if (onPresetsChanged)
        onPresetsChanged(); // refreshes the manager and calls rebuild()
    else
    {
        presets.refresh();
        rebuild();
    }
    if (folderToShow.isNotEmpty())
        selectFolder (folderToShow);
    if (selectKey.isNotEmpty())
    {
        const int idx = presets.indexOfKey (selectKey);
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == idx)
                selectRow ((int) i);
    }
}

void PresetBrowser::showProblem (const juce::String& title, const juce::String& message)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::WarningIcon)
                                      .withTitle (title)
                                      .withMessage (message)
                                      .withButton ("OK")
                                      .withAssociatedComponent (this),
                                  [] (int) {});
}

void PresetBrowser::askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                             const juce::String& okText, std::function<void (const juce::String&)> done)
{
    auto* w = new juce::AlertWindow (title, message, juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("text", initial);
    w->addButton (okText, 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBrowser> safe (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, done] (int result)
                                                                   {
                                                                       if (result == 1 && safe != nullptr)
                                                                           done (w->getTextEditorContents ("text").trim());
                                                                       if (safe != nullptr)
                                                                           safe->focusSearch();
                                                                   }),
                        true);
}

void PresetBrowser::renamePreset (int entryIndex)
{
    const auto e = presets.getEntries()[(size_t) entryIndex];
    if (e.factory)
        return;
    juce::Component::SafePointer<PresetBrowser> safe (this);
    askText ("Rename preset", "New name:", e.name, "Rename", [safe, entryIndex, e] (const juce::String& name)
             {
                 if (safe == nullptr || name.isEmpty() || name == e.name)
                     return;
                 auto& pm = safe->presets;
                 // the list may have been read again meanwhile: find the preset by its key
                 const auto oldKey = PresetManager::keyOf (e);
                 const int idx = pm.indexOfKey (oldKey) >= 0 ? pm.indexOfKey (oldKey) : entryIndex;
                 if (! pm.renameUser (idx, name))
                 {
                     safe->showProblem ("Rename preset", "There is already a preset called \"" + pm.finalSaveName (name)
                                                             + "\" in this folder, or the file could not be renamed.");
                     return;
                 }
                 auto renamed = e;
                 renamed.name = pm.finalSaveName (name);
                 const auto newKey = PresetManager::keyOf (renamed);
                 if (safe->onKeyChanged)
                     safe->onKeyChanged (oldKey, newKey);
                 safe->presetsChanged (newKey, {});
             });
}

void PresetBrowser::deletePreset (int entryIndex)
{
    const auto e = presets.getEntries()[(size_t) entryIndex];
    if (e.factory)
        return;
    juce::Component::SafePointer<PresetBrowser> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete preset",
                                                                                "Delete the preset \"" + e.name + "\"?\nIt goes to the trash (Recycle Bin).", "Delete",
                                                                                "Cancel", this),
                                  [safe, e] (int result)
                                  {
                                      if (safe == nullptr)
                                          return;
                                      if (result == 1)
                                      {
                                          const int idx = safe->presets.indexOfKey (PresetManager::keyOf (e));
                                          if (! safe->presets.deleteUser (idx))
                                              safe->showProblem ("Delete preset", "The preset could not be deleted.\n" + e.file.getFullPathName());
                                          safe->presetsChanged ({}, {});
                                      }
                                      safe->focusSearch();
                                  });
}

void PresetBrowser::movePreset (int entryIndex, const juce::String& folder)
{
    const auto e = presets.getEntries()[(size_t) entryIndex];
    if (! presets.moveUser (entryIndex, folder))
    {
        showProblem ("Move preset", "There is already a preset called \"" + e.name + "\" in that folder, or the file could not be moved.");
        return;
    }
    auto moved = e;
    moved.folder = folder;
    if (onKeyChanged)
        onKeyChanged (PresetManager::keyOf (e), PresetManager::keyOf (moved));
    presetsChanged (PresetManager::keyOf (moved), {});
}

void PresetBrowser::newFolder (const juce::String& parent)
{
    juce::Component::SafePointer<PresetBrowser> safe (this);
    const auto where = parent.isEmpty() ? juce::String ("User") : "User / " + parent.replace ("/", " / ");
    askText ("New folder", "A new folder in " + where + ":", {}, "Create", [safe, parent] (const juce::String& name)
             {
                 if (safe == nullptr || name.isEmpty())
                     return;
                 const auto folder = safe->presets.createFolder (parent, name);
                 if (folder.isEmpty())
                 {
                     safe->showProblem ("New folder", "The folder \"" + name + "\" could not be made (it may exist already).");
                     return;
                 }
                 safe->presetsChanged ({}, folder);
             });
}

void PresetBrowser::renameFolder (const juce::String& folder)
{
    juce::Component::SafePointer<PresetBrowser> safe (this);
    askText ("Rename folder", "New name:", folder.fromLastOccurrenceOf ("/", false, false), "Rename",
             [safe, folder] (const juce::String& name)
             {
                 if (safe == nullptr || name.isEmpty())
                     return;
                 const auto renamed = safe->presets.renameFolder (folder, name);
                 if (renamed.isEmpty())
                 {
                     safe->showProblem ("Rename folder", "The folder could not be renamed (one of that name may exist already).");
                     return;
                 }
                 if (safe->onKeyChanged)
                     safe->onKeyChanged ("user/" + folder + "/", "user/" + renamed + "/");
                 safe->presetsChanged ({}, renamed);
             });
}

void PresetBrowser::deleteFolder (const juce::String& folder)
{
    const int n = presets.countInFolder (folder);
    juce::Component::SafePointer<PresetBrowser> safe (this);
    const auto message = "Delete the folder \"" + folder.replace ("/", " / ") + "\""
                         + (n > 0 ? " and the " + countText (n) + " in it?" : juce::String ("?"))
                         + "\nIt goes to the trash (Recycle Bin), where it can be restored.";
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete folder",
                                                                                message, "Delete", "Cancel", this),
                                  [safe, folder] (int result)
                                  {
                                      if (safe == nullptr)
                                          return;
                                      if (result == 1)
                                      {
                                          if (! safe->presets.deleteFolder (folder))
                                              safe->showProblem ("Delete folder", "The folder could not be deleted.\n"
                                                                                      + PresetManager::userFolderFile (folder).getFullPathName());
                                          safe->presetsChanged ({}, "User");
                                      }
                                      safe->focusSearch();
                                  });
}
} // namespace bidoo::kit
