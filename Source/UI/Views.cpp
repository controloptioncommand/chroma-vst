#include "Views.h"

namespace chroma
{
using LnF = ChromaLookAndFeel;

namespace
{
const char* kTabNames[2] = { "Curves", "Scope" };
const char* kOttTabNames[2] = { "Stack", "Scope" };
const char* kCrowdTabNames[2] = { "Crowd", "Scope" };
constexpr double kPi = 3.141592653589793;
constexpr float kFMin = 20.0f, kFMax = 20000.0f;
}

// ===========================================================================
// Мини-спектрограмма (как в Talk2Me)
MiniSpectrogram::MiniSpectrogram()
{
    setTitle ("Mini spectrogram");
    setDescription ("A small decorative spectrogram of the output.");
    setInterceptsMouseClicks (false, false);
}

void MiniSpectrogram::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i) ring[(size_t) ((written + i) & 2047)] = x[i];
    written += n;
}

void MiniSpectrogram::tick()
{
    std::array<int8_t, kRows> col;
    col.fill (-1);
    if (written - lastColumnAt >= 256)
    {
        lastColumnAt = written;
        constexpr int N = 512;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (N - 1));
            work[(size_t) i] = ring[(size_t) ((written - N + i) & 2047)] * w;
        }
        std::fill (work.begin() + N, work.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (work.data());
        // полосы по логарифму: 120 Гц … 7 кГц, снизу вверх
        for (int r = 0; r < kRows; ++r)
        {
            const double f0 = 120.0 * std::pow (7000.0 / 120.0, (double) r / kRows);
            const double f1 = 120.0 * std::pow (7000.0 / 120.0, (double) (r + 1) / kRows);
            const int b0 = juce::jlimit (1, N / 2 - 1, (int) (f0 / sampleRate * N));
            const int b1 = juce::jlimit (b0 + 1, N / 2, (int) (f1 / sampleRate * N) + 1);
            float m = 0;
            for (int b = b0; b < b1; ++b) m = juce::jmax (m, work[(size_t) b]);
            const float db = juce::Decibels::gainToDecibels (m / (float) (N / 4), -120.0f) + 12.0f;
            col[(size_t) r] = db < -34.0f ? (int8_t) -1 : (int8_t) rampBucket (db);
        }
    }
    cols.push_back (col);
    const int maxCols = juce::jmax (1, (getWidth() - 8) / kCell);
    while ((int) cols.size() > maxCols) cols.pop_front();
    repaint();
}

void MiniSpectrogram::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    LnF::drawPlate (g, r, 8.0f, Colours::well);
    const int h = kRows * kCell;
    const int y0 = (getHeight() - h) / 2;
    const int maxCols = juce::jmax (1, (getWidth() - 8) / kCell);
    int x = 4 + (maxCols - (int) cols.size()) * kCell;
    juce::RectangleList<int> buckets[10];
    for (auto& c : cols)
    {
        for (int row = 0; row < kRows; ++row)
            if (c[(size_t) row] >= 0)
                buckets[c[(size_t) row]].addWithoutMerging ({ x, y0 + (kRows - 1 - row) * kCell, kCell, kCell });
        x += kCell;
    }
    for (int b = 0; b < 10; ++b)
    {
        if (buckets[b].isEmpty()) continue;
        g.setColour (rampBucketColour (Colours::carrierRamp, b));
        g.fillRectList (buckets[b]);
    }
}

// ===========================================================================
// Снимок Ophan и АЧХ
void OphanSnapshot::read (const OphanView& v, float mixAmount)
{
    live = v.live.load (std::memory_order_relaxed);
    for (int s = 0; s < 2; ++s)
        for (int b = 0; b < OphanRecipe::kBands; ++b)
        {
            freq[s][b] = v.freq[(size_t) s][(size_t) b].load (std::memory_order_relaxed);
            gain[s][b] = v.gain[(size_t) s][(size_t) b].load (std::memory_order_relaxed);
            q[s][b] = v.q[(size_t) s][(size_t) b].load (std::memory_order_relaxed);
        }
    lowCut = v.lowCut.load (std::memory_order_relaxed);
    mix = mixAmount;
}

