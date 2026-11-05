#include "Widgets.h"

namespace chroma
{
using LnF = ChromaLookAndFeel;

juce::String minus() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }

juce::String formatDb (float db, int decimals, float silent)
{
    if (! std::isfinite (db) || db <= silent) return minus() + juce::String::fromUTF8 ("\xe2\x88\x9e");
    auto s = juce::String (std::abs (db), decimals);
    if (db < 0.0f && s != juce::String (0.0f, decimals)) s = minus() + s;
    return s;
}

// ===========================================================================
Segmented::Segmented() { setWantsKeyboardFocus (true); }

void Segmented::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float rad = r.getHeight() * 0.5f;
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    if (items.empty()) return;
    const float inset = 2.0f;
    const float w = (r.getWidth() - inset * 2) / (float) items.size();
    for (size_t i = 0; i < items.size(); ++i)
    {
        // концентрично: радиус сегмента = радиус дорожки − отступ
        auto cell = juce::Rectangle<float> (r.getX() + inset + w * (float) i, r.getY() + inset, w, r.getHeight() - inset * 2);
        const bool sel = items[i].value == selected;
        if (sel)
        {
            g.setColour (Colours::surface);
            g.fillRoundedRectangle (cell, rad - inset);
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (cell.reduced (0.5f), rad - inset, 1.0f);
        }
        g.setColour (sel ? Colours::ink : Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout, sel));
        g.drawText (items[i].text, cell, juce::Justification::centred);
    }
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r, rad);
}

void Segmented::choose (int i)
{
    if (items.empty()) return;
    i = juce::jlimit (0, (int) items.size() - 1, i);
    if (items[(size_t) i].value == selected) return;
    selected = items[(size_t) i].value;
    repaint();
    if (onChange) onChange (selected);
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    choose ((int) ((float) e.x / (float) getWidth() * (float) items.size()));
}

