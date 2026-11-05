// Офлайн-тест chrōma: фильтры и LFO, Ophan (не тишина, не NaN, ≤ 0 dBFS, L ≠ R, Spread 0 → моно), обход пустых и выключенных
// слотов, Mix 0, порядок цепочки, режим Parallel, кубик, состояние, пресеты, крайние значения, частоты дискретизации, скорость, демо-WAV,
// скриншоты интерфейса в обеих темах, About.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Randomizer.h"

using namespace chroma;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "\n"; } } while (0)

namespace
{
constexpr int kBlock = 256;

void setParam (ChromaProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

// тестовый сигнал: аккорд из пил (стерео-одинаковый) + шум; level — пик примерно
juce::AudioBuffer<float> testSignal (double sr, double seconds, float level = 0.25f, bool sameLR = true)
{
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (7);
    const double fs[] = { 110.0, 164.8, 220.0, 277.2, 329.6 };
    double ph[5] {};
    float lp = 0;
    for (int i = 0; i < n; ++i)
    {
        float v = 0;
        for (int k = 0; k < 5; ++k)
        {
            ph[k] += fs[k] / sr;
            ph[k] -= std::floor (ph[k]);
            v += (float) (2.0 * ph[k] - 1.0) * 0.18f;
        }
        lp += 0.2f * ((rng.nextFloat() * 2.0f - 1.0f) - lp);
        v += lp * 0.25f;
        b.setSample (0, i, v * level / 0.8f);
        b.setSample (1, i, sameLR ? v * level / 0.8f : (v * 0.7f + lp * 0.3f) * level / 0.8f);
    }
    return b;
}

// «музыкальный» сигнал для демо: аккорды пилами с фильтром + хэт
juce::AudioBuffer<float> demoSignal (double sr, double seconds)
{
    const int n = (int) (seconds * sr);
    juce::AudioBuffer<float> b (2, n);
    juce::Random rng (3);
    const int chords[4][4] = { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 43, 55, 59, 62 }, { 40, 55, 59, 64 } };
    double ph[2][4] {};
    float z[2] {};
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        const int c = (int) (t / 2.0) % 4;
        float v[2] {};
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 4; ++k)
            {
                const double f = 440.0 * std::pow (2.0, (chords[c][k] - 69) / 12.0) * (ch == 0 ? 1.0 : 1.003);
                ph[ch][k] += f / sr;
                ph[ch][k] -= std::floor (ph[ch][k]);
                v[ch] += (float) (2.0 * ph[ch][k] - 1.0) * 0.13f;
            }
        const double beat = std::fmod (t, 0.25);
        const float hat = (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-beat * 60.0) * 0.12f;
        for (int ch = 0; ch < 2; ++ch)
        {
            z[ch] += 0.12f * (v[ch] - z[ch]);
            b.setSample (ch, i, z[ch] * 0.9f + hat);
        }
    }
    return b;
}

juce::AudioBuffer<float> run (ChromaProcessor& p, const juce::AudioBuffer<float>& in, ChromaEditor* ed = nullptr)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    const int total = out.getNumSamples();
    for (int pos = 0; pos < total; pos += kBlock)
    {
        const int n = juce::jmin (kBlock, total - pos);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
        p.processBlock (view, midi);
        if (ed != nullptr && (pos / kBlock) % 6 == 0) ed->pumpForTest();
    }
    return out;
}

struct Stats { float peak = 0, rms = 0; bool nan = false; };
Stats stats (const juce::AudioBuffer<float>& b)
{
    Stats s;
    double sum = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = b.getSample (c, i);
            if (! std::isfinite (v)) { s.nan = true; continue; }
            s.peak = juce::jmax (s.peak, std::abs (v));
            sum += (double) v * v;
        }
    s.rms = (float) std::sqrt (sum / juce::jmax (1, b.getNumChannels() * b.getNumSamples()));
    return s;
}

float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int chA = -1)
{
    float d = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = juce::jmax (d, std::abs (a.getSample (c, i) - b.getSample (chA >= 0 ? chA : c, i)));
    return d;
}

float correlation (const juce::AudioBuffer<float>& b)
{
    double xy = 0, xx = 0, yy = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const double x = b.getSample (0, i), y = b.getSample (1, i);
        xy += x * y; xx += x * x; yy += y * y;
    }
    return (float) (xy / std::sqrt (juce::jmax (1e-12, xx * yy)));
}

void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
    if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    std::cout << "  wrote " << f.getFileName() << "\n";
}

void checkAudio (const juce::AudioBuffer<float>& b, const juce::String& what)
{
    const auto s = stats (b);
    CHECK (! s.nan, what << ": NaN");
    CHECK (s.peak <= 1.0f, what << ": peak above 0 dBFS (" << s.peak << ")");
    CHECK (s.rms > 0.003f, what << ": silent (rms " << s.rms << ")");
}

