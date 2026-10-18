#include "ArrangementView.h"

using namespace dm;

int ArrangementView::rowHeight() const
{
    const int n = juce::jmax (1, processor.song().numParts());
    return juce::jlimit (20, 56, (getHeight() - kHeader) / n);
}

juce::Rectangle<int> ArrangementView::columnBounds (int section) const
{
    auto s = processor.song();
    const double total = juce::jmax (1.0, s.lengthBeats());
    const double w = getWidth() - kGutter;
    const int x0 = kGutter + juce::roundToInt (w * s.sectionStartBeat (section) / total);
    const int x1 = kGutter + juce::roundToInt (w * s.sectionStartBeat (section + 1) / total);
    return { x0, 0, juce::jmax (1, x1 - x0), getHeight() };
}

juce::Rectangle<int> ArrangementView::rowBounds (int part) const
{
    return { 0, kHeader + part * rowHeight(), getWidth(), rowHeight() };
}

juce::Rectangle<int> ArrangementView::cellBounds (int part, int section) const
{
    const auto c = columnBounds (section), r = rowBounds (part);
    return { c.getX(), r.getY(), c.getWidth(), r.getHeight() };
}

ArrangementView::Hit ArrangementView::hitTest (juce::Point<int> p) const
{
    Hit h;
    auto s = processor.song();
    if (p.y < kHeader)
    {
        for (int i = 0; i < s.numSections(); ++i)
            if (columnBounds (i).contains (p))
                return { Hit::Where::header, -1, i };
        return h;
    }
    const int part = (p.y - kHeader) / rowHeight();
    if (part < 0 || part >= s.numParts())
        return h;
    if (p.x < kGutter)
    {
        const auto r = rowBounds (part);
        const int bx = kGutter - 50, by = r.getCentreY() - 9;
        if (juce::Rectangle<int> (bx, by, 20, 18).contains (p))
            return { Hit::Where::mute, part, -1 };
        if (juce::Rectangle<int> (bx + 24, by, 20, 18).contains (p))
            return { Hit::Where::solo, part, -1 };
        return { Hit::Where::gutter, part, -1 };
    }
    for (int i = 0; i < s.numSections(); ++i)
        if (columnBounds (i).contains (p))
            return { Hit::Where::cell, part, i };
    return h;
}

