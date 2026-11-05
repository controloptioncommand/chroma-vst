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

std::array<int, kSlots> identityOrder()
{
    std::array<int, kSlots> o {};
    for (int k = 0; k < kSlots; ++k) o[(size_t) k] = k;
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

juce::String fxKey (int slot) { return "fxPreset" + juce::String (slot + 1); }   // имя пресета эффекта в состоянии

// Advanced Ophan ↔ свойства узла: «L2fFrom», «R8q», …, «driftRate»
void writeAdvanced (juce::ValueTree& t, OphanAdvanced& a)
{
    t.setProperty ("driftRate", a.driftRate.load(), nullptr);
    for (int side = 0; side < 2; ++side)
        for (int b = 0; b < OphanRecipe::kBands; ++b)
            for (int f = 0; f < 5; ++f)
                t.setProperty (juce::String (side == 0 ? "L" : "R") + juce::String (b + 2) + kBandFields[f],
                               bandField (a.band[(size_t) side][(size_t) b], f).load(), nullptr);
}

void readAdvanced (const juce::ValueTree& t, OphanAdvanced& a)
{
    a.driftRate = juce::jlimit (0.01f, 20.0f, (float) t.getProperty ("driftRate", 0.35f));
    for (int side = 0; side < 2; ++side)
        for (int b = 0; b < OphanRecipe::kBands; ++b)
            for (int f = 0; f < 5; ++f)
            {
                const auto key = juce::String (side == 0 ? "L" : "R") + juce::String (b + 2) + kBandFields[f];
                if (t.hasProperty (key)) bandField (a.band[(size_t) side][(size_t) b], f).store ((float) t.getProperty (key));
            }
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
        p.count = get (ParamId::count); p.ottDepth = get (ParamId::ottDepth); p.time = get (ParamId::time);
        p.upward = get (ParamId::upward); p.downward = get (ParamId::downward); p.trim = get (ParamId::trim);
        ott[(size_t) s].prepare (sr);
        adv[(size_t) s].resetToRecipe();
        ophan[(size_t) s].prepare (sr, (uint32_t) (s + 1));
    }
    ensureScratch (2048);
    presets.loadFactory (0);
}

bool ChromaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void ChromaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    ensureScratch (samplesPerBlock);
    for (int s = 0; s < kSlots; ++s)
    {
        ophan[(size_t) s].prepare (sr, (uint32_t) (s + 1));
        ott[(size_t) s].prepare (sr);
    }
    outGainNow = juce::Decibels::decibelsToGain (outParam->load());
    modeNow = chainMode.load();
    modeFade = 1.0f;
    for (int s = 0; s < kSlots; ++s) branchGain[(size_t) s] = slotActive (s) ? 1.0f : 0.0f;
    dryGain = std::any_of (branchGain.begin(), branchGain.end(), [] (float g) { return g > 0.0f; }) ? 0.0f : 1.0f;
}

// ---------------------------------------------------------------------------
// rack
EffectType ChromaProcessor::getSlotType (int slot) const
{
    return (EffectType) juce::roundToInt (sp[(size_t) juce::jlimit (0, kSlots - 1, slot)].type->load());
}

