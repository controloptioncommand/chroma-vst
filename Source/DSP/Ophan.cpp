#include "Ophan.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
constexpr double kPi = 3.141592653589793;
template <typename T> T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
double lerp (double a, double b, double t) { return a + (b - a) * t; }
double logLerp (double a, double b, double t) { return std::exp (lerp (std::log (a), std::log (b), t)); }

// Рецепт из видео. Левый канал — «чётные» частоты, правый — «нечётные» (сдвиг на полшага шкалы EQ Eight).
// Map-диапазоны Gain: 20↔80% через полосу (−9 ↔ +9 дБ), последняя полоса 0↔100% (±15 дБ).
// Map-диапазоны Freq (L): 40–50, 50–60, 60–70, 70–80, 80–90, 40–100, 100–40%.
constexpr float kFreqPct[2][7][2] = {
    { { 40, 50 }, { 50, 60 }, { 60, 70 }, { 70, 80 }, { 80, 90 }, { 40, 100 }, { 100, 40 } },
    { { 45, 55 }, { 55, 65 }, { 65, 75 }, { 75, 85 }, { 85, 95 }, { 35, 95 }, { 95, 35 } },
};
constexpr float kGainPct[7][2] = { { 20, 80 }, { 80, 20 }, { 20, 80 }, { 80, 20 }, { 20, 80 }, { 80, 20 }, { 0, 100 } };
constexpr float kQ[2][7] = {
    { 15.3f, 7.68f, 12.5f, 11.5f, 9.41f, 18.0f, 8.67f },
    { 12.0f, 9.0f, 14.0f, 8.5f, 10.2f, 6.53f, 7.68f },
};
} // namespace

OphanAdvanced::Defaults OphanAdvanced::recipe (int side, int b)
{
    side = clampv (side, 0, 1);
    b = clampv (b, 0, OphanRecipe::kBands - 1);
    return { (float) OphanRecipe::eqFreq (kFreqPct[side][b][0]), (float) OphanRecipe::eqFreq (kFreqPct[side][b][1]),
             (float) OphanRecipe::eqGain (kGainPct[b][0]), (float) OphanRecipe::eqGain (kGainPct[b][1]), kQ[side][b] };
}

void OphanAdvanced::resetToRecipe()
{
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < OphanRecipe::kBands; ++b)
        {
            const auto d = recipe (s, b);
            auto& bs = band[(size_t) s][(size_t) b];
            bs.fFrom = d.fFrom; bs.fTo = d.fTo; bs.gFrom = d.gFrom; bs.gTo = d.gTo; bs.q = d.q;
        }
    driftRate = 0.35f;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
void Ophan::prepare (double sampleRate, uint32_t seed)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    seedBase = seed;
    reset();
}

void Ophan::reset()
{
    // фазы разведены, чтобы при Sine каналы и Gain/Freq не шли строем
    const double phases[4] = { 0.0, 0.5, 0.25, 0.75 };
    for (int i = 0; i < 4; ++i) lfo[i].reset (seedBase * 7u + (uint32_t) i * 97u + 11u, phases[i]);
    drifter.reset (seedBase * 13u + 5u, 0.0);
    for (auto& side : bands) for (auto& b : side) b.clear();
    for (auto& l : lowcut) l.clear();
    first = true;
}

