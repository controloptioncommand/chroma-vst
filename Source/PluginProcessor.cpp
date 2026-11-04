#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace chroma
{
namespace
{
const char* kBandFields[5] = { "fFrom", "fTo", "gFrom", "gTo", "q" };

std::atomic<float>& bandField (OphanBandSettings& b, int f)
{
    switch (f)
    {
        case 0: return b.fFrom;
        case 1: return b.fTo;
        case 2: return b.gFrom;
        case 3: return b.gTo;
        default: return b.q;
    }
}

int packOrder (const std::array<int, kSlots>& o)
{
    int v = 0;
    for (int k = 0; k < kSlots; ++k) v |= (o[(size_t) k] & 0xf) << (4 * k);
    return v;
}

std::array<int, kSlots> unpackOrder (int v)
{
    std::array<int, kSlots> o {};
    for (int k = 0; k < kSlots; ++k) o[(size_t) k] = (v >> (4 * k)) & 0xf;
    return o;
}

// порядок валиден, только если это перестановка 0…kSlots−1
bool validOrder (const std::array<int, kSlots>& o)
{
    std::array<bool, kSlots> seen {};
    for (int s : o)
    {
        if (s < 0 || s >= kSlots || seen[(size_t) s]) return false;
        seen[(size_t) s] = true;
    }
    return true;
}

// мягкий лимитер: ниже −1 dBFS ничего не трогает, выше — плавно к 0 dBFS
inline float softLimit (float x)
{
    constexpr float knee = 0.891f, room = 1.0f - knee;
    const float a = std::abs (x);
    if (a <= knee) return x;
    return std::copysign (knee + room * std::tanh ((a - knee) / room), x);
}
} // namespace

ChromaProcessor::ChromaProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "chroma", createLayout()),
      presets (apvts, { [this] { pushRackState(); }, [this] (const FactoryPreset* fp) { onPresetLoaded (fp); } })
{
    outParam = apvts.getRawParameterValue (ParamId::output);
    for (int s = 0; s < kSlots; ++s)
    {
        auto get = [&] (const char* id) { return apvts.getRawParameterValue (slotParam (s, id)); };
        auto& p = sp[(size_t) s];
        p.type = get (ParamId::type);  p.on = get (ParamId::on);        p.mix = get (ParamId::mix);
        p.rate = get (ParamId::rate);  p.drift = get (ParamId::drift);  p.depth = get (ParamId::depth);
        p.q = get (ParamId::q);        p.spread = get (ParamId::spread); p.lowCut = get (ParamId::lowCut);
        p.shape = get (ParamId::shape);
        adv[(size_t) s].resetToRecipe();
        ophan[(size_t) s].prepare (sr, (uint32_t) (s + 1));
    }
    presets.loadFactory (0);
}

bool ChromaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void ChromaProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (int s = 0; s < kSlots; ++s) ophan[(size_t) s].prepare (sr, (uint32_t) (s + 1));
    outGainNow = juce::Decibels::decibelsToGain (outParam->load());
}

// ---------------------------------------------------------------------------
// rack
EffectType ChromaProcessor::getSlotType (int slot) const
{
    return (EffectType) juce::roundToInt (sp[(size_t) juce::jlimit (0, kSlots - 1, slot)].type->load());
}

void ChromaProcessor::setSlotType (int slot, EffectType t)
{
    if (auto* p = apvts.getParameter (slotParam (slot, ParamId::type)))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) t));
}

std::array<int, kSlots> ChromaProcessor::getOrder() const { return unpackOrder (orderPacked.load()); }

void ChromaProcessor::setOrder (std::array<int, kSlots> o)
{
    if (! validOrder (o)) return;
    orderPacked.store (packOrder (o));
    juce::String s;
    for (int v : o) s << v;
    apvts.state.setProperty ("order", s, nullptr);
}

