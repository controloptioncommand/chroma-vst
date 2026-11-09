#include "Rack.h"
#include "../PluginProcessor.h"

namespace chroma
{
using LnF = ChromaLookAndFeel;

namespace
{
constexpr float kGap = 22.0f;   // промежуток между слотами — в нём стрелка хода сигнала
float raw (ChromaProcessor& p, int slot, const char* id) { return p.apvts.getRawParameterValue (slotParam (slot, id))->load(); }
}

RackStrip::RackStrip (ChromaProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("Rack");
    updateDescription();
}

void RackStrip::setSelected (int slot)
{
    slot = juce::jlimit (0, kSlots - 1, slot);
    if (slot == selected) return;
    selected = slot;
    if (getWidth() > 0 && posOfSlot (slot) < shown) scrollToPos (posOfSlot (slot));
    updateDescription();
    repaint();
}

void RackStrip::updateDescription()
{
    const auto order = proc.getOrder();
    const bool parallel = proc.getChainMode() == ChainMode::Parallel;
    juce::String d = parallel ? "Parallel effects, outputs averaged:" : "Effect chain, left to right:";
    for (int k = 0; k < shown; ++k)
    {
        const int s = order[(size_t) k];
        const auto t = proc.getSlotType (s);
        d << " " << (k + 1) << ". " << (t == EffectType::Empty ? juce::String ("empty") : effectNames()[(int) t])
          << (t != EffectType::Empty && raw (proc, s, ParamId::on) < 0.5f ? " (off)" : "") << (s == selected ? " (selected)" : "") << ";";
    }
    setDescription (d + " Arrow keys select, Option+arrows move, Space toggles, Return opens the menu.");
}

void RackStrip::update (const std::array<OphanSnapshot, kSlots>& s, const std::array<OttSnapshot, kSlots>& o,
                        const std::array<LegionSnapshot, kSlots>& lg, const std::array<PsalterSnapshot, kSlots>& ps, double sr)
{
    snaps = s;
    ottSnaps = o;
    legionSnaps = lg;
    psalterSnaps = ps;
    sampleRate = sr;
    updateShown();
    updateDescription();
    repaint();
}

// 8 мест — когда эффектов уже четыре или какой-то стоит дальше четвёртого места
void RackStrip::updateShown()
{
    const auto o = proc.getOrder();
    int filled = 0;
    bool beyond = false;
    for (int k = 0; k < kSlots; ++k)
        if (proc.getSlotType (o[(size_t) k]) != EffectType::Empty)
        {
            ++filled;
            beyond = beyond || k >= kBaseSlots;
        }
    const int n = filled >= kBaseSlots || beyond ? kSlots : kBaseSlots;
    if (n == shown) return;
    shown = n;
    scrollTarget = juce::jlimit (0.0f, maxScroll(), scrollTarget);
    if (shown == kBaseSlots) scrollX = scrollTarget = 0.0f;
    // выбранный слот спрятался — выбрать первый
    if (posOfSlot (selected) >= shown) { selected = o[0]; if (onSelect) onSelect (selected); }
}

float RackStrip::cardWidth() const { return ((float) getWidth() - kGap * (float) (kBaseSlots - 1)) / (float) kBaseSlots; }

float RackStrip::maxScroll() const { return juce::jmax (0.0f, (cardWidth() + kGap) * (float) (shown - kBaseSlots)); }

void RackStrip::scrollBy (float dx)
{
    scrollTarget = juce::jlimit (0.0f, maxScroll(), scrollTarget + dx);
    if (Accessibility::reduceMotion()) scrollX = scrollTarget;
    if (std::abs (scrollTarget - scrollX) > 0.5f) startTimerHz (60);
    repaint();
}

// место pos целиком на экране
void RackStrip::scrollToPos (int pos)
{
    const float step = cardWidth() + kGap;
    const float x = step * (float) pos;
    if (x < scrollTarget) scrollBy (x - scrollTarget);
    else if (x + cardWidth() > scrollTarget + (float) getWidth()) scrollBy (x + cardWidth() - (float) getWidth() - scrollTarget);
}

void RackStrip::timerCallback()
{
    scrollX += (scrollTarget - scrollX) * 0.3f;
    if (std::abs (scrollTarget - scrollX) < 0.5f) { scrollX = scrollTarget; stopTimer(); }
    repaint();
}

void RackStrip::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (shown <= kBaseSlots) return;
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    scrollBy (-d * 320.0f);
}

