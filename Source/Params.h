#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace chroma
{
// Rack: цепочка из kSlots слотов. У каждого слота свой набор параметров с префиксом «s1_» … «s8_».
// Параметры привязаны к слоту, а не к месту в цепочке: при перестановке автоматизация едет вместе с эффектом.
inline constexpr int kSlots = 8;
inline constexpr int kBaseSlots = 4;   // столько мест видно в rack, пока эффектов меньше четырёх

enum class EffectType { Empty = 0, Ophan = 1, OttStack = 2, Legion = 3, Psalter = 4 };   // новые — только в конец
inline constexpr int kNumEffectTypes = 5;   // вместе с Empty
enum class ChainMode { Serial = 0, Parallel = 1 };
juce::StringArray effectNames();          // для параметра типа слота
juce::String effectTagline (EffectType);  // подзаголовок эффекта
juce::StringArray legionCharacters();     // характеры толпы Legion: Choir, Stadium, Monks, Tavern
juce::StringArray psalterTimbres();       // тембр нот Psalter: FM, String, Bell, Glass

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
    // OTT Stack
    inline constexpr const char* count = "count";
    inline constexpr const char* ottDepth = "odepth";
    inline constexpr const char* time = "time";
    inline constexpr const char* upward = "upward";
    inline constexpr const char* downward = "downward";
    inline constexpr const char* trim = "trim";
    inline constexpr const char* guard = "guard";   // Click guard (v2.03)
    // Legion (v3)
    inline constexpr const char* voices = "voices";
    inline constexpr const char* character = "character";
    inline constexpr const char* pitch = "pitch";
    inline constexpr const char* pitchSpread = "pspread";
    inline constexpr const char* loose = "loose";
    inline constexpr const char* space = "space";
    // Psalter (v4)
    inline constexpr const char* note1 = "pnote1";
    inline constexpr const char* note2 = "pnote2";
    inline constexpr const char* note3 = "pnote3";
    inline constexpr const char* timbre = "ptimbre";
    inline constexpr const char* decay = "pdecay";
    inline constexpr const char* tone = "ptone";
    inline constexpr const char* psRate = "prate";
    inline constexpr const char* psDepth = "pdepth";
    inline constexpr const char* width = "pwidth";
}

// ноты Psalter по номеру (0…2)
const char* psalterNoteId (int k);

// все параметры слота, кроме типа эффекта (очистка и копия слота)
const std::vector<const char*>& slotParamIds();

// «s2_rate»
juce::String slotParam (int slot, const char* id);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
} // namespace chroma