std::complex<double> OphanSnapshot::response (int side, double f, double sr) const
{
    // аналоговый прототип с предыскажением — совпадает с цифровым SVF на всех частотах до Найквиста
    auto warp = [sr] (double x) { return std::tan (kPi * juce::jlimit (1.0, sr * 0.499, x) / sr); };
    const double wf = warp (f);
    std::complex<double> h (1.0, 0.0);
    {
        const std::complex<double> s (0.0, wf / warp (lowCut));
        h *= (s * s) / (s * s + s / 0.71 + 1.0);
    }
    for (int b = 0; b < OphanRecipe::kBands; ++b)
    {
        const double A = std::pow (10.0, gain[side][b] / 40.0), Q = juce::jmax (0.1, (double) q[side][b]);
        const std::complex<double> s (0.0, wf / warp (freq[side][b]));
        h *= (s * s + s * (A / Q) + 1.0) / (s * s + s / (A * Q) + 1.0);
    }
    return 1.0 + (double) mix * (h - 1.0);
}

// ===========================================================================
// Главный вид
MainView::MainView()
{
    setWantsKeyboardFocus (true);
    setTitle ("Main view");
    setDescription ("Curves: the live EQ curves of the left and right channel over the output spectrum. Stack: what every OTT of the stack does. "
                    "Crowd: the voices of Legion by stereo place and pitch. Scope: the output wave.");
}

juce::Colour MainView::background() const { return currentTheme() == ThemeId::Rubber ? Colours::well : Colours::surface; }

void MainView::setMode (Mode m)
{
    mode = m;
    setDescription (juce::String ("Main view, ") + kTabNames[(int) m] + " selected.");
    repaint();
}

void MainView::setSlot (const juce::String& c, const OphanSnapshot* s)
{
    caption = c;
    content = Content::Eq;
    snap = s != nullptr ? *s : OphanSnapshot {};
    if (s == nullptr) snap.live = false;
}

void MainView::setOttSlot (const juce::String& c, const OttSnapshot* s)
{
    caption = c;
    content = Content::Ott;
    ottSnap = s != nullptr ? *s : OttSnapshot {};
    if (s == nullptr) ottSnap.live = false;
}

void MainView::setLegionSlot (const juce::String& c, const LegionSnapshot* s)
{
    caption = c;
    content = Content::Crowd;
    legionSnap = s != nullptr ? *s : LegionSnapshot {};
    if (s == nullptr) legionSnap.live = false;
}

void LegionSnapshot::read (const LegionView& v)
{
    live = v.live.load (std::memory_order_relaxed);
    count = v.count.load (std::memory_order_relaxed);
    inputHz = v.inputHz.load (std::memory_order_relaxed);
    for (int k = 0; k < LegionRecipe::kMaxVoices; ++k)
    {
        pan[k] = v.pan[(size_t) k].load (std::memory_order_relaxed);
        pitch[k] = v.pitch[(size_t) k].load (std::memory_order_relaxed);
        near[k] = v.near[(size_t) k].load (std::memory_order_relaxed);
        level[k] = v.level[(size_t) k].load (std::memory_order_relaxed);
    }
}

float LegionSnapshot::pitchToUnit (float st)
{
    constexpr float k = 1.5f;
    return std::asinh (juce::jlimit (-kPitchRange, kPitchRange, st) / k) / std::asinh (kPitchRange / k);
}

