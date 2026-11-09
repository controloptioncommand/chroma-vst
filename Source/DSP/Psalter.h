#pragma once
#include "Svf.h"
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace chroma
{
// ===========================================================================
// Psalter — «гармонические волны» по рецепту NEST Acoustics (Serum 2: три Convolve с нотами FM Axe,
// у каждого Mix ведёт свой случайный LFO).
// Вход сворачивается с тремя нотами. Свёртка пропускает только те частоты, которые есть и во входе, и в ноте:
// звук «ловит» гармоники аккорда и звенит ими. Новых тонов не добавляет — отзывается то, что уже есть во входе.
// Mix каждой ноты ведёт свой плавный случайный LFO (S&H со сглаживанием): ноты по очереди проявляются и уходят,
// а когда нота «отдыхает», вместо неё звучит сухой сигнал — как у Convolve с Mix, уведённым вниз.
// Ноты синтезируются (FM, String, Bell, Glass) или берутся из своих сэмплов.
// IR строится в фоновом потоке и передаётся в аудиопоток через «почтовый ящик» (без выделений памяти).
// ===========================================================================

namespace PsalterRecipe
{
    inline constexpr int kNotes = 3;
    // тембр нот; номера — индексы параметра Timbre (новые — только в конец)
    enum Timbre { Fm = 0, String = 1, Bell = 2, Glass = 3, NumTimbres };
    inline constexpr double kMinDecay = 0.2, kMaxDecay = 6.0;   // длина ноты (IR), с
    inline constexpr int kHeadSize = 4096;       // свёртка: голова без задержки, хвост — блоками по 4096
    inline constexpr double kAgcMaxDb = 36.0;    // подстройка громкости ноты под вход: 0…+36 дБ (шум через узкие ноты — тихий)
    inline constexpr double kAgcStartDb = 12.0;
    inline constexpr double kAgcUpSec = 0.8, kAgcDownSec = 0.15;   // вверх медленно, вниз быстрее
    inline constexpr double kEnvSec = 0.3;       // окно громкости входа и нот
    inline constexpr double kHoldSec = 1.0;      // пик входа: пока вход затихает, подстройка замирает (хвост не поднимается)
    inline constexpr double kPan = 0.6;          // ноты 1 и 3 в стерео при Width 100% (нота 2 — по центру)
    inline constexpr double kToneLowHz = 250.0, kToneHighHz = 2000.0;   // Tone: наклон — полки 250 Гц и 2 кГц
    inline constexpr double kToneHighDb = 10.0, kToneLowDb = 5.0;       // при Tone 0 / 100%: верх ∓10 дБ, низ ±5 дБ
    inline constexpr double kMaxMakeup = 2.0;    // ноты «отдыхают» — громкость поднимается, не больше чем ×2 (+6 дБ)
    inline constexpr double kRateJitter = 0.3;   // LFO нот: своя скорость ±30%, отрезок ×0.7…1.3

    double noteHz (int midi);
    juce::String noteName (int midi);   // «C5», «F#3» (C4 = 60 = 261.6 Гц)
}

struct PsalterParams
{
    std::array<int, PsalterRecipe::kNotes> note { 72, 74, 76 };   // MIDI
    float tone = 0.5f;    // 0…1, 0.5 — ровно
    float rate = 1.0f;    // LFO нот, Гц
    float depth = 1.0f;   // насколько глубоко уходит Mix нот, 0…1
    float width = 0.7f;   // стерео мокрого сигнала, 0…1
    float mix = 1.0f;
};

// Для интерфейса
struct PsalterView
{
    PsalterView() { for (auto& w : weight) w.store (0.0f); for (auto& g : gainDb) g.store (0.0f); }
    std::array<std::atomic<float>, PsalterRecipe::kNotes> weight;   // текущий Mix ноты (LFO), 0…1
    std::array<std::atomic<float>, PsalterRecipe::kNotes> gainDb;   // подстройка громкости ноты
    std::atomic<bool> live { false };
};

// Что синтезировать для одной ноты. Ноту из сэмпла задаёт версия сэмпла (тембр и нота тогда не важны).
struct PsalterIrSpec
{
    int timbre = -1, note = -1, decayStep = -1;
    uint32_t sample = 0;
    double sr = 0.0;
    bool operator== (const PsalterIrSpec& o) const
    {
        return timbre == o.timbre && note == o.note && decayStep == o.decayStep && sample == o.sample && sr == o.sr;
    }
    bool operator!= (const PsalterIrSpec& o) const { return ! (*this == o); }
    static int stepOf (double decay);       // Decay квантуется шагами по 2%: автоматизация не пересобирает IR на каждый сдвиг
    static double decayOf (int step);
};

// Синтез ноты (фоновый поток): стерео IR длиной ≈ decay, L и R — разные фазы и чуть разная подстройка партиалов
juce::AudioBuffer<float> psalterSynth (int timbre, double hz, double decaySec, double sr, uint32_t seed);
// Сэмпл как нота: до половины Decay — как есть, дальше затухание до −60 дБ к Decay
juce::AudioBuffer<float> psalterShapeSample (const juce::AudioBuffer<float>& src, double srcRate, double decaySec);
// Нормировка: самая громкая частота IR — 0 дБ (ни одна частота входа не поднимается свёрткой)
void psalterNormalise (juce::AudioBuffer<float>& ir);

class Psalter
{
public:
    static constexpr int kControl = 32;   // шаг пересчёта, сэмплов

    void init (juce::dsp::ConvolutionMessageQueue& queue);   // один раз, поток сообщений
    void prepare (double sampleRate, int maxBlock, uint32_t seed);   // вне аудиопотока; IR, отданные loadNow, станут активны
    void reset();
    // вне аудиопотока, до prepare: IR станет активным сразу
    void loadNow (int k, juce::AudioBuffer<float>&& ir, double rate);
    // фоновый поток: положить IR в ящик; false — ящик занят (аудиопоток ещё не забрал прошлый)
    bool offer (int k, juce::AudioBuffer<float>&& ir, double rate);

    // стерео на месте; R == nullptr — моно
    void process (float* L, float* R, int n, const PsalterParams& p, PsalterView* view);

    // что уже отдано свёртке (ведёт фоновый поток под замком процессора)
    std::array<PsalterIrSpec, PsalterRecipe::kNotes> sent {}, candidate {};
    std::array<double, PsalterRecipe::kNotes> candidateSince {};

private:
    struct Mailbox
    {
        juce::AudioBuffer<float> buf;
        double rate = 48000.0;
        std::atomic<bool> ready { false };
    };
    struct Wander   // плавный случайный LFO: случайные уровни, сшитые косинусом
    {
        double pos = 0.0, len = 1.0, from = 0.0, to = 0.0, value = 0.0, rateMul = 1.0;
    };

    void control (const PsalterParams& p, int len, int offset);
    float nextRand();

    std::array<std::unique_ptr<juce::dsp::Convolution>, PsalterRecipe::kNotes> conv;
    std::array<Mailbox, PsalterRecipe::kNotes> box;
    juce::AudioBuffer<float> dry;   // стерео копия входа куска
    std::array<juce::AudioBuffer<float>, PsalterRecipe::kNotes> wet;
    int maxBlock = 512;
    double sr = 48000.0;
    uint32_t rng = 1, seed = 1;

    std::array<Wander, PsalterRecipe::kNotes> lfo;
    double dryEnv = 0.0, dryHold = 0.0;
    std::array<double, PsalterRecipe::kNotes> wetEnv {}, gainDb {};

    // значения на начало и конец текущего шага пересчёта (линейно между ними)
    std::array<float, PsalterRecipe::kNotes> w0 {}, w1 {}, g0 {}, g1 {};
    float mix0 = 1.0f, mix1 = 1.0f, width0 = 0.7f, width1 = 0.7f;
    double depthNow = 1.0, widthNow = 0.7, mixNow = 1.0, toneNow = 0.5, toneSet = -1.0;
    Svf lowL, lowR, highL, highR;
    SvfCoefs lowC0, lowC1, highC0, highC1;
    bool toneMoving = false, first = true;
};
} // namespace chroma