void ArrangementView::paint (juce::Graphics& g)
{
    const auto& pal = lnf.palette();
    auto s = processor.song();
    const int rh = rowHeight();
    const auto selPart = s.partIndex (processor.selectedPart);
    const auto selSection = s.sectionIndex (processor.selectedSection);

    // Section headers
    for (int i = 0; i < s.numSections(); ++i)
    {
        auto sec = s.section (i);
        auto c = columnBounds (i).withHeight (kHeader).reduced (2, 3);
        g.setColour (i == selSection ? pal.accent.withAlpha (0.18f) : pal.well.withAlpha (0.6f));
        g.fillRoundedRectangle (c.toFloat(), 5.0f);
        auto t = c.reduced (6, 3);
        g.setColour (pal.text);
        g.setFont (bidoo::look::font (13.0f, true));
        g.drawFittedText (sec[ids::name].toString(), t.removeFromTop (18), juce::Justification::centredLeft, 1, 0.7f);
        g.setColour (pal.textDim);
        g.setFont (bidoo::look::font (11.0f));
        g.drawFittedText (juce::String ((int) sec[ids::bars]) + " bars  " + sec[ids::chords].toString(), t,
                          juce::Justification::centredLeft, 1, 0.6f);
    }

    // Part rows
    bool anySolo = false;
    for (int p = 0; p < s.numParts(); ++p)
        anySolo = anySolo || (bool) s.part (p)[ids::solo];

    for (int p = 0; p < s.numParts(); ++p)
    {
        auto pt = s.part (p);
        const auto role = roleFromName (pt[ids::role].toString());
        const auto colour = lnf.roleColour (role);
        const bool audible = ! (bool) pt[ids::mute] && (! anySolo || (bool) pt[ids::solo]);
        auto r = rowBounds (p);

        auto gutter = r.withWidth (kGutter - 4).reduced (2, 2);
        g.setColour (p == selPart ? pal.accent.withAlpha (0.18f) : pal.well.withAlpha (0.6f));
        g.fillRoundedRectangle (gutter.toFloat(), 5.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (gutter.removeFromLeft (4).toFloat(), 2.0f);
        auto text = gutter.reduced (6, 0).withTrimmedRight (52);
        g.setColour (audible ? pal.text : pal.textDim);
        g.setFont (bidoo::look::font (13.0f, true));
        g.drawFittedText (pt[ids::name].toString(), text.removeFromTop (text.getHeight() / 2 + 3),
                          rh >= 34 ? juce::Justification::bottomLeft : juce::Justification::centredLeft, 1, 0.7f);
        if (rh >= 34)
        {
            g.setColour (pal.textDim);
            g.setFont (bidoo::look::font (10.5f));
            g.drawFittedText (pt[ids::role].toString() + "  ch " + juce::String ((int) pt[ids::channel]), text,
                              juce::Justification::topLeft, 1, 0.7f);
        }

        // M and S
        const int bx = kGutter - 50, by = r.getCentreY() - 9;
        auto box = [&] (int x, const char* label, bool on, juce::Colour onColour)
        {
            auto b = juce::Rectangle<float> ((float) x, (float) by, 20.0f, 18.0f);
            g.setColour (on ? onColour : pal.button);
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (on ? pal.onAccent : pal.textDim);
            g.setFont (bidoo::look::font (10.5f, true));
            g.drawText (label, b, juce::Justification::centred);
        };
        box (bx, "M", (bool) pt[ids::mute], juce::Colour (0xffe53935));
        box (bx + 24, "S", (bool) pt[ids::solo], pal.accent);

        // Cells
        for (int i = 0; i < s.numSections(); ++i)
        {
            auto sec = s.section (i);
            const auto sid = sec[ids::id].toString();
            auto c = cellBounds (p, i).reduced (2, 2).toFloat();
            const bool selected = p == selPart && i == selSection;
            g.setColour (pal.screen);
            g.fillRoundedRectangle (c, 4.0f);

            const auto& notes = notesOf (s.activeTake (pt, sid));
            if (! notes.empty())
            {
                int lo = 127, hi = 0;
                for (auto& n : notes)
                {
                    lo = juce::jmin (lo, n.pitch);
                    hi = juce::jmax (hi, n.pitch);
                }
                lo -= 1;
                hi += 1;
                const double len = s.sectionLengthBeats (sec);
                auto area = c.reduced (3.0f, 3.0f);
                g.setColour (colour.withAlpha (audible ? 0.95f : 0.35f));
                for (auto& n : notes)
                {
                    const float x = area.getX() + (float) (n.start / len) * area.getWidth();
                    const float w = juce::jmax (1.5f, (float) (n.length / len) * area.getWidth());
                    const float y = area.getBottom() - (float) (n.pitch - lo + 1) / (float) (hi - lo + 1) * area.getHeight();
                    const float h = juce::jmax (1.5f, area.getHeight() / (float) (hi - lo + 1));
                    g.fillRect (x, y, juce::jmin (w, area.getRight() - x), h);
                }
            }

            const int takes = s.numTakes (pt, sid);
            if (takes > 1)
            {
                const auto badge = juce::Rectangle<float> (c.getRight() - 28.0f, c.getY() + 2.0f, 26.0f, 13.0f);
                g.setColour (pal.panel.withAlpha (0.9f));
                g.fillRoundedRectangle (badge, 3.0f);
                g.setColour (pal.textDim);
                g.setFont (bidoo::look::font (10.0f, true));
                g.drawText (juce::String (s.activeTakeIndex (pt, sid) + 1) + "/" + juce::String (takes), badge,
                            juce::Justification::centred);
            }

            if (selected)
            {
                g.setColour (pal.accent);
                g.drawRoundedRectangle (c, 4.0f, 2.0f);
            }
        }
    }

    // Playhead
    const double pos = processor.livePosition.load();
    if (pos >= 0.0 && s.lengthBeats() > 0.0)
    {
        const float x = (float) kGutter + (float) (pos / s.lengthBeats()) * (float) (getWidth() - kGutter);
        g.setColour (pal.accent);
        g.fillRect (x - 1.0f, 0.0f, 2.0f, (float) (kHeader + rh * s.numParts()));
    }

    if (s.numParts() == 0 || s.numSections() == 0)
    {
        g.setColour (pal.textDim);
        g.setFont (bidoo::look::font (14.0f));
        g.drawText ("Describe a song on the left and press PLAN SONG, or add sections and parts.",
                    getLocalBounds().withTrimmedLeft (kGutter), juce::Justification::centred);
    }
}

const Notes& ArrangementView::notesOf (const juce::ValueTree& take)
{
    static const Notes none;
    if (! take.isValid())
        return none;
    const auto& text = take[ids::notes].toString();
    auto& entry = cache[take[ids::id].toString()];
    if (entry.first != text)
        entry = { text, notesFromString (text) };
    return entry.second;
}

void ArrangementView::select (int part, int section)
{
    auto s = processor.song();
    const auto pid = part >= 0 ? s.part (part)[ids::id].toString() : processor.selectedPart;
    const auto sid = section >= 0 ? s.section (section)[ids::id].toString() : processor.selectedSection;
    processor.setSelection (pid, sid);
    if (onSelectionChanged)
        onSelectionChanged();
    repaint();
}

void ArrangementView::mouseDown (const juce::MouseEvent& e)
{
    pressed = hitTest (e.getPosition());
    dragStarted = false;
    auto s = processor.song();

    switch (pressed.where)
    {
        case Hit::Where::mute:
        case Hit::Where::solo:
        {
            auto pt = s.part (pressed.part);
            const auto& id = pressed.where == Hit::Where::mute ? ids::mute : ids::solo;
            processor.undo.beginNewTransaction (pressed.where == Hit::Where::mute ? "Mute" : "Solo");
            pt.setProperty (id, ! (bool) pt[id], &processor.undo);
            repaint();
            pressed = {};
            return;
        }
        case Hit::Where::header:
            select (-1, pressed.section);
            if (e.mods.isPopupMenu())
                showSectionMenu (pressed.section);
            break;
        case Hit::Where::gutter:
            select (pressed.part, -1);
            if (e.mods.isPopupMenu())
                showPartMenu (pressed.part);
            break;
        case Hit::Where::cell:
            select (pressed.part, pressed.section);
            if (e.mods.isPopupMenu())
                showCellMenu (pressed.part, pressed.section);
            break;
        case Hit::Where::none:
            break;
    }
}

void ArrangementView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 8 || onDragOut == nullptr)
        return;
    auto s = processor.song();
    juce::String pid, sid;
    if (pressed.where == Hit::Where::cell)
    {
        pid = s.part (pressed.part)[ids::id].toString();
        sid = s.section (pressed.section)[ids::id].toString();
    }
    else if (pressed.where == Hit::Where::gutter)
        pid = s.part (pressed.part)[ids::id].toString();
    else if (pressed.where == Hit::Where::header)
        sid = s.section (pressed.section)[ids::id].toString();
    else
        return;
    dragStarted = true;
    onDragOut (pid, sid);
}

