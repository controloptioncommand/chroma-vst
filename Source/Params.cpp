#include "Params.h"

namespace chroma
{
juce::StringArray effectNames() { return { "Empty", "Ophan" }; }

juce::String effectTagline (EffectType t)
{
    switch (t)
    {
        case EffectType::Ophan: return "Biblically accurate EQ";
        case EffectType::Empty: break;
    }
    return "Empty slot";
}

juce::String slotParam (int slot, const char* id) { return "s" + juce::String (slot + 1) + "_" + id; }

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
        l.add (std::move (group));
    }
    return l;
}
} // namespace chroma
