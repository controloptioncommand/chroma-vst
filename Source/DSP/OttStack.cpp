#include "OttStack.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
template <typename T> T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double toDb (double x) { return 20.0 * std::log10 (std::max (x, 1e-9)); }

// потолок ступени: до 0 dBFS сигнал не трогается, выше — мягко к +6 dBFS.
// Без него атаки (детектор догоняет 13–48 мс) копят по +10 дБ на каждой ступени и стопка «взрывается».
inline float ceilingClip (float x)
{
    constexpr float knee = 1.0f, room = 1.0f;
    const float a = std::abs (x);
    if (a <= knee) return x;
    return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
}
}

void OttStack::Stage::clear()
{
    for (auto& c : ch) c.clear();
    for (int b = 0; b < OttRecipe::kBands; ++b)
    {
        // огибающая — там, где сжатие вниз ровно съедает подъём полосы: ступень стартует с усилением 0 дБ
        const double neutral = OttRecipe::kAbove[b] + OttRecipe::kMakeup[b] / (1.0 - 1.0 / OttRecipe::kRatioDown);
        env[b] = (float) std::pow (10.0, neutral / 20.0);
        gain[b] = 1.0f;
        gainStep[b] = 0.0f;
        shownDb[b] = 0.0f;
    }
}

void OttStack::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    const double q = 1.0 / std::sqrt (2.0);   // Баттерворт: два прохода — Linkwitz–Riley 24 дБ/окт
    cHiA = SvfCoefs::base (OttRecipe::kHighX, std::sqrt (2.0), sr);
    cHiLp = SvfCoefs::lowpass (OttRecipe::kHighX, q, sr);
    cHiHp = SvfCoefs::highpass (OttRecipe::kHighX, q, sr);
    cLoA = SvfCoefs::base (OttRecipe::kLowX, std::sqrt (2.0), sr);
    cLoLp = SvfCoefs::lowpass (OttRecipe::kLowX, q, sr);
    cLoHp = SvfCoefs::highpass (OttRecipe::kLowX, q, sr);
    cAp = SvfCoefs::allpass (OttRecipe::kLowX, q, sr);
    for (auto& s : stages) configure (s);
    reset();
}

void OttStack::configure (Stage& s)
{
    for (auto& c : s.ch)
    {
        cHiA.applyTo (c.hiA); cHiLp.applyTo (c.hiLp); cHiHp.applyTo (c.hiHp);
        cLoA.applyTo (c.loA); cLoLp.applyTo (c.loLp); cLoHp.applyTo (c.loHp);
        cAp.applyTo (c.ap);
    }
}

void OttStack::reset()
{
    for (auto& s : stages) { s.clear(); s.active = false; }
    for (auto& l : outLevel) l = 0.0;
    first = true;
}

void OttStack::control (const OttParams& p, int len)
{
    const double dt = (double) len / sr;
    const double a = first ? 1.0 : 1.0 - std::exp (-dt / 0.04);
    countNow += (clampv ((double) p.count, 1.0, (double) OttRecipe::kMaxStages) - countNow) * a;
    outNow += (std::pow (10.0, (double) p.outputDb / 20.0) - outNow) * a;
    mixNow += (p.mix - mixNow) * a;
    if (std::abs (countNow - p.count) < 1e-3) countNow = p.count;
    if (std::abs (mixNow - p.mix) < 1e-3) mixNow = p.mix;

    timeNow = clampv ((double) p.time, 0.05, 10.0);
    for (int b = 0; b < OttRecipe::kBands; ++b)
    {
        att[b] = (float) std::exp (-1.0 / (OttRecipe::kAttackMs[b] * 0.001 * timeNow * sr));
        rel[b] = (float) std::exp (-1.0 / (OttRecipe::kReleaseMs[b] * 0.001 * timeNow * sr));
    }

    const double up = clampv ((double) p.upward, 0.0, 1.0) * (1.0 - 1.0 / OttRecipe::kRatioUp);
    const double down = clampv ((double) p.downward, 0.0, 1.0) * (1.0 - 1.0 / OttRecipe::kRatioDown);
    const float inv = 1.0f / (float) len;
    for (int k = 0; k < OttRecipe::kMaxStages; ++k)
    {
        auto& s = stages[(size_t) k];
        const bool on = countNow - (double) k > 1e-4;
        if (on && ! s.active) s.clear();   // ступень включилась — с чистого листа
        s.active = on;
        if (! on) continue;
        for (int b = 0; b < OttRecipe::kBands; ++b)
        {
            const double lev = toDb ((double) s.env[b]);
            const double gate = clampv ((lev - OttRecipe::kUpFloor) / (OttRecipe::kUpFull - OttRecipe::kUpFloor), 0.0, 1.0);
            double g = 0.0;
            if (lev > OttRecipe::kAbove[b]) g -= (lev - OttRecipe::kAbove[b]) * down;
            if (lev < OttRecipe::kBelow[b]) g += (OttRecipe::kBelow[b] - lev) * up * gate;
            s.shownDb[b] = (float) g;
            const float target = (float) std::pow (10.0, (g + OttRecipe::kMakeup[b] * gate) / 20.0);
            s.gainStep[b] = (target - s.gain[b]) * inv;
        }
    }
    first = false;
}