void ArrangementView::mouseUp (const juce::MouseEvent&) { pressed = {}; }

void ArrangementView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.getPosition());
    auto s = processor.song();
    if (h.where == Hit::Where::header && onEditSection)
        onEditSection (s.section (h.section)[ids::id].toString());
    else if (h.where == Hit::Where::cell && onGenerate)
        if (s.numTakes (s.part (h.part), s.section (h.section)[ids::id].toString()) == 0)
            onGenerate (s.part (h.part)[ids::id].toString(), s.section (h.section)[ids::id].toString(), false);
}

void ArrangementView::showSectionMenu (int section)
{
    auto s = processor.song();
    const auto sid = s.section (section)[ids::id].toString();
    juce::PopupMenu m;
    m.addItem (1, "Edit section...");
    m.addItem (2, "Write every part here (AI)");
    m.addSeparator();
    m.addItem (3, "Insert section before");
    m.addItem (4, "Insert section after");
    m.addItem (5, "Duplicate");
    m.addItem (6, "Move left", section > 0);
    m.addItem (7, "Move right", section < s.numSections() - 1);
    m.addSeparator();
    m.addItem (8, "Delete section", s.numSections() > 1);

    juce::Component::SafePointer<ArrangementView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, section, sid] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& proc = safe->processor;
        auto song = proc.song();
        proc.undo.beginNewTransaction ("Section");
        switch (r)
        {
            case 1: if (safe->onEditSection) safe->onEditSection (sid); break;
            case 2: if (safe->onGenerate) safe->onGenerate ({}, sid, false); break;
            case 3: song.addSection ("Section", 8, {}, section); break;
            case 4: song.addSection ("Section", 8, {}, section + 1); break;
            case 5: song.duplicateSection (section); break;
            case 6: song.moveSection (section, section - 1); break;
            case 7: song.moveSection (section, section + 1); break;
            case 8:
                song.removeSection (section);
                proc.setSelection (proc.selectedPart, song.section (juce::jmin (section, song.numSections() - 1))[ids::id].toString());
                break;
            default: break;
        }
        if (safe->onSelectionChanged)
            safe->onSelectionChanged();
        safe->repaint();
    });
}