void ChromaProcessor::setSlotType (int slot, EffectType t)
{
    if (t != getSlotType (slot)) setEffectPresetName (slot, {});
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

void ChromaProcessor::setChainMode (ChainMode m)
{
    chainMode.store ((int) m);
    apvts.state.setProperty ("mode", m == ChainMode::Parallel ? "parallel" : "serial", nullptr);
}

void ChromaProcessor::ensureScratch (int samples)
{
    samples = juce::jmax (512, samples);
    if (dryBuf.getNumSamples() >= samples) return;
    dryBuf.setSize (2, samples);
    workBuf.setSize (2, samples);
    accBuf.setSize (2, samples);
    normBuf.assign ((size_t) samples, 0.0f);
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
    for (auto* id : { ParamId::on, ParamId::mix, ParamId::rate, ParamId::drift, ParamId::depth, ParamId::q, ParamId::spread, ParamId::lowCut, ParamId::shape,
                     ParamId::count, ParamId::ottDepth, ParamId::time, ParamId::upward, ParamId::downward, ParamId::trim })
        if (auto* p = apvts.getParameter (slotParam (slot, id))) p->setValueNotifyingHost (p->getDefaultValue());
    setSlotType (slot, EffectType::Empty);
    setEffectPresetName (slot, {});
    resetAdvanced (slot);
}

void ChromaProcessor::copySlot (int from, int to)
{
    if (from == to) return;
    for (auto* id : { ParamId::type, ParamId::on, ParamId::mix, ParamId::rate, ParamId::drift, ParamId::depth, ParamId::q, ParamId::spread, ParamId::lowCut, ParamId::shape,
                     ParamId::count, ParamId::ottDepth, ParamId::time, ParamId::upward, ParamId::downward, ParamId::trim })
        if (auto* src = apvts.getParameter (slotParam (from, id)))
            if (auto* dst = apvts.getParameter (slotParam (to, id))) dst->setValueNotifyingHost (src->getValue());
    auto& a = adv[(size_t) from];
    auto& b = adv[(size_t) to];
    for (int s = 0; s < 2; ++s)
        for (int k = 0; k < OphanRecipe::kBands; ++k)
            for (int f = 0; f < 5; ++f)
                bandField (b.band[(size_t) s][(size_t) k], f).store (bandField (a.band[(size_t) s][(size_t) k], f).load());
    b.driftRate.store (a.driftRate.load());
    setEffectPresetName (to, apvts.state.getProperty (fxKey (from)).toString());
}

void ChromaProcessor::resetAdvanced (int slot) { advanced (slot).resetToRecipe(); }

// ---------------------------------------------------------------------------
// пресеты эффекта

juce::String ChromaProcessor::getEffectPresetName (int slot) const
{
    const auto n = apvts.state.getProperty (fxKey (slot)).toString();
    return n.isEmpty() ? juce::String ("Init") : n;
}

void ChromaProcessor::setEffectPresetName (int slot, const juce::String& name) { apvts.state.setProperty (fxKey (slot), name, nullptr); }

juce::ValueTree ChromaProcessor::captureEffect (int slot)
{
    const auto type = getSlotType (slot);
    juce::ValueTree t ("ChromaEffect");
    t.setProperty ("effect", effectNames()[(int) type], nullptr);
    for (auto* id : effectParamIds (type))
        if (auto* p = apvts.getParameter (slotParam (slot, id)))
            t.setProperty (id, p->convertFrom0to1 (p->getValue()), nullptr);
    if (type == EffectType::Ophan)
    {
        juce::ValueTree a ("Advanced");
        writeAdvanced (a, adv[(size_t) slot]);
        t.appendChild (a, nullptr);
    }
    return t;
}

bool ChromaProcessor::applyEffect (int slot, const juce::ValueTree& t)
{
    const auto type = getSlotType (slot);
    if (type == EffectType::Empty || ! t.hasType ("ChromaEffect") || t.getProperty ("effect").toString() != effectNames()[(int) type]) return false;
    for (auto* id : effectParamIds (type))
        if (auto* p = apvts.getParameter (slotParam (slot, id)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (t.hasProperty (id) ? p->convertTo0to1 ((float) t.getProperty (id)) : p->getDefaultValue());
            p->endChangeGesture();
        }
    if (type == EffectType::Ophan)
    {
        adv[(size_t) slot].resetToRecipe();
        const auto a = t.getChildWithName ("Advanced");
        if (a.isValid()) readAdvanced (a, adv[(size_t) slot]);
    }
    return true;
}

void ChromaProcessor::loadEffectFactory (int slot, int index)
{
    const auto type = getSlotType (slot);
    const auto& list = effectFactoryPresets (type);
    if (index < 0 || index >= (int) list.size()) return;
    juce::ValueTree t ("ChromaEffect");
    t.setProperty ("effect", effectNames()[(int) type], nullptr);
    for (auto& kv : list[(size_t) index].values) t.setProperty (kv.first, kv.second, nullptr);
    if (applyEffect (slot, t)) setEffectPresetName (slot, list[(size_t) index].name);
}

bool ChromaProcessor::loadEffectUser (int slot, const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! applyEffect (slot, juce::ValueTree::fromXml (*xml))) return false;
    setEffectPresetName (slot, f.getFileNameWithoutExtension());
    return true;
}

bool ChromaProcessor::saveEffectPreset (int slot, const juce::String& name)
{
    const auto type = getSlotType (slot);
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (type == EffectType::Empty || clean.isEmpty()) return false;
    auto dir = effectPresetFolder (type);
    if (! dir.createDirectory()) return false;
    auto xml = captureEffect (slot).createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + ".chromafx"))) return false;
    setEffectPresetName (slot, clean);
    return true;
}