float sineGain (const SvfCoefs& c, double f, double sr)
{
    Svf s;
    s.a1 = c.a1; s.a2 = c.a2; s.a3 = c.a3; s.m0 = c.m0; s.m1 = c.m1; s.m2 = c.m2;
    const int n = (int) sr;
    double inE = 0, outE = 0;
    for (int i = 0; i < n; ++i)
    {
        const float x = (float) std::sin (2.0 * 3.141592653589793 * f * i / sr);
        const float y = s.tick (x);
        if (i > n / 2) { inE += x * x; outE += y * y; }
    }
    return (float) std::sqrt (outE / inE);
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
                                       : juce::File::getCurrentWorkingDirectory().getChildFile ("render");
    outDir.createDirectory();

    // ---------------------------------------------------------------- фильтры и LFO
    std::cout << "filters\n";
    {
        const double sr = 48000.0;
        const float g12 = sineGain (SvfCoefs::bell (1000.0, 4.0, 12.0, sr), 1000.0, sr);
        CHECK (std::abs (juce::Decibels::gainToDecibels (g12) - 12.0f) < 0.2f, "bell +12 dB at centre: " << juce::Decibels::gainToDecibels (g12));
        const float gCut = sineGain (SvfCoefs::bell (2000.0, 10.0, -18.0, sr), 2000.0, sr);
        CHECK (std::abs (juce::Decibels::gainToDecibels (gCut) + 18.0f) < 0.3f, "bell −18 dB at centre: " << juce::Decibels::gainToDecibels (gCut));
        const float gFar = sineGain (SvfCoefs::bell (2000.0, 10.0, -18.0, sr), 200.0, sr);
        CHECK (std::abs (juce::Decibels::gainToDecibels (gFar)) < 0.2f, "narrow bell leaves far frequencies: " << juce::Decibels::gainToDecibels (gFar));
        const float hpLow = sineGain (SvfCoefs::highpass (225.0, 0.71, sr), 56.0, sr);
        const float hpHigh = sineGain (SvfCoefs::highpass (225.0, 0.71, sr), 2000.0, sr);
        CHECK (juce::Decibels::gainToDecibels (hpLow) < -20.0f && std::abs (juce::Decibels::gainToDecibels (hpHigh)) < 0.2f,
               "low cut 225 Hz: " << juce::Decibels::gainToDecibels (hpLow) << " / " << juce::Decibels::gainToDecibels (hpHigh));
        // частоты рецепта: шкала EQ Eight 10 Гц … 22 кГц
        CHECK (std::abs (OphanRecipe::eqFreq (40) - 217.0) < 2.0 && std::abs (OphanRecipe::eqFreq (100) - 22000.0) < 1.0, "EQ Eight frequency scale");
        CHECK (std::abs (OphanRecipe::eqGain (20) + 9.0) < 1e-6 && std::abs (OphanRecipe::eqGain (80) - 9.0) < 1e-6, "EQ Eight gain scale");

        for (int shape : { 0, 1 })
        {
            Lfo l;
            l.reset (5, 0.0);
            double mn = 9, mx = -9, prev = 0, maxStep = 0;
            for (int i = 0; i < 48000 * 20 / 32; ++i)
            {
                const double v = l.advance (1.0, 32.0 / 48000.0, (Lfo::Shape) shape);
                mn = std::min (mn, v); mx = std::max (mx, v);
                if (i > 0) maxStep = std::max (maxStep, std::abs (v - prev));
                prev = v;
            }
            CHECK (mn >= -1.0001 && mx <= 1.0001 && mx - mn > 1.2, "LFO " << shape << " range " << mn << "…" << mx);
            CHECK (maxStep < 0.05, "LFO " << shape << " is not smooth (step " << maxStep << ")");
        }
    }

    // ---------------------------------------------------------------- Ophan
    std::cout << "ophan\n";
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        ChromaProcessor p;
        p.prepareToPlay (sr, kBlock);
        p.presets.loadFactory (0);   // Biblically Accurate
        CHECK (p.getSlotType (0) == EffectType::Ophan && p.getSlotType (1) == EffectType::Empty, "default rack: Ophan + empty slots");
        const auto in = testSignal (sr, 6.0);
        auto out = run (p, in);
        checkAudio (out, "ophan @" + juce::String (sr));
        const float corr = correlation (out);
        CHECK (corr < 0.98f, "L and R must differ (correlation " << corr << ")");
        CHECK (maxDiff (out, in) > 0.01f, "ophan changes the sound");
        std::cout << "  " << sr << " Hz: peak " << juce::String (stats (out).peak, 3) << ", corr " << juce::String (corr, 3) << "\n";
    }
    {
        // Spread 0: правый канал повторяет левый
        ChromaProcessor p;
        p.prepareToPlay (48000.0, kBlock);
        setParam (p, slotParam (0, ParamId::spread), 0.0f);
        auto out = run (p, testSignal (48000.0, 3.0));
        float d = 0;
        for (int i = 0; i < out.getNumSamples(); ++i) d = juce::jmax (d, std::abs (out.getSample (0, i) - out.getSample (1, i)));
        CHECK (d < 1e-4f, "spread 0 → mono (diff " << d << ")");
        // Depth 0 и низкий Low cut: почти без изменений
        setParam (p, slotParam (0, ParamId::spread), 100.0f);
        setParam (p, slotParam (0, ParamId::depth), 0.0f);
        setParam (p, slotParam (0, ParamId::lowCut), 10.0f);
        const auto in = testSignal (48000.0, 2.0);
        auto flat = run (p, in);
        juce::AudioBuffer<float> tail (2, 48000);
        juce::AudioBuffer<float> tailIn (2, 48000);
        for (int c = 0; c < 2; ++c) { tail.copyFrom (c, 0, flat, c, 48000, 48000); tailIn.copyFrom (c, 0, in, c, 48000, 48000); }
        const float r = stats (tail).rms / stats (tailIn).rms;
        CHECK (std::abs (juce::Decibels::gainToDecibels (r)) < 0.5f, "depth 0 is nearly flat (" << juce::Decibels::gainToDecibels (r) << " dB)");
        // Mix 0 — сигнал без изменений
        setParam (p, slotParam (0, ParamId::depth), 200.0f);
        setParam (p, slotParam (0, ParamId::mix), 0.0f);
        run (p, testSignal (48000.0, 0.5));   // сглаживание Mix (≈ 30 мс)
        const auto in2 = testSignal (48000.0, 1.0);
        auto dry = run (p, in2);
        CHECK (maxDiff (dry, in2) < 1e-5f, "mix 0 → dry (diff " << maxDiff (dry, in2) << ")");
    }
    {
        // пустой и выключенный слот — бит-в-бит
        ChromaProcessor p;
        p.prepareToPlay (48000.0, kBlock);
        const auto in = testSignal (48000.0, 1.0);
        setParam (p, slotParam (0, ParamId::on), 0.0f);
        CHECK (maxDiff (run (p, in), in) == 0.0f, "slot off → bit-exact passthrough");
        setParam (p, slotParam (0, ParamId::on), 1.0f);
        p.setSlotType (0, EffectType::Empty);
        CHECK (maxDiff (run (p, in), in) == 0.0f, "empty rack → bit-exact passthrough");
    }
    {
        // крайние значения и громкий вход: лимитер держит 0 dBFS
        ChromaProcessor p;
        p.prepareToPlay (48000.0, 64);
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                 { ParamId::rate, 12.0f }, { ParamId::drift, 100.0f }, { ParamId::depth, 200.0f }, { ParamId::q, 400.0f }, { ParamId::lowCut, 10.0f } })
            setParam (p, slotParam (0, id), v);
        for (int s = 1; s < kSlots; ++s) { p.setSlotType (s, EffectType::Ophan); setParam (p, slotParam (s, ParamId::q), 25.0f); setParam (p, slotParam (s, ParamId::rate), 0.02f); }
        auto out = run (p, testSignal (48000.0, 4.0, 0.95f, false));
        checkAudio (out, "extremes, 4 slots, loud input");
        setParam (p, slotParam (0, ParamId::lowCut), 2000.0f);
        setParam (p, slotParam (0, ParamId::shape), 0.0f);
        checkAudio (run (p, testSignal (48000.0, 2.0, 0.95f, false)), "extremes 2");
        // моно-раскладка
        ChromaProcessor m;
        juce::AudioProcessor::BusesLayout mono;
        mono.inputBuses.add (juce::AudioChannelSet::mono());
        mono.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (m.setBusesLayout (mono), "mono layout supported");
        m.prepareToPlay (48000.0, kBlock);
        auto in = testSignal (48000.0, 2.0);
        juce::AudioBuffer<float> one (1, in.getNumSamples());
        one.copyFrom (0, 0, in, 0, 0, in.getNumSamples());
        auto mo = run (m, one);
        const auto ms = stats (mo);
        CHECK (! ms.nan && ms.peak <= 1.0f && ms.rms > 0.003f, "mono processing");
    }

    // ---------------------------------------------------------------- OTT Stack
    std::cout << "ott stack\n";
    {
        const double sr = 48000.0;
        auto ottProc = [&] (ChromaProcessor& p)
        {
            p.prepareToPlay (sr, kBlock);
            p.setSlotType (0, EffectType::OttStack);
        };
        auto rmsDb = [] (const juce::AudioBuffer<float>& b, int from)
        {
            double sum = 0;
            int cnt = 0;
            for (int c = 0; c < b.getNumChannels(); ++c)
                for (int i = from; i < b.getNumSamples(); ++i) { sum += (double) b.getSample (c, i) * b.getSample (c, i); ++cnt; }
            return (float) (10.0 * std::log10 (std::max (1e-20, sum / std::max (1, cnt))));
        };
        {
            // Depth 0: 40 ступеней — только кроссоверы, сумма полос ровная
            ChromaProcessor p;
            ottProc (p);
            setParam (p, slotParam (0, ParamId::count), 40.0f);
            setParam (p, slotParam (0, ParamId::ottDepth), 0.0f);
            setParam (p, slotParam (0, ParamId::trim), 0.0f);
            const auto in = testSignal (sr, 3.0, 0.25f, false);
            auto out = run (p, in);
            const float d = rmsDb (out, (int) sr) - rmsDb (in, (int) sr);
            CHECK (std::abs (d) < 0.3f, "OTT depth 0: crossovers sum flat (" << d << " dB)");
            for (double f : { 40.0, 88.3, 300.0, 2500.0, 9000.0 })
            {
                juce::AudioBuffer<float> sine (2, (int) sr);
                for (int i = 0; i < sine.getNumSamples(); ++i)
                    for (int c = 0; c < 2; ++c) sine.setSample (c, i, 0.25f * (float) std::sin (2.0 * 3.141592653589793 * f * i / sr));
                auto so = run (p, sine);
                const float g = rmsDb (so, (int) sr / 2) - rmsDb (sine, (int) sr / 2);
                CHECK (std::abs (g) < 0.2f, "OTT depth 0: flat at " << f << " Hz (" << g << " dB)");
            }
        }
        {
            // громкость по числу ступеней (Output 0 dB) — для выбора Output по умолчанию
            const auto music = demoSignal (sr, 6.0);
            juce::String line = "  rms in " + juce::String (rmsDb (music, (int) sr), 1) + " dB; out:";
            for (int c : { 2, 4, 8, 16, 24, 40 })
            {
                ChromaProcessor p;
                ottProc (p);
                setParam (p, slotParam (0, ParamId::count), (float) c);
                setParam (p, slotParam (0, ParamId::trim), 0.0f);
                auto out = run (p, music);
                checkAudio (out, "OTT x" + juce::String (c));
                line << " x" << c << " " << juce::String (rmsDb (out, (int) sr), 1);
            }
            std::cout << line << "\n";
        }
        {
            // сжимает: громкая и тихая половины сближаются
            ChromaProcessor p;
            ottProc (p);
            setParam (p, slotParam (0, ParamId::count), 4.0f);
            auto in = testSignal (sr, 6.0, 0.5f, false);
            for (int c = 0; c < 2; ++c)
                for (int i = (int) (3 * sr); i < in.getNumSamples(); ++i) in.setSample (c, i, in.getSample (c, i) * 0.02f);   // −34 дБ
            auto out = run (p, in);
            auto part = [&] (const juce::AudioBuffer<float>& b, double t0, double t1)
            {
                juce::AudioBuffer<float> x (2, (int) ((t1 - t0) * sr));
                for (int c = 0; c < 2; ++c) x.copyFrom (c, 0, b, c, (int) (t0 * sr), x.getNumSamples());
                return rmsDb (x, 0);
            };
            const float inGap = part (in, 1.5, 3.0) - part (in, 4.5, 6.0), outGap = part (out, 1.5, 3.0) - part (out, 4.5, 6.0);
            std::cout << "  loud/quiet gap: in " << juce::String (inGap, 1) << " dB, out " << juce::String (outGap, 1) << " dB\n";
            CHECK (outGap < inGap * 0.5f, "OTT compresses the loud/quiet gap");
        }
        {
            // тишина остаётся тишиной, очень тихий шум не раздувается
            ChromaProcessor p;
            ottProc (p);
            setParam (p, slotParam (0, ParamId::count), 40.0f);
            setParam (p, slotParam (0, ParamId::trim), 0.0f);
            juce::AudioBuffer<float> zero (2, (int) sr * 2);
            zero.clear();
            auto out = run (p, zero);
            CHECK (stats (out).peak == 0.0f, "OTT: silence stays silent");
            juce::AudioBuffer<float> hiss (2, (int) sr * 3);
            juce::Random rng (11);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < hiss.getNumSamples(); ++i) hiss.setSample (c, i, (rng.nextFloat() * 2 - 1) * 3e-5f);   // ≈ −90 дБ
            auto ho = run (p, hiss);
            const float lvl = rmsDb (ho, (int) sr);
            CHECK (lvl < -60.0f, "OTT x40: −90 dB hiss is not blown up (" << lvl << " dB)");
            // Mix 0 — сухой сигнал бит-в-бит
            setParam (p, slotParam (0, ParamId::mix), 0.0f);
            run (p, testSignal (sr, 0.5));
            const auto in2 = testSignal (sr, 1.0);
            CHECK (maxDiff (run (p, in2), in2) == 0.0f, "OTT mix 0 → dry");
        }
        {
            // крайние значения, смена числа ступеней на ходу, громкий вход
            ChromaProcessor p;
            ottProc (p);
            setParam (p, slotParam (0, ParamId::time), 10.0f);
            setParam (p, slotParam (0, ParamId::trim), 12.0f);
            auto a = run (p, testSignal (sr, 1.0, 0.95f, false));
            setParam (p, slotParam (0, ParamId::count), 40.0f);
            setParam (p, slotParam (0, ParamId::time), 1000.0f);
            auto b = run (p, testSignal (sr, 1.0, 0.95f, false));
            setParam (p, slotParam (0, ParamId::count), 2.0f);
            setParam (p, slotParam (0, ParamId::upward), 0.0f);
            auto c = run (p, testSignal (sr, 1.0, 0.95f, false));
            for (auto* buf : { &a, &b, &c }) checkAudio (*buf, "OTT extremes");
            CHECK (std::abs (p.ottViews[0].count.load() - 2.0f) < 0.01f, "OTT count follows the knob");
            // моно
            ChromaProcessor m;
            juce::AudioProcessor::BusesLayout mono;
            mono.inputBuses.add (juce::AudioChannelSet::mono());
            mono.outputBuses.add (juce::AudioChannelSet::mono());
            m.setBusesLayout (mono);
            ottProc (m);
            juce::AudioBuffer<float> one (1, (int) sr);
            auto src = testSignal (sr, 1.0);
            one.copyFrom (0, 0, src, 0, 0, (int) sr);
            const auto ms = stats (run (m, one));
            CHECK (! ms.nan && ms.peak <= 1.0f && ms.rms > 0.003f, "OTT mono");
        }
    }

    // ---------------------------------------------------------------- OTT: Click guard
    std::cout << "ott click guard\n";
    {
        // синус с мягкой атакой после тишины: без защиты — всплеск в начале ноты, с защитой — не выше входа
        const double sr = 48000.0;
        const int n = (int) (sr * 4.0), on = (int) (sr * 3.0);
        juce::AudioBuffer<float> note (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float env = i < on ? 0.0f : juce::jmin (1.0f, (float) (i - on) / (float) (0.01 * sr));
            const float v = 0.3f * env * (float) std::sin (2.0 * 3.141592653589793 * 220.0 * i / sr);
            note.setSample (0, i, v);
            note.setSample (1, i, v);
        }
        auto onsetPeak = [&] (bool guard, int count)
        {
            ChromaProcessor p;
            p.presets.loadFactory (0);
            p.setSlotType (0, EffectType::OttStack);
            setParam (p, slotParam (0, ParamId::count), (float) count);
            setParam (p, slotParam (0, ParamId::trim), 0.0f);
            setParam (p, slotParam (0, ParamId::guard), guard ? 1.0f : 0.0f);
            setParam (p, ParamId::output, -12.0f);   // ниже лимитера: меряем саму стопку
            p.prepareToPlay (sr, kBlock);
            const auto out = run (p, note);
            float pk = 0;
            for (int i = on; i < on + (int) (0.03 * sr); ++i) pk = juce::jmax (pk, std::abs (out.getSample (0, i)));
            return 20.0f * std::log10 (pk / (0.3f * juce::Decibels::decibelsToGain (-12.0f)));   // относительно входа
        };
        for (int c : { 2, 8, 40 })
        {
            const float off = onsetPeak (false, c), guarded = onsetPeak (true, c);
            std::cout << "  x" << c << ": onset vs input " << juce::String (off, 1) << " dB, with guard " << juce::String (guarded, 1) << " dB\n";
            CHECK (guarded <= 3.0f, "click guard: burst at note start x" << c);
            CHECK (off > guarded + 6.0f, "click guard changes nothing x" << c);
        }
        // включение защиты посреди ноты — без скачка громкости
        {
            ChromaProcessor p;
            p.presets.loadFactory (0);
            p.setSlotType (0, EffectType::OttStack);
            p.prepareToPlay (sr, kBlock);
            run (p, testSignal (sr, 3.0));
            const auto before = stats (run (p, testSignal (sr, 1.0)));
            setParam (p, slotParam (0, ParamId::guard), 1.0f);
            const auto after = stats (run (p, testSignal (sr, 1.0)));
            std::cout << "  switching on mid-signal: " << juce::String (juce::Decibels::gainToDecibels (after.rms / before.rms), 2) << " dB\n";
            CHECK (std::abs (juce::Decibels::gainToDecibels (after.rms / before.rms)) < 1.0f, "click guard switch jumps");
            CHECK (! after.nan, "click guard NaN");
        }
    }

    // ---------------------------------------------------------------- порядок, копия, состояние
    std::cout << "rack and state\n";
    {
        ChromaProcessor p;
        p.prepareToPlay (48000.0, kBlock);
        p.presets.loadFactory (7);   // Wheels Within Wheels: два слота
        CHECK (p.getSlotType (0) == EffectType::Ophan && p.getSlotType (1) == EffectType::Ophan, "two-slot preset");
        p.moveSlot (0, 3);
        auto o = p.getOrder();
        CHECK (o[0] == 1 && o[1] == 2 && o[2] == 3 && o[3] == 0, "moveSlot 0 → 3");
        p.advanced (1).band[1][3].q = 3.5f;
        p.advanced (1).band[0][6].fTo = 777.0f;
        p.advanced (2).driftRate = 1.25f;
        setParam (p, slotParam (1, ParamId::rate), 2.5f);
        juce::MemoryBlock mb;
        p.getStateInformation (mb);

        ChromaProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        o = q.getOrder();
        CHECK (o[0] == 1 && o[3] == 0, "order restored");
        CHECK (std::abs (q.advanced (1).band[1][3].q.load() - 3.5f) < 1e-4f, "advanced Q restored");
        CHECK (std::abs (q.advanced (1).band[0][6].fTo.load() - 777.0f) < 1e-3f, "advanced freq restored");
        CHECK (std::abs (q.advanced (2).driftRate.load() - 1.25f) < 1e-4f, "drift rate restored");
        CHECK (std::abs (q.apvts.getRawParameterValue (slotParam (1, ParamId::rate))->load() - 2.5f) < 0.01f, "slot param restored");
        CHECK (q.presets.getCurrentName() == "Wheels Within Wheels", "preset name restored");

        // копия слота
        q.copySlot (1, 3);
        CHECK (q.getSlotType (3) == EffectType::Ophan && std::abs (q.advanced (3).band[1][3].q.load() - 3.5f) < 1e-4f, "copySlot copies advanced");
        q.clearSlot (3);
        CHECK (q.getSlotType (3) == EffectType::Empty && std::abs (q.advanced (3).band[1][3].q.load() - OphanAdvanced::recipe (1, 3).q) < 1e-4f, "clearSlot");
        // фабричный пресет сбрасывает порядок и расширенные настройки
        q.presets.loadFactory (0);
        o = q.getOrder();
        CHECK (o[0] == 0 && o[1] == 1 && o[2] == 2 && o[3] == 3, "factory preset resets order");
        CHECK (std::abs (q.advanced (1).band[1][3].q.load() - OphanAdvanced::recipe (1, 3).q) < 1e-4f, "factory preset resets advanced");
        // порядок меняет звук (два разных Ophan подряд не коммутируют из-за лимитера/модуляции — проверяем просто, что не тишина)
        checkAudio (run (q, testSignal (48000.0, 1.0)), "after state round trip");
    }

    // ---------------------------------------------------------------- 8 слотов
    std::cout << "eight slots\n";
    {
        // проект v2.01: порядок из 4 цифр, параметров слотов 5–8 нет
        ChromaProcessor p;
        p.presets.loadFactory (0);
        for (int s = 4; s < kSlots; ++s) { p.setSlotType (s, EffectType::OttStack); setParam (p, slotParam (s, ParamId::count), 30.0f); }
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->setAttribute ("order", "1023");
        for (int s = 4; s < kSlots; ++s)
            for (auto* e = xml->getFirstChildElement(); e != nullptr;)
            {
                auto* next = e->getNextElement();
                if (e->getStringAttribute ("id").startsWith ("s" + juce::String (s + 1) + "_")) xml->removeChildElement (e, true);
                e = next;
            }
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        p.setStateInformation (old.getData(), (int) old.getSize());
        const auto o = p.getOrder();
        CHECK (o[0] == 1 && o[1] == 0 && o[2] == 2 && o[3] == 3 && o[4] == 4 && o[7] == 7, "old 4-slot order not extended");
        bool emptyRest = true;
        for (int s = 4; s < kSlots; ++s) emptyRest = emptyRest && p.getSlotType (s) == EffectType::Empty;
        CHECK (emptyRest, "old project: slots 5-8 not reset to empty");
        CHECK (p.getSlotType (0) == EffectType::Ophan, "old project: slot 1 lost");
        // 8 эффектов в Serial и Parallel
        for (int s = 0; s < kSlots; ++s) p.setSlotType (s, s % 2 == 0 ? EffectType::Ophan : EffectType::OttStack);
        p.prepareToPlay (48000.0, kBlock);
        checkAudio (run (p, testSignal (48000.0, 1.0)), "eight slots serial");
        p.setChainMode (ChainMode::Parallel);
        p.prepareToPlay (48000.0, kBlock);
        checkAudio (run (p, testSignal (48000.0, 1.0)), "eight slots parallel");
    }

    // ---------------------------------------------------------------- пресеты эффектов
    std::cout << "effect presets\n";
    {
        ChromaProcessor p;
        p.presets.loadFactory (0);
        const auto& fo = effectFactoryPresets (EffectType::Ophan);
        const auto& ft = effectFactoryPresets (EffectType::OttStack);
        std::cout << "  " << fo.size() << " Ophan, " << ft.size() << " OTT Stack\n";
        CHECK (fo.size() >= 8 && fo[0].name == "Biblically Accurate", "Ophan factory effect presets");
        CHECK (ft.size() >= 4, "OTT factory effect presets");
        CHECK (p.getEffectPresetName (0) == "Biblically Accurate", "rack preset does not name the effect");
        auto value = [&p] (int s, const char* id) { return p.apvts.getRawParameterValue (slotParam (s, id))->load(); };
        // фабричный: ручки, Mix — да, On — нет
        setParam (p, slotParam (0, ParamId::on), 0.0f);
        for (int i = 0; i < (int) fo.size(); ++i)
            if (fo[(size_t) i].name == "Gentle Halo") p.loadEffectFactory (0, i);
        CHECK (std::abs (value (0, ParamId::rate) - 0.3f) < 0.01f && std::abs (value (0, ParamId::mix) - 70.0f) < 0.01f, "effect factory values");
        CHECK (value (0, ParamId::on) < 0.5f, "effect preset changed On");
        CHECK (p.getEffectPresetName (0) == "Gentle Halo", "effect preset name");
        setParam (p, slotParam (0, ParamId::on), 1.0f);
        // свой: снимок → изменить → вернуть (с Advanced)
        setParam (p, slotParam (0, ParamId::rate), 3.3f);
        p.advanced (0).band[1][2].q = 4.25f;
        const auto snap = p.captureEffect (0);
        setParam (p, slotParam (0, ParamId::rate), 0.1f);
        p.advanced (0).band[1][2].q = 17.0f;
        CHECK (p.applyEffect (0, snap), "applyEffect failed");
        CHECK (std::abs (value (0, ParamId::rate) - 3.3f) < 0.01f, "effect preset rate not restored");
        CHECK (std::abs (p.advanced (0).band[1][2].q.load() - 4.25f) < 1e-4f, "effect preset advanced not restored");
        // через XML, как в файле
        auto xml = snap.createXml();
        CHECK (xml != nullptr && p.applyEffect (0, juce::ValueTree::fromXml (*xml)), "effect preset XML round trip");
        // пресет другого эффекта не грузится
        p.setSlotType (1, EffectType::OttStack);
        CHECK (p.getEffectPresetName (1) == "Init", "new effect is not Init");
        CHECK (! p.applyEffect (1, snap), "Ophan preset applied to OTT Stack");
        p.loadEffectFactory (1, 0);
        CHECK (p.getEffectPresetName (1) == ft[0].name, "OTT effect preset name");
        p.stepEffectPreset (1, 1);
        CHECK (p.getEffectPresetName (1) == ft[1].name, "step effect preset");
        // копия слота несёт имя; смена эффекта — сбрасывает
        p.copySlot (1, 2);
        CHECK (p.getEffectPresetName (2) == ft[1].name, "copySlot does not copy preset name");
        p.setSlotType (2, EffectType::Ophan);
        CHECK (p.getEffectPresetName (2) == "Init", "changing effect keeps preset name");
        // имя едет в проект
        juce::MemoryBlock mb;
        p.getStateInformation (mb);
        ChromaProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (q.getEffectPresetName (1) == ft[1].name, "effect preset name not restored");
    }

    // ---------------------------------------------------------------- параллельный режим
    std::cout << "parallel\n";
    {
        const double sr = 48000.0;
        const auto in = testSignal (sr, 1.0, 0.25f, false);
        auto fresh = [&] (ChromaProcessor& p, ChainMode m)
        {
            p.presets.loadFactory (0);   // один Ophan
            p.setChainMode (m);
            p.prepareToPlay (sr, kBlock);
        };
        // одна ветка звучит так же, как в Serial, бит-в-бит
        {
            ChromaProcessor a, b;
            fresh (a, ChainMode::Serial);
            fresh (b, ChainMode::Parallel);
            CHECK (maxDiff (run (a, in), run (b, in)) == 0.0f, "parallel with one slot differs from serial");
        }
        // всё выключено — сухой бит-в-бит; два слота с Mix 0 — тоже
        {
            ChromaProcessor p;
            fresh (p, ChainMode::Parallel);
            setParam (p, slotParam (0, ParamId::on), 0.0f);
            p.prepareToPlay (sr, kBlock);
            CHECK (maxDiff (run (p, in), in) == 0.0f, "parallel, all off: not bit-exact");
            setParam (p, slotParam (0, ParamId::on), 1.0f);
            p.copySlot (0, 1);
            setParam (p, slotParam (0, ParamId::mix), 0.0f);
            setParam (p, slotParam (1, ParamId::mix), 0.0f);
            p.prepareToPlay (sr, kBlock);
            CHECK (maxDiff (run (p, in), in) == 0.0f, "parallel, two slots at Mix 0: not bit-exact");
        }
        // выключили слот на ходу: веса докатываются, дальше — сухой бит-в-бит
        {
            ChromaProcessor p;
            fresh (p, ChainMode::Parallel);
            run (p, in);
            setParam (p, slotParam (0, ParamId::on), 0.0f);
            auto fade = run (p, testSignal (sr, 0.3, 0.25f, false));
            checkAudio (fade, "parallel fade-out");
            CHECK (maxDiff (run (p, in), in) == 0.0f, "parallel: slot switched off does not settle to dry");
        }
        // два Ophan: в Parallel иначе, чем в Serial; громкость в пределах
        {
            ChromaProcessor a, b;
            for (auto* p : { &a, &b })
            {
                p->presets.loadFactory (7);   // Wheels Within Wheels: два Ophan
                p->setChainMode (p == &a ? ChainMode::Serial : ChainMode::Parallel);
                p->prepareToPlay (sr, kBlock);
            }
            const auto sa = run (a, in), sb = run (b, in);
            checkAudio (sb, "parallel, two Ophan");
            CHECK (maxDiff (sa, sb) > 0.01f, "parallel sounds the same as serial");
        }
        // смена режима на ходу: короткий провал, без NaN и перегруза; большой блок тоже
        {
            ChromaProcessor p;
            p.presets.loadFactory (13);   // Squash Choir: Ophan → OTT
            p.prepareToPlay (sr, 4096);
            juce::AudioBuffer<float> out (in);
            juce::MidiBuffer midi;
            for (int pos = 0, k = 0; pos < out.getNumSamples(); pos += kBlock, ++k)
            {
                if (k % 20 == 10) p.setChainMode (p.getChainMode() == ChainMode::Serial ? ChainMode::Parallel : ChainMode::Serial);
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, pos, juce::jmin (kBlock, out.getNumSamples() - pos));
                p.processBlock (view, midi);
            }
            checkAudio (out, "mode switching");
            juce::AudioBuffer<float> big (testSignal (sr, 0.5, 0.25f, false));
            p.setChainMode (ChainMode::Parallel);
            p.processBlock (big, midi);   // блок больше, чем объявлен в prepareToPlay
            checkAudio (big, "parallel, oversized block");
        }
        // состояние: режим сохраняется, старый проект без режима — Serial, фабричный пресет ставит свой
        {
            ChromaProcessor p;
            p.setChainMode (ChainMode::Parallel);
            juce::MemoryBlock mb;
            p.getStateInformation (mb);
            ChromaProcessor q;
            q.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q.getChainMode() == ChainMode::Parallel, "mode not restored");
            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
            xml->removeAttribute ("mode");
            juce::MemoryBlock old;
            juce::AudioProcessor::copyXmlToBinary (*xml, old);
            q.setStateInformation (old.getData(), (int) old.getSize());
            CHECK (q.getChainMode() == ChainMode::Serial, "old project without mode is not serial");
            q.setChainMode (ChainMode::Parallel);
            q.presets.loadFactory (0);
            CHECK (q.getChainMode() == ChainMode::Serial, "factory preset does not reset mode");
            for (int i = 0; i < q.presets.getNumFactory(); ++i)
                if (juce::String (factoryPresets()[(size_t) i].name) == "Ophanim") q.presets.loadFactory (i);
            CHECK (q.getChainMode() == ChainMode::Parallel, "Ophanim is not parallel");
        }
    }

    // ---------------------------------------------------------------- кубик
    std::cout << "dice\n";
    {
        const double sr = 48000.0;
        auto setup = [&] (ChromaProcessor& p)
        {
            p.presets.loadFactory (0);
            p.setSlotType (1, EffectType::OttStack);
            p.setSlotType (2, EffectType::Ophan);
            setParam (p, slotParam (2, ParamId::on), 0.0f);
            setParam (p, ParamId::output, -2.0f);
            p.prepareToPlay (sr, kBlock);
        };
        auto value = [] (ChromaProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); };

        ChromaProcessor p;
        setup (p);
        juce::Random rng (5);
        int worstSilent = 0;
        float worstPeak = 0;
        bool rateChanged = false, countChanged = false, mixChanged = false, advChanged = false, orderChanged = false;
        for (int k = 0; k < 40; ++k)
        {
            const float rate0 = value (p, slotParam (0, ParamId::rate)), count0 = value (p, slotParam (1, ParamId::count));
            const float mix0 = value (p, slotParam (0, ParamId::mix)), q0 = p.advanced (0).band[0][2].q.load();
            const auto o0 = p.getOrder();
            randomizeRack (p, rng);
            rateChanged |= value (p, slotParam (0, ParamId::rate)) != rate0;
            countChanged |= value (p, slotParam (1, ParamId::count)) != count0;
            mixChanged |= value (p, slotParam (0, ParamId::mix)) != mix0;
            advChanged |= p.advanced (0).band[0][2].q.load() != q0;
            orderChanged |= p.getOrder() != o0;
            const auto st = stats (run (p, testSignal (sr, 0.5, 0.5f, false)));
            CHECK (! st.nan, "dice: NaN");
            worstPeak = juce::jmax (worstPeak, st.peak);
            if (st.rms < 0.003f) ++worstSilent;
        }
        std::cout << "  40 rolls: worst peak " << juce::String (worstPeak, 3) << ", silent " << worstSilent << "\n";
        CHECK (worstSilent == 0, "dice produced silence");
        CHECK (worstPeak <= 1.0f, "dice: peak above 0 dBFS");
        CHECK (rateChanged && countChanged && mixChanged && advChanged && orderChanged, "dice did not change something it may change");
        // никогда не трогает: тип, пустой слот, On, громкость, Output у OTT, режим
        CHECK (p.getSlotType (0) == EffectType::Ophan && p.getSlotType (1) == EffectType::OttStack && p.getSlotType (2) == EffectType::Ophan
               && p.getSlotType (3) == EffectType::Empty, "dice changed effect types");
        CHECK (value (p, slotParam (2, ParamId::on)) < 0.5f && value (p, slotParam (0, ParamId::on)) > 0.5f, "dice changed On");
        CHECK (std::abs (value (p, ParamId::output) + 2.0f) < 0.01f, "dice changed output volume");
        CHECK (std::abs (value (p, slotParam (1, ParamId::trim)) + 3.0f) < 0.01f, "dice changed OTT output");
        CHECK (std::abs (value (p, slotParam (3, ParamId::rate)) - 1.0f) < 0.01f, "dice changed an empty slot");
        CHECK (p.getChainMode() == ChainMode::Serial, "dice changed the mode");

        // запрещённые группы не меняются
        {
            ChromaProcessor q;
            setup (q);
            RandomizerOptions o;
            o.enabled.fill (false);
            o.enabled[RandomizerOptions::OttStack] = true;
            juce::MemoryBlock before;
            const float rate0 = value (q, slotParam (0, ParamId::rate)), mix0 = value (q, slotParam (1, ParamId::mix)), q0 = q.advanced (0).band[1][4].q.load();
            const float count0 = value (q, slotParam (1, ParamId::count));
            juce::Random r2 (9);
            bool cnt = false;
            for (int k = 0; k < 10; ++k) { randomizeRack (q, r2, o); cnt |= value (q, slotParam (1, ParamId::count)) != count0; }
            CHECK (cnt, "dice: OTT allowed but count never changed");
            CHECK (value (q, slotParam (0, ParamId::rate)) == rate0, "dice: Ophan disabled but changed");
            CHECK (value (q, slotParam (1, ParamId::mix)) == mix0, "dice: Mix disabled but changed");
            CHECK (q.advanced (0).band[1][4].q.load() == q0, "dice: advanced disabled but changed");
            const auto o0 = q.getOrder();
            CHECK (o0[0] == 0 && o0[1] == 1 && o0[2] == 2 && o0[3] == 3, "dice: order disabled but changed");
            // в Parallel порядок не трогается даже при разрешении
            o.enabled.fill (true);
            q.setChainMode (ChainMode::Parallel);
            for (int k = 0; k < 10; ++k) randomizeRack (q, r2, o);
            CHECK (q.getOrder() == o0, "dice shuffled order in parallel mode");
            // последовательность кубика не зависит от настроек
            ChromaProcessor x, y;
            setup (x);
            setup (y);
            juce::Random rx (3), ry (3);
            RandomizerOptions onlyOphan;
            onlyOphan.enabled.fill (false);
            onlyOphan.enabled[RandomizerOptions::Ophan] = true;
            randomizeRack (x, rx);
            randomizeRack (y, ry, onlyOphan);
            CHECK (value (x, slotParam (0, ParamId::rate)) == value (y, slotParam (0, ParamId::rate)), "dice sequence depends on options");
        }
    }

    // ---------------------------------------------------------------- пресеты
    std::cout << "presets\n";
    {
        ChromaProcessor p;
        p.prepareToPlay (48000.0, kBlock);
        for (int i = 0; i < p.presets.getNumFactory(); ++i)
        {
            p.presets.loadFactory (i);
            auto out = run (p, testSignal (48000.0, 3.0));
            checkAudio (out, juce::String ("preset ") + factoryPresets()[(size_t) i].name);
        }
        std::cout << "  " << p.presets.getNumFactory() << " presets\n";
    }

    // ---------------------------------------------------------------- скорость
    std::cout << "speed\n";
    {
        ChromaProcessor p;
        p.prepareToPlay (48000.0, kBlock);
        for (int s = 0; s < 4; ++s) p.setSlotType (s, EffectType::Ophan);
        const auto in = testSignal (48000.0, 10.0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto out = run (p, in);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  4 x Ophan, 10 s stereo in " << juce::String (ms, 0) << " ms (" << juce::String (10000.0 / ms, 1) << "x realtime)\n";
        CHECK (ms < 2000.0, "too slow");
        checkAudio (out, "speed");
        for (int s = 0; s < 4; ++s) { p.setSlotType (s, EffectType::OttStack); setParam (p, slotParam (s, ParamId::count), 40.0f); }
        const auto t1 = juce::Time::getMillisecondCounterHiRes();
        auto out2 = run (p, in);
        const double ms2 = juce::Time::getMillisecondCounterHiRes() - t1;
        std::cout << "  4 x OTT Stack x40, 10 s stereo in " << juce::String (ms2, 0) << " ms (" << juce::String (10000.0 / ms2, 1) << "x realtime)\n";
        CHECK (ms2 < 8000.0, "OTT stack too slow");
        checkAudio (out2, "speed OTT");
        for (int s = 0; s < 4; ++s) setParam (p, slotParam (s, ParamId::guard), 1.0f);
        const auto t2 = juce::Time::getMillisecondCounterHiRes();
        auto out3 = run (p, in);
        const double ms3 = juce::Time::getMillisecondCounterHiRes() - t2;
        std::cout << "  4 x OTT Stack x40 + click guard, 10 s stereo in " << juce::String (ms3, 0) << " ms (" << juce::String (10000.0 / ms3, 1) << "x realtime)\n";
        CHECK (ms3 < 9000.0, "OTT stack with click guard too slow");
        checkAudio (out3, "speed OTT guard");
    }

    // ---------------------------------------------------------------- демо WAV
    std::cout << "demo\n";
    {
        const double sr = 48000.0;
        const auto in = demoSignal (sr, 16.0);
        writeWav (outDir.getChildFile ("demo_00_dry.wav"), in, sr);
        ChromaProcessor p;
        p.prepareToPlay (sr, kBlock);
        int k = 1;
        for (auto* nm : { "Biblically Accurate", "Gentle Halo", "Wheels Within Wheels", "Glossolalia", "Double OTT", "Tower of Babel", "Squash Choir" })
            for (int i = 0; i < p.presets.getNumFactory(); ++i)
                if (juce::String (factoryPresets()[(size_t) i].name) == nm)
                {
                    p.presets.loadFactory (i);
                    p.prepareToPlay (sr, kBlock);
                    auto out = run (p, in);
                    writeWav (outDir.getChildFile ("demo_" + juce::String (k++).paddedLeft ('0', 2) + "_" + juce::String (nm).replace (" ", "_").toLowerCase() + ".wav"), out, sr);
                }
    }

    // ---------------------------------------------------------------- интерфейс
    std::cout << "ui\n";
    {
        ChromaProcessor p;
        p.prepareToPlay (48000.0, kBlock);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<ChromaEditor*> (base.get());
        CHECK (ed != nullptr, "editor");
        ed->setSize (ChromaEditor::kBaseW, ChromaEditor::kBaseH);
        const auto music = demoSignal (48000.0, 1.6);

        auto shoot = [&] (const juce::String& name)
        {
            ed->pumpForTest();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat().writeImageToStream (img, fos);
            std::cout << "  wrote " << f.getFileName() << "\n";
        };

        for (auto theme : { ThemeId::Oxygen, ThemeId::Rubber })
        {
            const juce::String tn = theme == ThemeId::Oxygen ? "oxygen" : "rubber";
            ed->setThemeForTest (theme);

            p.presets.loadFactory (7);   // два слота
            ed->selectSlotForTest (0);
            ed->setAdvancedForTest (false);
            ed->setViewForTest (0);
            run (p, music, ed);
            shoot ("ui_" + tn + "_macro");

            ed->setAdvancedForTest (true);
            run (p, demoSignal (48000.0, 0.4), ed);
            shoot ("ui_" + tn + "_advanced");
            ed->setAdvancedForTest (false);

            ed->setViewForTest (1);
            run (p, music, ed);
            shoot ("ui_" + tn + "_scope");
            ed->setViewForTest (0);

            ed->selectSlotForTest (2);
            run (p, demoSignal (48000.0, 0.4), ed);
            shoot ("ui_" + tn + "_empty");
            ed->selectSlotForTest (0);

            for (int i = 0; i < p.presets.getNumFactory(); ++i)
                if (juce::String (factoryPresets()[(size_t) i].name) == "Squash Choir") p.presets.loadFactory (i);
            ed->selectSlotForTest (1);
            run (p, music, ed);
            shoot ("ui_" + tn + "_ott");
            ed->setViewForTest (1);
            run (p, music, ed);
            shoot ("ui_" + tn + "_ott_scope");
            ed->setViewForTest (0);
            ed->selectSlotForTest (0);

            for (int i = 0; i < p.presets.getNumFactory(); ++i)
                if (juce::String (factoryPresets()[(size_t) i].name) == "Ophanim") p.presets.loadFactory (i);
            run (p, music, ed);
            shoot ("ui_" + tn + "_parallel");
            const float rate0 = p.apvts.getRawParameterValue (slotParam (0, ParamId::rate))->load();
            ed->rollDiceForTest();
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);   // клик кнопки — асинхронный, кнопка отжимается не сразу
            CHECK (p.apvts.getRawParameterValue (slotParam (0, ParamId::rate))->load() != rate0, "dice button did nothing");
            p.presets.loadFactory (0);

            // 8 мест: пока эффектов меньше четырёх — видно 4
            CHECK (ed->getRackForTest().getShownCount() == kBaseSlots, "rack shows 8 with one effect");
            p.setSlotType (1, EffectType::OttStack);
            p.setSlotType (2, EffectType::Ophan);
            p.setSlotType (3, EffectType::OttStack);
            run (p, music, ed);
            CHECK (ed->getRackForTest().getShownCount() == kSlots, "rack does not show 8 with four effects");
            shoot ("ui_" + tn + "_rack8");
            ed->getRackForTest().scrollToEndForTest();
            shoot ("ui_" + tn + "_rack8_end");
            p.presets.loadFactory (0);
            ed->pumpForTest();
            CHECK (ed->getRackForTest().getShownCount() == kBaseSlots, "rack does not go back to 4");

            ed->showCreditsForTest (true);
            shoot ("credits_" + tn);
            ed->getCreditsForTest().keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (! ed->getCreditsForTest().isVisible(), "escape closes Credits");

            ed->showAboutForTest (true, false);
            shoot ("about_" + tn);
            {
                auto& fog = ed->getAboutForTest().getFogForTest();
                std::cout << "  fog particles: " << fog.getNumParticles() << "\n";
                CHECK (fog.getNumParticles() > 100, "fog: too few particles");
                const auto c = fog.getLocalBounds().getCentre().toFloat();
                fog.blowForTest ({ c.x - 160.0f, c.y }, { c.x + 40.0f, c.y + 10.0f }, 12);
                CHECK (fog.isAnimating(), "fog: did not start");
                fog.stepForTest (90);
                CHECK (! fog.isAnimating(), "fog: did not settle");
            }
            ed->showAboutForTest (true, true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
            shoot ("game_" + tn);
            auto& about = ed->getAboutForTest();
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (about.isVisible(), "escape in game closes only the game");
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (! about.isVisible(), "escape closes About");
        }
        ed->setThemeForTest (ThemeId::Oxygen);
        base.reset();
    }

    std::cout << (failures == 0 ? "ALL OK\n" : juce::String (failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}
