#pragma once
#include "Widgets.h"
#include "../DSP/Ophan.h"
#include "../DSP/OttStack.h"
#include "../DSP/Legion.h"
#include "../DSP/Psalter.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <deque>

namespace chroma
{
// Мини-спектрограмма в тулбаре: пиксельная лента в духе Eido, чистое украшение
class MiniSpectrogram : public juce::Component
{
public:
    MiniSpectrogram();
    void push (const float* x, int n);
    void tick();
    void paint (juce::Graphics& g) override;
    void setSampleRate (double r) { sampleRate = r; }

    static constexpr int kCell = 3, kRows = 8;

private:
    std::vector<float> ring = std::vector<float> (2048, 0.0f);
    int64_t written = 0, lastColumnAt = 0;
    std::deque<std::array<int8_t, kRows>> cols;   // корзина цвета 0…9, −1 — пусто
    juce::dsp::FFT fft { 9 };
    std::vector<float> work = std::vector<float> (1024, 0.0f);
    double sampleRate = 44100.0;
};

// Снимок Ophan для рисования кривых
struct OphanSnapshot
{
    bool live = false;
    float freq[2][OphanRecipe::kBands] {}, gain[2][OphanRecipe::kBands] {}, q[2][OphanRecipe::kBands] {};
    float lowCut = 225.0f, mix = 1.0f;
    void read (const OphanView& v, float mixAmount);
    // комплексная АЧХ цепи стороны side на частоте f (с учётом Mix)
    std::complex<double> response (int side, double f, double sr) const;
};

// Снимок OTT Stack: работа каждой ступени по полосам
struct OttSnapshot
{
    bool live = false;
    float count = 0.0f;
    float gain[OttRecipe::kMaxStages][OttRecipe::kBands] {};
    void read (const OttView& v);
};

// Снимок Legion: голоса толпы (место в стерео, высота, дальность, активность)
struct LegionSnapshot
{
    bool live = false;
    float count = 0.0f, inputHz = 0.0f;
    float pan[LegionRecipe::kMaxVoices] {}, pitch[LegionRecipe::kMaxVoices] {}, near[LegionRecipe::kMaxVoices] {}, level[LegionRecipe::kMaxVoices] {};
    void read (const LegionView& v);
    static constexpr float kPitchRange = 24.0f;   // шкала высоты, ± полутоны
    // высота → −1…+1: шкала сжата к краям (asinh) — разброс в доли полутона и сдвиг на октаву видны на одной картинке
    static float pitchToUnit (float semitones);
    // нарисовать голоса в области: x — стерео, y — высота; small — миниатюра в карточке rack
    void paintVoices (juce::Graphics& g, juce::Rectangle<float> area, bool small, juce::Colour background) const;
};

// Снимок Psalter: три ноты (номер MIDI или сэмпл) и текущий Mix каждой (LFO)
struct PsalterSnapshot
{
    bool live = false;
    int timbre = 0;
    std::array<int, PsalterRecipe::kNotes> note { 72, 74, 76 };
    std::array<bool, PsalterRecipe::kNotes> sample {};
    std::array<float, PsalterRecipe::kNotes> weight {};
    void read (const PsalterView& v);
    // партиалы ноты для рисования: частота относительно ноты и громкость (как в синтезе тембра)
    static void partials (int timbre, std::vector<std::pair<float, float>>& out);
    // нарисовать «струны»: ряд на ноту, риски на партиалах по частоте (лог 20 Гц – 20 кГц), яркость — Mix ноты
    void paintStrings (juce::Graphics& g, juce::Rectangle<float> area, bool small, juce::Colour background, float fMin, float fMax) const;
};

// Главный вид: кривые EQ левого и правого канала поверх спектра выхода (Curves) или осциллограмма (Scope)
class MainView : public juce::Component
{
public:
    enum Mode { Curves = 0, Scope = 1 };
    MainView();
    void push (const float* x, int n);
    void tick();
    void setMode (Mode m);
    Mode getMode() const { return mode; }
    void setSampleRate (double r) { sampleRate = r > 0 ? r : 48000.0; }
    // что показывать: заголовок слота и снимок Ophan (если слот пустой или выключен — только спектр)
    void setSlot (const juce::String& caption, const OphanSnapshot* snap);
    // слот OTT Stack: вместо кривых — карта ступеней (вкладка Stack)
    void setOttSlot (const juce::String& caption, const OttSnapshot* snap);
    // слот Legion: вместо кривых — толпа (вкладка Crowd)
    void setLegionSlot (const juce::String& caption, const LegionSnapshot* snap);
    // слот Psalter: вместо кривых — три ноты над спектром выхода (вкладка Strings)
    void setPsalterSlot (const juce::String& caption, const PsalterSnapshot* snap);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    static constexpr float kDbRange = 30.0f;

private:
    void paintCurves (juce::Graphics& g, juce::Rectangle<float> area);
    void paintGrid (juce::Graphics& g, juce::Rectangle<float> area, bool dbLines);
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> area);
    void paintStack (juce::Graphics& g, juce::Rectangle<float> area);
    void paintCrowd (juce::Graphics& g, juce::Rectangle<float> area);
    void paintStringsView (juce::Graphics& g, juce::Rectangle<float> area);
    void paintScope (juce::Graphics& g, juce::Rectangle<float> area);
    void paintOverlay (juce::Graphics& g);
    juce::Colour background() const;
    float xForFreq (float f, juce::Rectangle<float> a) const;

    Mode mode = Curves;
    double sampleRate = 48000.0;
    juce::String caption;
    OphanSnapshot snap;
    OttSnapshot ottSnap;
    LegionSnapshot legionSnap;
    PsalterSnapshot psalterSnap;
    enum class Content { Eq, Ott, Crowd, Strings };   // что показывает первая вкладка
    Content content = Content::Eq;
    juce::Rectangle<float> tabsArea, legendArea;

    // кольцо выхода
    std::vector<float> ring = std::vector<float> (1 << 15, 0.0f);
    int64_t written = 0, scopeRead = 0, lastFft = 0;
    // спектр
    static constexpr int kFftOrder = 12, kFftSize = 1 << kFftOrder;
    juce::dsp::FFT fft { kFftOrder };
    std::vector<float> fftWork = std::vector<float> (kFftSize * 2, 0.0f);
    std::vector<float> spectrum = std::vector<float> (kFftSize / 2, -120.0f);
    // осциллограмма
    struct Col { float mn = 1, mx = -1, db = -100; };
    std::deque<Col> scopeCols;
};
} // namespace chroma
