#include "Legion.h"
#include <algorithm>
#include <cmath>

namespace chroma
{
namespace
{
constexpr double kPi = 3.141592653589793;
template <typename T> T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

int pow2AtLeast (double n)
{
    int s = 1;
    while ((double) s < n) s <<= 1;
    return s;
}

// последовательность ван дер Корпута: голоса раскладываются ровно — первые два по краям центра, дальше между ними
double vdc (int k, int base)
{
    double v = 0.0, f = 1.0 / base;
    while (k > 0)
    {
        v += f * (k % base);
        k /= base;
        f /= base;
    }
    return v;
}

// длины линий FDN, мс (при размере 1)
constexpr double kLineMs[8] = { 31.3, 37.9, 43.1, 47.9, 53.3, 59.7, 67.1, 73.9 };
constexpr double kMaxSize = 1.8;
// эхо от трибун: две задержки, мс
constexpr double kEchoMs[2] = { 230.0, 370.0 };
// громкость пространства относительно голосов (подобрана тестом: Choir, Space 100% — хвост примерно вровень с голосами)
constexpr float kRevGain = 0.55f;
}

float Legion::nextRand()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) (rng & 0xffffff) / (float) 0xffffff;
}

void Legion::prepare (double sampleRate, uint32_t seed)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    rng = seed * 2654435761u + 12345u;

    histSize = pow2AtLeast (0.75 * sr);
    hist.assign ((size_t) histSize, 0.0f);
    histMask = histSize - 1;

    // детектор тона: вход прорежен до ~12 кГц, окно 25 мс, тон 70 Гц … 1 кГц
    decim = std::max (1, (int) std::lround (sr / 12000.0));
    const double fsd = sr / decim;
    yinW = (int) std::lround (0.025 * fsd);
    tauMin = std::max (2, (int) std::floor (fsd / LegionRecipe::kMaxHz));
    tauMax = (int) std::ceil (fsd / LegionRecipe::kMinHz);
    decHop = std::max (1, (int) std::lround (0.004 * fsd));
    const int ring = pow2AtLeast (yinW + tauMax + 8);
    decRing.assign ((size_t) ring, 0.0f);
    decMask = ring - 1;
    yinBuf.assign ((size_t) (yinW + tauMax + 1), 0.0f);
    yinDiff.assign ((size_t) (tauMax + 2), 0.0f);
    SvfCoefs::lowpass (std::min (2000.0, 0.3 * fsd), 0.71, sr).applyTo (decLp1);
    SvfCoefs::lowpass (std::min (2000.0, 0.3 * fsd), 0.71, sr).applyTo (decLp2);

    for (int i = 0; i <= 1024; ++i) hann[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / 1024.0));

    // постоянные голосов: голос 1 — чистая копия по центру высоты, без опоздания и вблизи
    for (int k = 0; k < LegionRecipe::kMaxVoices; ++k)
    {
        auto& v = voices[(size_t) k];
        const float j = (nextRand() - 0.5f) * 0.03f;
        v.uPitch = k == 0 ? 0.0f : clampv ((float) (2.0 * vdc (k + 1, 2) - 1.0) + j, -1.0f, 1.0f);
        v.pan = (float) (2.0 * vdc (k + 1, 3) - 1.0);
        v.uDelay = (float) vdc (k, 5);
        v.dist = k == 0 ? 0.0f : std::pow (nextRand(), 0.8f);
        v.uFormant = k == 0 ? 0.0f : nextRand() * 2.0f - 1.0f;
        v.vibRate = 0.85f + 0.3f * nextRand();
        v.vibDepth = 0.6f + 0.8f * nextRand();
        v.drift.reset (seed * 977u + (uint32_t) k * 31u + 1u, nextRand());
        v.wobble.reset (seed * 131u + (uint32_t) k * 7u + 5u, nextRand());
        v.vibPhase = nextRand();
    }

    // пространство
    preSize = pow2AtLeast (0.7 * sr);
    preL.assign ((size_t) preSize, 0.0f);
    preR.assign ((size_t) preSize, 0.0f);
    preMask = preSize - 1;
    difL[0].setup ((int) (0.00477 * sr)); difL[1].setup ((int) (0.00153 * sr));
    difR[0].setup ((int) (0.00531 * sr)); difR[1].setup ((int) (0.00187 * sr));
    for (int i = 0; i < kLines; ++i)
    {
        const int size = pow2AtLeast (kLineMs[i] * 0.001 * kMaxSize * sr + 8);
        line[(size_t) i].assign ((size_t) size, 0.0f);
        lineMask[(size_t) i] = size - 1;
    }
    reset();
}

