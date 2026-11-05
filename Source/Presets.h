#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"

namespace chroma
{
// Фабричный пресет: значения параметров (остальные — по умолчанию) и порядок слотов.
// Расширенные настройки Ophan у фабричных пресетов — рецепт из видео.
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<juce::String, float>> values;
    juce::String order = "0123";
    ChainMode mode = ChainMode::Serial;
};

const std::vector<FactoryPreset>& factoryPresets();

// После replaceState: параметры, которых нет в загруженном дереве (старый проект, меньше слотов), — к значению по умолчанию
void resetMissingParams (juce::AudioProcessorValueTreeState& apvts, const juce::ValueTree& loaded);

// Пресеты: фабричные и пользовательские (~/Music/chroma/Presets/*.chroma)
class PresetManager
{
public:
    struct Hooks
    {
        std::function<void()> beforeSave;                    // записать состояние rack в дерево
        std::function<void (const FactoryPreset*)> loaded;   // после загрузки: nullptr — пользовательский
    };
    PresetManager (juce::AudioProcessorValueTreeState& state, Hooks hooks);

    int getNumFactory() const { return (int) factoryPresets().size(); }
    void loadFactory (int index);
    void step (int dir);                        // ‹ › по фабричным и пользовательским
    juce::String getCurrentName() const;
    void setCurrentName (const juce::String& n);

    static juce::File userFolder();
    juce::Array<juce::File> userPresets() const;
    bool loadUser (const juce::File& f);
    bool save (const juce::String& name);

private:
    juce::AudioProcessorValueTreeState& apvts;
    Hooks hooks;
};
} // namespace chroma
