#include "Psalter.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace chroma
{
namespace
{
constexpr double kPi = 3.141592653589793;

struct Rand
{
    uint32_t s;
    explicit Rand (uint32_t seed) : s (seed * 2654435761u + 0x9e3779b9u) { if (s == 0) s = 1; }
    double next()
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (double) (s & 0xffffff) / (double) 0xffffff;
    }
};

// партиал: частота относительно ноты, громкость, время затухания относительно Decay
struct Partial { double ratio, amp, t60; };

// сумма затухающих синусов — поворотом (без sin на каждый сэмпл). Каналы — свои фазы и подстройка ±cents.
void addPartials (juce::AudioBuffer<float>& out, const std::vector<Partial>& ps, double hz, double decay, double sr, Rand& rnd, double cents)
{
    const int n = out.getNumSamples();
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        float* y = out.getWritePointer (ch);
        for (const auto& p : ps)
        {
            const double det = std::pow (2.0, (rnd.next() - 0.5) * 2.0 * cents / 1200.0);
            const double phase = rnd.next() * 2.0 * kPi;
            const double f = hz * p.ratio * det;
            if (f >= 0.45 * sr || f < 15.0) continue;
            const double w = 2.0 * kPi * f / sr;
            const double cr = std::cos (w), sr1 = std::sin (w);
            const double r = std::exp (-6.907755 / (juce::jmax (0.02, p.t60 * decay) * sr));   // −60 дБ за t60
            double c = std::cos (phase), s = std::sin (phase), a = p.amp;
            for (int i = 0; i < n && a > 1e-5; ++i)
            {
                y[i] += (float) (a * s);
                const double c2 = c * cr - s * sr1;
                s = s * cr + c * sr1;
                c = c2;
                a *= r;
            }
        }
    }
}

// FM-щипок (как FM Axe: 2 оператора 1:1, индекс быстро спадает, плюс короткий «звон» 14:1)
void fmNote (juce::AudioBuffer<float>& out, double hz, double decay, double sr, Rand& rnd)
{
    const int n = out.getNumSamples();
    // индекс ограничен, чтобы боковые полосы не уходили за Найквист
    const double iMax = juce::jmax (0.0, 0.45 * sr / hz - 2.0);
    const double iPeak = juce::jmin (2.8, iMax), iSus = juce::jmin (0.7, iMax);
    const double tauI = 0.12 + 0.06 * decay;
    const double tine = 14.0 * hz < 0.45 * sr ? 0.12 : 0.0;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        float* y = out.getWritePointer (ch);
        const double det = std::pow (2.0, (rnd.next() - 0.5) * 2.0 * 2.0 / 1200.0);   // ±2 цента между каналами
        double pc = rnd.next(), pm = rnd.next(), pt = rnd.next();
        const double dc = hz * det / sr, dm = hz * det * (1.0 + 0.0007 * (rnd.next() - 0.5)) / sr, dt = 14.0 * hz * det / sr;
        const double envR = std::exp (-6.907755 / (decay * sr)), indexR = std::exp (-1.0 / (tauI * sr)), tineR = std::exp (-1.0 / (0.025 * sr));
        double env = 1.0, idx = iPeak - iSus, tineA = tine;
        for (int i = 0; i < n; ++i)
        {
            const double v = std::sin (2.0 * kPi * pc + (iSus + idx) * std::sin (2.0 * kPi * pm)) + tineA * std::sin (2.0 * kPi * pt);
            y[i] += (float) (env * v);
            pc += dc; pm += dm; pt += dt;
            pc -= std::floor (pc); pm -= std::floor (pm); pt -= std::floor (pt);
            env *= envR; idx *= indexR; tineA *= tineR;
        }
    }
}
} // namespace

double PsalterRecipe::noteHz (int midi) { return 440.0 * std::pow (2.0, (midi - 69) / 12.0); }

juce::String PsalterRecipe::noteName (int midi)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int m = juce::jlimit (0, 127, midi);
    return juce::String (names[m % 12]) + juce::String (m / 12 - 1);
}

int PsalterIrSpec::stepOf (double decay)
{
    return (int) std::lround (std::log (juce::jlimit (PsalterRecipe::kMinDecay, PsalterRecipe::kMaxDecay, decay) / PsalterRecipe::kMinDecay) / std::log (1.02));
}

double PsalterIrSpec::decayOf (int step)
{
    return juce::jlimit (PsalterRecipe::kMinDecay, PsalterRecipe::kMaxDecay, PsalterRecipe::kMinDecay * std::pow (1.02, step));
}

