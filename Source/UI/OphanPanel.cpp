#include "OphanPanel.h"
#include "../PluginProcessor.h"

namespace chroma
{
using LnF = ChromaLookAndFeel;

namespace
{
const char* kColNames[5] = { "Freq from", "Freq to", "Gain from", "Gain to", "Q" };
constexpr int kCols = 5;

std::atomic<float>& field (OphanBandSettings& b, int col)
{
    switch (col)
    {
        case 0: return b.fFrom;
        case 1: return b.fTo;
        case 2: return b.gFrom;
        case 3: return b.gTo;
        default: return b.q;
    }
}

float defaultField (int side, int band, int col)
{
    const auto d = OphanAdvanced::recipe (side, band);
    switch (col)
    {
        case 0: return d.fFrom;
        case 1: return d.fTo;
        case 2: return d.gFrom;
        case 3: return d.gTo;
        default: return d.q;
    }
}
} // namespace

// ===========================================================================
DragValue::DragValue (Kind k, const juce::String& title) : kind (k)
{
    setTitle (title);
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
}

juce::String DragValue::format (Kind k, float v)
{
    switch (k)
    {
        case Freq:
            if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 9995.0f ? 1 : 2) + " kHz";
            return juce::String (juce::roundToInt (v)) + " Hz";
        case Gain:
        {
            const auto s = juce::String (std::abs (v), 1);
            return (v > 0.04f ? "+" : v < -0.04f ? minus() : juce::String()) + s + " dB";
        }
        case Q:    return juce::String (v, v < 10.0f ? 2 : 1);
        case Rate: return juce::String (v, 2) + " Hz";
    }
    return {};
}

juce::String DragValue::text() const { return get ? format (kind, get()) : juce::String(); }

float DragValue::limit (float v) const
{
    switch (kind)
    {
        case Freq: return juce::jlimit (10.0f, 22000.0f, v);
        case Gain: return juce::jlimit (-15.0f, 15.0f, v);
        case Q:    return juce::jlimit (0.3f, 40.0f, v);
        case Rate: return juce::jlimit (0.01f, 20.0f, v);
    }
    return v;
}

float DragValue::stepped (float v0, float delta) const
{
    switch (kind)
    {
        case Freq: return limit (v0 * std::pow (2.0f, delta / 40.0f));   // октава на 40 px
        case Gain: return limit (v0 + delta * 0.1f);
        case Q:
        case Rate: return limit (v0 * std::pow (2.0f, delta / 80.0f));
    }
    return v0;
}

void DragValue::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (isMouseOver (true) ? Colours::ink.withAlpha (0.35f) : Colours::outline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    g.setColour (Colours::ink);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.drawText (text(), getLocalBounds().reduced (6, 0), juce::Justification::centred);
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r.expanded (2.0f), 10.0f);
    setDescription (getTitle() + " " + text());
}

