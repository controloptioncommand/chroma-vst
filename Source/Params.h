#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace chroma
{
// Rack: цепочка из kSlots слотов. У каждого слота свой набор параметров с префиксом «s1_» … «s4_».
// Параметры привязаны к слоту, а не к месту в цепочке: при перестановке автоматизация едет вместе с эффектом.
inline constexpr int kSlots = 4;

enum class EffectType { Empty = 0, Ophan = 1 };
juce::StringArray effectNames();          // для параметра типа слота
juce::String effectTagline (EffectType);  // подзаголовок эффекта

namespace ParamId
{
    inline constexpr const char* output = "output";
    // параметры слота (без префикса)
    inline constexpr const char* type = "type";
    inline constexpr const char* on = "on";
    inline constexpr const char* mix = "mix";
    // Ophan
    inline constexpr const char* rate = "rate";
    inline constexpr const char* drift = "drift";
    inline constexpr const char* depth = "depth";
    inline constexpr const char* q = "q";
    inline constexpr const char* spread = "spread";
    inline constexpr const char* lowCut = "lowcut";
    inline constexpr const char* shape = "shape";
}

// «s2_rate»
juce::String slotParam (int slot, const char* id);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace chroma
