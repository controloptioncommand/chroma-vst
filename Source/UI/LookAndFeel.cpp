#include "LookAndFeel.h"
#include "BinaryData.h"
#include <map>

namespace chroma
{
// ---------------------------------------------------------------------------
// Палитры (из дизайна)
namespace
{
struct Palette
{
    juce::uint32 window, surface, well, track, ink, ink2, signal, onSignal, carrier, modulator, live;
    std::array<juce::uint32, 3> carrierRamp, modulatorRamp;
};

const Palette oxygen {
    0xffE4E5E1, 0xffF5F5F2, 0xffE9EAE6, 0xffC2C3BE, 0xff131416, 0xff4B4D52, 0xffE84A00, 0xff131416,
    0xff2740F0, 0xffD7245E, 0xff138A62,
    { 0xff6676E6, 0xff2740F0, 0xffE84A00 }, { 0xffD4577F, 0xffD7245E, 0xffE84A00 } };

const Palette rubber {
    0xff0F1012, 0xff18191C, 0xff121316, 0xff3E3F45, 0xffEDEDE9, 0xffA9AAAE, 0xffFF6B2C, 0xff131416,
    0xff8EA0FF, 0xffFF7AA2, 0xff4CD9A6,
    { 0xff5C69C9, 0xff8EA0FF, 0xffFF6B2C }, { 0xffC25A82, 0xffFF7AA2, 0xffFF6B2C } };

ThemeId gTheme = ThemeId::Oxygen;
bool gReduceTransparency = false, gReduceMotion = false;

bool readMacDefault (const char* key)
{
   #if JUCE_MAC
    juce::ChildProcess p;
    if (p.start (juce::StringArray { "/usr/bin/defaults", "read", "com.apple.universalaccess", key }))
    {
        const auto out = p.readAllProcessOutput().trim();
        p.waitForProcessToFinish (500);
        return out == "1";
    }
   #else
    juce::ignoreUnused (key);
   #endif
    return false;
}
} // namespace

ThemeId currentTheme() { return gTheme; }

void applyTheme (ThemeId id)
{
    gTheme = id;
    const auto& p = id == ThemeId::Rubber ? rubber : oxygen;
    using C = juce::Colour;
    Colours::window = C (p.window);   Colours::surface = C (p.surface); Colours::well = C (p.well);
    Colours::track = C (p.track);     Colours::ink = C (p.ink);         Colours::ink2 = C (p.ink2);
    Colours::signal = C (p.signal);   Colours::onSignal = C (p.onSignal);
    Colours::carrier = C (p.carrier); Colours::modulator = C (p.modulator); Colours::live = C (p.live);
    Colours::outline = Colours::ink.withAlpha (0.12f);
    for (int i = 0; i < 3; ++i)
    {
        Colours::carrierRamp[(size_t) i] = C (p.carrierRamp[(size_t) i]);
        Colours::modulatorRamp[(size_t) i] = C (p.modulatorRamp[(size_t) i]);
    }

    Colours::bg = Colours::window;   Colours::panel = Colours::surface; Colours::panelHi = Colours::well;
    Colours::text = Colours::ink;    Colours::textDim = Colours::ink2;
    Colours::accent = Colours::signal; Colours::accent2 = Colours::signal;
    Colours::good = Colours::live;   Colours::danger = Colours::signal;
    Colours::vizBg = Colours::well;
}

bool Accessibility::reduceTransparency() { return gReduceTransparency; }
bool Accessibility::reduceMotion()       { return gReduceMotion; }
void Accessibility::refresh()
{
    gReduceTransparency = readMacDefault ("reduceTransparency");
    gReduceMotion = readMacDefault ("reduceMotion");
}

juce::Colour rampColour (const std::array<juce::Colour, 3>& ramp, float t)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    if (t < 0.55f) return ramp[0].interpolatedWith (ramp[1], t / 0.55f);
    return ramp[1].interpolatedWith (ramp[2], (t - 0.55f) / 0.45f);
}