void ChromaProcessor::moveSlot (int fromPos, int toPos)
{
    auto o = getOrder();
    fromPos = juce::jlimit (0, kSlots - 1, fromPos);
    toPos = juce::jlimit (0, kSlots - 1, toPos);
    if (fromPos == toPos) return;
    const int s = o[(size_t) fromPos];
    if (fromPos < toPos) for (int k = fromPos; k < toPos; ++k) o[(size_t) k] = o[(size_t) k + 1];
    else for (int k = fromPos; k > toPos; --k) o[(size_t) k] = o[(size_t) k - 1];
    o[(size_t) toPos] = s;
    setOrder (o);
}

void ChromaProcessor::clearSlot (int slot)
{
    for (auto* id : { ParamId::on, ParamId::mix, ParamId::rate, ParamId::drift, ParamId::depth, ParamId::q, ParamId::spread, ParamId::lowCut, ParamId::shape })
        if (auto* p = apvts.getParameter (slotParam (slot, id))) p->setValueNotifyingHost (p->getDefaultValue());
    setSlotType (slot, EffectType::Empty);
    resetAdvanced (slot);
}

void ChromaProcessor::copySlot (int from, int to)
{
    if (from == to) return;
    for (auto* id : { ParamId::type, ParamId::on, ParamId::mix, ParamId::rate, ParamId::drift, ParamId::depth, ParamId::q, ParamId::spread, ParamId::lowCut, ParamId::shape })
        if (auto* src = apvts.getParameter (slotParam (from, id)))
            if (auto* dst = apvts.getParameter (slotParam (to, id))) dst->setValueNotifyingHost (src->getValue());
    auto& a = adv[(size_t) from];
    auto& b = adv[(size_t) to];
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < OphanRecipe::kBands; ++k)
            for (int f = 0; f < 5; ++f)
                bandField (b.band[(size_t) s][(size_t) k], f).store (bandField (a.band[(size_t) s][(size_t) k], f).load());
    b.driftRate.store (a.driftRate.load());
}

void ChromaProcessor::resetAdvanced (int slot) { advanced (slot).resetToRecipe(); }

// ---------------------------------------------------------------------------
// состояние: расширенные настройки и порядок живут в дочернем узле «Rack»
void ChromaProcessor::pushRackState()
{
    auto rack = apvts.state.getOrCreateChildWithName ("Rack", nullptr);
    juce::String s;
    for (int v : getOrder()) s << v;
    apvts.state.setProperty ("order", s, nullptr);
    rack.removeAllChildren (nullptr);
    for (int slot = 0; slot < kSlots; ++slot)
    {
        juce::ValueTree t ("Slot");
        t.setProperty ("index", slot, nullptr);
        auto& a = adv[(size_t) slot];
        t.setProperty ("driftRate", a.driftRate.load(), nullptr);
        for (int side = 0; side < 2; ++side)
            for (int b = 0; b < OphanRecipe::kBands; ++b)
                for (int f = 0; f < 5; ++f)
                    t.setProperty (juce::String (side == 0 ? "L" : "R") + juce::String (b + 2) + kBandFields[f],
                                   bandField (a.band[(size_t) side][(size_t) b], f).load(), nullptr);
        rack.appendChild (t, nullptr);
    }
}

void ChromaProcessor::pullRackState()
{
    std::array<int, kSlots> o { 0, 1, 2, 3 };
    const auto s = apvts.state.getProperty ("order").toString();
    if (s.length() == kSlots)
        for (int k = 0; k < kSlots; ++k) o[(size_t) k] = s[k] - '0';
    if (! validOrder (o)) o = { 0, 1, 2, 3 };
    setOrder (o);

    for (auto& a : adv) a.resetToRecipe();
    const auto rack = apvts.state.getChildWithName ("Rack");
    for (const auto& t : rack)
    {
        const int slot = t.getProperty ("index", -1);
        if (slot < 0 || slot >= kSlots) continue;
        auto& a = adv[(size_t) slot];
        a.driftRate = juce::jlimit (0.01f, 20.0f, (float) t.getProperty ("driftRate", 0.35f));
        for (int side = 0; side < 2; ++side)
            for (int b = 0; b < OphanRecipe::kBands; ++b)
                for (int f = 0; f < 5; ++f)
                {
                    const auto key = juce::String (side == 0 ? "L" : "R") + juce::String (b + 2) + kBandFields[f];
                    if (t.hasProperty (key)) bandField (a.band[(size_t) side][(size_t) b], f).store ((float) t.getProperty (key));
                }
    }
}