juce::Rectangle<float> RackStrip::arrowRect (bool right) const
{
    const float s = 28.0f, cy = (float) getHeight() * 0.5f;
    return { right ? (float) getWidth() - s - 2.0f : 2.0f, cy - s * 0.5f, s, s };
}

bool RackStrip::arrowVisible (bool right) const
{
    if (shown <= kBaseSlots) return false;
    return right ? scrollTarget < maxScroll() - 0.5f : scrollTarget > 0.5f;
}

juce::Rectangle<float> RackStrip::cardRect (int pos) const
{
    const float w = cardWidth();
    return { (w + kGap) * (float) pos - scrollX, 0.0f, w, (float) getHeight() };
}

juce::Rectangle<float> RackStrip::switchRect (int pos) const
{
    auto c = cardRect (pos);
    return { c.getRight() - 14.0f - 28.0f - 30.0f, c.getY() + 15.0f, 30.0f, 18.0f };
}

juce::Rectangle<float> RackStrip::menuRect (int pos) const
{
    auto c = cardRect (pos);
    return { c.getRight() - 8.0f - 28.0f, c.getY() + 10.0f, 28.0f, 28.0f };
}

int RackStrip::posAt (juce::Point<float> p) const
{
    for (int k = 0; k < shown; ++k)
        if (cardRect (k).expanded (kGap * 0.5f, 0.0f).contains (p)) return k;
    return p.x < cardRect (0).getX() ? 0 : shown - 1;
}

int RackStrip::posOfSlot (int slot) const
{
    const auto o = proc.getOrder();
    for (int k = 0; k < kSlots; ++k) if (o[(size_t) k] == slot) return k;
    return 0;
}

void RackStrip::toggleOn (int slot)
{
    if (auto* p = proc.apvts.getParameter (slotParam (slot, ParamId::on)))
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
    updateDescription();
    repaint();
}

void RackStrip::showMenu (int slot)
{
    const int pos = posOfSlot (slot);
    const auto type = proc.getSlotType (slot);
    juce::PopupMenu m;
    m.addSectionHeader ("Slot " + juce::String (pos + 1));
    m.addItem (1, juce::String ("Ophan") + midDot() + effectTagline (EffectType::Ophan), true, type == EffectType::Ophan);
    m.addItem (2, juce::String ("OTT Stack") + midDot() + effectTagline (EffectType::OttStack), true, type == EffectType::OttStack);
    m.addItem (3, juce::String ("Legion") + midDot() + effectTagline (EffectType::Legion), true, type == EffectType::Legion);
    m.addItem (4, juce::String ("Psalter") + midDot() + effectTagline (EffectType::Psalter), true, type == EffectType::Psalter);
    m.addSeparator();
    m.addItem (10, "Move left", pos > 0);
    m.addItem (11, "Move right", pos < shown - 1);
    int emptySlot = -1;
    for (int k = 0; k < kSlots && emptySlot < 0; ++k)
    {
        const int s = proc.getOrder()[(size_t) k];
        if (s != slot && proc.getSlotType (s) == EffectType::Empty) emptySlot = s;
    }
    m.addItem (12, "Duplicate", type != EffectType::Empty && emptySlot >= 0);
    m.addItem (13, "Clear slot", type != EffectType::Empty);
    juce::Component::SafePointer<RackStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (cardRect (pos).toNearestInt())),
                     [safe, slot, pos, emptySlot] (int r)
    {
        if (safe == nullptr || r == 0) return;
        auto& pr = safe->proc;
        if (r >= 1 && r <= 4)
        {
            const auto t = (EffectType) r;
            if (pr.getSlotType (slot) != t) { pr.clearSlot (slot); pr.setSlotType (slot, t); }
        }
        else if (r == 10) pr.moveSlot (pos, pos - 1);
        else if (r == 11) pr.moveSlot (pos, pos + 1);
        else if (r == 12 && emptySlot >= 0)
        {
            pr.copySlot (slot, emptySlot);
            // копия встаёт сразу за оригиналом
            pr.moveSlot (safe->posOfSlot (emptySlot), juce::jmin (kSlots - 1, safe->posOfSlot (slot) + 1));
        }
        else if (r == 13) pr.clearSlot (slot);
        safe->updateDescription();
        safe->repaint();
        if (safe->onSelect) safe->onSelect (safe->selected);
    });
}