int rampBucket (float db)
{
    const float t = juce::jlimit (0.0f, 1.0f, (db + 24.0f) / 24.0f);
    return juce::jmin (9, (int) (t * 10.0f));
}

juce::Colour rampBucketColour (const std::array<juce::Colour, 3>& ramp, int bucket)
{
    return rampColour (ramp, ((float) bucket + 0.5f) / 10.0f);
}

// ---------------------------------------------------------------------------
// Шрифты
namespace
{
// Шрифты Medeo (SIL OFL), встроены через BinaryData:
// Sans — весь интерфейс, Mono — числа и значения, Pixel — только логотип chrōma
struct Typefaces
{
    juce::Typeface::Ptr sans, sansBold, mono, monoBold, pixel;
    Typefaces()
    {
        auto load = [] (const void* d, int n) { return juce::Typeface::createSystemTypefaceFor (d, (size_t) n); };
        sans     = load (ChromaFonts::MedeoSansRegular_ttf,  ChromaFonts::MedeoSansRegular_ttfSize);
        sansBold = load (ChromaFonts::MedeoSansSemiBold_ttf, ChromaFonts::MedeoSansSemiBold_ttfSize);
        mono     = load (ChromaFonts::MedeoMonoRegular_ttf,  ChromaFonts::MedeoMonoRegular_ttfSize);
        monoBold = load (ChromaFonts::MedeoMonoSemiBold_ttf, ChromaFonts::MedeoMonoSemiBold_ttfSize);
        pixel    = load (ChromaFonts::MedeoPixelRegular_ttf, ChromaFonts::MedeoPixelRegular_ttfSize);
    }
};
const Typefaces& typefaces() { static const Typefaces t; return t; }
juce::Font makeFont (juce::Typeface::Ptr tf, float size) { return juce::Font (juce::FontOptions (tf).withHeight (size)); }
} // namespace

juce::Font ChromaLookAndFeel::font (float size, bool bold)  { return makeFont (bold ? typefaces().sansBold : typefaces().sans, size); }
juce::Font ChromaLookAndFeel::mono (float size, bool bold)  { return makeFont (bold ? typefaces().monoBold : typefaces().mono, size); }
juce::Font ChromaLookAndFeel::pixel (float size)            { return makeFont (typefaces().pixel, size); }

// ---------------------------------------------------------------------------
// Сетка пикселей слова: набираем его Medeo Pixel крупно и снимаем клетки (шаг шрифта — 38 единиц при 1000 UPM)
namespace
{
struct PixelGrid { int cols = 0, rows = 0; std::vector<bool> on; };
const PixelGrid& pixelGrid (const juce::String& text)
{
    static std::map<juce::String, PixelGrid> cache;
    auto it = cache.find (text);
    if (it != cache.end()) return it->second;
    PixelGrid grid;
    const float h = 400.0f;   // высота шрифта; em = h / 1.3, клетка = 0.038 em ≈ 11.7 px
    auto f = ChromaLookAndFeel::pixel (h);
    const int w = (int) juce::GlyphArrangement::getStringWidth (f, text) + 20;
    juce::Image img (juce::Image::ARGB, juce::jmax (1, w), (int) h + 20, true);
    {
        juce::Graphics g (img);
        g.setColour (juce::Colours::white);
        g.setFont (f);
        g.drawText (text, img.getBounds(), juce::Justification::centred);
    }
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int x0 = img.getWidth(), y0 = img.getHeight(), x1 = -1, y1 = -1;
    for (int y = 0; y < img.getHeight(); ++y)
        for (int x = 0; x < img.getWidth(); ++x)
            if (bd.getPixelColour (x, y).getAlpha() > 127)
                { x0 = juce::jmin (x0, x); y0 = juce::jmin (y0, y); x1 = juce::jmax (x1, x); y1 = juce::jmax (y1, y); }
    if (x1 >= x0)
    {
        const float cell = h / 1.3f * 0.038f;
        grid.cols = juce::roundToInt ((float) (x1 - x0 + 1) / cell);
        grid.rows = juce::roundToInt ((float) (y1 - y0 + 1) / cell);
        const float cw = (float) (x1 - x0 + 1) / (float) grid.cols, ch = (float) (y1 - y0 + 1) / (float) grid.rows;
        grid.on.resize ((size_t) (grid.cols * grid.rows));
        for (int j = 0; j < grid.rows; ++j)
            for (int i = 0; i < grid.cols; ++i)
                grid.on[(size_t) (j * grid.cols + i)] = bd.getPixelColour ((int) ((float) x0 + ((float) i + 0.5f) * cw),
                                                                            (int) ((float) y0 + ((float) j + 0.5f) * ch)).getAlpha() > 127;
    }
    return cache.emplace (text, std::move (grid)).first->second;
}
} // namespace