juce::AudioBuffer<float> psalterSynth (int timbre, double hz, double decay, double sr, uint32_t seed)
{
    using namespace PsalterRecipe;
    decay = juce::jlimit (kMinDecay, kMaxDecay, decay);
    const int n = juce::jmax (64, (int) ((decay + 0.05) * sr));
    juce::AudioBuffer<float> ir (2, n);
    ir.clear();
    Rand rnd (seed);
    double attackMs = 1.5;
    switch (timbre)
    {
        case String:
        {
            // щипок струны (псалтерий): гармоники с вырезом от места щипка, верхние гаснут быстрее, лёгкая негармоничность
            std::vector<Partial> ps;
            for (int k = 1; k <= 40; ++k)
            {
                const double amp = (0.08 + std::abs (std::sin (kPi * k * 0.13))) / std::pow ((double) k, 0.9);
                ps.push_back ({ k * std::sqrt (1.0 + 0.00008 * k * k), amp, std::pow ((double) k, -0.55) });
            }
            addPartials (ir, ps, hz, decay, sr, rnd, 1.5);
            attackMs = 2.0;
            break;
        }
        case Bell:
        {
            // колокол: хум, прайм, терция, квинта, номинал (= нота), дециме… — негармоничный звон
            const std::vector<Partial> ps {
                { 0.25, 0.5, 1.8 }, { 0.5, 0.7, 1.3 }, { 0.6, 0.6, 1.0 }, { 0.75, 0.35, 0.7 }, { 1.0, 1.0, 0.9 }, { 1.25, 0.45, 0.5 },
                { 1.335, 0.3, 0.45 }, { 1.5, 0.3, 0.4 }, { 2.0, 0.25, 0.3 }, { 2.66, 0.15, 0.22 }, { 3.4, 0.1, 0.16 } };
            addPartials (ir, ps, hz, decay, sr, rnd, 1.0);
            attackMs = 1.0;
            break;
        }
        case Glass:
        {
            // стекло: почти чистый тон и пара тихих гармоник — самая избирательная нота
            const std::vector<Partial> ps { { 1.0, 1.0, 1.0 }, { 2.0, 0.22, 0.6 }, { 3.0, 0.07, 0.4 } };
            addPartials (ir, ps, hz, decay, sr, rnd, 1.0);
            attackMs = 5.0;
            break;
        }
        case Fm:
        default:
            fmNote (ir, hz, decay, sr, rnd);
            attackMs = 1.0;
            break;
    }
    // атака без щелчка и мягкий край в конце
    const int att = juce::jmax (1, (int) (attackMs * 0.001 * sr)), fade = juce::jmax (1, n / 20);
    for (int ch = 0; ch < 2; ++ch)
    {
        float* y = ir.getWritePointer (ch);
        for (int i = 0; i < att && i < n; ++i) y[i] *= (float) (0.5 - 0.5 * std::cos (kPi * i / att));
        for (int i = 0; i < fade; ++i) y[n - 1 - i] *= (float) (0.5 - 0.5 * std::cos (kPi * i / fade));
    }
    return ir;
}

juce::AudioBuffer<float> psalterShapeSample (const juce::AudioBuffer<float>& src, double srcRate, double decay)
{
    using namespace PsalterRecipe;
    decay = juce::jlimit (kMinDecay, kMaxDecay, decay);
    const int n = juce::jlimit (1, juce::jmax (1, src.getNumSamples()), (int) (decay * srcRate));
    juce::AudioBuffer<float> ir (2, n);
    const int half = (int) (0.5 * decay * srcRate);
    const double r = std::exp (-6.907755 / juce::jmax (1.0, 0.5 * decay * srcRate));
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* x = src.getReadPointer (juce::jmin (ch, src.getNumChannels() - 1));
        float* y = ir.getWritePointer (ch);
        double env = 1.0;
        for (int i = 0; i < n; ++i)
        {
            if (i >= half) env *= r;
            y[i] = (float) (x[i] * env);
        }
        const int fade = juce::jmin (n, juce::jmax (1, (int) (0.005 * srcRate)));
        for (int i = 0; i < fade; ++i) y[n - 1 - i] *= (float) i / (float) fade;
    }
    return ir;
}