void DragValue::mouseDown (const juce::MouseEvent&)
{
    if (get) startValue = get();
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

void DragValue::mouseDrag (const juce::MouseEvent& e)
{
    if (! set) return;
    const float k = e.mods.isShiftDown() ? 0.2f : 1.0f;
    set (stepped (startValue, -(float) e.getDistanceFromDragStartY() * k));
    repaint();
}

void DragValue::mouseDoubleClick (const juce::MouseEvent&)
{
    if (set && getDefault) set (getDefault());
    repaint();
}

bool DragValue::keyPressed (const juce::KeyPress& k)
{
    if (! set || ! get) return false;
    const float step = k.getModifiers().isShiftDown() ? 1.0f : 5.0f;
    if (k.getKeyCode() == juce::KeyPress::upKey)   { set (stepped (get(), step)); repaint(); return true; }
    if (k.getKeyCode() == juce::KeyPress::downKey) { set (stepped (get(), -step)); repaint(); return true; }
    if (k == juce::KeyPress::backspaceKey && getDefault) { set (getDefault()); repaint(); return true; }
    return false;
}

// ===========================================================================
SlotPanel::SlotPanel (ChromaProcessor& p) : proc (p)
{
    setTitle ("Slot panel");
    modeSwitch.setTitle ("Panel view");
    modeSwitch.setItems ({ { "Macro", 0 }, { "Advanced", 1 } });
    modeSwitch.onChange = [this] (int) { updateVisibility(); };
    shapeSwitch.setTitle ("LFO shape");
    shapeSwitch.setItems ({ { "Sine", 0 }, { "Stray", 1 } });
    characterSwitch.setTitle ("Crowd character");
    characterSwitch.setDescription ("Who sings: Choir, Stadium (a football crowd), Monks or Tavern.");
    {
        std::vector<Segmented::Item> items;
        const auto names = legionCharacters();
        for (int i = 0; i < names.size(); ++i) items.push_back ({ names[i], i });
        characterSwitch.setItems (items);
    }
    timbreSwitch.setTitle ("Note timbre");
    timbreSwitch.setDescription ("What the three notes sound like: FM (a plucked FM note, as in the recipe), String, Bell or Glass.");
    {
        std::vector<Segmented::Item> items;
        const auto names = psalterTimbres();
        for (int i = 0; i < names.size(); ++i) items.push_back ({ names[i], i });
        timbreSwitch.setItems (items);
    }
    for (juce::Component* c : std::initializer_list<juce::Component*> { &modeSwitch, &shapeSwitch, &rate, &drift, &depth, &q, &spread, &lowCut, &mix,
                                                                        &onToggle, &guardToggle, &status, &driftRate, &resetButton, &addOphan, &addOtt,
                                                                        &count, &ottDepth, &time, &upward, &downward, &trim,
                                                                        &characterSwitch, &voices, &pitch, &pitchSpread, &loose, &space, &addLegion,
                                                                        &prevPreset, &presetName, &nextPreset,
                                                                        &notes[0], &notes[1], &notes[2], &timbreSwitch, &decay, &tone, &psRate, &psDepth, &width,
                                                                        &samplesButton, &addPsalter })
        addAndMakeVisible (c);
    samplesButton.setTitle ("Own samples for the notes");
    samplesButton.setDescription ("Load your own one-note samples instead of the built-in notes.");
    samplesButton.onClick = [this] { if (onShowSamples) onShowSamples (slot); };
    presetName.setTitle ("Effect preset");
    guardToggle.setDescription ("Stops the burst at the start of a note after silence. Off: the stack pops at note starts, like OTT does.");
    presetName.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { proc.stepEffectPreset (slot, -1); updatePresetName(); repaint(); };
    nextPreset.onClick = [this] { proc.stepEffectPreset (slot, 1); updatePresetName(); repaint(); };
    count.setBig (true);
    voices.setBig (true);
    status.setFont (LnF::mono (LnF::Type::footnote));
    status.getProperties().set ("dim", true);
    status.setJustificationType (juce::Justification::centredRight);

    resetButton.setTitle ("Reset advanced settings to the recipe");
    resetButton.onClick = [this] { proc.resetAdvanced (slot); repaint(); };
    addOphan.setButtonText (juce::String ("Ophan") + midDot() + effectTagline (EffectType::Ophan));
    addOphan.onClick = [this] { addEffect (EffectType::Ophan); };
    addOtt.setButtonText (juce::String ("OTT Stack") + midDot() + effectTagline (EffectType::OttStack));
    addOtt.onClick = [this] { addEffect (EffectType::OttStack); };
    addLegion.setButtonText (juce::String ("Legion") + midDot() + effectTagline (EffectType::Legion));
    addLegion.onClick = [this] { addEffect (EffectType::Legion); };
    addPsalter.setButtonText (juce::String ("Psalter") + midDot() + effectTagline (EffectType::Psalter));
    addPsalter.onClick = [this] { addEffect (EffectType::Psalter); };

    driftRate.get = [this] { return adv().driftRate.load(); };
    driftRate.set = [this] (float v) { adv().driftRate.store (v); };
    driftRate.getDefault = [] { return 0.35f; };

    buildGrid();
    attachSlot();
}

OphanAdvanced& SlotPanel::adv() { return proc.advanced (slot); }

// нота из своего сэмпла: вместо имени ноты — «sample»; на кнопке — сколько сэмплов загружено
void SlotPanel::updateSampleLabels()
{
    int n = 0;
    for (int k = 0; k < 3; ++k)
    {
        const bool has = proc.hasPsalterSample (slot, k);
        notes[(size_t) k].setValueOverride (has ? juce::String ("sample") : juce::String());
        n += has ? 1 : 0;
    }
    samplesButton.setButtonText (n > 0 ? "Own samples (" + juce::String (n) + ")" : juce::String ("Own samples..."));
}

void SlotPanel::updatePresetName()
{
    const auto n = proc.getEffectPresetName (slot);
    presetName.setButtonText (n);
    presetName.setDescription ("Effect preset " + n);
}

void SlotPanel::showPresetMenu()
{
    const auto type = proc.getSlotType (slot);
    if (type == EffectType::Empty) return;
    juce::PopupMenu m;
    m.addSectionHeader (effectNames()[(int) type] + " presets");
    const auto& fac = effectFactoryPresets (type);
    const auto cur = proc.getEffectPresetName (slot);
    for (int i = 0; i < (int) fac.size(); ++i) m.addItem (1 + i, fac[(size_t) i].name, true, cur == fac[(size_t) i].name);
    const auto users = effectUserPresets (type);
    if (! users.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("User");
        for (int i = 0; i < users.size(); ++i)
            m.addItem (1000 + i, users[i].getFileNameWithoutExtension(), true, cur == users[i].getFileNameWithoutExtension());
    }
    m.addSeparator();
    m.addItem (2000, "Save effect preset...");
    m.addItem (2001, "Show effect presets folder");
    juce::Component::SafePointer<SlotPanel> safe (this);
    const int s = slot;
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [safe, s, users, type] (int r)
    {
        if (safe == nullptr || r == 0 || safe->proc.getSlotType (s) != type) return;
        if (r >= 1 && r < 1000) safe->proc.loadEffectFactory (s, r - 1);
        else if (r >= 1000 && r < 2000 && r - 1000 < users.size()) safe->proc.loadEffectUser (s, users[r - 1000]);
        else if (r == 2000) { safe->savePreset(); return; }
        else if (r == 2001) { auto d = effectPresetFolder (type); d.createDirectory(); d.startAsProcess(); }
        safe->updatePresetName();
        safe->repaint();
    });
}