bool Segmented::keyPressed (const juce::KeyPress& k)
{
    int cur = 0;
    for (size_t i = 0; i < items.size(); ++i) if (items[i].value == selected) cur = (int) i;
    if (k.getKeyCode() == juce::KeyPress::leftKey)  { choose (cur - 1); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { choose (cur + 1); return true; }
    return false;
}

// ===========================================================================
void LogoButton::paintButton (juce::Graphics& g, bool over, bool)
{
    // пиксель шрифта = 1 px окна (на Retina — 2 физических): логотип читается именно пиксельным
    // Oxygen — логотип всегда сигнальный (оранжевый), Rubber — цвета текста (белый).
    // Фокус с клавиатуры у логотипа никак не подсвечивается (ни обводкой, ни цветом).
    const auto base = currentTheme() == ThemeId::Oxygen ? Colours::signal : Colours::ink;
    const auto c = over ? base.withAlpha (0.75f) : base;
    LnF::drawPixelText (g, productName(), getLocalBounds(), 1, c);
}

void TextLinkButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    g.setFont (LnF::font (LnF::Type::body, true));
    g.setColour (over || down || hasKeyboardFocus (false) ? Colours::signal : Colours::ink);
    g.drawFittedText (getButtonText(), getLocalBounds(), juce::Justification::centred, 1, 0.8f);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    if (over || down)
    {
        g.setColour (Colours::ink.withAlpha (down ? 0.10f : 0.05f));
        g.fillRoundedRectangle (r, 8.0f);
    }
    g.setColour (Colours::ink);
    const auto c = r.getCentre();
    const auto stroke = juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    switch (kind)
    {
        case Settings:
        {
            auto ir = juce::Rectangle<float> (16.0f, 11.0f).withCentre (c);
            for (int i = 0; i < 2; ++i)
            {
                const float y = ir.getY() + (float) i * ir.getHeight();
                g.drawLine (ir.getX(), y, ir.getRight(), y, 1.4f);
                const float kx = i == 0 ? ir.getX() + 5.0f : ir.getRight() - 5.0f;
                g.setColour (Colours::surface);
                g.fillEllipse (kx - 2.6f, y - 2.6f, 5.2f, 5.2f);
                g.setColour (Colours::ink);
                g.drawEllipse (kx - 2.6f, y - 2.6f, 5.2f, 5.2f, 1.3f);
            }
            break;
        }
        case Pause:
            g.fillRoundedRectangle (c.x - 5.0f, c.y - 6.0f, 3.2f, 12.0f, 1.2f);
            g.fillRoundedRectangle (c.x + 1.8f, c.y - 6.0f, 3.2f, 12.0f, 1.2f);
            break;
        case Play:
        {
            juce::Path p;
            p.addTriangle (c.x - 4.0f, c.y - 6.0f, c.x - 4.0f, c.y + 6.0f, c.x + 6.0f, c.y);
            g.setColour (Colours::signal);   // на паузе — сигнальный: вид заморожен
            g.fillPath (p);
            break;
        }
        case Dice:
        {
            // игральный кубик (как в Mist): квадрат со скруглением и пять точек
            auto d = juce::Rectangle<float> (16.0f, 16.0f).withCentre (c);
            g.drawRoundedRectangle (d, 3.5f, 1.4f);
            const float o = 4.3f, pr = 1.5f;
            for (auto pt : { juce::Point<float> (-o, -o), { o, -o }, { 0, 0 }, { -o, o }, { o, o } })
                g.fillEllipse (c.x + pt.x - pr, c.y + pt.y - pr, pr * 2, pr * 2);
            break;
        }
        case Prev:
        case Next:
        {
            const float d = kind == Prev ? 1.0f : -1.0f;
            juce::Path p;
            p.startNewSubPath (c.x + 2.5f * d, c.y - 5.0f);
            p.lineTo (c.x - 2.5f * d, c.y);
            p.lineTo (c.x + 2.5f * d, c.y + 5.0f);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case Reset:
        {
            // круговая стрелка
            juce::Path p;
            const float rr = 6.0f;
            p.addCentredArc (c.x, c.y, rr, rr, 0.0f, 0.6f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath (p, stroke);
            const float a = 0.6f;
            const juce::Point<float> tip (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
            juce::Path head;
            head.startNewSubPath (tip.x - 3.5f, tip.y - 2.5f);
            head.lineTo (tip);
            head.lineTo (tip.x - 0.5f, tip.y + 4.0f);
            g.strokePath (head, stroke);
            break;
        }
    }
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r, 8.0f);
}

// ===========================================================================
ParamKnob::ParamKnob (const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    slider.getProperties().set ("accent", "accent");
    slider.setMouseDragSensitivity (220);
    slider.setWantsKeyboardFocus (true);
    slider.setTitle (text);
    slider.onValueChange = [this] { updateValue(); };
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (LnF::font (LnF::Type::subheadline, true));
    label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (label);

    value.setJustificationType (juce::Justification::centred);
    value.setFont (LnF::mono (LnF::Type::footnote));
    value.getProperties().set ("dim", true);
    value.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (value);
}

void ParamKnob::updateValue()
{
    value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
    slider.setDescription (label.getText() + " " + value.getText());
}

void ParamKnob::attach (APVTS& state, const juce::String& id)
{
    attachment.reset();
    attachment = std::make_unique<APVTS::SliderAttachment> (state, id, slider);
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    updateValue();
}

void ParamKnob::setBig (bool b)
{
    big = b;
    slider.getProperties().set ("big", b);
    label.setFont (b ? LnF::font (LnF::Type::headline, true) : LnF::font (LnF::Type::subheadline, true));
    value.setFont (b ? LnF::mono (LnF::Type::body) : LnF::mono (LnF::Type::footnote));
    resized();
}

void ParamKnob::resized()
{
    // [круг] [имя] [значение] — по центру ячейки
    auto r = getLocalBounds();
    const int nameH = big ? 18 : 15, valH = big ? 16 : 13;
    const int knob = juce::jlimit (20, big ? 116 : 54, juce::jmin (r.getWidth() - 6, r.getHeight() - nameH - valH - 4));
    auto area = r.withSizeKeepingCentre (r.getWidth(), knob + 4 + nameH + valH);
    slider.setBounds (area.removeFromTop (knob).withSizeKeepingCentre (knob, knob));
    area.removeFromTop (4);
    label.setBounds (area.removeFromTop (nameH));
    value.setBounds (area.removeFromTop (valH));
}

// ===========================================================================
PillChoice::PillChoice (APVTS& s, const juce::String& id, const juce::String& cap)
    : param (s.getParameter (id)), caption (cap)
{
    setWantsKeyboardFocus (true);
    setTitle (cap);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        index = (int) v;
        setDescription (caption + " " + param->getCurrentValueAsText());
        repaint();
    }, nullptr);
    attachment->sendInitialUpdate();
}

void PillChoice::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const float rad = juce::jmin (r.getHeight() * 0.5f, 8.0f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r, rad, 1.0f);
    auto t = getLocalBounds().reduced (12, 0);
    g.setFont (LnF::font (LnF::Type::callout));
    g.setColour (Colours::ink2);
    const int cw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), caption);
    g.drawText (caption, t.removeFromLeft (cw + 6), juce::Justification::centredLeft);
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::callout, true));
    g.drawText (param->getCurrentValueAsText(), t.withTrimmedRight (14), juce::Justification::centredLeft);
    juce::Path arrow;
    const float ax = (float) getWidth() - 16.0f, ay = (float) getHeight() * 0.5f;
    arrow.startNewSubPath (ax - 3, ay - 1.5f); arrow.lineTo (ax, ay + 1.5f); arrow.lineTo (ax + 3, ay - 1.5f);
    g.setColour (Colours::ink2);
    g.strokePath (arrow, juce::PathStrokeType (1.3f));
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r, rad);
}

