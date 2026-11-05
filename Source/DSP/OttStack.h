#pragma once
#include "Svf.h"
#include <array>
#include <atomic>
#include <cstdint>

namespace chroma
{
// ===========================================================================
// OTT Stack — несколько OTT подряд (2–40).
// Один OTT — пресет OTT устройства Multiband Dynamics в Ableton: три полосы (кроссоверы 88.3 Гц и 2.5 кГц),
// в каждой — сжатие вниз выше верхнего порога (1:66.7) и вверх ниже нижнего (1:4.17), затем подъём полосы.
// Ступени стопки одинаковые; каналы сжимаются вместе (один детектор на L и R).
// ===========================================================================

namespace OttRecipe
{
    inline constexpr int kBands = 3;                 // Low, Mid, High
    inline constexpr int kMaxStages = 40;
    inline constexpr double kLowX = 88.3, kHighX = 2500.0;
    inline constexpr double kAbove[kBands] = { -33.8, -30.2, -35.5 };   // порог сжатия вниз, дБ
    inline constexpr double kBelow[kBands] = { -40.8, -41.8, -40.8 };   // порог сжатия вверх, дБ
    inline constexpr double kRatioDown = 66.7, kRatioUp = 4.17;
    inline constexpr double kMakeup[kBands] = { 10.3, 5.7, 10.3 };     // подъём полосы, дБ
    inline constexpr double kAttackMs[kBands] = { 47.8, 22.4, 13.5 };
    inline constexpr double kReleaseMs[kBands] = { 282.0, 282.0, 132.0 };
    // ниже этого уровня ступень плавно перестаёт поднимать полосу (и вверх, и подъёмом полосы):
    // иначе 40 ступеней по +10 дБ раздувают шум и хвосты до громкости музыки
    inline constexpr double kUpFloor = -84.0, kUpFull = -66.0;
    // Click guard: быстрый детектор (атака ~1 мс, восстановление — как у полосы). Когда он выше обычной огибающей
    // больше чем на kGuardMargin, усиление считается по нему: на резком нарастании (нота после тишины)
    // ступень не успевает «поднять тихое» и не даёт всплеска. В установившемся звуке разница меньше — рецепт не меняется.
    // И порог тишины движется плавно (kGuardGateMs): после паузы ступень не включает
    // полный подъём тихого (до +29 дБ) на первых, ещё тихих миллисекундах ноты.
    inline constexpr double kGuardAttackMs = 1.0, kGuardMargin = 6.0, kGuardGateMs = 50.0;
}

struct OttParams
{
    float count = 8.0f;      // сколько OTT подряд, 2…40
    float depth = 1.0f;      // Depth каждого OTT (параллельно: сухой ↔ обработанный), 0…1
    float time = 1.0f;       // множитель атаки и восстановления
    float upward = 1.0f;     // доля сжатия вверх, 0…1
    float downward = 1.0f;   // доля сжатия вниз, 0…1
    float outputDb = 0.0f;   // громкость после всей стопки
    float mix = 1.0f;
    bool guard = false;      // Click guard
};

// Для интерфейса: работа каждой ступени по полосам (дБ: + — подъём снизу, − — сжатие сверху)
struct OttView
{
    std::array<std::array<std::atomic<float>, OttRecipe::kBands>, OttRecipe::kMaxStages> gain;
    std::array<std::atomic<float>, OttRecipe::kBands> level;   // уровень полос на выходе стопки, дБ
    std::atomic<float> count { 0.0f };                        // сглаженное число ступеней
    std::atomic<bool> live { false };
};

class OttStack
{
public:
    static constexpr int kControl = 16;   // шаг пересчёта усиления, сэмплов

    void prepare (double sampleRate);
    void reset();
    // стерео на месте; R == nullptr — моно
    void process (float* L, float* R, int n, const OttParams& p, OttView* view);

    double currentCount() const { return countNow; }

private:
    struct Channel
    {
        Svf hiA, hiLp, hiHp;   // кроссовер 2.5 кГц (LR4: общий проход + второй проход для низа и верха)
        Svf loA, loLp, loHp;   // кроссовер 88.3 Гц на нижней части
        Svf ap;                // фазовращатель 88.3 Гц на верхней полосе — сумма полос остаётся ровной
        void clear() { hiA.clear(); hiLp.clear(); hiHp.clear(); loA.clear(); loLp.clear(); loHp.clear(); ap.clear(); }
    };
    struct Stage
    {
        Channel ch[2];
        float env[OttRecipe::kBands] {};           // огибающая (линейная, пик)
        float fast[OttRecipe::kBands] {};          // быстрый детектор для Click guard
        float gate[OttRecipe::kBands] {};          // открытие порога тишины для Click guard (0…1)
        float gain[OttRecipe::kBands] {};          // текущее усиление (линейное)
        float gainStep[OttRecipe::kBands] {};
        float shownDb[OttRecipe::kBands] {};
        bool active = false;
        void clear();
    };

    void control (const OttParams& p, int len);
    template <bool Guard> void runStage (Stage& s, float* xl, float* xr, int len, float w, double* peak);
    void configure (Stage& s);

    double sr = 48000.0;
    std::array<Stage, OttRecipe::kMaxStages> stages;
    SvfCoefs cHiA, cHiLp, cHiHp, cLoA, cLoLp, cLoHp, cAp;
    float kBw = 1.41421356f;
    float att[OttRecipe::kBands] {}, rel[OttRecipe::kBands] {}, attFast = 0.0f;
    bool guardOn = false;
    double countNow = 8.0, outNow = 1.0, mixNow = 1.0, timeNow = 1.0;
    double outLevel[OttRecipe::kBands] {};
    bool first = true;
};
} // namespace chroma