void SlotPanel::savePreset()
{
    const auto type = proc.getSlotType (slot);
    saveWindow = std::make_unique<juce::AlertWindow> ("Save " + effectNames()[(int) type] + " preset",
        type == EffectType::Ophan ? "Knobs, Mix and advanced settings of this effect are saved."
            : type == EffectType::Psalter ? "Knobs, Mix and own samples of this effect are saved." : "Knobs and Mix of this effect are saved.",
        juce::MessageBoxIconType::NoIcon);
    saveWindow->addTextEditor ("name", proc.getEffectPresetName (slot), "Name");
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    const int s = slot;
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this, s] (int r)
    {
        if (r == 1 && saveWindow != nullptr)
        {
            if (! proc.saveEffectPreset (s, saveWindow->getTextEditorContents ("name")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, productName(), "Could not save effect preset.");
            updatePresetName();
        }
        saveWindow.reset();
    }), false);
}

void SlotPanel::layoutHead()
{
    // «‹ имя ›» — слева от вкладок у Ophan, у правого края у OTT Stack
    auto h = headArea.withHeight (30);
    if (shownType == EffectType::Ophan) h.removeFromRight (220 + 16);
    nextPreset.setBounds (h.removeFromRight (32));
    presetName.setBounds (h.removeFromRight (170));
    prevPreset.setBounds (h.removeFromRight (32));
}

// заголовок слева; справа — пресет эффекта и (у Ophan) вкладки Macro / Advanced
juce::Rectangle<int> SlotPanel::titleArea() const
{
    if (shownType == EffectType::Empty) return headArea.withTrimmedRight (240);
    return headArea.withRight (prevPreset.getX() - 20);
}

void SlotPanel::addEffect (EffectType t)
{
    proc.clearSlot (slot);
    proc.setSlotType (slot, t);
    refresh();
    if (onSlotChanged) onSlotChanged();
}

void SlotPanel::buildGrid()
{
    for (int side = 0; side < 2; ++side)
        for (int b = 0; b < OphanRecipe::kBands; ++b)
            for (int col = 0; col < kCols; ++col)
            {
                const auto kind = col < 2 ? DragValue::Freq : col < 4 ? DragValue::Gain : DragValue::Q;
                auto cell = std::make_unique<DragValue> (kind, juce::String (side == 0 ? "Left" : "Right") + " band " + juce::String (b + 2) + " " + kColNames[col]);
                cell->get = [this, side, b, col] { return field (adv().band[(size_t) side][(size_t) b], col).load(); };
                cell->set = [this, side, b, col] (float v) { field (adv().band[(size_t) side][(size_t) b], col).store (v); };
                cell->getDefault = [side, b, col] { return defaultField (side, b, col); };
                addChildComponent (*cell);
                cells.push_back (std::move (cell));
            }
}