// Одна ступень над блоком из len сэмплов (≤ kControl): состояние фильтров живёт в регистрах весь блок
void OttStack::runStage (Stage& s, float* xl, float* xr, int len, float w, double* peak)
{
    const float k = kBw;
    const bool stereo = xr != nullptr;
    Channel& cl = s.ch[0];
    Channel& cr = s.ch[1];
    float env0 = s.env[0], env1 = s.env[1], env2 = s.env[2];
    float g0 = s.gain[0], g1 = s.gain[1], g2 = s.gain[2];
    const float d0 = s.gainStep[0], d1 = s.gainStep[1], d2 = s.gainStep[2];
    float pk0 = 0, pk1 = 0, pk2 = 0;
    auto split = [k] (Channel& ch, float x, float& lo, float& mid, float& hi)
    {
        float lpA, hpA, lpB, hpB;
        ch.hiA.tickLpHp (x, k, lpA, hpA);
        const float lo2 = ch.hiLp.tick (lpA);
        const float h = ch.hiHp.tick (hpA);
        ch.loA.tickLpHp (lo2, k, lpB, hpB);
        lo = ch.loLp.tick (lpB);
        mid = ch.loHp.tick (hpB);
        hi = ch.ap.tick (h);
    };
    auto follow = [] (float e, float m, float a, float r) { return m > e ? a * e + (1.0f - a) * m : r * e + (1.0f - r) * m; };
    for (int i = 0; i < len; ++i)
    {
        float l0, l1, l2, r0 = 0, r1 = 0, r2 = 0;
        split (cl, xl[i], l0, l1, l2);
        if (stereo) split (cr, xr[i], r0, r1, r2);
        // общий детектор для L и R
        const float m0 = std::max (std::abs (l0), std::abs (r0)), m1 = std::max (std::abs (l1), std::abs (r1)), m2 = std::max (std::abs (l2), std::abs (r2));
        env0 = follow (env0, m0, att[0], rel[0]);
        env1 = follow (env1, m1, att[1], rel[1]);
        env2 = follow (env2, m2, att[2], rel[2]);
        pk0 = std::max (pk0, m0); pk1 = std::max (pk1, m1); pk2 = std::max (pk2, m2);
        g0 += d0; g1 += d1; g2 += d2;
        // Depth — смесь с необработанными полосами (их сумма уже прошла те же фазовые сдвиги)
        const float fl = l0 + l1 + l2, wl = l0 * g0 + l1 * g1 + l2 * g2;
        xl[i] = ceilingClip (fl + w * (wl - fl));
        if (stereo)
        {
            const float fr = r0 + r1 + r2, wr = r0 * g0 + r1 * g1 + r2 * g2;
            xr[i] = ceilingClip (fr + w * (wr - fr));
        }
    }
    s.env[0] = env0; s.env[1] = env1; s.env[2] = env2;
    s.gain[0] = g0; s.gain[1] = g1; s.gain[2] = g2;
    if (peak != nullptr) { peak[0] = pk0; peak[1] = pk1; peak[2] = pk2; }
}

void OttStack::process (float* L, float* R, int n, const OttParams& p, OttView* view)
{
    const bool stereo = R != nullptr;
    const double depth = clampv ((double) p.depth, 0.0, 1.0);
    int pos = 0;
    while (pos < n)
    {
        const int len = std::min (kControl, n - pos);
        const double out0 = outNow, mix0 = mixNow;
        control (p, len);
        const float dOut = (float) ((outNow - out0) / len), dMix = (float) ((mixNow - mix0) / len);
        const int active = std::min (OttRecipe::kMaxStages, (int) std::ceil (countNow - 1e-4));

        float xl[kControl], xr[kControl];
        std::copy (L + pos, L + pos + len, xl);
        if (stereo) std::copy (R + pos, R + pos + len, xr);
        double peak[OttRecipe::kBands] {};
        for (int st = 0; st < active; ++st)
        {
            const float w = (float) (clampv (countNow - (double) st, 0.0, 1.0) * depth);
            runStage (stages[(size_t) st], xl, stereo ? xr : nullptr, len, w, st == active - 1 ? peak : nullptr);
        }

        bool bad = false;
        for (int i = 0; i < len; ++i)
        {
            const float og = (float) out0 + dOut * (float) i, m = (float) mix0 + dMix * (float) i;
            float yl = xl[i] * og;
            if (! std::isfinite (yl)) { yl = 0.0f; bad = true; }
            L[pos + i] += (yl - L[pos + i]) * m;
            if (stereo)
            {
                float yr = xr[i] * og;
                if (! std::isfinite (yr)) { yr = 0.0f; bad = true; }
                R[pos + i] += (yr - R[pos + i]) * m;
            }
        }
        if (bad) reset();

        if (view != nullptr)
        {
            for (int st = 0; st < OttRecipe::kMaxStages; ++st)
                for (int b = 0; b < OttRecipe::kBands; ++b)
                    view->gain[(size_t) st][(size_t) b].store (st < active ? stages[(size_t) st].shownDb[b] * (float) depth : 0.0f, std::memory_order_relaxed);
            for (int b = 0; b < OttRecipe::kBands; ++b)
            {
                outLevel[b] = std::max (peak[b], outLevel[b] * 0.97);
                view->level[(size_t) b].store ((float) toDb (outLevel[b]), std::memory_order_relaxed);
            }
            view->count.store ((float) countNow, std::memory_order_relaxed);
            view->live.store (true, std::memory_order_relaxed);
        }
        pos += len;
    }
}
} // namespace chroma