// ---------------------------------------------------------------------------
void RackStrip::mouseMove (const juce::MouseEvent& e)
{
    const int h = posAt (e.position);
    const int a = arrowVisible (false) && arrowRect (false).contains (e.position) ? -1
                : arrowVisible (true) && arrowRect (true).contains (e.position) ? 1 : 0;
    if (h != hover || a != hoverArrow) { hover = h; hoverArrow = a; repaint(); }
}

void RackStrip::mouseDown (const juce::MouseEvent& e)
{
    // стрелки прокрутки — на страницу (4 места)
    for (bool right : { false, true })
        if (arrowVisible (right) && arrowRect (right).contains (e.position))
        {
            dragFrom = -1;
            scrollBy ((right ? 1.0f : -1.0f) * (cardWidth() + kGap) * (float) kBaseSlots);
            return;
        }
    const int pos = posAt (e.position);
    if (! cardRect (pos).contains (e.position)) return;
    const int slot = proc.getOrder()[(size_t) pos];
    dragFrom = pos;
    dropPos = -1;
    dragging = false;
    if (slot != selected)
    {
        selected = slot;
        if (onSelect) onSelect (slot);
    }
    const bool empty = proc.getSlotType (slot) == EffectType::Empty;
    if (e.mods.isPopupMenu() || menuRect (pos).contains (e.position) || empty) { dragFrom = -1; showMenu (slot); }
    else if (switchRect (pos).expanded (4.0f).contains (e.position)) { dragFrom = -1; toggleOn (slot); }
    updateDescription();
    repaint();
}

void RackStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0) return;
    if (! dragging && e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    // у края — прокрутка
    if (e.position.x < 36.0f) scrollBy (-14.0f);
    else if (e.position.x > (float) getWidth() - 36.0f) scrollBy (14.0f);
    dropPos = posAt (e.position);
    repaint();
}

void RackStrip::mouseUp (const juce::MouseEvent&)
{
    if (dragging && dragFrom >= 0 && dropPos >= 0 && dropPos != dragFrom) proc.moveSlot (dragFrom, dropPos);
    dragging = false;
    dragFrom = dropPos = -1;
    updateDescription();
    repaint();
}

bool RackStrip::keyPressed (const juce::KeyPress& k)
{
    const int pos = posOfSlot (selected);
    const bool move = k.getModifiers().isAltDown() || k.getModifiers().isCommandDown();
    auto select = [this] (int p)
    {
        p = juce::jlimit (0, shown - 1, p);
        const int s = proc.getOrder()[(size_t) p];
        scrollToPos (p);
        selected = s;
        if (onSelect) onSelect (s);
        updateDescription();
        repaint();
    };
    if (k.getKeyCode() == juce::KeyPress::leftKey)
    {
        if (move) { proc.moveSlot (pos, pos - 1); scrollToPos (pos - 1); }
        else select (pos - 1);
        updateDescription();
        repaint();
        return true;
    }
    if (k.getKeyCode() == juce::KeyPress::rightKey)
    {
        if (move && pos + 1 < shown) { proc.moveSlot (pos, pos + 1); scrollToPos (pos + 1); }
        else if (! move) select (pos + 1);
        updateDescription();
        repaint();
        return true;
    }
    if (k == juce::KeyPress::spaceKey) { if (proc.getSlotType (selected) != EffectType::Empty) toggleOn (selected); return true; }
    if (k == juce::KeyPress::returnKey) { showMenu (selected); return true; }
    return false;
}