void SlotPanel::setSlot (int s)
{
    s = juce::jlimit (0, kSlots - 1, s);
    if (s == slot && proc.getSlotType (s) == shownType) return;
    slot = s;
    attachSlot();
}

void SlotPanel::attachSlot()
{
    rate.attach (proc.apvts, slotParam (slot, ParamId::rate));
    drift.attach (proc.apvts, slotParam (slot, ParamId::drift));
    depth.attach (proc.apvts, slotParam (slot, ParamId::depth));
    q.attach (proc.apvts, slotParam (slot, ParamId::q));
    spread.attach (proc.apvts, slotParam (slot, ParamId::spread));
    lowCut.attach (proc.apvts, slotParam (slot, ParamId::lowCut));
    mix.attach (proc.apvts, slotParam (slot, ParamId::mix));
    onToggle.attach (proc.apvts, slotParam (slot, ParamId::on));
    guardToggle.attach (proc.apvts, slotParam (slot, ParamId::guard));
    count.attach (proc.apvts, slotParam (slot, ParamId::count));
    ottDepth.attach (proc.apvts, slotParam (slot, ParamId::ottDepth));
    time.attach (proc.apvts, slotParam (slot, ParamId::time));
    upward.attach (proc.apvts, slotParam (slot, ParamId::upward));
    downward.attach (proc.apvts, slotParam (slot, ParamId::downward));
    trim.attach (proc.apvts, slotParam (slot, ParamId::trim));
    voices.attach (proc.apvts, slotParam (slot, ParamId::voices));
    pitch.attach (proc.apvts, slotParam (slot, ParamId::pitch));
    pitchSpread.attach (proc.apvts, slotParam (slot, ParamId::pitchSpread));
    loose.attach (proc.apvts, slotParam (slot, ParamId::loose));
    space.attach (proc.apvts, slotParam (slot, ParamId::space));
    for (int k = 0; k < 3; ++k) notes[(size_t) k].attach (proc.apvts, slotParam (slot, psalterNoteId (k)));
    decay.attach (proc.apvts, slotParam (slot, ParamId::decay));
    tone.attach (proc.apvts, slotParam (slot, ParamId::tone));
    psRate.attach (proc.apvts, slotParam (slot, ParamId::psRate));
    psDepth.attach (proc.apvts, slotParam (slot, ParamId::psDepth));
    width.attach (proc.apvts, slotParam (slot, ParamId::width));
    timbreAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (slotParam (slot, ParamId::timbre)),
        [this] (float v) { timbreSwitch.setSelectedValue (juce::roundToInt (v)); }, nullptr);
    timbreSwitch.onChange = [this] (int v) { timbreAtt->setValueAsCompleteGesture ((float) v); };
    timbreAtt->sendInitialUpdate();
    characterAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (slotParam (slot, ParamId::character)),
        [this] (float v) { characterSwitch.setSelectedValue (juce::roundToInt (v)); }, nullptr);
    characterSwitch.onChange = [this] (int v) { characterAtt->setValueAsCompleteGesture ((float) v); };
    characterAtt->sendInitialUpdate();
    shapeAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter (slotParam (slot, ParamId::shape)),
        [this] (float v) { shapeSwitch.setSelectedValue (juce::roundToInt (v)); }, nullptr);
    shapeSwitch.onChange = [this] (int v) { shapeAtt->setValueAsCompleteGesture ((float) v); };
    shapeAtt->sendInitialUpdate();
    refresh();
}

void SlotPanel::refresh()
{
    shownType = proc.getSlotType (slot);
    updateVisibility();
    repaint();
}