void psalterNormalise (juce::AudioBuffer<float>& ir)
{
    const int n = ir.getNumSamples();
    int order = 1;
    while ((1 << order) < 2 * n && order < 21) ++order;
    const int N = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> w ((size_t) N * 2);
    float peak = 0.0f;
    for (int ch = 0; ch < ir.getNumChannels(); ++ch)
    {
        std::fill (w.begin(), w.end(), 0.0f);
        std::copy (ir.getReadPointer (ch), ir.getReadPointer (ch) + juce::jmin (n, N), w.begin());
        fft.performFrequencyOnlyForwardTransform (w.data(), true);
        for (int k = 0; k <= N / 2; ++k) peak = juce::jmax (peak, w[(size_t) k]);
    }
    if (peak > 1e-9f) ir.applyGain (1.0f / peak);
}

// ===========================================================================
float Psalter::nextRand()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) (rng & 0xffffff) / (float) 0xffffff;
}

void Psalter::init (juce::dsp::ConvolutionMessageQueue& queue)
{
    for (auto& c : conv)
        if (c == nullptr) c = std::make_unique<juce::dsp::Convolution> (juce::dsp::Convolution::NonUniform { PsalterRecipe::kHeadSize }, queue);
}

void Psalter::loadNow (int k, juce::AudioBuffer<float>&& ir, double rate)
{
    if (conv[(size_t) k] != nullptr)
        conv[(size_t) k]->loadImpulseResponse (std::move (ir), rate, juce::dsp::Convolution::Stereo::yes,
                                               juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
}

bool Psalter::offer (int k, juce::AudioBuffer<float>&& ir, double rate)
{
    auto& b = box[(size_t) k];
    if (b.ready.load (std::memory_order_acquire)) return false;
    b.buf = std::move (ir);
    b.rate = rate;
    b.ready.store (true, std::memory_order_release);
    return true;
}

void Psalter::prepare (double sampleRate, int block, uint32_t s)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (64, block);
    seed = s;
    dry.setSize (2, maxBlock);
    for (auto& w : wet) w.setSize (2, maxBlock);
    for (auto& b : box) { b.ready.store (false); b.buf.setSize (0, 0); }
    for (auto& c : conv)
        if (c != nullptr) c->prepare ({ sr, (juce::uint32) maxBlock, 2 });
    reset();
}

void Psalter::reset()
{
    rng = seed * 2654435761u + 777u;
    for (auto& c : conv) if (c != nullptr) c->reset();
    for (int k = 0; k < PsalterRecipe::kNotes; ++k)
    {
        auto& l = lfo[(size_t) k];
        l.rateMul = 1.0 + PsalterRecipe::kRateJitter * (2.0 * nextRand() - 1.0);
        l.from = nextRand();
        l.to = nextRand();
        l.pos = nextRand();
        l.len = 0.7 + 0.6 * nextRand();
        l.value = l.from;
        gainDb[(size_t) k] = PsalterRecipe::kAgcStartDb;
        wetEnv[(size_t) k] = 0.0;
    }
    dryEnv = dryHold = 0.0;
    lowL.clear(); lowR.clear(); highL.clear(); highR.clear();
    toneSet = -1.0;
    first = true;
}