juce::Point<int> ChromaLookAndFeel::pixelTextSize (const juce::String& text, int cell)
{
    const auto& gr = pixelGrid (text);
    return { gr.cols * cell, gr.rows * cell };
}

void ChromaLookAndFeel::drawPixelText (juce::Graphics& g, const juce::String& text, juce::Rectangle<int> area, int cell, juce::Colour c)
{
    const auto& gr = pixelGrid (text);
    const int x0 = area.getX() + (area.getWidth() - gr.cols * cell) / 2, y0 = area.getY() + (area.getHeight() - gr.rows * cell) / 2;
    juce::RectangleList<int> cells;
    for (int j = 0; j < gr.rows; ++j)
        for (int i = 0; i < gr.cols; ++i)
            if (gr.on[(size_t) (j * gr.cols + i)]) cells.addWithoutMerging ({ x0 + i * cell, y0 + j * cell, cell, cell });
    g.setColour (c);
    g.fillRectList (cells);
}

juce::Typeface::Ptr ChromaLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // всё, что рисуется шрифтом «по умолчанию» (меню, диалоги, поля ввода), — Medeo Sans
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName() || f.getTypefaceName().isEmpty())
        return f.isBold() ? typefaces().sansBold : typefaces().sans;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

// ---------------------------------------------------------------------------
ChromaLookAndFeel::ChromaLookAndFeel()
{
    if (Colours::window == juce::Colour()) applyTheme (ThemeId::Oxygen);
    refreshColours();
}

void ChromaLookAndFeel::refreshColours()
{
    using namespace Colours;
    setColour (juce::ResizableWindow::backgroundColourId, window);
    setColour (juce::Label::textColourId, ink);
    setColour (juce::Slider::textBoxTextColourId, ink2);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, well);
    setColour (juce::ComboBox::textColourId, ink);
    setColour (juce::ComboBox::outlineColourId, outline);
    setColour (juce::ComboBox::arrowColourId, ink2);
    setColour (juce::PopupMenu::backgroundColourId, surface);
    setColour (juce::PopupMenu::textColourId, ink);
    setColour (juce::PopupMenu::headerTextColourId, ink2);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, signal);
    setColour (juce::PopupMenu::highlightedTextColourId, onSignal);
    setColour (juce::TextButton::buttonColourId, well);
    setColour (juce::TextButton::buttonOnColourId, signal);
    setColour (juce::TextButton::textColourOffId, ink);
    setColour (juce::TextButton::textColourOnId, onSignal);
    setColour (juce::TextEditor::backgroundColourId, well);
    setColour (juce::TextEditor::textColourId, ink);
    setColour (juce::TextEditor::highlightColourId, signal.withAlpha (0.3f));
    setColour (juce::TextEditor::outlineColourId, outline);
    setColour (juce::TextEditor::focusedOutlineColourId, signal);
    setColour (juce::CaretComponent::caretColourId, ink);
    setColour (juce::AlertWindow::backgroundColourId, surface);
    setColour (juce::AlertWindow::textColourId, ink);
    setColour (juce::AlertWindow::outlineColourId, outline);
}