void ChromaProcessor::stepEffectPreset (int slot, int dir)
{
    // общий список: фабричные, затем свои
    const auto type = getSlotType (slot);
    const auto& fac = effectFactoryPresets (type);
    const auto users = effectUserPresets (type);
    const int nf = (int) fac.size(), total = nf + users.size();
    if (type == EffectType::Empty || total == 0) return;
    const auto cur = getEffectPresetName (slot);
    int idx = -1;
    for (int i = 0; i < nf; ++i) if (cur == fac[(size_t) i].name) idx = i;
    for (int i = 0; i < users.size(); ++i) if (cur == users[i].getFileNameWithoutExtension()) idx = nf + i;
    idx = idx < 0 ? (dir > 0 ? 0 : total - 1) : (idx + dir + total) % total;
    if (idx < nf) loadEffectFactory (slot, idx);
    else loadEffectUser (slot, users[idx - nf]);
}

// ---------------------------------------------------------------------------
// состояние: расширенные настройки и порядок живут в дочернем узле «Rack»
void ChromaProcessor::pushRackState()
{
    auto rack = apvts.state.getOrCreateChildWithName ("Rack", nullptr);
    juce::String s;
    for (int v : getOrder()) s << v;
    apvts.state.setProperty ("order", s, nullptr);
    apvts.state.setProperty ("mode", getChainMode() == ChainMode::Parallel ? "parallel" : "serial", nullptr);
    rack.removeAllChildren (nullptr);
    for (int slot = 0; slot < kSlots; ++slot)
    {
        juce::ValueTree t ("Slot");
        t.setProperty ("index", slot, nullptr);
        writeAdvanced (t, adv[(size_t) slot]);
        rack.appendChild (t, nullptr);
    }
}

// «order» — строка номеров слотов; в проектах до v2.02 (4 слота) — «0123»: недостающие слоты встают в конец по порядку
static std::array<int, kSlots> parseOrder (const juce::String& s)
{
    std::array<int, kSlots> o {};
    std::array<bool, kSlots> used {};
    int k = 0;
    for (int i = 0; i < s.length() && k < kSlots; ++i)
    {
        const int v = s[i] - '0';
        if (v < 0 || v >= kSlots || used[(size_t) v]) return identityOrder();
        used[(size_t) v] = true;
        o[(size_t) k++] = v;
    }
    for (int v = 0; v < kSlots; ++v)
        if (! used[(size_t) v]) o[(size_t) k++] = v;
    return o;
}

void ChromaProcessor::pullRackState()
{
    auto o = parseOrder (apvts.state.getProperty ("order").toString());
    if (! validOrder (o)) o = identityOrder();
    setOrder (o);
    // старые проекты без «mode» — последовательно
    setChainMode (apvts.state.getProperty ("mode").toString() == "parallel" ? ChainMode::Parallel : ChainMode::Serial);

    for (auto& a : adv) a.resetToRecipe();
    const auto rack = apvts.state.getChildWithName ("Rack");
    for (const auto& t : rack)
    {
        const int slot = t.getProperty ("index", -1);
        if (slot < 0 || slot >= kSlots) continue;
        readAdvanced (t, adv[(size_t) slot]);
    }
}