void Psalter::control (const PsalterParams& p, int len, int offset)
{
    using namespace PsalterRecipe;
    const double dt = (double) len / sr;
    if (first)
    {
        depthNow = p.depth; widthNow = p.width; mixNow = p.mix; toneNow = p.tone;
    }
    // ручки — плавно (≈ 30 мс), Mix доходит до цели ровно (Mix 0 — сухой бит-в-бит)
    const double a = 1.0 - std::exp (-dt / 0.03);
    depthNow += (p.depth - depthNow) * a;
    widthNow += (p.width - widthNow) * a;
    mixNow += (p.mix - mixNow) * a;
    if (std::abs (p.mix - mixNow) < 1e-4) mixNow = p.mix;
    toneNow += (p.tone - toneNow) * a;
    if (std::abs (p.tone - toneNow) < 1e-4) toneNow = p.tone;

    // громкость входа и нот (до LFO): подстройка каждой ноты под вход
    double dryPow = 0.0;
    std::array<double, kNotes> wetPow {};
    {
        const float* dl = dry.getReadPointer (0, offset);
        const float* dr = dry.getReadPointer (1, offset);
        for (int i = 0; i < len; ++i) dryPow += 0.5 * ((double) dl[i] * dl[i] + (double) dr[i] * dr[i]);
        for (int k = 0; k < kNotes; ++k)
        {
            const float* yl = wet[(size_t) k].getReadPointer (0, offset);
            const float* yr = wet[(size_t) k].getReadPointer (1, offset);
            double s = 0.0;
            for (int i = 0; i < len; ++i) s += 0.5 * ((double) yl[i] * yl[i] + (double) yr[i] * yr[i]);
            wetPow[(size_t) k] = s / len;
        }
        dryPow /= len;
    }
    const double ae = 1.0 - std::exp (-dt / kEnvSec);
    dryEnv += (dryPow - dryEnv) * ae;
    dryHold = juce::jmax (dryEnv, dryHold * std::exp (-dt / kHoldSec));
    // подстройка — только пока вход звучит ровно; на спаде (хвост ноты) замирает, иначе подняла бы хвост
    const bool adapt = dryEnv > 1e-9 && dryEnv >= 0.25 * dryHold;
    const double up = 1.0 - std::exp (-dt / kAgcUpSec), down = 1.0 - std::exp (-dt / kAgcDownSec);

    for (int k = 0; k < kNotes; ++k)
    {
        wetEnv[(size_t) k] += (wetPow[(size_t) k] - wetEnv[(size_t) k]) * ae;
        if (adapt && wetEnv[(size_t) k] > 1e-14)
        {
            const double target = juce::jlimit (0.0, kAgcMaxDb, 10.0 * std::log10 (dryEnv / wetEnv[(size_t) k]));
            gainDb[(size_t) k] += (target - gainDb[(size_t) k]) * (target > gainDb[(size_t) k] ? up : down);
        }
        // LFO ноты: плавно к следующему случайному уровню
        auto& l = lfo[(size_t) k];
        l.pos += dt * p.rate * l.rateMul / l.len;
        while (l.pos >= 1.0)
        {
            l.pos -= 1.0;
            l.from = l.to;
            l.to = nextRand();
            l.len = 1.0 + kRateJitter * (2.0 * nextRand() - 1.0);
        }
        l.value = l.from + (l.to - l.from) * (0.5 - 0.5 * std::cos (kPi * l.pos));

        w0[(size_t) k] = first ? (float) (1.0 - depthNow * l.value) : w1[(size_t) k];
        w1[(size_t) k] = (float) (1.0 - depthNow * l.value);
        const float g = (float) std::pow (10.0, gainDb[(size_t) k] / 20.0);
        g0[(size_t) k] = first ? g : g1[(size_t) k];
        g1[(size_t) k] = g;
    }
    mix0 = first ? (float) mixNow : mix1;
    mix1 = (float) mixNow;
    width0 = first ? (float) widthNow : width1;
    width1 = (float) widthNow;

    // Tone: наклон полками; коэффициенты интерполируются по шагу пересчёта
    if (toneNow != toneSet)
    {
        const double t = 2.0 * toneNow - 1.0;
        const auto lc = SvfCoefs::lowShelf (kToneLowHz, 0.71, -kToneLowDb * t, sr);
        const auto hc = SvfCoefs::highShelf (kToneHighHz, 0.71, kToneHighDb * t, sr);
        lowC0 = toneSet < 0.0 ? lc : lowC1;
        highC0 = toneSet < 0.0 ? hc : highC1;
        lowC1 = lc;
        highC1 = hc;
        toneMoving = toneSet >= 0.0;
        toneSet = toneNow;
        if (! toneMoving) { lowC1.applyTo (lowL); lowC1.applyTo (lowR); highC1.applyTo (highL); highC1.applyTo (highR); }
    }
    else if (toneMoving)
    {
        toneMoving = false;
        lowC1.applyTo (lowL); lowC1.applyTo (lowR); highC1.applyTo (highL); highC1.applyTo (highR);
    }
    first = false;
}