// Голос — кружок carrier: ближний крупнее, дальний мельче; заливка ярче, когда голос звучит громче.
// Дальние рисуются первыми — ближние поверх.
void LegionSnapshot::paintVoices (juce::Graphics& g, juce::Rectangle<float> a, bool small, juce::Colour back) const
{
    const int n = juce::jlimit (0, LegionRecipe::kMaxVoices, (int) std::ceil (count - 1e-3f));
    std::array<int, LegionRecipe::kMaxVoices> idx {};
    for (int k = 0; k < n; ++k) idx[(size_t) k] = k;
    std::sort (idx.begin(), idx.begin() + n, [this] (int x, int y) { return near[x] < near[y]; });
    for (int i = 0; i < n; ++i)
    {
        const int k = idx[(size_t) i];
        const float fade = juce::jlimit (0.0f, 1.0f, count - (float) k);   // голос, который въезжает
        const float t = juce::jlimit (0.0f, 1.0f, (level[k] + 42.0f) / 42.0f);
        const float d = small ? 2.5f + 2.5f * near[k] : 6.0f + 9.0f * near[k];
        const float x = a.getCentreX() + juce::jlimit (-1.0f, 1.0f, pan[k]) * (a.getWidth() * 0.5f - d);
        const float y = a.getCentreY() - pitchToUnit (pitch[k]) * (a.getHeight() * 0.5f - d * 0.5f);
        auto dot = juce::Rectangle<float> (d, d).withCentre ({ x, y });
        g.setColour (back.interpolatedWith (Colours::carrier, (0.18f + 0.82f * t) * fade));
        g.fillEllipse (dot);
        if (! small)
        {
            g.setColour (Colours::carrier.withAlpha (fade));
            g.drawEllipse (dot.reduced (0.5f), 1.0f);
        }
    }
}

void OttSnapshot::read (const OttView& v)
{
    live = v.live.load (std::memory_order_relaxed);
    count = v.count.load (std::memory_order_relaxed);
    for (int st = 0; st < OttRecipe::kMaxStages; ++st)
        for (int b = 0; b < OttRecipe::kBands; ++b)
            gain[st][b] = v.gain[(size_t) st][(size_t) b].load (std::memory_order_relaxed);
}

void MainView::push (const float* x, int n)
{
    for (int i = 0; i < n; ++i) ring[(size_t) ((written + i) & ((1 << 15) - 1))] = x[i];
    written += n;
}

void MainView::tick()
{
    constexpr int mask = (1 << 15) - 1;
    if (mode == Scope)
    {
        const int perCol = 96;
        if (written - scopeRead > (1 << 14)) scopeRead = written - (1 << 14);
        while (scopeRead + perCol <= written)
        {
            Col c;
            float pk = 0;
            for (int i = 0; i < perCol; ++i)
            {
                const float v = ring[(size_t) ((scopeRead + i) & mask)];
                c.mn = juce::jmin (c.mn, v);
                c.mx = juce::jmax (c.mx, v);
                pk = juce::jmax (pk, std::abs (v));
            }
            c.db = juce::Decibels::gainToDecibels (pk, -100.0f);
            scopeRead += perCol;
            scopeCols.push_back (c);
        }
        while ((int) scopeCols.size() > juce::jmax (1, getWidth() - 2)) scopeCols.pop_front();
    }
    else
    {
        scopeRead = written;
        // спектр: окно Ханна, спад ~ 18 дБ/с, рост мгновенный
        if (written - lastFft >= 1024)
        {
            lastFft = written;
            for (int i = 0; i < kFftSize; ++i)
            {
                const float w = 0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (kFftSize - 1));
                fftWork[(size_t) i] = ring[(size_t) ((written - kFftSize + i) & mask)] * w;
            }
            std::fill (fftWork.begin() + kFftSize, fftWork.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (fftWork.data());
            for (int b = 0; b < kFftSize / 2; ++b)
            {
                const float db = juce::Decibels::gainToDecibels (fftWork[(size_t) b] / (float) (kFftSize / 4), -120.0f);
                spectrum[(size_t) b] = juce::jmax (db, spectrum[(size_t) b] - 0.6f);
            }
        }
    }
    repaint();
}

void MainView::resized()
{
    const float h = 28.0f;
    const float tabW = 2 * 72.0f;
    tabsArea = { (float) getWidth() - 12.0f - tabW, (float) getHeight() - 12.0f - h, tabW, h };
    legendArea = { (float) getWidth() - 12.0f - 236.0f, 12.0f, 236.0f, h };
}

float MainView::xForFreq (float f, juce::Rectangle<float> a) const
{
    const float t = std::log (juce::jlimit (kFMin, kFMax, f) / kFMin) / std::log (kFMax / kFMin);
    return a.getX() + t * a.getWidth();
}

