#pragma once
#include "Presets.h"

namespace chroma
{
// Пресет одного эффекта: ручки эффекта и Mix (On не входит), у Ophan — ещё Advanced.
// Фабричные — из фабричных пресетов rack: первый слот этого эффекта в каждом. У Ophan Advanced фабричных — рецепт.
// Свои — ~/Music/chroma/Presets/Effects/<эффект>/*.chromafx (XML «ChromaEffect»).
const std::vector<const char*>& effectParamIds (EffectType t);   // id без префикса слота

struct EffectFactoryPreset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;   // id без префикса → значение
};
const std::vector<EffectFactoryPreset>& effectFactoryPresets (EffectType t);

juce::File effectPresetFolder (EffectType t);
juce::Array<juce::File> effectUserPresets (EffectType t);
} // namespace chroma
