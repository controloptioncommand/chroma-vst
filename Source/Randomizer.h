#pragma once
#include "PluginProcessor.h"

namespace chroma
{
// Что кубику можно менять. Настраивается в меню настроек → Randomizer, хранится в настройках плагина (не в пресете).
struct RandomizerOptions
{
    enum Group
    {
        Ophan = 0,       // ручки слотов с Ophan
        OttStack,        // ручки слотов с OTT Stack
        Legion,          // ручки слотов с Legion
        Psalter,         // ручки слотов с Psalter (ноты — случайный аккорд)
        Order,           // порядок слотов (только в Serial)
        Mix,             // Mix слотов, 50–100%
        OphanAdvanced,   // рецепт полос Ophan: частоты, усиление, Q, Drift rate
        NumGroups
    };

    std::array<bool, NumGroups> enabled { true, true, true, true, true, true, true };

    bool allows (Group g) const noexcept { return enabled[(size_t) g]; }
    static juce::String name (Group g);
    static juce::String prefKey (Group g);   // ключ в файле настроек
};

// Бросок кубика по rack. Меняются только ручки эффектов, которые уже стоят в слотах, в «безопасных» диапазонах,
// и только разрешённые группы. Никогда не трогаются: тип эффекта, пустые слоты, On, режим Serial/Parallel,
// громкость выхода, Output у OTT Stack и свои сэмплы Psalter.
void randomizeRack (ChromaProcessor& p, juce::Random& rng, const RandomizerOptions& options = {});
} // namespace chroma