void MainView::paintGrid (juce::Graphics& g, juce::Rectangle<float> a, bool dbLines)
{
    const float dashes[] = { 3.0f, 4.0f };
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (float f : { 100.0f, 1000.0f, 10000.0f })
    {
        const float x = xForFreq (f, a);
        g.setColour (Colours::outline);
        g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (Colours::ink2);
        g.drawText (f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + "k" : juce::String ((int) f),
                    juce::Rectangle<float> (x + 4.0f, a.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
    }
    if (! dbLines) return;
    auto yForDb = [&] (float db) { return a.getCentreY() - juce::jlimit (-kDbRange, kDbRange, db) / kDbRange * a.getHeight() * 0.5f; };
    for (float db : { 24.0f, 12.0f, 0.0f, -12.0f, -24.0f })
    {
        const float y = yForDb (db);
        g.setColour (Colours::ink2.withAlpha (std::abs (db) < 0.5f ? 0.55f : 0.35f));
        g.drawDashedLine ({ a.getX(), y, a.getRight(), y }, dashes, 2, 1.0f);
        g.setColour (Colours::ink2);
        const auto txt = db > 0 ? "+" + juce::String ((int) db) : db < 0 ? minus() + juce::String ((int) -db) : juce::String ("0");
        g.drawText (txt, juce::Rectangle<float> (a.getRight() + 4.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
    }
}

void MainView::paintSpectrum (juce::Graphics& g, juce::Rectangle<float> a)
{
    // спектр выхода: тихая заливка снизу (−90…−6 дБ на всю высоту)
    juce::Path p;
    bool started = false;
    const int nb = kFftSize / 2;
    for (int px = 0; px <= (int) a.getWidth(); px += 2)
    {
        const float f = kFMin * std::pow (kFMax / kFMin, (float) px / a.getWidth());
        const float bin = f / (float) sampleRate * (float) kFftSize;
        const int b0 = juce::jlimit (1, nb - 1, (int) bin);
        const float db = spectrum[(size_t) b0] + 3.0f * std::log2 (juce::jmax (1.0f, f / 1000.0f));   // наклон +3 дБ/окт
        const float t = juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 84.0f);
        const float y = a.getBottom() - t * a.getHeight();
        if (! started) { p.startNewSubPath (a.getX() + (float) px, a.getBottom()); started = true; }
        p.lineTo (a.getX() + (float) px, y);
    }
    p.lineTo (a.getRight(), a.getBottom());
    p.closeSubPath();
    g.setColour (Colours::ink.withAlpha (0.07f));
    g.fillPath (p);
}

void MainView::paintStack (juce::Graphics& g, juce::Rectangle<float> a)
{
    paintGrid (g, a, false);
    paintSpectrum (g, a);
    const float dashes[] = { 3.0f, 4.0f };
    const float xs[4] = { a.getX(), xForFreq ((float) OttRecipe::kLowX, a), xForFreq ((float) OttRecipe::kHighX, a), a.getRight() };
    // кроссоверы
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (int i = 1; i <= 2; ++i)
    {
        g.setColour (Colours::ink2.withAlpha (0.55f));
        g.drawDashedLine ({ xs[i], a.getY(), xs[i], a.getBottom() }, dashes, 2, 1.0f);
        g.setColour (Colours::ink2);
        g.drawText (i == 1 ? juce::String ("88 Hz") : juce::String ("2.5 kHz"), juce::Rectangle<float> (xs[i] + 4.0f, a.getY() - 16.0f, 60.0f, 14.0f),
                    juce::Justification::centredLeft);
    }
    if (! ottSnap.live) return;

    // карта ступеней: строка — ступень (1 сверху), три клетки — полосы. Подъём снизу — carrier, сжатие сверху — modulator.
    const int n = juce::jlimit (1, OttRecipe::kMaxStages, (int) std::ceil (ottSnap.count - 1e-3f));
    const float rowH = a.getHeight() / (float) n;
    const float gap = rowH >= 6.0f ? 2.0f : 1.0f;
    for (int st = 0; st < n; ++st)
    {
        const float fade = juce::jlimit (0.0f, 1.0f, ottSnap.count - (float) st);   // ступень, которая въезжает
        for (int b = 0; b < OttRecipe::kBands; ++b)
        {
            const float gdb = ottSnap.gain[st][b];
            const float t = juce::jlimit (0.0f, 1.0f, std::abs (gdb) / 24.0f);
            auto cell = juce::Rectangle<float> (xs[b] + 6.0f, a.getY() + rowH * (float) st, xs[b + 1] - xs[b] - 12.0f, rowH - gap);
            const auto col = gdb >= 0.0f ? Colours::carrier : Colours::modulator;
            g.setColour (Colours::well.interpolatedWith (col, (0.12f + 0.88f * t) * fade));
            g.fillRoundedRectangle (cell, juce::jmin (3.0f, cell.getHeight() * 0.3f));
        }
    }
    g.setColour (Colours::ink2);
    g.drawText ("1", juce::Rectangle<float> (a.getRight() + 4.0f, a.getY(), 30.0f, juce::jmin (14.0f, rowH)), juce::Justification::centredLeft);
    g.drawText (juce::String (n), juce::Rectangle<float> (a.getRight() + 4.0f, a.getBottom() - juce::jmin (14.0f, rowH), 30.0f, juce::jmin (14.0f, rowH)),
                juce::Justification::centredLeft);
}

// Толпа: по горизонтали — место в стерео (L / C / R), по вертикали — высота голоса относительно входа
void MainView::paintCrowd (juce::Graphics& g, juce::Rectangle<float> a)
{
    const float dashes[] = { 3.0f, 4.0f };
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (int i = 0; i < 3; ++i)
    {
        const float x = a.getX() + a.getWidth() * 0.5f * (float) i;
        g.setColour (Colours::outline);
        if (i == 1) g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
        g.setColour (Colours::ink2);
        const char* names[3] = { "L", "C", "R" };
        g.drawText (names[i], juce::Rectangle<float> (i == 2 ? x - 40.0f : x + (i == 1 ? 4.0f : 0.0f), a.getBottom() + 2.0f, 40.0f, 14.0f),
                    i == 2 ? juce::Justification::centredRight : juce::Justification::centredLeft);
    }
    for (float st : { 12.0f, 3.0f, 0.0f, -3.0f, -12.0f })
    {
        const float y = a.getCentreY() - LegionSnapshot::pitchToUnit (st) * a.getHeight() * 0.5f;
        g.setColour (Colours::ink2.withAlpha (std::abs (st) < 0.5f ? 0.55f : 0.35f));
        g.drawDashedLine ({ a.getX(), y, a.getRight(), y }, dashes, 2, 1.0f);
        g.setColour (Colours::ink2);
        const auto txt = st > 0 ? "+" + juce::String ((int) st) : st < 0 ? minus() + juce::String ((int) -st) : juce::String ("0");
        g.drawText (txt, juce::Rectangle<float> (a.getRight() + 4.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
    }
    if (legionSnap.live) legionSnap.paintVoices (g, a, false, background());
}

void MainView::paintCurves (juce::Graphics& g, juce::Rectangle<float> a)
{
    auto yForDb = [&] (float db) { return a.getCentreY() - juce::jlimit (-kDbRange, kDbRange, db) / kDbRange * a.getHeight() * 0.5f; };
    paintGrid (g, a, true);
    paintSpectrum (g, a);

    if (! snap.live) return;

    // кривые: правая под левой, левая — сверху
    for (int side : { 1, 0 })
    {
        const auto col = side == 0 ? Colours::carrier : Colours::modulator;
        juce::Path p;
        for (int px = 0; px <= (int) a.getWidth(); ++px)
        {
            const double f = kFMin * std::pow (kFMax / kFMin, (double) px / a.getWidth());
            const float db = (float) (20.0 * std::log10 (juce::jmax (1e-6, std::abs (snap.response (side, f, sampleRate)))));
            const float y = yForDb (db);
            if (px == 0) p.startNewSubPath (a.getX(), y);
            else p.lineTo (a.getX() + (float) px, y);
        }
        g.setColour (col);
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // точки полос: кружок с номером полосы EQ Eight (2–8)
        g.setFont (LnF::mono (9.0f, true));
        for (int b = 0; b < OphanRecipe::kBands; ++b)
        {
            const float f = snap.freq[side][b];
            if (f < kFMin || f > kFMax) continue;
            const float x = xForFreq (f, a);
            const float y = yForDb ((float) (20.0 * std::log10 (juce::jmax (1e-6, std::abs (snap.response (side, f, sampleRate))))));
            auto dot = juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ x, y });
            g.setColour (background());
            g.fillEllipse (dot);
            g.setColour (col);
            g.drawEllipse (dot.reduced (0.75f), 1.5f);
            g.setColour (Colours::ink);
            g.drawText (juce::String (b + 2), dot.translated (0.0f, 0.5f), juce::Justification::centred);
        }
    }
}

void MainView::paintScope (juce::Graphics& g, juce::Rectangle<float> area)
{
    const float mid = area.getCentreY(), hh = area.getHeight() * 0.5f;
    const float dashes[] = { 3.0f, 4.0f };
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (float db : { -6.0f, -12.0f })
    {
        const float a = juce::Decibels::decibelsToGain (db) * hh;
        for (float y : { mid - a, mid + a })
        {
            g.setColour (Colours::ink2.withAlpha (0.45f));
            g.drawDashedLine ({ area.getX() + 12.0f, y, area.getRight() - 48.0f, y }, dashes, 2, 1.0f);
        }
        g.setColour (Colours::ink2);
        g.drawText (minus() + juce::String ((int) -db), juce::Rectangle<float> (area.getRight() - 44.0f, mid - a - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
    }
    juce::RectangleList<float> buckets[10];
    float x = area.getRight() - (float) scopeCols.size();
    for (auto& c : scopeCols)
    {
        if (x >= area.getX() && c.mx >= c.mn)
        {
            const float y0 = mid - juce::jlimit (-1.0f, 1.0f, c.mx) * hh;
            const float y1 = mid - juce::jlimit (-1.0f, 1.0f, c.mn) * hh;
            buckets[rampBucket (c.db)].addWithoutMerging ({ x, y0, 1.0f, juce::jmax (1.0f, y1 - y0) });
        }
        x += 1.0f;
    }
    for (int b = 0; b < 10; ++b)
    {
        if (buckets[b].isEmpty()) continue;
        g.setColour (rampBucketColour (Colours::carrierRamp, b));
        g.fillRectList (buckets[b]);
    }
}

void MainView::paintOverlay (juce::Graphics& g)
{
    const auto plateFill = currentTheme() == ThemeId::Rubber ? Colours::surface : Colours::well;
    // слот — слева сверху
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink2);
    g.drawText (caption, juce::Rectangle<float> (16.0f, 12.0f, legendArea.getX() - 24.0f, 28.0f), juce::Justification::centredLeft);

    LnF::drawPlate (g, legendArea, 8.0f, plateFill);
    auto r = legendArea.reduced (12.0f, 0.0f);
    g.setFont (LnF::mono (LnF::Type::footnote));
    if (mode == Curves && content == Content::Crowd)
    {
        // легенда толпы: «● Near  ● Far»
        auto half = r.getWidth() * 0.5f;
        for (int i = 0; i < 2; ++i)
        {
            auto cell = juce::Rectangle<float> (r.getX() + half * (float) i, r.getY(), half, r.getHeight());
            const float d = i == 0 ? 12.0f : 7.0f;
            auto dot = cell.removeFromLeft (22.0f).withSizeKeepingCentre (d, d);
            g.setColour ((currentTheme() == ThemeId::Rubber ? Colours::surface : Colours::well).interpolatedWith (Colours::carrier, i == 0 ? 1.0f : 0.45f));
            g.fillEllipse (dot);
            g.setColour (Colours::ink);
            g.drawText (i == 0 ? "Near" : "Far", cell.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
        }
    }
    else if (mode == Curves)
    {
        // легенда каналов: «— Left  — Right»
        auto half = r.getWidth() * 0.5f;
        for (int side = 0; side < 2; ++side)
        {
            auto cell = juce::Rectangle<float> (r.getX() + half * (float) side, r.getY(), half, r.getHeight());
            g.setColour (side == 0 ? Colours::carrier : Colours::modulator);
            const bool ott = content == Content::Ott;
            g.fillRoundedRectangle (cell.removeFromLeft (22.0f).withSizeKeepingCentre (18.0f, ott ? 8.0f : 3.0f), ott ? 2.0f : 1.5f);
            g.setColour (Colours::ink);
            g.drawText (ott ? (side == 0 ? "Up" : "Down") : (side == 0 ? "Left" : "Right"), cell.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
        }
    }
    else
    {
        g.setColour (Colours::ink);
        g.drawText (minus() + "24 dB", r.removeFromLeft (46.0f), juce::Justification::centredLeft);
        g.drawText ("0 dB", r.removeFromRight (30.0f), juce::Justification::centredRight);
        auto bar = r.reduced (6.0f, 0.0f).withSizeKeepingCentre (r.getWidth() - 12.0f, 6.0f);
        const float seg = bar.getWidth() / 10.0f;
        for (int b = 0; b < 10; ++b)
        {
            g.setColour (rampBucketColour (Colours::carrierRamp, b));
            g.fillRect (juce::Rectangle<float> (bar.getX() + seg * (float) b + 0.5f, bar.getY(), seg - 1.0f, bar.getHeight()));
        }
    }

    LnF::drawPlate (g, tabsArea, tabsArea.getHeight() * 0.5f, plateFill);
    const float w = (tabsArea.getWidth() - 4.0f) / 2.0f;
    for (int i = 0; i < 2; ++i)
    {
        auto cell = juce::Rectangle<float> (tabsArea.getX() + 2.0f + w * (float) i, tabsArea.getY() + 2.0f, w, tabsArea.getHeight() - 4.0f);
        const bool sel = (int) mode == i;
        if (sel)
        {
            g.setColour (currentTheme() == ThemeId::Rubber ? Colours::well : Colours::surface);
            g.fillRoundedRectangle (cell, cell.getHeight() * 0.5f);
            g.setColour (Colours::outline);
            g.drawRoundedRectangle (cell.reduced (0.5f), cell.getHeight() * 0.5f, 1.0f);
        }
        g.setColour (sel ? Colours::ink : Colours::ink2);
        g.setFont (LnF::font (LnF::Type::subheadline, sel));
        g.drawText (content == Content::Ott ? kOttTabNames[i] : content == Content::Crowd ? kCrowdTabNames[i] : kTabNames[i], cell, juce::Justification::centred);
    }
    if (hasKeyboardFocus (false))
        LnF::drawFocusRing (g, tabsArea.expanded (2.0f), tabsArea.getHeight() * 0.5f + 2.0f);
}

void MainView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float radius = 12.0f;
    g.setColour (background());
    g.fillRoundedRectangle (r, radius);
    {
        juce::Graphics::ScopedSaveState s (g);
        juce::Path clip;
        clip.addRoundedRectangle (r.reduced (1.0f), radius - 1.0f);
        g.reduceClipRegion (clip);
        if (mode == Curves && content == Content::Ott) paintStack (g, r.withTrimmedLeft (16.0f).withTrimmedRight (44.0f).withTrimmedTop (52.0f).withTrimmedBottom (60.0f));
        else if (mode == Curves && content == Content::Crowd) paintCrowd (g, r.withTrimmedLeft (16.0f).withTrimmedRight (44.0f).withTrimmedTop (52.0f).withTrimmedBottom (60.0f));
        else if (mode == Curves) paintCurves (g, r.withTrimmedLeft (16.0f).withTrimmedRight (44.0f).withTrimmedTop (52.0f).withTrimmedBottom (60.0f));
        else paintScope (g, r.reduced (1.0f, 46.0f));
        paintOverlay (g);
    }
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

void MainView::mouseDown (const juce::MouseEvent& e)
{
    if (tabsArea.contains (e.position))
        setMode ((Mode) juce::jlimit (0, 1, (int) ((e.position.x - tabsArea.getX()) / tabsArea.getWidth() * 2.0f)));
}

bool MainView::keyPressed (const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::leftKey)  { setMode (Curves); return true; }
    if (k.getKeyCode() == juce::KeyPress::rightKey) { setMode (Scope); return true; }
    return false;
}
} // namespace chroma