void Ophan::control (const OphanParams& p, const OphanAdvanced& adv, OphanView* view, int len)
{
    const double dt = (double) len / sr;
    const double nyq = sr * 0.45;

    // плавное движение ручек (≈ 30 мс)
    const double a = first ? 1.0 : 1.0 - std::exp (-dt / 0.03);
    smDepth += (p.depth - smDepth) * a;
    smQ += (p.q - smQ) * a;
    smMix += (p.mix - smMix) * a;
    smLow += (p.lowCut - smLow) * a;
    // докатились — встаём ровно в цель (Mix 0 даёт сухой сигнал бит-в-бит)
    if (std::abs (smMix - p.mix) < 1e-3) smMix = p.mix;
    if (std::abs (smDepth - p.depth) < 1e-3) smDepth = p.depth;

    // пятый LFO: Stray, плавает скоростью (Mod: ±2 октавы при 100%)
    const double d = drifter.advance (clampv ((double) adv.driftRate.load (std::memory_order_relaxed), 0.01, 20.0), dt, Lfo::Stray);
    rateNow = clampv ((double) p.rate * std::pow (2.0, 2.0 * (double) p.drift * d), 0.01, 40.0);

    const auto shape = (Lfo::Shape) p.shape;
    double v[4];
    for (int i = 0; i < 4; ++i) v[i] = 0.5 + 0.5 * lfo[i].advance (rateNow, dt, shape);

    const double spread = clampv ((double) p.spread, 0.0, 1.0);
    const double gainV[2] = { v[0], lerp (v[0], v[1], spread) };
    const double freqV[2] = { v[2], lerp (v[2], v[3], spread) };

    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < OphanRecipe::kBands; ++b)
        {
            const auto& L = adv.band[0][(size_t) b];
            const auto& S = adv.band[(size_t) s][(size_t) b];
            auto ld = [] (const std::atomic<float>& x) { return (double) x.load (std::memory_order_relaxed); };
            // правая сторона при Spread < 1 тянется к левой
            double fFrom = ld (L.fFrom), fTo = ld (L.fTo), gFrom = ld (L.gFrom), gTo = ld (L.gTo), q = ld (L.q);
            if (s == 1)
            {
                fFrom = logLerp (clampv (fFrom, 10.0, 22000.0), clampv (ld (S.fFrom), 10.0, 22000.0), spread);
                fTo = logLerp (clampv (fTo, 10.0, 22000.0), clampv (ld (S.fTo), 10.0, 22000.0), spread);
                gFrom = lerp (gFrom, ld (S.gFrom), spread);
                gTo = lerp (gTo, ld (S.gTo), spread);
                q = lerp (q, ld (S.q), spread);
            }
            const double f = clampv (logLerp (clampv (fFrom, 10.0, 22000.0), clampv (fTo, 10.0, 22000.0), freqV[s]), 15.0, nyq);
            const double db = clampv (lerp (gFrom, gTo, gainV[s]) * smDepth, -36.0, 36.0);
            // Adaptive Q: чем больше подъём или вырез, тем уже колокол
            const double qq = clampv (q * smQ * (1.0 + std::abs (db) / 24.0), 0.1, 120.0);

            const auto target = SvfCoefs::bell (f, qq, db, sr);
            auto& r = ramps[(size_t) s][(size_t) b];
            if (first) { r.cur = target; r.step = {}; r.step.a1 = r.step.a2 = r.step.a3 = r.step.m0 = r.step.m1 = r.step.m2 = 0.0f; }
            else
            {
                const float inv = 1.0f / (float) len;
                r.step.a1 = (target.a1 - r.cur.a1) * inv;
                r.step.a2 = (target.a2 - r.cur.a2) * inv;
                r.step.a3 = (target.a3 - r.cur.a3) * inv;
                r.step.m0 = 0.0f;
                r.step.m1 = (target.m1 - r.cur.m1) * inv;
                r.step.m2 = 0.0f;
            }
            if (view != nullptr)
            {
                view->freq[(size_t) s][(size_t) b].store ((float) f, std::memory_order_relaxed);
                view->gain[(size_t) s][(size_t) b].store ((float) db, std::memory_order_relaxed);
                view->q[(size_t) s][(size_t) b].store ((float) qq, std::memory_order_relaxed);
            }
        }

    const auto hp = SvfCoefs::highpass (clampv (smLow, 10.0, nyq), 0.71, sr);
    for (auto& l : lowcut) { l.a1 = hp.a1; l.a2 = hp.a2; l.a3 = hp.a3; l.m0 = hp.m0; l.m1 = hp.m1; l.m2 = hp.m2; }

    if (view != nullptr)
    {
        view->lowCut.store ((float) smLow, std::memory_order_relaxed);
        for (int i = 0; i < 4; ++i) view->lfo[i].store ((float) v[i], std::memory_order_relaxed);
        view->drift.store ((float) d, std::memory_order_relaxed);
        view->live.store (true, std::memory_order_relaxed);
    }
    first = false;
}

void Ophan::process (float* L, float* R, int n, const OphanParams& p, const OphanAdvanced& adv, OphanView* view)
{
    const int channels = R != nullptr ? 2 : 1;
    int pos = 0;
    while (pos < n)
    {
        const int len = std::min (kControl, n - pos);
        const double mixFrom = first ? (double) p.mix : smMix;   // после сброса Mix сразу в цели, без хвоста от прошлого значения
        control (p, adv, view, len);
        const float mix0 = (float) mixFrom, dMix = (float) ((smMix - mixFrom) / len);

        for (int c = 0; c < channels; ++c)
        {
            float* x = (c == 0 ? L : R) + pos;
            auto& chain = bands[(size_t) c];
            auto& rr = ramps[(size_t) c];
            auto& hp = lowcut[(size_t) c];
            for (int i = 0; i < len; ++i)
            {
                const float dry = x[i];
                float y = hp.tick (dry);
                for (int b = 0; b < OphanRecipe::kBands; ++b)
                {
                    auto& f = chain[(size_t) b];
                    auto& r = rr[(size_t) b];
                    f.a1 = r.cur.a1; f.a2 = r.cur.a2; f.a3 = r.cur.a3; f.m0 = 1.0f; f.m1 = r.cur.m1; f.m2 = 0.0f;
                    y = f.tick (y);
                    r.cur.a1 += r.step.a1; r.cur.a2 += r.step.a2; r.cur.a3 += r.step.a3; r.cur.m1 += r.step.m1;
                }
                if (! std::isfinite (y)) { y = 0.0f; for (auto& f : chain) f.clear(); hp.clear(); }
                const float m = mix0 + dMix * (float) i;
                x[i] = dry + (y - dry) * m;
            }
        }
        pos += len;
    }
}
} // namespace chroma
