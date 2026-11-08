#include "Params.h"

namespace chroma
{
juce::StringArray effectNames() { return { "Empty", "Ophan", "OTT Stack", "Legion" }; }

juce::String effectTagline (EffectType t)
{
    switch (t)
    {
        case EffectType::Ophan: return "Biblically accurate EQ";
        case EffectType::OttStack: return "OTT in series";
        case EffectType::Legion: return "One voice, a crowd";
        case EffectType::Empty: break;
    }
    return "Empty slot";
}

juce::StringArray legionCharacters() { return { "Choir", "Stadium", "Monks", "Tavern" }; }

juce::String slotParam (int slot, const char* id) { return "s" + juce::String (slot + 1) + "_" + id; }

const std::vector<const char*>& slotParamIds()
{
    using namespace ParamId;
    static const std::vector<const char*> ids { on, mix, rate, drift, depth, q, spread, lowCut, shape,
                                                count, ottDepth, time, upward, downward, trim, guard,
                                                voices, character, pitch, pitchSpread, loose, space };
    return ids;
}

namespace
{
juce::String minusSign() { return juce::String::fromUTF8 ("\xe2\x88\x92"); }

juce::String signedText (float v, const juce::String& unit, int decimals = 0)
{
    const auto r = decimals == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
    if (v > 0.0001f) return "+" + r + unit;
    if (v < -0.0001f) return minusSign() + r.substring (1) + unit;
    return (decimals == 0 ? juce::String ("0") : juce::String (0.0f, decimals)) + unit;
}

juce::String hzText (float v)
{
    if (v >= 1000.0f) return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";
    if (v >= 100.0f) return juce::String (juce::roundToInt (v)) + " Hz";
    if (v >= 10.0f) return juce::String (v, 1) + " Hz";
    return juce::String (v, 2) + " Hz";
}

using Float = juce::AudioParameterFloat;
using Attr = juce::AudioParameterFloatAttributes;

std::unique_ptr<Float> pct (const juce::String& id, const juce::String& name, float lo, float hi, float def)
{
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 1.0f), def,
        Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; }));
}

std::unique_ptr<Float> hz (const juce::String& id, const juce::String& name, float lo, float hi, float def, float centre)
{
    juce::NormalisableRange<float> r (lo, hi);
    r.setSkewForCentre (centre);
    return std::make_unique<Float> (juce::ParameterID { id, 1 }, name, r, def,
        Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int) { return hzText (v); }));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<Float> (juce::ParameterID { ParamId::output, 1 }, "Output",
        juce::NormalisableRange<float> (-36.0f, 12.0f, 0.1f), 0.0f,
        Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedText (v, " dB", 1); })));

    for (int s = 0; s < kSlots; ++s)
    {
        const auto pre = "Slot " + juce::String (s + 1) + " ";
        auto group = std::make_unique<juce::AudioProcessorParameterGroup> ("slot" + juce::String (s + 1), "Slot " + juce::String (s + 1), " | ");
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { slotParam (s, ParamId::type), 1 }, pre + "Effect",
                                                                       effectNames(), s == 0 ? (int) EffectType::Ophan : 0));
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { slotParam (s, ParamId::on), 1 }, pre + "On", true));
        group->addChild (pct (slotParam (s, ParamId::mix), pre + "Mix", 0.0f, 100.0f, 100.0f));
        // Ophan
        group->addChild (hz (slotParam (s, ParamId::rate), pre + "Rate", 0.02f, 12.0f, 1.0f, 1.0f));
        group->addChild (pct (slotParam (s, ParamId::drift), pre + "Drift", 0.0f, 100.0f, 35.0f));
        group->addChild (pct (slotParam (s, ParamId::depth), pre + "Depth", 0.0f, 200.0f, 200.0f));
        {
            juce::NormalisableRange<float> r (25.0f, 400.0f, 1.0f);
            r.setSkewForCentre (100.0f);
            group->addChild (std::make_unique<Float> (juce::ParameterID { slotParam (s, ParamId::q), 1 }, pre + "Q", r, 100.0f,
                Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
        }
        group->addChild (pct (slotParam (s, ParamId::spread), pre + "Spread", 0.0f, 100.0f, 100.0f));
        group->addChild (hz (slotParam (s, ParamId::lowCut), pre + "Low cut", 10.0f, 2000.0f, 225.0f, 200.0f));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { slotParam (s, ParamId::shape), 1 }, pre + "Shape",
                                                                       juce::StringArray { "Sine", "Stray" }, 1));
        // OTT Stack
        group->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { slotParam (s, ParamId::count), 1 }, pre + "OTT count", 2, 40, 8,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String::fromUTF8 ("\xc3\x97") + juce::String (v); })));
        group->addChild (pct (slotParam (s, ParamId::ottDepth), pre + "OTT depth", 0.0f, 100.0f, 100.0f));
        {
            juce::NormalisableRange<float> r (10.0f, 1000.0f, 1.0f);
            r.setSkewForCentre (100.0f);
            group->addChild (std::make_unique<Float> (juce::ParameterID { slotParam (s, ParamId::time), 1 }, pre + "OTT time", r, 100.0f,
                Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)) + "%"; })));
        }
        group->addChild (pct (slotParam (s, ParamId::upward), pre + "OTT upward", 0.0f, 100.0f, 100.0f));
        group->addChild (pct (slotParam (s, ParamId::downward), pre + "OTT downward", 0.0f, 100.0f, 100.0f));
        group->addChild (std::make_unique<Float> (juce::ParameterID { slotParam (s, ParamId::trim), 1 }, pre + "OTT output",
            juce::NormalisableRange<float> (-48.0f, 12.0f, 0.1f), -3.0f,
            Attr().withLabel ("dB").withStringFromValueFunction ([] (float v, int) { return signedText (v, " dB", 1); })));
        // по умолчанию выключен: стопка звучит как раньше, всплеск в начале ноты — часть характера OTT
        group->addChild (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { slotParam (s, ParamId::guard), 1 }, pre + "OTT click guard", false));
        // Legion
        group->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { slotParam (s, ParamId::voices), 1 }, pre + "Legion voices", 2, 64, 16,
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return juce::String (v); })));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { slotParam (s, ParamId::character), 1 }, pre + "Legion character",
                                                                       legionCharacters(), 0));
        group->addChild (std::make_unique<Float> (juce::ParameterID { slotParam (s, ParamId::pitch), 1 }, pre + "Legion pitch",
            juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int) { return signedText (v, " st", 1); })));
        {
            juce::NormalisableRange<float> r (0.0f, 12.0f, 0.01f);
            r.setSkewForCentre (1.5f);
            group->addChild (std::make_unique<Float> (juce::ParameterID { slotParam (s, ParamId::pitchSpread), 1 }, pre + "Legion spread", r, 0.3f,
                Attr().withLabel ("st").withStringFromValueFunction ([] (float v, int)
                    { return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (v, v < 1.0f ? 2 : 1) + " st"; })));
        }
        group->addChild (pct (slotParam (s, ParamId::loose), pre + "Legion loose", 0.0f, 200.0f, 100.0f));
        group->addChild (pct (slotParam (s, ParamId::space), pre + "Legion space", 0.0f, 100.0f, 40.0f));
        l.add (std::move (group));
    }
    return l;
}
} // namespace chroma