void PillChoice::openMenu()
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param);
    if (choice == nullptr) return;
    juce::PopupMenu m;
    for (int i = 0; i < choice->choices.size(); ++i)
        m.addItem (i + 1, choice->choices[i], true, i == index);
    juce::Component::SafePointer<PillChoice> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe != nullptr && r > 0) safe->attachment->setValueAsCompleteGesture ((float) (r - 1));
    });
}

bool PillChoice::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) { openMenu(); return true; }
    return false;
}

// ===========================================================================
OutMeter::OutMeter (APVTS& state)
{
    setTitle ("Output");
    gain.setSliderStyle (juce::Slider::LinearBar);
    gain.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    gain.setAlpha (0.0f);   // перетаскивание по шкале меняет громкость, сам слайдер не виден
    gain.setTitle ("Output volume");
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.onValueChange = [this] { repaint(); };
    addAndMakeVisible (gain);
    attachment = std::make_unique<APVTS::SliderAttachment> (state, "output", gain);
}

void OutMeter::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (28);
    r.removeFromRight (52);
    gain.setBounds (r);
}

void OutMeter::setLevel (float peak)
{
    if (std::abs (peak - level) < 1e-5f) return;
    level = peak;
    repaint();
}

void OutMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setFont (LnF::font (LnF::Type::subheadline));
    g.setColour (Colours::ink2);
    g.drawText ("Out", r.removeFromLeft (28), juce::Justification::centredLeft);
    auto txt = r.removeFromRight (52);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink);
    // справа — громкость выхода (ручка), на шкале — уровень сигнала
    g.drawText (formatDb ((float) gain.getValue(), 1) + " dB", txt, juce::Justification::centredRight);
    auto bar = r.reduced (2, 0).toFloat().withSizeKeepingCentre ((float) r.getWidth() - 4.0f, 5.0f);
    g.setColour (Colours::track);
    g.fillRoundedRectangle (bar, 2.5f);
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -1.0f ? Colours::signal : Colours::live);
    if (t > 0) g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * t), 2.5f);
    // отметка громкости
    const float gx = bar.getX() + bar.getWidth() * (float) gain.valueToProportionOfLength (gain.getValue());
    g.setColour (Colours::ink);
    g.fillRoundedRectangle (gx - 1.0f, bar.getY() - 4.0f, 2.0f, bar.getHeight() + 8.0f, 1.0f);
    setDescription ("Output " + formatDb ((float) gain.getValue(), 1) + " dB, level " + formatDb (db, 0) + " dB");
}

// ===========================================================================
void InMeter::setLevel (float peak)
{
    if (std::abs (peak - level) < 1e-5f) return;
    level = peak;
    repaint();
}

void InMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setFont (LnF::font (LnF::Type::subheadline));
    g.setColour (Colours::ink2);
    g.drawText ("In", r.removeFromLeft (22), juce::Justification::centredLeft);
    const float db = juce::Decibels::gainToDecibels (level, -60.0f);
    auto txt = r.removeFromRight (52);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink);
    g.drawText (formatDb (db, 1, -59.9f) + " dB", txt, juce::Justification::centredRight);
    auto bar = r.reduced (2, 0).toFloat().withSizeKeepingCentre ((float) r.getWidth() - 4.0f, 5.0f);
    g.setColour (Colours::track);
    g.fillRoundedRectangle (bar, 2.5f);
    const float t = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (db > -1.0f ? Colours::signal : Colours::live);
    if (t > 0) g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * t), 2.5f);
    setDescription ("Input level " + formatDb (db, 0, -59.9f) + " dB");
}

// ===========================================================================
void disableClickFocus (juce::Component& root)
{
    root.setMouseClickGrabsKeyboardFocus (false);
    for (auto* c : root.getChildren()) disableClickFocus (*c);
}

void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int gap)
{
    const int n = (int) items.size();
    if (n == 0) return;
    const int w = juce::jmin (96, (area.getWidth() - gap * (n - 1)) / n);
    int x = area.getX() + (area.getWidth() - (w * n + gap * (n - 1))) / 2;
    for (auto* c : items)
    {
        c->setBounds (x, area.getY(), w, area.getHeight());
        x += w + gap;
    }
}

void repaintTree (juce::Component& root)
{
    for (auto* c : root.getChildren()) repaintTree (*c);
    root.repaint();
}
} // namespace chroma