void ChromaProcessor::onPresetLoaded (const FactoryPreset* fp)
{
    if (fp != nullptr)
    {
        apvts.state.removeChild (apvts.state.getChildWithName ("Rack"), nullptr);
        apvts.state.setProperty ("order", fp->order, nullptr);
        apvts.state.setProperty ("mode", fp->mode == ChainMode::Parallel ? "parallel" : "serial", nullptr);
        // имя пресета эффекта: у первого слота каждого эффекта — имя пресета rack (такой же есть в списке пресетов эффекта)
        std::array<bool, 3> seen {};
        for (int s = 0; s < kSlots; ++s)
        {
            const auto t = getSlotType (s);
            const bool first = t != EffectType::Empty && ! seen[(size_t) t];
            seen[(size_t) t] = true;
            setEffectPresetName (s, first ? juce::String (fp->name) : juce::String());
        }
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
            const auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            resetMissingParams (apvts, tree);   // проект до v2.02: слотов 5–8 в нём нет — они пустые
            pullRackState();
        }
}

// ---------------------------------------------------------------------------
bool ChromaProcessor::slotActive (int slot) const
{
    const auto& p = sp[(size_t) slot];
    return p.on->load() > 0.5f && juce::roundToInt (p.type->load()) != (int) EffectType::Empty;
}

void ChromaProcessor::runSlot (int s, float* L, float* R, int n)
{
    const auto& p = sp[(size_t) s];
    const int type = juce::roundToInt (p.type->load());
    const bool on = p.on->load() > 0.5f;
    if (type == (int) EffectType::OttStack)
    {
        OttParams tp;
        tp.count = p.count->load();
        tp.depth = p.ottDepth->load() / 100.0f;
        tp.time = p.time->load() / 100.0f;
        tp.upward = p.upward->load() / 100.0f;
        tp.downward = p.downward->load() / 100.0f;
        tp.outputDb = p.trim->load();
        tp.mix = p.mix->load() / 100.0f;
        ott[(size_t) s].process (L, R, n, tp, on ? &ottViews[(size_t) s] : nullptr);
    }
    else if (type == (int) EffectType::Ophan)
    {
        OphanParams op;
        op.rate = p.rate->load();
        op.drift = p.drift->load() / 100.0f;
        op.depth = p.depth->load() / 100.0f;
        op.q = p.q->load() / 100.0f;
        op.spread = p.spread->load() / 100.0f;
        op.lowCut = p.lowCut->load();
        op.shape = juce::roundToInt (p.shape->load());
        op.mix = p.mix->load() / 100.0f;
        ophan[(size_t) s].process (L, R, n, op, adv[(size_t) s], on ? &views[(size_t) s] : nullptr);
    }
}

