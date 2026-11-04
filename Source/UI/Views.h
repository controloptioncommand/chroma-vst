#pragma once
#include "Widgets.h"
#include "../DSP/Ophan.h"
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

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    static constexpr float kDbRange = 30.0f;

private:
    void paintCurves (juce::Graphics& g, juce::Rectangle<float> area);
    void paintScope (juce::Graphics& g, juce::Rectangle<float> area);
    void paintOverlay (juce::Graphics& g);
    juce::Colour background() const;
    float xForFreq (float f, juce::Rectangle<float> a) const;

    Mode mode = Curves;
    double sampleRate = 48000.0;
    juce::String caption;
    OphanSnapshot snap;
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
