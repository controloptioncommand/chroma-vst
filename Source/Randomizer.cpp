#include "Randomizer.h"

namespace chroma
{
juce::String RandomizerOptions::name (Group g)
{
    switch (g)
    {
        case Ophan:         return "Ophan";
        case OttStack:      return "OTT Stack";
        case Legion:        return "Legion";
        case Psalter:       return "Psalter";
        case Order:         return "Slot order";
        case Mix:           return "Slot mix";
        case OphanAdvanced: return "Ophan advanced (band recipe)";
        case NumGroups:     break;
    }
    return {};
}

juce::String RandomizerOptions::prefKey (Group g)
{
    static const char* keys[] = { "ophan", "ott", "legion", "psalter", "order", "mix", "advanced" };
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
        const bool ophan = type == EffectType::Ophan, ott = type == EffectType::OttStack, legion = type == EffectType::Legion,
                   psalter = type == EffectType::Psalter;
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

        // Legion: голосов чаще немного, высота чаще своя, изредка — на октаву вниз или вверх
        { const float r = rng.nextFloat(); pick (legion, G::Legion, ParamId::voices, (float) juce::roundToInt (2.0f + 62.0f * r * r)); }
        pick (legion, G::Legion, ParamId::character, (float) juce::jmin (3, (int) (rng.nextFloat() * 4.0f)));
        { const float r = rng.nextFloat(), st = (float) juce::roundToInt (uni (-12.0f, 12.0f)); pick (legion, G::Legion, ParamId::pitch, r < 0.6f ? 0.0f : st); }
        pick (legion, G::Legion, ParamId::pitchSpread, logUni (0.05f, 4.0f));
        pick (legion, G::Legion, ParamId::loose, uni (30.0f, 160.0f));
        pick (legion, G::Legion, ParamId::space, uni (10.0f, 80.0f));

        // Psalter: ноты — аккорд от случайной тоники (C3…G5), в том числе как в рецепте (тон-тон);
        // ноты чаще длинные, LFO чаще медленный, глубина — не меньше половины (разговор нот слышен)
        {
            static const int shapes[][3] = { { 0, 2, 4 }, { 0, 4, 7 }, { 0, 3, 7 }, { 0, 5, 7 }, { 0, 7, 14 }, { 0, 2, 7 }, { 0, 4, 11 }, { 0, 5, 10 }, { 0, 7, 16 } };
            constexpr int nShapes = (int) (sizeof (shapes) / sizeof (shapes[0]));
            const int root = 48 + juce::jmin (31, (int) (rng.nextFloat() * 32.0f));
            const int sh = juce::jmin (nShapes - 1, (int) (rng.nextFloat() * (float) nShapes));
            for (int k = 0; k < 3; ++k) pick (psalter, G::Psalter, psalterNoteId (k), (float) juce::jmin (108, root + shapes[sh][k]));
        }
        pick (psalter, G::Psalter, ParamId::timbre, (float) juce::jmin (3, (int) (rng.nextFloat() * 4.0f)));
        pick (psalter, G::Psalter, ParamId::decay, logUni (1.0f, 6.0f));
        pick (psalter, G::Psalter, ParamId::tone, uni (25.0f, 75.0f));
        pick (psalter, G::Psalter, ParamId::psRate, logUni (0.1f, 3.0f));
        pick (psalter, G::Psalter, ParamId::psDepth, uni (50.0f, 100.0f));
        pick (psalter, G::Psalter, ParamId::width, uni (30.0f, 100.0f));

        // Mix — у любого эффекта
        pick (ophan || ott || legion || psalter, G::Mix, ParamId::mix, uni (50.0f, 100.0f));

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