// Параллельно: каждая ветка обрабатывает сухой вход, выход — среднее по включённым веткам.
// Веса плавные (≈ 15 мс): включение/выключение слота не щёлкает; выключенная ветка досчитывается, пока не затихнет.
// Когда веса докатились: одна ветка — её выход бит-в-бит, ни одной — сухой сигнал бит-в-бит.
void ChromaProcessor::processParallel (float* L, float* R, int n)
{
    const int cap = dryBuf.getNumSamples();
    const float a = (float) (1.0 - std::exp (-1.0 / (0.015 * sr)));
    const bool stereo = R != nullptr;
    bool any = false;
    for (int s = 0; s < kSlots; ++s) any = any || slotActive (s);

    // одна ветка: x[i] — её выход, вес g плавно к цели
    auto mixIn = [&] (const float* xl, const float* xr, float& g, float target, int len)
    {
        float* al = accBuf.getWritePointer (0);
        float* ar = accBuf.getWritePointer (1);
        for (int i = 0; i < len; ++i)
        {
            g += (target - g) * a;
            if (std::abs (target - g) < 1e-4f) g = target;
            al[i] += g * xl[i];
            if (stereo) ar[i] += g * xr[i];
            normBuf[(size_t) i] += g;
        }
    };

    for (int pos = 0; pos < n; pos += cap)
    {
        const int len = juce::jmin (cap, n - pos);
        float* l = L + pos;
        float* r = stereo ? R + pos : nullptr;
        dryBuf.copyFrom (0, 0, l, len);
        if (stereo) dryBuf.copyFrom (1, 0, r, len);
        accBuf.clear (0, len);
        accBuf.clear (1, 0, len);
        std::fill (normBuf.begin(), normBuf.begin() + len, 0.0f);

        for (int s = 0; s < kSlots; ++s)
        {
            const float target = slotActive (s) ? 1.0f : 0.0f;
            float& g = branchGain[(size_t) s];
            if (g == 0.0f && target == 0.0f) continue;
            workBuf.copyFrom (0, 0, dryBuf, 0, 0, len);
            if (stereo) workBuf.copyFrom (1, 0, dryBuf, 1, 0, len);
            runSlot (s, workBuf.getWritePointer (0), stereo ? workBuf.getWritePointer (1) : nullptr, len);
            mixIn (workBuf.getReadPointer (0), workBuf.getReadPointer (1), g, target, len);
        }
        if (dryGain > 0.0f || ! any)
            mixIn (dryBuf.getReadPointer (0), dryBuf.getReadPointer (1), dryGain, any ? 0.0f : 1.0f, len);

        const float* al = accBuf.getReadPointer (0);
        const float* ar = accBuf.getReadPointer (1);
        const float* dl = dryBuf.getReadPointer (0);
        const float* dr = dryBuf.getReadPointer (1);
        for (int i = 0; i < len; ++i)
        {
            const float w = normBuf[(size_t) i];
            l[i] = w > 1e-6f ? al[i] / w : dl[i];
            if (stereo) r[i] = w > 1e-6f ? ar[i] / w : dr[i];
        }
    }
}

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

    // учёт слотов: смена типа или включение — эффект начинает с чистого состояния
    for (int s = 0; s < kSlots; ++s)
    {
        const auto& p = sp[(size_t) s];
        const int type = juce::roundToInt (p.type->load());
        const bool on = p.on->load() > 0.5f;
        if (type != lastType[(size_t) s] || (on && ! lastOn[(size_t) s])) { ophan[(size_t) s].reset(); ott[(size_t) s].reset(); }
        lastType[(size_t) s] = type;
        lastOn[(size_t) s] = on;
        if (type != (int) EffectType::Ophan || ! on) views[(size_t) s].live.store (false, std::memory_order_relaxed);
        if (type != (int) EffectType::OttStack || ! on) ottViews[(size_t) s].live.store (false, std::memory_order_relaxed);
    }

    if (modeNow == (int) ChainMode::Parallel) processParallel (L, R, n);
    else
    {
        const auto order = unpackOrder (orderPacked.load());
        for (int k = 0; k < kSlots; ++k)
            if (slotActive (order[(size_t) k])) runSlot (order[(size_t) k], L, R, n);
    }

    // смена режима: ≈ 8 мс вниз, переключение, ≈ 8 мс вверх (у эффектов одно состояние — двумя схемами сразу не посчитать)
    {
        const int want = chainMode.load();
        const float step = (float) (1.0 / (0.008 * sr)) * (float) n;
        const float f0 = modeFade, f1 = want != modeNow ? juce::jmax (0.0f, f0 - step) : juce::jmin (1.0f, f0 + step);
        if (f0 < 1.0f || f1 < 1.0f)
            for (int c = 0; c < numOut; ++c)
                buffer.applyGainRamp (c, 0, n, f0, f1);
        modeFade = f1;
        if (want != modeNow && f1 <= 0.0f)
        {
            modeNow = want;
            // в тишине веса веток сразу встают в цель
            bool any = false;
            for (int s = 0; s < kSlots; ++s)
            {
                branchGain[(size_t) s] = slotActive (s) ? 1.0f : 0.0f;
                any = any || slotActive (s);
            }
            dryGain = any ? 0.0f : 1.0f;
        }
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