void ChromaProcessor::onPresetLoaded (const FactoryPreset* fp)
{
    if (fp != nullptr)
    {
        apvts.state.removeChild (apvts.state.getChildWithName ("Rack"), nullptr);
        apvts.state.setProperty ("order", fp->order, nullptr);
    }
    pullRackState();
}

void ChromaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    pushRackState();
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void ChromaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            pullRackState();
        }
}

// ---------------------------------------------------------------------------
void ChromaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int c = numIn; c < numOut; ++c) buffer.clear (c, 0, n);
    if (numIn == 1 && numOut == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);   // моно → стерео: Ophan разведёт каналы
    if (n == 0 || numOut == 0) return;

    float* L = buffer.getWritePointer (0);
    float* R = numOut > 1 ? buffer.getWritePointer (1) : nullptr;

    float ip = 0;
    for (int c = 0; c < numOut; ++c) ip = juce::jmax (ip, buffer.getMagnitude (c, 0, n));
    inPeak.store (juce::jmax (ip, inPeak.load() * 0.85f));

    const auto order = unpackOrder (orderPacked.load());
    for (int k = 0; k < kSlots; ++k)
    {
        const int s = order[(size_t) k];
        const auto& p = sp[(size_t) s];
        const int type = juce::roundToInt (p.type->load());
        const bool on = p.on->load() > 0.5f;
        if (type != lastType[(size_t) s] || (on && ! lastOn[(size_t) s])) ophan[(size_t) s].reset();
        lastType[(size_t) s] = type;
        lastOn[(size_t) s] = on;
        if (type != (int) EffectType::Ophan || ! on)
        {
            views[(size_t) s].live.store (false, std::memory_order_relaxed);
            continue;
        }
        OphanParams op;
        op.rate = p.rate->load();
        op.drift = p.drift->load() / 100.0f;
        op.depth = p.depth->load() / 100.0f;
        op.q = p.q->load() / 100.0f;
        op.spread = p.spread->load() / 100.0f;
        op.lowCut = p.lowCut->load();
        op.shape = juce::roundToInt (p.shape->load());
        op.mix = p.mix->load() / 100.0f;
        ophan[(size_t) s].process (L, R, n, op, adv[(size_t) s], &views[(size_t) s]);
    }

    // выход: громкость (плавно) и мягкий лимитер
    const float target = juce::Decibels::decibelsToGain (outParam->load());
    const float g0 = outGainNow, dg = (target - g0) / (float) n;
    float op = 0;
    for (int c = 0; c < numOut; ++c)
    {
        float* x = buffer.getWritePointer (c);
        for (int i = 0; i < n; ++i)
        {
            x[i] = softLimit (x[i] * (g0 + dg * (float) i));
            op = juce::jmax (op, std::abs (x[i]));
        }
    }
    outGainNow = target;
    outPeak.store (juce::jmax (op, outPeak.load() * 0.85f));

    // в интерфейс: моно-сумма выхода
    if (scopeFifo.getFreeSpace() >= n)
    {
        const auto w = scopeFifo.write (n);
        auto put = [&] (int start, int size, int offset)
        {
            for (int i = 0; i < size; ++i)
                scopeBuf[(size_t) (start + i)] = R != nullptr ? 0.5f * (L[offset + i] + R[offset + i]) : L[offset + i];
        };
        if (w.blockSize1 > 0) put (w.startIndex1, w.blockSize1, 0);
        if (w.blockSize2 > 0) put (w.startIndex2, w.blockSize2, w.blockSize1);
    }

    const double secs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0);
    const float load = (float) (secs / juce::jmax (1e-6, n / sr));
    cpuLoad.store (cpuLoad.load() * 0.9f + load * 0.1f);
}

juce::AudioProcessorEditor* ChromaProcessor::createEditor() { return new ChromaEditor (*this); }
} // namespace chroma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new chroma::ChromaProcessor(); }
