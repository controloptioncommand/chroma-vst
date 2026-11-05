#pragma once
#include "Lfo.h"
#include "Svf.h"
#include <array>
#include <atomic>

namespace chroma
{
// ===========================================================================
// Ophan — «Biblically accurate EQ» одним эффектом.
// Рецепт из Ableton: EQ Eight в режиме L/R, полоса 1 — срез низа, полосы 2–8 — узкие колокола (Adaptive Q, Scale 200%).
// Четыре LFO (Map): Gain левого, Gain правого, Freq левого, Freq правого канала — каждый ведёт свои 7 полос
// в своём диапазоне. Пятый LFO (Stray, ~0.35 Гц, Mod 30–40%) плавает общей скоростью первых четырёх.
// ===========================================================================

namespace OphanRecipe
{
    inline constexpr int kBands = 7;   // полосы 2–8 EQ Eight
    // Частота EQ Eight: 10 Гц … 22 кГц по логарифму; процент Map → Гц
    inline double eqFreq (double pct) { return 10.0 * std::pow (2200.0, pct / 100.0); }
    // Gain EQ Eight: −15 … +15 дБ
    inline double eqGain (double pct) { return -15.0 + 30.0 * pct / 100.0; }
}

// Расширенные настройки (вкладка Advanced): по полосе и стороне — диапазон частоты, диапазон усиления, Q.
// Пишутся в потоке сообщений, читаются в аудиопотоке (атомики, без блокировок).
struct OphanBandSettings
{
    std::atomic<float> fFrom { 1000 }, fTo { 1000 };   // Гц: куда ведёт LFO частоты при 0 и при 1
    std::atomic<float> gFrom { 0 }, gTo { 0 };         // дБ (шкала EQ Eight, до Depth)
    std::atomic<float> q { 10 };
};

struct OphanAdvanced
{
    std::array<std::array<OphanBandSettings, OphanRecipe::kBands>, 2> band;   // [сторона L/R][полоса]
    std::atomic<float> driftRate { 0.35f };                                   // Гц, пятый LFO

    // значения рецепта из видео
    struct Defaults { float fFrom, fTo, gFrom, gTo, q; };
    static Defaults recipe (int side, int b);
    void resetToRecipe();
};

// Макро-ручки (автоматизируются)
struct OphanParams
{
    float rate = 1.0f;       // Гц, общая скорость четырёх LFO
    float drift = 0.35f;     // 0…1, глубина пятого LFO по скорости
    float depth = 2.0f;      // Scale EQ Eight: 0…2 (200%)
    float q = 1.0f;          // множитель Q всех полос
    float spread = 1.0f;     // 0 — правый канал повторяет левый, 1 — независимы
    float lowCut = 225.0f;   // Гц, полоса 1
    int shape = Lfo::Stray;
    float mix = 1.0f;
};

// Текущее состояние для интерфейса (кривые и точки полос)
struct OphanView
{
    std::array<std::array<std::atomic<float>, OphanRecipe::kBands>, 2> freq, gain, q;
    std::atomic<float> lowCut { 225.0f };
    std::atomic<float> lfo[4] {};   // текущие значения LFO 0…1: Gain L, Gain R, Freq L, Freq R
    std::atomic<float> drift { 0.0f };
    std::atomic<bool> live { false };
};

class Ophan
{
public:
    static constexpr int kControl = 32;   // шаг пересчёта коэффициентов, сэмплов

    void prepare (double sampleRate, uint32_t seed);
    void reset();
    // стерео на месте; R == nullptr — моно (только левая цепь)
    void process (float* L, float* R, int n, const OphanParams& p, const OphanAdvanced& adv, OphanView* view);

    // для тестов
    double currentRate() const { return rateNow; }

private:
    void control (const OphanParams& p, const OphanAdvanced& adv, OphanView* view, int len);

    double sr = 48000.0;
    uint32_t seedBase = 1;
    Lfo lfo[4], drifter;   // 0 Gain L, 1 Gain R, 2 Freq L, 3 Freq R
    double rateNow = 1.0;
    // коэффициенты с линейной интерполяцией внутри шага
    struct Ramp { SvfCoefs cur, step; };
    std::array<std::array<Svf, OphanRecipe::kBands>, 2> bands;
    std::array<std::array<Ramp, OphanRecipe::kBands>, 2> ramps;
    std::array<Svf, 2> lowcut;
    double smDepth = 2.0, smQ = 1.0, smMix = 1.0, smLow = 225.0;
    bool first = true;
};
} // namespace chroma