// ---------------------------------------------------------------------------
void RackStrip::paintCard (juce::Graphics& g, int pos)
{
    const int slot = proc.getOrder()[(size_t) pos];
    const auto type = proc.getSlotType (slot);
    auto c = cardRect (pos);
    const bool sel = slot == selected;
    const bool on = raw (proc, slot, ParamId::on) > 0.5f;

    g.setColour (Colours::surface);
    g.fillRoundedRectangle (c, 12.0f);
    g.setColour (sel ? Colours::ink : Colours::outline);
    g.drawRoundedRectangle (c.reduced (0.5f), 12.0f, 1.0f);
    if (sel && hasKeyboardFocus (false)) LnF::drawFocusRing (g, c.expanded (3.0f), 15.0f);

    auto in = c.reduced (14.0f, 12.0f);
    auto head = in.removeFromTop (24.0f);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink2);
    g.drawText (juce::String (pos + 1), head.removeFromLeft (14.0f), juce::Justification::centredLeft);

    if (type == EffectType::Empty)
    {
        g.setFont (LnF::font (LnF::Type::headline, true));
        g.setColour (Colours::ink2);
        g.drawText ("Empty", head, juce::Justification::centredLeft);
        auto well = in.withTrimmedTop (6.0f);
        g.setColour (Colours::well);
        g.fillRoundedRectangle (well, 8.0f);
        g.setColour (pos == hover ? Colours::ink : Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawText ("+  Add effect", well, juce::Justification::centred);
        return;
    }

    g.setFont (LnF::font (LnF::Type::headline, true));
    g.setColour (on ? Colours::ink : Colours::ink2);
    g.drawText (effectNames()[(int) type], head.removeFromLeft (110.0f), juce::Justification::centredLeft);

    // On (Live) и «⋯»
    LnF::drawSwitch (g, switchRect (pos), on, Colours::live);
    {
        auto m = menuRect (pos);
        g.setColour (pos == hover ? Colours::ink : Colours::ink2);
        for (int i = -1; i <= 1; ++i) g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (m.getCentre().translated ((float) i * 6.0f, 0.0f)));
    }

    // миниатюра кривых
    auto foot = in.removeFromBottom (14.0f);
    in.removeFromTop (6.0f);
    in.removeFromBottom (6.0f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (in, 8.0f);
    const auto& sn = snaps[(size_t) slot];
    const auto& os = ottSnaps[(size_t) slot];
    if (type == EffectType::OttStack && on && os.live)
    {
        // миниатюра карты ступеней: три полосы, ступени сверху вниз
        auto a = in.reduced (8.0f, 5.0f);
        const int n = juce::jlimit (1, OttRecipe::kMaxStages, (int) std::ceil (os.count - 1e-3f));
        const float rowH = a.getHeight() / (float) n, colW = a.getWidth() / 3.0f;
        for (int st = 0; st < n; ++st)
            for (int b = 0; b < OttRecipe::kBands; ++b)
            {
                const float gdb = os.gain[st][b];
                const float t = juce::jlimit (0.0f, 1.0f, std::abs (gdb) / 24.0f);
                g.setColour (Colours::well.interpolatedWith (gdb >= 0.0f ? Colours::carrier : Colours::modulator, 0.12f + 0.88f * t));
                g.fillRect (juce::Rectangle<float> (a.getX() + colW * (float) b + 2.0f, a.getY() + rowH * (float) st, colW - 4.0f, juce::jmax (1.0f, rowH - (rowH > 3.0f ? 1.0f : 0.0f))));
            }
    }
    else if (type == EffectType::Legion && on && legionSnaps[(size_t) slot].live)
    {
        // миниатюра толпы: голоса по стерео и высоте
        legionSnaps[(size_t) slot].paintVoices (g, in.reduced (8.0f, 5.0f), true, Colours::well);
    }
    else if (type == EffectType::Psalter && on && psalterSnaps[(size_t) slot].live)
    {
        // миниатюра струн: три ноты, риски на партиалах (50 Гц – 12 кГц), яркость — Mix ноты
        psalterSnaps[(size_t) slot].paintStrings (g, in.reduced (8.0f, 5.0f), true, Colours::well, 50.0f, 12000.0f);
    }
    else if (type == EffectType::Ophan && on && sn.live)
    {
        auto a = in.reduced (6.0f, 4.0f);
        for (int side : { 1, 0 })
        {
            juce::Path p;
            for (int px = 0; px <= (int) a.getWidth(); px += 2)
            {
                const double f = 20.0 * std::pow (1000.0, (double) px / a.getWidth());
                const float db = (float) (20.0 * std::log10 (juce::jmax (1e-6, std::abs (sn.response (side, f, sampleRate)))));
                const float y = a.getCentreY() - juce::jlimit (-30.0f, 30.0f, db) / 30.0f * a.getHeight() * 0.5f;
                if (px == 0) p.startNewSubPath (a.getX(), y);
                else p.lineTo (a.getX() + (float) px, y);
            }
            g.setColour (side == 0 ? Colours::carrier : Colours::modulator);
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }
    }
    else
    {
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::footnote));
        g.drawText ("Off", in, juce::Justification::centred);
    }

    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink2);
    g.drawText ("Mix " + juce::String (juce::roundToInt (raw (proc, slot, ParamId::mix))) + "%", foot, juce::Justification::centredLeft);
    if (type == EffectType::Legion)
        g.drawText (juce::String (juce::roundToInt (raw (proc, slot, ParamId::voices))) + " voices", foot, juce::Justification::centredRight);
    else if (type == EffectType::Psalter)
    {
        juce::String notes;
        for (int k = 0; k < PsalterRecipe::kNotes; ++k)
            notes << (k > 0 ? " " : "") << (proc.hasPsalterSample (slot, k) ? juce::String ("smp") : PsalterRecipe::noteName (juce::roundToInt (raw (proc, slot, psalterNoteId (k)))));
        g.drawText (notes, foot, juce::Justification::centredRight);
    }
    else
    {
        auto* rightParam = proc.apvts.getParameter (slotParam (slot, type == EffectType::OttStack ? ParamId::count : ParamId::rate));
        g.drawText (rightParam->getCurrentValueAsText(), foot, juce::Justification::centredRight);
    }
}

