#pragma once
#include <cmath>

namespace chroma
{
// Фильтр-переменная состояния (Simper / Cytomic SVF): устойчив при быстрой модуляции.
// Выход = m0·вход + m1·полоса + m2·низ; коэффициенты собирают колокол, срезы и фазовращатель.
struct Svf
{
    float a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;   // коэффициенты
    float ic1 = 0, ic2 = 0;                                // состояние
    inline float tick (float v0)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        return m0 * v0 + m1 * v1 + m2 * v2;
    }
    // низ и верх одного прохода (для кроссовера): lp = v2, hp = v0 − k·v1 − v2
    inline void tickLpHp (float v0, float k, float& lp, float& hp)
    {
        const float v3 = v0 - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        lp = v2;
        hp = v0 - k * v1 - v2;
    }
    void clear() { ic1 = ic2 = 0; }
};

struct SvfCoefs
{
    float a1 = 1, a2 = 0, a3 = 0, m0 = 1, m1 = 0, m2 = 0;

    static SvfCoefs base (double f, double k, double sr)
    {
        SvfCoefs c;
        const double g = std::tan (3.141592653589793 * f / sr);
        const double a1 = 1.0 / (1.0 + g * (g + k));
        c.a1 = (float) a1;
        c.a2 = (float) (g * a1);
        c.a3 = (float) (g * g * a1);
        return c;
    }
    static SvfCoefs bell (double f, double q, double db, double sr)
    {
        const double A = std::pow (10.0, db / 40.0), k = 1.0 / (q * A);
        auto c = base (f, k, sr);
        c.m0 = 1.0f; c.m1 = (float) (k * (A * A - 1.0)); c.m2 = 0.0f;
        return c;
    }
    static SvfCoefs highpass (double f, double q, double sr)
    {
        const double k = 1.0 / q;
        auto c = base (f, k, sr);
        c.m0 = 1.0f; c.m1 = (float) -k; c.m2 = -1.0f;
        return c;
    }
    static SvfCoefs lowpass (double f, double q, double sr)
    {
        auto c = base (f, 1.0 / q, sr);
        c.m0 = 0.0f; c.m1 = 0.0f; c.m2 = 1.0f;
        return c;
    }
    static SvfCoefs allpass (double f, double q, double sr)
    {
        const double k = 1.0 / q;
        auto c = base (f, k, sr);
        c.m0 = 1.0f; c.m1 = (float) (-2.0 * k); c.m2 = 0.0f;
        return c;
    }
    void applyTo (Svf& s) const { s.a1 = a1; s.a2 = a2; s.a3 = a3; s.m0 = m0; s.m1 = m1; s.m2 = m2; }
};
} // namespace chroma
