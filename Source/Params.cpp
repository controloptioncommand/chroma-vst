#include "Params.h"
#include "DSP/Psalter.h"

namespace chroma
{
juce::StringArray effectNames() { return { "Empty", "Ophan", "OTT Stack", "Legion", "Psalter" }; }

juce::String effectTagline (EffectType t)
{
    switch (t)
    {
        case EffectType::Ophan: return "Biblically accurate EQ";
        case EffectType::OttStack: return "OTT in series";
        case EffectType::Legion: return "One voice, a crowd";
        case EffectType::Psalter: return "Three notes ring along";
        case EffectType::Empty: break;
    }
    return "Empty slot";
}

juce::StringArray legionCharacters() { return { "Choir", "Stadium", "Monks", "Tavern" }; }

juce::StringArray psalterTimbres() { return { "FM", "String", "Bell", "Glass" }; }

const char* psalterNoteId (int k)
{
    static const char* ids[3] = { ParamId::note1, ParamId::note2, ParamId::note3 };
    return ids[juce::jlimit (0, 2, k)];
}

juce::String slotParam (int slot, const char* id) { return "s" + juce::String (slot + 1) + "_" + id; }

const std::vector<const char*>& slotParamIds()
{
    using namespace ParamId;
    static const std::vector<const char*> ids { on, mix, rate, drift, depth, q, spread, lowCut, shape,
                                                count, ottDepth, time, upward, downward, trim, guard,
                                                voices, character, pitch, pitchSpread, loose, space,
                                                note1, note2, note3, timbre, decay, tone, psRate, psDepth, width };
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

// «C5», «f#3», «60» → номер MIDI (C4 = 60)
int noteFromText (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();
    if (t.containsOnly ("0123456789")) return juce::jlimit (24, 108, t.getIntValue());
    static const char* names[7] = { "C", "D", "E", "F", "G", "A", "B" };
    static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
        if (t.startsWith (names[i]))
        {
            auto rest = t.substring (1);
            int n = semis[i];
            if (rest.startsWith ("#")) { ++n; rest = rest.substring (1); }
            else if (rest.startsWith ("B")) { --n; rest = rest.substring (1); }
            return juce::jlimit (24, 108, (rest.getIntValue() + 1) * 12 + n);
        }
    return 72;
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
        // Psalter: три ноты (по умолчанию C5 D5 E5 — как в рецепте), тембр, длина нот, наклон, LFO нот, стерео
        for (int k = 0; k < 3; ++k)
            group->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { slotParam (s, psalterNoteId (k)), 1 },
                pre + "Psalter note " + juce::String (k + 1), 24, 108, 72 + 2 * k,
                juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return PsalterRecipe::noteName (v); })
                                                   .withValueFromStringFunction ([] (const juce::String& t) { return noteFromText (t); })));
        group->addChild (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { slotParam (s, ParamId::timbre), 1 }, pre + "Psalter timbre",
                                                                       psalterTimbres(), 0));
        {
            juce::NormalisableRange<float> r ((float) PsalterRecipe::kMinDecay, (float) PsalterRecipe::kMaxDecay, 0.01f);
            r.setSkewForCentre (1.5f);
            group->addChild (std::make_unique<Float> (juce::ParameterID { slotParam (s, ParamId::decay), 1 }, pre + "Psalter decay", r, 3.0f,
                Attr().withLabel ("s").withStringFromValueFunction ([] (float v, int) { return juce::String (v, v < 10.0f ? 2 : 1) + " s"; })));
        }
        group->addChild (pct (slotParam (s, ParamId::tone), pre + "Psalter tone", 0.0f, 100.0f, 50.0f));
        group->addChild (hz (slotParam (s, ParamId::psRate), pre + "Psalter rate", 0.05f, 8.0f, 1.0f, 1.0f));
        group->addChild (pct (slotParam (s, ParamId::psDepth), pre + "Psalter depth", 0.0f, 100.0f, 100.0f));
        group->addChild (pct (slotParam (s, ParamId::width), pre + "Psalter width", 0.0f, 100.0f, 70.0f));
        l.add (std::move (group));
    }
    return l;
}
} // namespace chroma