void Legion::reset()
{
    std::fill (hist.begin(), hist.end(), 0.0f);
    written = 0;
    preHp.clear(); prePresence.clear(); decLp1.clear(); decLp2.clear();
    std::fill (decRing.begin(), decRing.end(), 0.0f);
    decPos = decCount = decSinceDetect = 0;
    period = sr / 200.0;
    voiced = false;
    voicedness = 0.0;
    for (auto& v : voices)
    {
        v.on = false;
        v.nGrains = 0;
        v.hasMark = false;
        v.lp = 0.0f;
        v.peak = 0.0f;
        v.shownDb = -100.0f;
        v.gain = v.gainPrev = v.send = v.sendPrev = 0.0f;
    }
    std::fill (preL.begin(), preL.end(), 0.0f);
    std::fill (preR.begin(), preR.end(), 0.0f);
    prePos = 0;
    for (auto& a : difL) a.clear();
    for (auto& a : difR) a.clear();
    for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
    lineLp.fill (0.0f);
    linePos = 0;
    echoLpL = echoLpR = 0.0f;
    first = true;
}

// ---------------------------------------------------------------------------
// тон входа: YIN по прорежённому входу
void Legion::detect()
{
    const int n = yinW + tauMax;
    double energy = 0.0;
    for (int j = 0; j < n; ++j)
    {
        const float x = decRing[(size_t) ((decPos - n + j) & decMask)];
        yinBuf[(size_t) j] = x;
        if (j >= tauMax) energy += (double) x * x;
    }
    if (energy / yinW < 1e-7)   // тише −70 dBFS — тона нет
    {
        voiced = false;
        return;
    }
    const float* x = yinBuf.data();
    double running = 0.0;
    yinDiff[0] = 1.0f;
    for (int tau = 1; tau <= tauMax; ++tau)
    {
        float s = 0.0f;
        const float* y = x + tau;
        for (int j = 0; j < yinW; ++j)
        {
            const float d = x[j] - y[j];
            s += d * d;
        }
        running += s;
        yinDiff[(size_t) tau] = running > 0.0 ? (float) (s * tau / running) : 1.0f;
    }
    int best = -1;
    for (int tau = tauMin; tau <= tauMax; ++tau)
        if (yinDiff[(size_t) tau] < (float) LegionRecipe::kVoicedThreshold)
        {
            while (tau + 1 <= tauMax && yinDiff[(size_t) tau + 1] < yinDiff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        voiced = false;
        return;
    }
    double shift = 0.0;
    if (best > 1 && best < tauMax)
    {
        const double a = yinDiff[(size_t) best - 1], b = yinDiff[(size_t) best], c = yinDiff[(size_t) best + 1];
        const double den = a - 2.0 * b + c;
        if (den > 1e-9) shift = clampv (0.5 * (a - c) / den, -0.5, 0.5);
    }
    period = clampv ((best + shift) * decim, sr / LegionRecipe::kMaxHz, sr / LegionRecipe::kMinHz);
    voiced = true;
}

// ---------------------------------------------------------------------------
void Legion::control (const LegionParams& p, int len)
{
    using namespace LegionRecipe;
    const double dt = (double) len / sr;
    const double a = first ? 1.0 : 1.0 - std::exp (-dt / 0.04);
    const double aRec = first ? 1.0 : 1.0 - std::exp (-dt / 0.15);
    countNow += (clampv ((double) p.voices, 1.0, (double) kMaxVoices) - countNow) * a;
    if (std::abs (countNow - p.voices) < 1e-3) countNow = clampv ((double) p.voices, 1.0, (double) kMaxVoices);
    mixNow += (p.mix - mixNow) * a;
    if (std::abs (mixNow - p.mix) < 1e-3) mixNow = p.mix;
    pitchNow += (clampv ((double) p.pitch, -24.0, 24.0) - pitchNow) * a;
    spreadNow += (clampv ((double) p.spread, 0.0, 24.0) - spreadNow) * a;
    looseNow += (clampv ((double) p.loose, 0.0, 2.0) - looseNow) * a;
    spaceNow += (clampv ((double) p.space, 0.0, 1.0) - spaceNow) * a;

    // характер — плавно, чтобы смена на ходу не щёлкала
    const auto& t = kRecipes[clampv (p.character, 0, (int) NumCharacters - 1)];
    auto glide = [aRec] (double& v, double to) { v += (to - v) * aRec; };
    glide (rec.scatterMs, t.scatterMs); glide (rec.wobbleMs, t.wobbleMs); glide (rec.driftCents, t.driftCents);
    glide (rec.driftHz, t.driftHz); glide (rec.vibCents, t.vibCents); glide (rec.vibHz, t.vibHz);
    glide (rec.formantVar, t.formantVar); glide (rec.distanceDb, t.distanceDb); glide (rec.nearHz, t.nearHz);
    glide (rec.farHz, t.farHz); glide (rec.width, t.width); glide (rec.drive, t.drive); glide (rec.presenceDb, t.presenceDb);
    glide (rec.lowCutHz, t.lowCutHz); glide (rec.size, t.size); glide (rec.rt60, t.rt60); glide (rec.dampHz, t.dampHz);
    glide (rec.predelayMs, t.predelayMs); glide (rec.echo, t.echo); glide (rec.wet, t.wet);

    SvfCoefs::highpass (rec.lowCutHz, 0.71, sr).applyTo (preHp);
    SvfCoefs::bell (2500.0, 0.9, rec.presenceDb, sr).applyTo (prePresence);
    voicedness += ((voiced ? 1.0 : 0.0) - voicedness) * (1.0 - std::exp (-dt / 0.012));

    // голоса: высота, тембр, опоздание, дальность, место
    double sumG2 = 0.0;
    for (int k = 0; k < kMaxVoices; ++k)
    {
        auto& v = voices[(size_t) k];
        const double w = clampv (countNow - (double) k, 0.0, 1.0);
        v.gainPrev = v.gain;
        v.sendPrev = v.send;
        if (w <= 0.0)
        {
            v.gain = v.send = 0.0f;
            if (v.on && v.gainPrev <= 0.0f && v.sendPrev <= 0.0f) { v.on = false; v.nGrains = 0; }
            continue;
        }
        const double drift = v.drift.advance (rec.driftHz, dt, Lfo::Stray);
        const double wob = v.wobble.advance (rec.driftHz * 0.6, dt, Lfo::Stray);
        v.vibPhase += rec.vibHz * v.vibRate * dt;
        v.vibPhase -= std::floor (v.vibPhase);
        const double base = pitchNow + spreadNow * v.uPitch;
        const double semis = base + (rec.driftCents * looseNow * drift + rec.vibCents * v.vibDepth * std::sin (2.0 * kPi * v.vibPhase)) / 100.0;
        v.semis = (float) semis;
        v.ratio = (float) clampv (std::pow (2.0, semis / 12.0), 0.25, 4.0);
        const double fsemis = kFormantFollow * base + rec.formantVar * v.uFormant;
        v.formant = (float) clampv (std::pow (2.0, fsemis / 12.0), 0.5, 2.0);

        const double delayMs = rec.scatterMs * looseNow * v.uDelay + rec.wobbleMs * looseNow * 0.5 * (1.0 + wob);
        const double target = std::min (delayMs * 0.001 * sr, 0.4 * histSize);
        if (! v.on)
        {
            v.on = true;
            v.nGrains = 0;
            v.hasMark = false;
            v.lp = 0.0f;
            v.delayNow = target;
            v.untilNext = nextRand() * period;   // голоса не стартуют зёрнами в один сэмпл
        }
        else v.delayNow += (target - v.delayNow) * (1.0 - std::exp (-dt / 0.1));

        const double distGain = std::pow (10.0, -rec.distanceDb * v.dist / 20.0);
        sumG2 += w * distGain * distGain;
        v.gain = (float) (w * distGain);   // нормировка — ниже
        v.send = (float) w;
        const double fc = rec.nearHz * std::pow (rec.farHz / rec.nearHz, (double) v.dist);
        v.lpCoef = (float) (1.0 - std::exp (-2.0 * kPi * std::min (fc, 0.45 * sr) / sr));
        const double th = ((double) v.pan * rec.width + 1.0) * kPi * 0.25;
        v.gl = (float) (std::cos (th) * std::sqrt (2.0));   // голос по центру — как сухой сигнал в каждом канале
        v.gr = (float) (std::sin (th) * std::sqrt (2.0));
    }
    // громкость не растёт с числом голосов: они не совпадают по фазе — складываем по мощности
    const double norm = 1.0 / std::sqrt (std::max (1e-6, sumG2));
    normNow = first ? norm : normNow + (norm - normNow) * a;
    const double sendNorm = 1.0 / std::sqrt (std::max (1.0, countNow));
    for (auto& v : voices)
        if (v.gain > 0.0f) { v.gain *= (float) normNow; v.send *= (float) sendNorm; }
    if (first) for (auto& v : voices) { v.gainPrev = v.gain; v.sendPrev = v.send; }

    // пространство
    for (int i = 0; i < kLines; ++i)
    {
        lineLen[(size_t) i] = (float) std::min (kLineMs[i] * 0.001 * clampv (rec.size, 0.1, kMaxSize) * sr, (double) lineMask[(size_t) i] - 4.0);
        lineGain[(size_t) i] = (float) std::pow (10.0, -3.0 * lineLen[(size_t) i] / (std::max (0.1, rec.rt60) * sr));
    }
    first = false;
}

// новое зерно голоса: два периода входа вокруг метки, шаг до следующего — новый период
void Legion::spawn (Voice& v, int offset, int64_t t0)
{
    const double now = (double) (t0 + offset);
    const bool vc = voicedness > 0.5;
    const double P = vc ? period : sr * (0.0035 + 0.0025 * nextRand());   // без тона — обычное наложение с неровным шагом
    const double r = vc ? (double) v.ratio : 1.0;
    const double phi = v.formant;
    const double target = now - (1.5 * P + 0.001 * sr) - v.delayNow;
    // метки идут с шагом периода: соседние зёрна совпадают по фазе
    double a = vc && v.hasMark ? v.mark + P * std::round ((target - v.mark) / P) : target;
    while (a + P > now) a -= P;
    const double oldest = (double) written - (double) histSize + 4.0;
    if (a - P < oldest) a = oldest + P;
    v.mark = a;
    v.hasMark = true;

    Grain g;
    g.src = a - P;
    g.speed = (float) phi;
    const double lg = 2.0 * P / phi;          // длина зерна на выходе
    g.left = std::max (2, (int) std::lround (lg));
    const double dw = 2.0 * kPi / g.left;
    g.rc = (float) std::cos (dw);
    g.rs = (float) std::sin (dw);
    g.wc = 1.0f;
    g.ws = 0.0f;
    g.start = offset;
    // громкость не зависит от сдвига: окна при шаге P/r складываются в r/φ; вверх зёрна не совпадают по фазе — корень
    g.gain = (float) (phi / (r >= 1.0 ? std::sqrt (r) : r));
    if (v.nGrains < LegionRecipe::kMaxGrains) v.grains[(size_t) v.nGrains++] = g;
    else
    {
        int oldestIdx = 0;
        for (int i = 1; i < v.nGrains; ++i) if (v.grains[(size_t) i].left < v.grains[(size_t) oldestIdx].left) oldestIdx = i;
        v.grains[(size_t) oldestIdx] = g;
    }
    v.untilNext += P / r;
}

void Legion::renderVoice (Voice& v, int len)
{
    std::fill (vbuf.begin(), vbuf.begin() + len, 0.0f);
    const float* h = hist.data();
    const int64_t mask = histMask;
    for (int gi = 0; gi < v.nGrains; ++gi)
    {
        Grain& g = v.grains[(size_t) gi];
        float wc = g.wc, ws = g.ws;
        const float rc = g.rc, rs = g.rs, half = 0.5f * g.gain;
        // позиция чтения: целая часть + дробь (зерно короче истории — точности float хватает)
        int64_t ip = (int64_t) std::floor (g.src);
        float fp = (float) (g.src - (double) ip);
        const float sp = g.speed;
        const int end = std::min (len, g.start + g.left);
        for (int j = g.start; j < end; ++j)
        {
            const float a = h[(size_t) (ip & mask)], b = h[(size_t) ((ip + 1) & mask)];
            vbuf[(size_t) j] += (half - half * wc) * (a + fp * (b - a));
            const float nc = wc * rc - ws * rs;
            ws = ws * rc + wc * rs;
            wc = nc;
            fp += sp;
            const int step = (int) fp;
            ip += step;
            fp -= (float) step;
        }
        g.left -= end - g.start;
        g.wc = wc;
        g.ws = ws;
        g.src = (double) ip + fp;
        g.start = 0;
    }
    for (int gi = 0; gi < v.nGrains;)
        if (v.grains[(size_t) gi].left <= 0) v.grains[(size_t) gi] = v.grains[(size_t) --v.nGrains];
        else ++gi;
}

// ---------------------------------------------------------------------------
void Legion::process (float* L, float* R, int n, const LegionParams& p, LegionView* view)
{
    const bool stereo = R != nullptr;
    int pos = 0;
    while (pos < n)
    {
        const int len = std::min (kControl, n - pos);
        const double mix0 = mixNow;
        control (p, len);
        const float dMix = (float) ((mixNow - mix0) / len);
        float* l = L + pos;
        float* r = stereo ? R + pos : nullptr;

        // вход: детектор тона и «горло» (срез низа, присутствие, крик) → история
        const float drive = (float) rec.drive, G = 1.0f + 5.0f * drive;
        const int64_t t0 = written;
        for (int i = 0; i < len; ++i)
        {
            const float x = stereo ? 0.5f * (l[i] + r[i]) : l[i];
            const float d = decLp2.tick (decLp1.tick (x));
            if (++decCount >= decim)
            {
                decCount = 0;
                decRing[(size_t) decPos] = d;
                decPos = (decPos + 1) & decMask;
                if (++decSinceDetect >= decHop) { decSinceDetect = 0; detect(); }
            }
            float y = prePresence.tick (preHp.tick (x));
            if (drive > 1e-3f) y += drive * (std::tanh (G * y) / G - y);
            hist[(size_t) ((t0 + i) & histMask)] = y;
        }
        written += len;

        // голоса
        std::fill (busL.begin(), busL.begin() + len, 0.0f);
        std::fill (busR.begin(), busR.begin() + len, 0.0f);
        std::fill (sendL.begin(), sendL.begin() + len, 0.0f);
        std::fill (sendR.begin(), sendR.begin() + len, 0.0f);
        const float inv = 1.0f / (float) len;
        for (auto& v : voices)
        {
            if (! v.on) continue;
            while (v.untilNext < (double) len) spawn (v, std::max (0, (int) v.untilNext), t0);
            v.untilNext -= len;
            renderVoice (v, len);
            float lp = v.lp, pk = 0.0f;
            const float c = v.lpCoef;
            const float dg = (v.gain - v.gainPrev) * inv, ds = (v.send - v.sendPrev) * inv;
            for (int i = 0; i < len; ++i)
            {
                lp += c * (vbuf[(size_t) i] - lp);
                const float g = v.gainPrev + dg * (float) i, s = v.sendPrev + ds * (float) i;
                busL[(size_t) i] += lp * g * v.gl;
                busR[(size_t) i] += lp * g * v.gr;
                sendL[(size_t) i] += lp * s * v.gl;
                sendR[(size_t) i] += lp * s * v.gr;
                pk = std::max (pk, std::abs (lp * g));
            }
            v.lp = lp;
            v.peak = std::max (pk, v.peak * 0.9f);
        }

        // пространство: предзадержка, эхо от трибун, диффузия, FDN
        const float space = (float) spaceNow;
        const float direct = 1.0f - 0.35f * space;
        const float wet = kRevGain * space * (float) rec.wet;
        const float echo = (float) rec.echo * space;
        const float pre = (float) (rec.predelayMs * 0.001 * sr);
        const float e1 = (float) (kEchoMs[0] * 0.001 * sr), e2 = (float) (kEchoMs[1] * 0.001 * sr);
        const float damp = (float) (1.0 - std::exp (-2.0 * kPi * std::min (rec.dampHz, 0.45 * sr) / sr));
        const float echoC = (float) (1.0 - std::exp (-2.0 * kPi * 2500.0 / sr));
        auto tap = [this] (const std::vector<float>& b, float d)
        {
            const float rp = (float) prePos - d;
            const float fl = std::floor (rp);
            const int i = (int) fl;
            const float f = rp - fl;
            return b[(size_t) (i & preMask)] + f * (b[(size_t) ((i + 1) & preMask)] - b[(size_t) (i & preMask)]);
        };
        bool bad = false;
        for (int i = 0; i < len; ++i)
        {
            preL[(size_t) prePos] = sendL[(size_t) i];
            preR[(size_t) prePos] = sendR[(size_t) i];
            float inL = tap (preL, pre), inR = tap (preR, pre);
            float eL = 0.0f, eR = 0.0f;
            if (echo > 1e-4f)
            {
                eL = 0.8f * tap (preL, e1) + 0.45f * tap (preR, e2);
                eR = 0.8f * tap (preR, e1 * 1.07f) + 0.45f * tap (preL, e2 * 0.93f);
                echoLpL += echoC * (eL - echoLpL);
                echoLpR += echoC * (eR - echoLpR);
                eL = echoLpL * echo;
                eR = echoLpR * echo;
                inL += eL;
                inR += eR;
            }
            prePos = (prePos + 1) & preMask;
            inL = difL[1].tick (difL[0].tick (inL));
            inR = difR[1].tick (difR[0].tick (inR));

            float y[kLines];
            float sum = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                const float rp = (float) linePos - lineLen[(size_t) k];
                const float fl = std::floor (rp);
                const int ii = (int) fl;
                const float f = rp - fl;
                const auto& b = line[(size_t) k];
                const int m = lineMask[(size_t) k];
                const float raw = b[(size_t) (ii & m)] + f * (b[(size_t) ((ii + 1) & m)] - b[(size_t) (ii & m)]);
                lineLp[(size_t) k] += damp * (raw - lineLp[(size_t) k]);
                y[k] = lineLp[(size_t) k] * lineGain[(size_t) k];
                sum += y[k];
            }
            sum *= 2.0f / kLines;   // Хаусхолдер: y − 2/N·Σy
            float revL = 0.0f, revR = 0.0f;
            for (int k = 0; k < kLines; ++k)
            {
                line[(size_t) k][(size_t) (linePos & lineMask[(size_t) k])] = y[k] - sum + 0.5f * ((k & 1) == 0 ? inL : inR);
                if ((k & 1) == 0) revL += y[k]; else revR += y[k];
            }
            ++linePos;

            float wl = busL[(size_t) i] * direct + revL * wet + eL;
            float wr = busR[(size_t) i] * direct + revR * wet + eR;
            if (! std::isfinite (wl) || ! std::isfinite (wr)) { wl = wr = 0.0f; bad = true; }
            const float m = (float) mix0 + dMix * (float) i;
            if (stereo)
            {
                l[i] += (wl - l[i]) * m;
                r[i] += (wr - r[i]) * m;
            }
            else l[i] += ((wl + wr) * 0.7071f - l[i]) * m;
        }
        if (bad) reset();
        pos += len;
    }
    linePos &= 0x3fffffff;

    if (view != nullptr)
    {
        for (int k = 0; k < LegionRecipe::kMaxVoices; ++k)
        {
            auto& v = voices[(size_t) k];
            const float db = v.on && v.gain > 0.0f ? 20.0f * std::log10 (std::max (1e-6f, v.peak * (float) std::sqrt (std::max (1.0, countNow)))) : -100.0f;
            view->pan[(size_t) k].store ((float) (v.pan * rec.width), std::memory_order_relaxed);
            view->pitch[(size_t) k].store (v.semis, std::memory_order_relaxed);
            view->near[(size_t) k].store (1.0f - v.dist, std::memory_order_relaxed);
            view->level[(size_t) k].store (db, std::memory_order_relaxed);
        }
        view->count.store ((float) countNow, std::memory_order_relaxed);
        view->inputHz.store (voicedness > 0.5 ? (float) (sr / period) : 0.0f, std::memory_order_relaxed);
        view->live.store (true, std::memory_order_relaxed);
    }
}
} // namespace chroma
