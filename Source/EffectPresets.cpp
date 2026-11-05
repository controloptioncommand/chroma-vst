#include "EffectPresets.h"

namespace chroma
{
const std::vector<const char*>& effectParamIds (EffectType t)
{
    using namespace ParamId;
    static const std::vector<const char*> ophan { mix, rate, drift, depth, q, spread, lowCut, shape };
    static const std::vector<const char*> ott { mix, count, ottDepth, time, upward, downward, trim };
    static const std::vector<const char*> none;
    return t == EffectType::Ophan ? ophan : t == EffectType::OttStack ? ott : none;
}

const std::vector<EffectFactoryPreset>& effectFactoryPresets (EffectType t)
{
    static std::map<int, std::vector<EffectFactoryPreset>> cache;
    auto it = cache.find ((int) t);
    if (it != cache.end()) return it->second;
    std::vector<EffectFactoryPreset> list;
    for (auto& fp : factoryPresets())
    {
        // первый слот с этим эффектом
        int slot = -1;
        for (int s = 0; s < kSlots && slot < 0; ++s)
            for (auto& kv : fp.values)
                if (kv.first == slotParam (s, ParamId::type) && juce::roundToInt (kv.second) == (int) t) { slot = s; break; }
        if (slot < 0) continue;
        EffectFactoryPreset e { fp.name, {} };
        for (auto* id : effectParamIds (t))
            for (auto& kv : fp.values)
                if (kv.first == slotParam (slot, id)) e.values.push_back ({ id, kv.second });
        list.push_back (std::move (e));
    }
    return cache[(int) t] = std::move (list);
}

juce::File effectPresetFolder (EffectType t)
{
    return PresetManager::userFolder().getChildFile ("Effects").getChildFile (effectNames()[(int) t]);
}

juce::Array<juce::File> effectUserPresets (EffectType t)
{
    auto files = effectPresetFolder (t).findChildFiles (juce::File::findFiles, false, "*.chromafx");
    files.sort();
    return files;
}
} // namespace chroma
