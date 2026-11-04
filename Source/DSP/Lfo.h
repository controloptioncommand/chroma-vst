#pragma once
#include <cmath>
#include <cstdint>

namespace chroma
{
// LFO с двумя формами, как в устройстве LFO в Ableton:
//  Sine  — обычный синус;
//  Stray — «блуждающая» волна: полупериоды разной длины и высоты, плавно сшитые косинусом.
// Значение — от −1 до +1. Без выделений памяти, свой генератор случайных чисел на объект.
class Lfo
{
public:
    enum Shape { Sine = 0, Stray = 1 };

    void reset (uint32_t seed, double startPhase)
    {
        rng = seed * 2654435761u + 1013904223u;
        phase = startPhase - std::floor (startPhase);
        segPos = 0.0;
        from = 0.0;
        sign = 1.0;
        to = nextTarget();
        segLen = nextLength();
        value = 0.0;
    }

    // шаг на dt секунд при частоте rateHz; возвращает текущее значение
    double advance (double rateHz, double dt, Shape shape)
    {
        const double inc = rateHz * dt;
        phase += inc;
        phase -= std::floor (phase);
        // Stray: сегмент — это полупериод, длина в периодах — segLen
        segPos += inc / segLen;
        while (segPos >= 1.0)
        {
            segPos -= 1.0;
            from = to;
            to = nextTarget();
            segLen = nextLength();
        }
        if (shape == Sine) value = std::sin (phase * 6.283185307179586);
        else value = from + (to - from) * (0.5 - 0.5 * std::cos (segPos * 3.141592653589793));
        return value;
    }

    double get() const { return value; }

private:
    double rand01()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (double) (rng & 0xffffff) / (double) 0xffffff;
    }
    // вершины чередуют знак, высота 35–100%, иногда «недолёт» через ноль
    double nextTarget()
    {
        sign = -sign;
        const double h = 0.35 + 0.65 * rand01();
        return rand01() < 0.12 ? sign * h * 0.25 : sign * h;
    }
    double nextLength() { return 0.5 * (0.55 + 1.0 * rand01()); }   // полупериод ×0.55…1.55

    double phase = 0.0, segPos = 0.0, segLen = 0.5, from = 0.0, to = 1.0, sign = 1.0, value = 0.0;
    uint32_t rng = 1;
};
} // namespace chroma