void Psalter::process (float* L, float* R, int n, const PsalterParams& p, PsalterView* view)
{
    using namespace PsalterRecipe;
    // новые ноты из фонового потока: свёртка сама плавно (≈ 50 мс) переходит на новый IR
    for (int k = 0; k < kNotes; ++k)
    {
        auto& b = box[(size_t) k];
        if (b.ready.load (std::memory_order_acquire))
        {
            if (conv[(size_t) k] != nullptr)
                conv[(size_t) k]->loadImpulseResponse (std::move (b.buf), b.rate, juce::dsp::Convolution::Stereo::yes,
                                                       juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            b.ready.store (false, std::memory_order_release);
        }
    }
    if (conv[0] == nullptr) return;

    const bool stereo = R != nullptr;
    const float invN = 1.0f / std::sqrt ((float) kNotes);
    // панорама нот: 1 — слева, 2 — по центру, 3 — справа (по Width); равная мощность, центр = 1
    for (int pos = 0; pos < n; pos += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - pos);
        float* l = L + pos;
        float* r = stereo ? R + pos : nullptr;
        dry.copyFrom (0, 0, l, len);
        dry.copyFrom (1, 0, stereo ? r : l, len);
        {
            const float* chans[2] = { dry.getReadPointer (0), dry.getReadPointer (1) };
            juce::dsp::AudioBlock<const float> in (chans, 2, (size_t) len);
            for (int k = 0; k < kNotes; ++k)
            {
                juce::dsp::AudioBlock<float> out (wet[(size_t) k].getArrayOfWritePointers(), 2, (size_t) len);
                conv[(size_t) k]->process (juce::dsp::ProcessContextNonReplacing<float> (in, out));
            }
        }

        for (int c = 0; c < len; c += kControl)
        {
            const int cn = juce::jmin (kControl, len - c);
            control (p, cn, c);
            const bool silentMix = mix0 == 0.0f && mix1 == 0.0f;
            const float inv = 1.0f / (float) cn;
            for (int i = 0; i < cn; ++i)
            {
                const int j = c + i;
                const float t = (float) (i + 1) * inv;
                if (toneMoving)
                {
                    auto lerp = [t] (const SvfCoefs& x, const SvfCoefs& y, Svf& f)
                    {
                        f.a1 = x.a1 + (y.a1 - x.a1) * t; f.a2 = x.a2 + (y.a2 - x.a2) * t; f.a3 = x.a3 + (y.a3 - x.a3) * t;
                        f.m0 = x.m0 + (y.m0 - x.m0) * t; f.m1 = x.m1 + (y.m1 - x.m1) * t; f.m2 = x.m2 + (y.m2 - x.m2) * t;
                    };
                    lerp (lowC0, lowC1, lowL); lerp (lowC0, lowC1, lowR); lerp (highC0, highC1, highL); lerp (highC0, highC1, highR);
                }
                const float width = width0 + (width1 - width0) * t;
                float wl = 0.0f, wr = 0.0f, dryAmt = 0.0f;
                for (int k = 0; k < kNotes; ++k)
                {
                    const float w = w0[(size_t) k] + (w1[(size_t) k] - w0[(size_t) k]) * t;
                    const float g = g0[(size_t) k] + (g1[(size_t) k] - g0[(size_t) k]) * t;
                    const float yl = wet[(size_t) k].getSample (0, j) * g, yr = wet[(size_t) k].getSample (1, j) * g;
                    const float mid = 0.5f * (yl + yr), side = 0.5f * (yl - yr) * width;
                    const float pan = (float) (k - 1) * (float) kPan * width;   // −0.6…+0.6
                    const float ang = (pan + 1.0f) * 0.25f * (float) kPi;
                    const float gl = std::cos (ang) * 1.41421356f, gr = std::sin (ang) * 1.41421356f;
                    wl += w * (mid + side) * gl;
                    wr += w * (mid - side) * gr;
                    dryAmt += 1.0f - w;
                }
                // громкость ровная, пока LFO гоняет ноты: мощность нот (некоррелированы) + сухой (когерентен) → к 1
                float wsq = 0.0f;
                for (int k = 0; k < kNotes; ++k)
                {
                    const float w = w0[(size_t) k] + (w1[(size_t) k] - w0[(size_t) k]) * t;
                    wsq += w * w;
                }
                dryAmt /= (float) kNotes;
                const float makeup = 1.0f / juce::jmax (1.0f / (float) kMaxMakeup, std::sqrt (wsq / (float) kNotes + dryAmt * dryAmt));
                wl = highL.tick (lowL.tick (wl * invN)) * makeup;
                wr = highR.tick (lowR.tick (wr * invN)) * makeup;
                dryAmt *= makeup;
                if (silentMix) continue;
                const float m = mix0 + (mix1 - mix0) * t;
                const float dl = dry.getSample (0, j), dr = dry.getSample (1, j);
                const float el = dryAmt * dl + wl, er = dryAmt * dr + wr;
                if (stereo)
                {
                    l[j] = dl + m * (el - dl);
                    r[j] = dr + m * (er - dr);
                }
                else l[j] = dl + m * (0.5f * (el + er) - dl);
            }
        }
    }

    if (view != nullptr)
    {
        for (int k = 0; k < kNotes; ++k)
        {
            view->weight[(size_t) k].store (w1[(size_t) k], std::memory_order_relaxed);
            view->gainDb[(size_t) k].store ((float) gainDb[(size_t) k], std::memory_order_relaxed);
        }
        view->live.store (true, std::memory_order_relaxed);
    }
}
} // namespace chroma
