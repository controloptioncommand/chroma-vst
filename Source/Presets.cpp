#include "Presets.h"
#include "Params.h"

namespace chroma
{
namespace
{
juce::String P (int slot, const char* id) { return slotParam (slot, id); }
}

const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace ParamId;
    static const std::vector<FactoryPreset> list = {
        // рецепт из видео один в один
        { "Biblically Accurate", { { P (0, type), 1 } } },
        { "Twerking", { { P (0, type), 1 }, { P (0, rate), 1.6f }, { P (0, drift), 40 } } },
        { "Gentle Halo", { { P (0, type), 1 }, { P (0, rate), 0.3f }, { P (0, depth), 90 }, { P (0, q), 70 }, { P (0, drift), 25 }, { P (0, mix), 70 } } },
        { "Sine Choir", { { P (0, type), 1 }, { P (0, shape), 0 }, { P (0, rate), 0.5f }, { P (0, drift), 0 }, { P (0, depth), 150 } } },
        { "Mono Prayer", { { P (0, type), 1 }, { P (0, spread), 0 }, { P (0, rate), 0.8f } } },
        { "Glossolalia", { { P (0, type), 1 }, { P (0, rate), 5.5f }, { P (0, drift), 60 }, { P (0, q), 160 } } },
        { "Thin Air", { { P (0, type), 1 }, { P (0, lowCut), 700 }, { P (0, rate), 0.45f }, { P (0, depth), 170 }, { P (0, mix), 85 } } },
        { "Wheels Within Wheels", { { P (0, type), 1 }, { P (0, rate), 0.35f }, { P (0, depth), 140 },
                                    { P (1, type), 1 }, { P (1, rate), 3.2f }, { P (1, depth), 80 }, { P (1, q), 200 }, { P (1, mix), 60 } } },
        { "Six Wings", { { P (0, type), 1 }, { P (0, rate), 0.6f }, { P (0, q), 50 }, { P (0, depth), 120 },
                         { P (1, type), 1 }, { P (1, rate), 0.21f }, { P (1, spread), 40 }, { P (1, q), 250 }, { P (1, depth), 160 }, { P (1, mix), 50 } } },
        { "Be Not Afraid", { { P (0, type), 1 }, { P (0, rate), 9.0f }, { P (0, drift), 80 }, { P (0, depth), 200 }, { P (0, q), 300 }, { P (0, lowCut), 120 } } },
        // OTT Stack (новые пресеты — только в конец)
        { "Double OTT", { { P (0, type), 2 }, { P (0, count), 2 } } },
        { "Tower of Babel", { { P (0, type), 2 }, { P (0, count), 40 } } },
        { "Gentle Glue", { { P (0, type), 2 }, { P (0, count), 4 }, { P (0, ottDepth), 40 }, { P (0, time), 160 }, { P (0, mix), 70 }, { P (0, trim), 0 } } },
        { "Squash Choir", { { P (0, type), 1 }, { P (0, rate), 0.6f }, { P (0, depth), 140 },
                            { P (1, type), 2 }, { P (1, count), 12 } } },
        { "Angel Crush", { { P (0, type), 1 }, { P (0, rate), 1.4f }, { P (0, q), 160 },
                           { P (1, type), 2 }, { P (1, count), 24 }, { P (1, time), 40 }, { P (1, trim), -6 } }, "1023" },
    };
    return list;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, Hooks h) : apvts (s), hooks (std::move (h)) {}

juce::String PresetManager::getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
void PresetManager::setCurrentName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }

void PresetManager::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& pr = list[(size_t) index];
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (rp->paramID == ParamId::output) continue;   // громкость выхода пресет не трогает
            float v = rp->getDefaultValue();
            if (rp->paramID == slotParam (0, ParamId::type)) v = 0.0f;   // по умолчанию слоты пустые, кроме указанных
            for (auto& kv : pr.values)
                if (rp->paramID == kv.first) v = rp->convertTo0to1 (kv.second);
            rp->setValueNotifyingHost (v);
        }
    setCurrentName (pr.name);
    if (hooks.loaded) hooks.loaded (&pr);
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("chroma").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::userPresets() const
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.chroma");
    files.sort();
    return files;
}

bool PresetManager::loadUser (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return false;
    auto tree = juce::ValueTree::fromXml (*xml);
    // громкость выхода остаётся текущей
    const float out = apvts.getRawParameterValue (ParamId::output)->load();
    apvts.replaceState (tree);
    if (auto* p = apvts.getParameter (ParamId::output)) p->setValueNotifyingHost (p->convertTo0to1 (out));
    setCurrentName (f.getFileNameWithoutExtension());
    if (hooks.loaded) hooks.loaded (nullptr);
    return true;
}

bool PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = userFolder();
    if (! dir.createDirectory()) return false;
    if (hooks.beforeSave) hooks.beforeSave();
    auto state = apvts.copyState();
    state.setProperty ("presetName", clean, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + ".chroma"))) return false;
    setCurrentName (clean);
    return true;
}

void PresetManager::step (int dir)
{
    // общий список: фабричные, затем пользовательские
    const auto users = userPresets();
    const int nf = getNumFactory(), total = nf + users.size();
    if (total == 0) return;
    const auto cur = getCurrentName();
    int idx = -1;
    for (int i = 0; i < nf; ++i) if (cur == factoryPresets()[(size_t) i].name) idx = i;
    for (int i = 0; i < users.size(); ++i) if (cur == users[i].getFileNameWithoutExtension()) idx = nf + i;
    idx = idx < 0 ? (dir > 0 ? 0 : total - 1) : (idx + dir + total) % total;
    if (idx < nf) loadFactory (idx);
    else loadUser (users[idx - nf]);
}
} // namespace chroma
