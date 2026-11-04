#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace chroma
{
inline juce::String midDot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
// Название плагина: строчными, «о» с макроном
inline juce::String productName() { return juce::String::fromUTF8 ("chr\xc5\x8dma"); }

// ---------------------------------------------------------------------------
// Цвета текущей темы (роли палитры). Меняются целиком через applyTheme().
// Старые имена (bg, panel, ...) оставлены как псевдонимы ролей.
namespace Colours
{
    inline juce::Colour window, surface, well, track, ink, ink2, signal, onSignal, carrier, modulator, live;
    inline juce::Colour outline;                                     // рамка 1 px
    inline std::array<juce::Colour, 3> carrierRamp, modulatorRamp;   // тихо / основной / пик

    // псевдонимы
    inline juce::Colour bg, panel, panelHi, text, textDim, accent, accent2, good, danger, vizBg;
}

enum class ThemeId { Oxygen = 0, Rubber = 1, Light = Oxygen, Dark = Rubber };
enum class ThemeMode { System = 0, Oxygen = 1, Rubber = 2 };

void applyTheme (ThemeId id);
ThemeId currentTheme();

// Системные настройки доступности (macOS: «Уменьшить прозрачность», «Уменьшить движение»)
struct Accessibility
{
    static bool reduceTransparency();
    static bool reduceMotion();
    static void refresh();
};

// Цвет громкости: t = clamp((dB + 24) / 24), до 0.55 — тихо→основной, дальше — основной→пик
juce::Colour rampColour (const std::array<juce::Colour, 3>& ramp, float t);
int rampBucket (float db);   // 10 корзин
juce::Colour rampBucketColour (const std::array<juce::Colour, 3>& ramp, int bucket);

class ChromaLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ChromaLookAndFeel();
    void refreshColours();

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // Типографика: Medeo Sans по шкале macOS, числа — Medeo Mono, Medeo Pixel — только логотип chrōma.
    struct Type
    {
        static constexpr float largeTitle = 26.0f, title1 = 22.0f, title2 = 17.0f, title3 = 15.0f,
                               headline = 13.0f, body = 13.0f, callout = 12.0f, subheadline = 11.0f,
                               footnote = 10.0f, caption = 10.0f;
    };
    static juce::Font font (float size, bool bold = false);
    static juce::Font mono (float size, bool bold = false);
    static juce::Font pixel (float size);

    // Пиксельное слово Medeo Pixel: каждая клетка шрифта — квадрат cell×cell на целых пикселях (чёткие края).
    static juce::Point<int> pixelTextSize (const juce::String& text, int cell);
    static void drawPixelText (juce::Graphics&, const juce::String& text, juce::Rectangle<int> area, int cell, juce::Colour c);

    static void drawSwitch (juce::Graphics&, juce::Rectangle<float> r, bool on, juce::Colour onColour);
    // Плотная плашка (Surface или заданный цвет + рамка 1 px) — тулбар, чипы, вкладки вида, легенда
    static void drawPlate (juce::Graphics&, juce::Rectangle<float> r, float radius, juce::Colour fill = {});
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    static void drawFocusRing (juce::Graphics&, juce::Rectangle<float> r, float radius);
};
} // namespace chroma