void SlotPanel::updateVisibility()
{
    advancedMode = modeSwitch.getSelectedValue() == 1;
    const bool ophan = shownType == EffectType::Ophan, ottType = shownType == EffectType::OttStack, legionType = shownType == EffectType::Legion;
    const bool psalterType = shownType == EffectType::Psalter;
    const bool empty = ! ophan && ! ottType && ! legionType && ! psalterType;
    modeSwitch.setVisible (ophan);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &prevPreset, &presetName, &nextPreset })
        c->setVisible (! empty);
    updatePresetName();
    layoutHead();
    for (juce::Component* c : std::initializer_list<juce::Component*> { &shapeSwitch, &rate, &drift, &depth, &q, &spread, &lowCut })
        c->setVisible (ophan && ! advancedMode);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &count, &ottDepth, &time, &upward, &downward, &trim })
        c->setVisible (ottType);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &characterSwitch, &voices, &pitch, &pitchSpread, &loose, &space })
        c->setVisible (legionType);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &notes[0], &notes[1], &notes[2], &timbreSwitch, &decay, &tone, &psRate, &psDepth, &width, &samplesButton })
        c->setVisible (psalterType);
    updateSampleLabels();
    mix.setVisible ((ophan && ! advancedMode) || ottType || legionType || psalterType);
    for (auto& c : cells) c->setVisible (ophan && advancedMode);
    driftRate.setVisible (ophan && advancedMode);
    resetButton.setVisible (ophan && advancedMode);
    onToggle.setVisible (! empty);
    guardToggle.setVisible (ottType);
    addOphan.setVisible (empty);
    addOtt.setVisible (empty);
    addLegion.setVisible (empty);
    addPsalter.setVisible (empty);
    layoutBody();
    repaint();
}

// ---------------------------------------------------------------------------
void SlotPanel::resized()
{
    auto inner = getLocalBounds().reduced (18, 14);
    footArea = inner.removeFromBottom (44);
    headArea = inner.removeFromTop (48);
    inner.removeFromTop (8);
    bodyArea = inner;
    modeSwitch.setBounds (headArea.withTrimmedLeft (headArea.getWidth() - 220).withHeight (30));
    layoutHead();

    layoutBody();

    // Advanced: сетка
    {
        gridArea = bodyArea;
        gridRowH = juce::jmin (26, (gridArea.getHeight() - 36) / OphanRecipe::kBands);
        gridTop = gridArea.getY() + 36;
        gridCol0 = 54;
        gridGroupGap = 28;
        const int gap = 6;
        gridCellW = (gridArea.getWidth() - gridCol0 - gridGroupGap - gap * (2 * kCols - 1)) / (2 * kCols);
        for (int side = 0; side < 2; ++side)
            for (int b = 0; b < OphanRecipe::kBands; ++b)
                for (int col = 0; col < kCols; ++col)
                {
                    const int x = gridArea.getX() + gridCol0 + side * (kCols * (gridCellW + gap) + gridGroupGap - gap) + col * (gridCellW + gap);
                    const int y = gridTop + b * gridRowH;
                    cells[(size_t) ((side * OphanRecipe::kBands + b) * kCols + col)]->setBounds (x, y + 2, gridCellW, gridRowH - 4);
                }
    }

    auto f = footArea.withTrimmedTop (10);
    onToggle.setBounds (f.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    guardToggle.setBounds (f.withWidth (140).withX (onToggle.getRight() + 16).withSizeKeepingCentre (140, 28));
    samplesButton.setBounds (juce::Rectangle<int> (onToggle.getRight() + 16, f.getY(), 140, f.getHeight()).withSizeKeepingCentre (140, 28));
    f.removeFromLeft (16);
    driftRate.setBounds (f.removeFromLeft (84 + 90).withTrimmedLeft (84).withSizeKeepingCentre (90, 24));
    f.removeFromLeft (12);
    resetButton.setBounds (f.removeFromLeft (140).withSizeKeepingCentre (140, 28));
    status.setBounds (f.removeFromRight (300));

    addArea = bodyArea;
    auto adds = bodyArea.withSizeKeepingCentre (320, 36 * 4 + 10 * 3);
    for (auto* b : { &addOphan, &addOtt, &addLegion, &addPsalter })
    {
        b->setBounds (adds.removeFromTop (36));
        adds.removeFromTop (10);
    }
}

void SlotPanel::layoutBody()
{
    if (bodyArea.isEmpty()) return;
    if (shownType == EffectType::OttStack)
    {
        // главная ручка Count слева, второстепенные — рядом, за линией
        auto row = bodyArea;
        count.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (200, juce::jmin (row.getHeight(), 170)));
        ottDividerX = row.getX() + 18;
        row.removeFromLeft (36);
        layoutRow (row.withSizeKeepingCentre (row.getWidth(), juce::jmin (row.getHeight(), 100)), { &ottDepth, &time, &upward, &downward, &trim, &mix }, 10);
        return;
    }
    if (shownType == EffectType::Legion)
    {
        // главная ручка Voices слева; за линией — характер толпы (сегменты с подписью, как Shape у Ophan) и ручки
        auto row = bodyArea;
        voices.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (200, juce::jmin (row.getHeight(), 170)));
        ottDividerX = row.getX() + 18;
        row.removeFromLeft (36);
        row = row.withSizeKeepingCentre (row.getWidth(), juce::jmin (row.getHeight(), 100));
        characterArea = row.removeFromLeft (300);
        characterSwitch.setBounds (characterArea.withSizeKeepingCentre (284, 30).translated (0, 8));
        row.removeFromLeft (12);
        layoutRow (row, { &pitch, &pitchSpread, &loose, &space, &mix }, 10);
        return;
    }
    if (shownType == EffectType::Psalter)
    {
        // три ноты слева; за линией — тембр (сегменты с подписью, как Character у Legion) и ручки
        auto row = bodyArea.withSizeKeepingCentre (bodyArea.getWidth(), juce::jmin (bodyArea.getHeight(), 100));
        layoutRow (row.removeFromLeft (252), { &notes[0], &notes[1], &notes[2] }, 6);
        ottDividerX = row.getX() + 18;
        row.removeFromLeft (36);
        timbreArea = row.removeFromLeft (264);
        timbreSwitch.setBounds (timbreArea.withSizeKeepingCentre (248, 30).translated (0, 8));
        row.removeFromLeft (12);
        layoutRow (row, { &decay, &tone, &psRate, &psDepth, &width, &mix }, 6);
        return;
    }
    // Ophan, Macro: форма LFO слева, ручки
    auto row = bodyArea.withSizeKeepingCentre (bodyArea.getWidth(), juce::jmin (bodyArea.getHeight(), 100));
    shapeArea = row.removeFromLeft (180);
    shapeSwitch.setBounds (shapeArea.withSizeKeepingCentre (164, 30).translated (0, 8));
    row.removeFromLeft (20);
    layoutRow (row, { &rate, &drift, &depth, &q, &spread, &lowCut, &mix }, 10);
}

void SlotPanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colours::surface);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.drawHorizontalLine (footArea.getY(), (float) footArea.getX(), (float) footArea.getRight());

    const int pos = [this] { auto o = proc.getOrder(); for (int k = 0; k < kSlots; ++k) if (o[(size_t) k] == slot) return k; return 0; }();
    auto ta = titleArea();
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    if (shownType == EffectType::OttStack)
    {
        g.drawText (juce::String ("OTT Stack") + midDot() + effectTagline (EffectType::OttStack), ta.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawFittedText ("OTT from Ableton (three bands, upward and downward compression) chained up to 40 times. "
                          "Count sets how many; Output sets the level after the whole stack.", ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
        g.setColour (Colours::outline);
        g.drawVerticalLine (ottDividerX, (float) bodyArea.getY() + 10.0f, (float) bodyArea.getBottom() - 10.0f);
        return;
    }
    if (shownType == EffectType::Legion)
    {
        g.drawText (juce::String ("Legion") + midDot() + effectTagline (EffectType::Legion), ta.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawFittedText ("Turns one voice into a choir or a crowd: every voice is a copy of the input with its own pitch, timing, timbre and place. "
                          "Voices sets how many; Character sets who they are.", ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
        g.setColour (Colours::outline);
        g.drawVerticalLine (ottDividerX, (float) bodyArea.getY() + 10.0f, (float) bodyArea.getBottom() - 10.0f);
        g.setColour (Colours::ink);
        g.setFont (LnF::font (LnF::Type::subheadline, true));
        g.drawText ("Character", characterSwitch.getBounds().translated (0, -22).withHeight (16), juce::Justification::centred);
        return;
    }
    if (shownType == EffectType::Psalter)
    {
        g.drawText (juce::String ("Psalter") + midDot() + effectTagline (EffectType::Psalter), ta.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawFittedText ("Convolves the sound with three notes: only the harmonics it shares with them ring out, nothing new is added. "
                          "A random LFO fades each note in and out. Notes set the chord; Timbre sets what the notes are.", ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
        g.setColour (Colours::outline);
        g.drawVerticalLine (ottDividerX, (float) bodyArea.getY() + 10.0f, (float) bodyArea.getBottom() - 10.0f);
        g.setColour (Colours::ink);
        g.setFont (LnF::font (LnF::Type::subheadline, true));
        g.drawText ("Timbre", timbreSwitch.getBounds().translated (0, -22).withHeight (16), juce::Justification::centred);
        return;
    }
    if (shownType == EffectType::Ophan)
    {
        g.drawText (juce::String ("Ophan") + midDot() + effectTagline (EffectType::Ophan), ta.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawFittedText ("EQ Eight in L/R mode in one device: a low cut and seven narrow bells per channel. Four LFOs sweep their gains "
                          "and frequencies, a fifth one drifts the rate.", ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
    }
    else
    {
        g.drawText ("Slot " + juce::String (pos + 1) + midDot() + "Empty", ta.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawFittedText ("Effects run left to right. Pick an effect for this slot.", ta.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);
        return;
    }

    if (! advancedMode)
    {
        // подпись формы LFO — как у ручки
        g.setColour (Colours::ink);
        g.setFont (LnF::font (LnF::Type::subheadline, true));
        g.drawText ("Shape", shapeSwitch.getBounds().translated (0, -22).withHeight (16), juce::Justification::centred);
        return;
    }

    // шапка сетки: группы Left / Right и названия столбцов
    const int gap = 6;
    for (int side = 0; side < 2; ++side)
    {
        const int x0 = gridArea.getX() + gridCol0 + side * (kCols * (gridCellW + gap) + gridGroupGap - gap);
        const int groupW = kCols * gridCellW + (kCols - 1) * gap;
        auto title = juce::Rectangle<int> (x0, gridArea.getY(), groupW, 16);
        g.setColour (side == 0 ? Colours::carrier : Colours::modulator);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) x0, (float) title.getCentreY() - 1.5f, 14.0f, 3.0f), 1.5f);
        g.setColour (Colours::ink);
        g.setFont (LnF::font (LnF::Type::subheadline, true));
        g.drawText (side == 0 ? "Left" : "Right", title.withTrimmedLeft (20), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::footnote));
        for (int col = 0; col < kCols; ++col)
            g.drawText (kColNames[col], juce::Rectangle<int> (x0 + col * (gridCellW + gap), gridArea.getY() + 18, gridCellW, 14), juce::Justification::centred);
    }
    g.setFont (LnF::font (LnF::Type::footnote));
    g.setColour (Colours::ink2);
    g.drawText ("Band", juce::Rectangle<int> (gridArea.getX(), gridArea.getY() + 18, gridCol0 - 8, 14), juce::Justification::centredLeft);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink);
    for (int b = 0; b < OphanRecipe::kBands; ++b)
        g.drawText (juce::String (b + 2), juce::Rectangle<int> (gridArea.getX(), gridTop + b * gridRowH, gridCol0 - 8, gridRowH), juce::Justification::centredLeft);
    // подпись Drift rate в подвале
    g.setFont (LnF::font (LnF::Type::callout));
    g.setColour (Colours::ink);
    g.drawText ("Drift rate", driftRate.getBounds().withX (driftRate.getX() - 80).withWidth (74), juce::Justification::centredRight);
}
} // namespace chroma

namespace chroma
{
// ===========================================================================
PsalterSamplesPopup::PsalterSamplesPopup (ChromaProcessor& p) : proc (p)
{
    setTitle ("Own samples");
    setWantsKeyboardFocus (true);
    for (int k = 0; k < 3; ++k)
    {
        auto& lb = loadButtons[(size_t) k];
        lb.setButtonText ("Load...");
        lb.setTitle ("Load a sample for note " + juce::String (k + 1));
        lb.onClick = [this, k] { choose (k); };
        addAndMakeVisible (lb);
        auto& cb = clearButtons[(size_t) k];
        cb.setButtonText ("Clear");
        cb.setTitle ("Back to the built-in note " + juce::String (k + 1));
        cb.onClick = [this, k] { proc.clearPsalterSample (slot, k); resized(); repaint(); if (onChanged) onChanged(); };
        addAndMakeVisible (cb);
    }
    doneButton.onClick = [this] { close(); };
    addAndMakeVisible (doneButton);
}

void PsalterSamplesPopup::open (int s, juce::Rectangle<int> over)
{
    slot = s;
    card = juce::Rectangle<int> (560, 268).withCentre (over.getCentre());
    setVisible (true);
    toFront (true);
    resized();
    repaint();
    grabKeyboardFocus();
}

void PsalterSamplesPopup::close()
{
    chooser.reset();
    setVisible (false);
    if (onChanged) onChanged();
}

juce::Rectangle<int> PsalterSamplesPopup::rowArea (int k) const
{
    return juce::Rectangle<int> (card.getX() + 20, card.getY() + 76 + k * 46, card.getWidth() - 40, 38);
}

void PsalterSamplesPopup::resized()
{
    for (int k = 0; k < 3; ++k)
    {
        auto r = rowArea (k);
        clearButtons[(size_t) k].setBounds (r.removeFromRight (72).withSizeKeepingCentre (72, 28));
        r.removeFromRight (8);
        loadButtons[(size_t) k].setBounds (r.removeFromRight (84).withSizeKeepingCentre (84, 28));
        clearButtons[(size_t) k].setEnabled (proc.hasPsalterSample (slot, k));
    }
    doneButton.setBounds (juce::Rectangle<int> (card.getRight() - 20 - 84, card.getBottom() - 16 - 28, 84, 28));
}

void PsalterSamplesPopup::paint (juce::Graphics& g)
{
    // без затемнения и тени: только карточка с рамкой
    auto c = card.toFloat();
    g.setColour (Colours::surface);
    g.fillRoundedRectangle (c, 12.0f);
    g.setColour (Colours::ink);   // как у выбранной карточки: без тени карточку отделяет рамка
    g.drawRoundedRectangle (c.reduced (0.5f), 12.0f, 1.0f);

    auto head = card.reduced (20, 16).removeFromTop (48);
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    g.drawText ("Own samples" + midDot() + "Slot " + juce::String (slot + 1), head.removeFromTop (20), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawFittedText ("A sample replaces the built-in note. Use one note per file: WAV, AIFF or FLAC, up to 8 s. "
                      "Decay still shortens it; Timbre and the note knob do not apply.", head.withTrimmedTop (4), juce::Justification::topLeft, 2, 1.0f);

    const int timbre = juce::roundToInt (proc.apvts.getRawParameterValue (slotParam (slot, ParamId::timbre))->load());
    for (int k = 0; k < 3; ++k)
    {
        auto r = rowArea (k);
        auto label = r.removeFromLeft (76);
        g.setColour (Colours::ink);
        g.setFont (LnF::font (LnF::Type::subheadline, true));
        g.drawText ("Note " + juce::String (k + 1), label.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft);
        const int note = juce::roundToInt (proc.apvts.getRawParameterValue (slotParam (slot, psalterNoteId (k)))->load());
        g.setColour (Colours::ink2);
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.drawText (PsalterRecipe::noteName (note), label, juce::Justification::topLeft);

        auto well = r.withTrimmedRight (72 + 8 + 84 + 10).toFloat().reduced (0.0f, 4.0f);
        g.setColour (Colours::well);
        g.fillRoundedRectangle (well, 8.0f);
        g.setColour (Colours::outline);
        g.drawRoundedRectangle (well.reduced (0.5f), 8.0f, 1.0f);
        const bool has = proc.hasPsalterSample (slot, k);
        g.setColour (has ? Colours::ink : Colours::ink2);
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.drawText (has ? proc.getPsalterSampleName (slot, k) : "Built-in" + midDot() + psalterTimbres()[timbre] + " " + PsalterRecipe::noteName (note),
                    well.reduced (10.0f, 0.0f), juce::Justification::centredLeft, true);
    }
}

void PsalterSamplesPopup::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition())) close();
}

bool PsalterSamplesPopup::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    return false;
}

void PsalterSamplesPopup::choose (int k)
{
    if (! lastFolder.isDirectory()) lastFolder = juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Sample for note " + juce::String (k + 1), lastFolder, "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<PsalterSamplesPopup> safe (this);
    const int s = slot;
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, s, k] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        safe->lastFolder = f.getParentDirectory();
        if (! safe->proc.loadPsalterSample (s, k, f))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, productName(), "Could not read " + f.getFileName() + ".");
        safe->resized();
        safe->repaint();
        if (safe->onChanged) safe->onChanged();
    });
}
} // namespace chroma
