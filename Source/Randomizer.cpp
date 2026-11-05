#include "Randomizer.h"

namespace chroma
{
juce::String RandomizerOptions::name (Group g)
{
    switch (g)
    {
        case Ophan:         return "Ophan";
        case OttStack:      return "OTT Stack";
        case Order:         return "Slot order";
        case Mix:           return "Slot mix";
        case OphanAdvanced: return "Ophan advanced (band recipe)";
        case NumGroups:     break;
    }
    return {};
}

juce::String RandomizerOptions::prefKey (Group g)
{
    static const char* keys[] = { "ophan", "ott", "order", "mix", "advanced" };
    return juce::String ("dice_") + keys[(int) g];
}

void randomizeRack (ChromaProcessor& p, juce::Random& rng, const RandomizerOptions& opt)
{
    using G = RandomizerOptions;
    auto set = [&p] (int slot, const char* id, float v)
    {
        if (auto* prm = p.apvts.getParameter (slotParam (slot, id)))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    auto uni = [&rng] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
    auto logUni = [&rng] (float a, float b) { return a * std::pow (b / a, rng.nextFloat()); };

    // Случайные числа тянем всегда, даже для запрещённых групп и пустых слотов: одна и та же
    // последовательность кубика не зависит от настроек и содержимого rack (предсказуемо и удобно для теста).
    for (int s = 0; s < kSlots; ++s)
    {
        const auto type = p.getSlotType (s);
        const bool ophan = type == EffectType::Ophan, ott = type == EffectType::OttStack;
        auto pick = [&] (bool fits, G::Group g, const char* id, float v) { if (fits && opt.allows (g)) set (s, id, v); };

        // Ophan: макро-ручки
        pick (ophan, G::Ophan, ParamId::rate, logUni (0.08f, 6.0f));
        pick (ophan, G::Ophan, ParamId::drift, uni (0.0f, 70.0f));
        pick (ophan, G::Ophan, ParamId::depth, uni (60.0f, 200.0f));
        pick (ophan, G::Ophan, ParamId::q, logUni (50.0f, 250.0f));
        { const float v = rng.nextFloat() < 0.15f ? 0.0f : uni (40.0f, 100.0f); pick (ophan, G::Ophan, ParamId::spread, v); }
        pick (ophan, G::Ophan, ParamId::lowCut, logUni (40.0f, 600.0f));
        pick (ophan, G::Ophan, ParamId::shape, rng.nextFloat() < 0.7f ? 1.0f : 0.0f);

        // OTT Stack: чаще мало ступеней, изредка — башня
        { const float r = rng.nextFloat(); pick (ott, G::OttStack, ParamId::count, (float) juce::roundToInt (2.0f + 38.0f * r * r * r)); }
        pick (ott, G::OttStack, ParamId::ottDepth, uni (30.0f, 100.0f));
        pick (ott, G::OttStack, ParamId::time, logUni (30.0f, 300.0f));
        pick (ott, G::OttStack, ParamId::upward, uni (30.0f, 100.0f));
        pick (ott, G::OttStack, ParamId::downward, uni (30.0f, 100.0f));

        // Mix — у любого эффекта
        pick (ophan || ott, G::Mix, ParamId::mix, uni (50.0f, 100.0f));

        // рецепт полос Ophan: диапазон частоты вокруг случайной точки шкалы EQ Eight, усиление ±(3…15) дБ, Q 4–18
        auto& a = p.advanced (s);
        const bool advOk = ophan && opt.allows (G::OphanAdvanced);
        for (int side = 0; side < 2; ++side)
            for (int b = 0; b < OphanRecipe::kBands; ++b)
            {
                const float centre = uni (35.0f, 95.0f), width = uni (4.0f, 30.0f);
                const bool up = rng.nextBool();
                const float lo = juce::jlimit (25.0f, 100.0f, centre - width * 0.5f), hi = juce::jlimit (25.0f, 100.0f, centre + width * 0.5f);
                const float amount = uni (3.0f, 15.0f);
                const bool boostFirst = rng.nextBool();
                const float q = logUni (4.0f, 18.0f);
                if (! advOk) continue;
                auto& bs = a.band[(size_t) side][(size_t) b];
                bs.fFrom = (float) OphanRecipe::eqFreq (up ? lo : hi);
                bs.fTo = (float) OphanRecipe::eqFreq (up ? hi : lo);
                bs.gFrom = boostFirst ? amount : -amount;
                bs.gTo = boostFirst ? -amount : amount;
                bs.q = q;
            }
        { const float v = logUni (0.15f, 1.0f); if (advOk) a.driftRate = v; }
    }

    // порядок: эффекты меняются местами только между занятыми местами (пустые места остаются на своих —
    // эффект не уедет в скрытую часть rack). Перестановка Фишера–Йейтса; в Parallel порядок не слышен — не трогаем.
    std::array<float, kSlots> r {};
    for (auto& v : r) v = rng.nextFloat();
    auto o = p.getOrder();
    std::vector<int> places;
    for (int k = 0; k < kSlots; ++k)
        if (p.getSlotType (o[(size_t) k]) != EffectType::Empty) places.push_back (k);
    for (int i = (int) places.size() - 1; i > 0; --i)
    {
        const int j = juce::jmin (i, (int) (r[(size_t) i] * (float) (i + 1)));
        std::swap (o[(size_t) places[(size_t) i]], o[(size_t) places[(size_t) j]]);
    }
    if (opt.allows (G::Order) && p.getChainMode() == ChainMode::Serial) p.setOrder (o);
}
} // namespace chroma