static juce::Colour accentOf (juce::Component& c)
{
    const auto v = c.getProperties()["accent"].toString();
    if (v.isEmpty() || v == "accent" || v == "signal") return Colours::signal;
    if (v == "good" || v == "live") return Colours::live;
    return juce::Colour::fromString (v);
}

void ChromaLookAndFeel::drawFocusRing (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour (Colours::signal);
    g.drawRoundedRectangle (r.reduced (1.0f), juce::jmax (0.0f, radius - 1.0f), 2.0f);
}

void ChromaLookAndFeel::drawPlate (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill)
{
    // плотная плашка: заливка + рамка 1 px. Без стекла, бликов и теней.
    g.setColour (fill.isTransparent() ? Colours::surface : fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void ChromaLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                        float startAngle, float endAngle, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
    const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float lineW = 3.0f;   // подложка и дуга значения — 3 px
    const float arcR = r - lineW * 0.5f;
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0, startAngle, endAngle, true);
    g.setColour (Colours::track);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // двуполярные — дуга растёт от центра
    const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    if (s.isEnabled() && std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, arcR, arcR, 0, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (accentOf (s));
        g.strokePath (val, juce::PathStrokeType (lineW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // риска-указатель: тонкая линия Ink от центра к дуге, без колпачка
    const float p1 = arcR - lineW - 2.0f;
    const auto dir = juce::Point<float> (std::sin (angle), -std::cos (angle));
    juce::Path tick;
    tick.startNewSubPath (c);
    tick.lineTo (c + dir * p1);
    g.setColour (s.isEnabled() ? Colours::ink : Colours::ink2);
    g.strokePath (tick, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (s.hasKeyboardFocus (false))
        drawFocusRing (g, juce::Rectangle<float> (c.x - r - 3.0f, c.y - r - 3.0f, (r + 3.0f) * 2, (r + 3.0f) * 2), r + 3.0f);
}

void ChromaLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& s)
{
    auto b = juce::Rectangle<int> (x, y, w, h).toFloat();
    if (style == juce::Slider::LinearHorizontal || style == juce::Slider::LinearBar)
    {
        auto tr = b.withSizeKeepingCentre (b.getWidth(), 4.0f);
        g.setColour (Colours::track);
        g.fillRoundedRectangle (tr, 2.0f);
        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float mid = bipolar ? (float) s.getPositionOfValue (0.0) : tr.getX();
        g.setColour (accentOf (s));
        g.fillRoundedRectangle (juce::Rectangle<float> (juce::jmin (mid, pos), tr.getY(), std::abs (pos - mid), 4.0f), 2.0f);
        g.setColour (Colours::surface);
        g.fillEllipse (pos - 6.0f, b.getCentreY() - 6.0f, 12.0f, 12.0f);
        g.setColour (Colours::ink2);
        g.drawEllipse (pos - 6.0f, b.getCentreY() - 6.0f, 12.0f, 12.0f, 1.0f);
        if (s.hasKeyboardFocus (false))
            drawFocusRing (g, b, 6.0f);
    }
    else
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s);
    }
}

void ChromaLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    // чип: видимая капсула может быть ниже зоны нажатия (property "chipH")
    auto full = b.getLocalBounds().toFloat();
    auto r = full;
    if (b.getProperties().contains ("chipH"))
        r = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), (float) (double) b.getProperties()["chipH"]));
    r = r.reduced (0.5f);
    const float rad = juce::jmin (r.getHeight() * 0.5f, 8.0f);   // контролы — радиус 8, капсулы только у сегментов и переключателей

    if (b.getProperties()["plate"])
        drawPlate (g, r, rad);
    else
    {
        auto base = b.getToggleState() ? Colours::signal : Colours::well;
        if (over) base = base.overlaidWith (Colours::ink.withAlpha (0.04f));
        if (down) base = base.overlaidWith (Colours::ink.withAlpha (0.08f));
        g.setColour (base);
        g.fillRoundedRectangle (r, rad);
        if (! b.getProperties()["noOutline"])
        {
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (r, rad, 1.0f);
        }
    }
    if (b.hasKeyboardFocus (false))
        drawFocusRing (g, r.expanded (2.0f), rad + 2.0f);
}

void ChromaLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (! b.isEnabled() ? Colours::ink2 : b.getToggleState() ? Colours::onSignal : Colours::ink);
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (8, 0), juce::Justification::centred, true);
}

void ChromaLookAndFeel::drawSwitch (juce::Graphics& g, juce::Rectangle<float> r, bool on, juce::Colour onColour)
{
    const float rad = r.getHeight() * 0.5f;
    g.setColour (on ? onColour : Colours::track);
    g.fillRoundedRectangle (r, rad);
    const float d = r.getHeight() - 4.0f;
    const float kx = on ? r.getRight() - d - 2.0f : r.getX() + 2.0f;
    g.setColour (Colours::surface);
    g.fillEllipse (kx, r.getY() + 2.0f, d, d);
    g.setColour (Colours::outline);
    g.drawEllipse (kx, r.getY() + 2.0f, d, d, 1.0f);
}

void ChromaLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat();
    const float sh = juce::jmin (18.0f, r.getHeight() - 4.0f);
    auto sw = juce::Rectangle<float> (r.getX() + 2, r.getCentreY() - sh * 0.5f, sh * 1.8f, sh);
    drawSwitch (g, sw, b.getToggleState(), b.isEnabled() ? accentOf (b) : Colours::ink2);
    g.setColour (b.isEnabled() ? Colours::ink : Colours::ink2);
    g.setFont (font (Type::callout));
    g.drawText (b.getButtonText(), r.withLeft (sw.getRight() + 7), juce::Justification::centredLeft);
    if (b.hasKeyboardFocus (false))
        drawFocusRing (g, sw.expanded (3.0f), sh * 0.5f + 3.0f);
}

void ChromaLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<int> (0, 0, w, h).toFloat().reduced (0.5f);
    const float rad = juce::jmin (r.getHeight() * 0.5f, 8.0f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, rad);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r, rad, 1.0f);
    const float ax = (float) w - 13.0f, ay = (float) h * 0.5f;
    juce::Path arrow;
    arrow.startNewSubPath (ax - 3.5f, ay - 1.5f);
    arrow.lineTo (ax, ay + 2.0f);
    arrow.lineTo (ax + 3.5f, ay - 1.5f);
    g.setColour (Colours::ink2);
    g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (box.hasKeyboardFocus (true))
        drawFocusRing (g, r, rad);
}

void ChromaLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void ChromaLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Colours::surface);
    g.setColour (Colours::outline);
    g.drawRect (0, 0, w, h);
}

juce::Font ChromaLookAndFeel::getComboBoxFont (juce::ComboBox&) { return font (Type::callout); }
juce::Font ChromaLookAndFeel::getLabelFont (juce::Label& l)   { return l.getFont(); }
juce::Font ChromaLookAndFeel::getPopupMenuFont()               { return font (Type::body); }
juce::Font ChromaLookAndFeel::getTextButtonFont (juce::TextButton&, int h) { return font (juce::jmin (Type::callout, (float) h * 0.55f)); }

juce::Label* ChromaLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (mono (Type::footnote));
    l->setColour (juce::Label::textColourId, Colours::ink2);
    l->setColour (juce::Label::outlineWhenEditingColourId, Colours::signal);
    l->setColour (juce::Label::backgroundWhenEditingColourId, Colours::surface);
    l->setColour (juce::TextEditor::textColourId, Colours::ink);
    return l;
}

void ChromaLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (l.isBeingEdited()) { LookAndFeel_V4::drawLabel (g, l); return; }
    // «dim» — второй текст (Ink 2); цвет берётся из темы при каждой отрисовке
    g.setColour (l.getProperties()["dim"] ? Colours::ink2 : l.findColour (juce::Label::textColourId));
    g.setFont (l.getFont());
    g.drawFittedText (l.getText(), l.getLocalBounds(), l.getJustificationType(), 1, 0.9f);
}
} // namespace chroma