void RackStrip::paint (juce::Graphics& g)
{
    for (int k = 0; k < shown; ++k)
    {
        if (cardRect (k).getRight() < -kGap || cardRect (k).getX() > (float) getWidth() + kGap) continue;
        if (dragging && k == dragFrom) g.setOpacity (0.55f);
        paintCard (g, k);
        g.setOpacity (1.0f);
        // между слотами: «›» — ход сигнала (Serial), «+» — выходы складываются (Parallel)
        if (k < shown - 1)
        {
            auto c = cardRect (k);
            const float cx = c.getRight() + kGap * 0.5f, cy = c.getCentreY();
            juce::Path chev;
            if (proc.getChainMode() == ChainMode::Parallel)
            {
                chev.startNewSubPath (cx - 4.5f, cy);
                chev.lineTo (cx + 4.5f, cy);
                chev.startNewSubPath (cx, cy - 4.5f);
                chev.lineTo (cx, cy + 4.5f);
            }
            else
            {
                chev.startNewSubPath (cx - 2.5f, cy - 5.0f);
                chev.lineTo (cx + 2.5f, cy);
                chev.lineTo (cx - 2.5f, cy + 5.0f);
            }
            g.setColour (Colours::ink2);
            g.strokePath (chev, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
    // куда встанет слот при перетаскивании
    if (dragging && dropPos >= 0 && dropPos != dragFrom)
    {
        auto c = cardRect (dropPos);
        const float x = juce::jlimit (1.0f, (float) getWidth() - 1.0f, dropPos > dragFrom ? c.getRight() + kGap * 0.5f : c.getX() - kGap * 0.5f);
        g.setColour (Colours::signal);
        g.fillRoundedRectangle (x - 1.0f, c.getY() + 6.0f, 2.0f, c.getHeight() - 12.0f, 1.0f);
    }
    // стрелки прокрутки: круг Surface с рамкой, шеврон Ink
    for (bool right : { false, true })
    {
        if (! arrowVisible (right)) continue;
        auto r = arrowRect (right);
        g.setColour (Colours::surface);
        g.fillEllipse (r);
        g.setColour ((right ? 1 : -1) == hoverArrow ? Colours::ink.withAlpha (0.35f) : Colours::outline);
        g.drawEllipse (r.reduced (0.5f), 1.0f);
        const auto c = r.getCentre();
        const float d = right ? -1.0f : 1.0f;
        juce::Path chev;
        chev.startNewSubPath (c.x + 2.5f * d, c.y - 5.0f);
        chev.lineTo (c.x - 2.5f * d, c.y);
        chev.lineTo (c.x + 2.5f * d, c.y + 5.0f);
        g.setColour (Colours::ink);
        g.strokePath (chev, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}
} // namespace chroma