void ArrangementView::showPartMenu (int part)
{
    auto s = processor.song();
    const auto pid = s.part (part)[ids::id].toString();
    juce::PopupMenu m;
    m.addItem (1, "Write this part in every section (AI)");
    m.addSeparator();
    m.addItem (2, "Move up", part > 0);
    m.addItem (3, "Move down", part < s.numParts() - 1);
    m.addSeparator();
    m.addItem (4, "Delete part");

    juce::Component::SafePointer<ArrangementView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, part, pid] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& proc = safe->processor;
        auto song = proc.song();
        proc.undo.beginNewTransaction ("Part");
        switch (r)
        {
            case 1: if (safe->onGenerate) safe->onGenerate (pid, {}, false); break;
            case 2: song.movePart (part, part - 1); break;
            case 3: song.movePart (part, part + 1); break;
            case 4:
                song.removePart (part);
                proc.setSelection (song.part (juce::jmin (part, song.numParts() - 1))[ids::id].toString(), proc.selectedSection);
                break;
            default: break;
        }
        if (safe->onSelectionChanged)
            safe->onSelectionChanged();
        safe->repaint();
    });
}

void ArrangementView::showCellMenu (int part, int section)
{
    auto s = processor.song();
    auto pt = s.part (part);
    const auto pid = pt[ids::id].toString();
    const auto sid = s.section (section)[ids::id].toString();
    const int takes = s.numTakes (pt, sid);

    juce::PopupMenu m;
    m.addItem (1, "Generate (AI)");
    m.addItem (2, "Vary this take (AI)", takes > 0);
    m.addSeparator();
    juce::PopupMenu takeMenu;
    for (int t = 0; t < takes; ++t)
    {
        auto take = pt.getChildWithProperty (ids::section, sid).getChild (t);
        auto label = "Take " + juce::String (t + 1) + " (" + take[ids::source].toString() + ")";
        if (take[ids::prompt].toString().isNotEmpty())
            label << ": " << take[ids::prompt].toString().substring (0, 40);
        takeMenu.addItem (100 + t, label, true, t == s.activeTakeIndex (pt, sid));
    }
    m.addSubMenu ("Takes", takeMenu, takes > 0);
    m.addItem (3, "Delete this take", takes > 0);
    m.addItem (4, "Copy from previous section", section > 0);

    juce::Component::SafePointer<ArrangementView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                     [safe, pid, sid, section] (int r)
    {
        if (safe == nullptr || r == 0)
            return;
        auto& proc = safe->processor;
        auto song = proc.song();
        auto partTree = song.partById (pid);
        if (r == 1 && safe->onGenerate)
            safe->onGenerate (pid, sid, false);
        else if (r == 2 && safe->onGenerate)
            safe->onGenerate (pid, sid, true);
        else if (r == 3)
        {
            proc.undo.beginNewTransaction ("Delete take");
            song.deleteActiveTake (partTree, sid);
        }
        else if (r == 4)
        {
            const auto prev = song.section (section - 1)[ids::id].toString();
            auto notes = song.activeNotes (partTree, prev);
            sanitise (notes, song.sectionLengthBeats (song.sectionById (sid)));
            proc.undo.beginNewTransaction ("Copy take");
            song.addTake (partTree, sid, notes, {}, "copy");
        }
        else if (r >= 100)
        {
            proc.undo.beginNewTransaction ("Choose take");
            song.setActiveTake (partTree, sid, r - 100);
        }
        if (safe->onSelectionChanged)
            safe->onSelectionChanged();
        safe->repaint();
    });
}
