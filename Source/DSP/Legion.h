#pragma once
#include "Svf.h"
#include "Lfo.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace chroma
{
// ===========================================================================
// Legion — из одного голоса хор или толпа.
// Каждый голос толпы — копия входа, собранная заново TD-PSOLA: зёрна длиной в два периода основного тона
// ставятся с шагом нового периода. Так высота меняется, а форманты (тембр) остаются на месте — голос
// не становится «мультяшным». Форманты двигаются отдельно: зерно читается быстрее или медленнее.
// У голоса своя высота, своё опоздание, свой тембр, своё место в стерео и своя дальность; общий — тон входа
// (детектор YIN) и пространство (FDN-реверберация, у Stadium — ещё эхо от трибун).
// Монофонический: тон следит за одним голосом. Без тона (согласные, шум, аккорды) голоса меняют только тембр и время.
// ===========================================================================

namespace LegionRecipe
{
    inline constexpr int kMaxVoices = 64;
    inline constexpr int kMaxGrains = 8;          // зёрен одного голоса одновременно
    inline constexpr double kMinHz = 70.0, kMaxHz = 1000.0;   // диапазон детектора тона
    inline constexpr double kFormantFollow = 0.3; // форманты голоса едут за его высотой на 30%: ниже — «крупнее»
    inline constexpr double kVoicedThreshold = 0.2;   // YIN: ниже — тон есть

    // Характер толпы. Номера — индексы параметра Character (новые — только в конец).
    enum Character { Choir = 0, Stadium = 1, Monks = 2, Tavern = 3, NumCharacters };

    struct Recipe
    {
        double scatterMs;    // разброс опоздания голосов (Loose 100%)
        double wobbleMs;     // медленное плавание опоздания
        double driftCents;   // медленное плавание высоты
        double driftHz;      // его скорость
        double vibCents;     // вибрато
        double vibHz;
        double formantVar;   // разброс тембра между голосами, полутоны
        double distanceDb;   // самый дальний голос тише ближнего на столько
        double nearHz, farHz;   // срез верха у ближнего и дальнего голоса
        double width;        // ширина толпы в стерео, 0…1
        double drive;        // «крик»: перегруз перед голосами, 0…1
        double presenceDb;   // подъём 2.5 кГц перед голосами
        double lowCutHz;     // срез низа перед голосами
        double size;         // размер пространства (множитель длины линий FDN)
        double rt60;         // время затухания, с
        double dampHz;       // затухание верха в пространстве
        double predelayMs;
        double echo;         // эхо от трибун (Stadium), уровень
        double wet;          // громкость пространства при Space 100%
    };

    inline constexpr Recipe kRecipes[NumCharacters] = {
        // Choir: церковный хор — вместе, мягкое вибрато, тёплый, большой зал
        { 22.0, 4.0, 10.0, 0.35, 22.0, 5.3, 1.0, 4.0, 9000.0, 5500.0, 0.8, 0.0, 0.0, 110.0, 1.0, 2.6, 6000.0, 24.0, 0.0, 0.9 },
        // Stadium: футбольная трибуна — кричат, вразнобой, далеко, открытое поле и эхо от трибун
        { 140.0, 28.0, 35.0, 0.7, 6.0, 5.6, 2.2, 12.0, 7000.0, 2200.0, 1.0, 0.65, 4.0, 170.0, 1.4, 2.4, 3200.0, 40.0, 0.55, 1.0 },
        // Monks: монахи — строгий унисон без вибрато, тёмный, каменный собор
        { 10.0, 2.0, 5.0, 0.25, 0.0, 5.0, 0.6, 3.0, 6500.0, 4000.0, 0.6, 0.0, -2.0, 70.0, 1.6, 5.5, 4200.0, 32.0, 0.0, 1.0 },
        // Tavern: таверна — пьяный хор, мимо нот и мимо доли, маленькая комната
        { 90.0, 22.0, 45.0, 0.9, 14.0, 4.6, 1.6, 6.0, 12000.0, 6000.0, 0.7, 0.3, 2.0, 100.0, 0.35, 0.8, 5000.0, 8.0, 0.0, 0.8 },
    };
}

struct LegionParams
{
    float voices = 16.0f;     // сколько голосов, 2…64
    int character = 0;        // LegionRecipe::Character
    float pitch = 0.0f;       // сдвиг высоты всей толпы, полутоны (−12…+12)
    float spread = 0.3f;      // разброс высоты между голосами, ± полутоны (0…12)
    float loose = 1.0f;       // насколько вразнобой (время и строй), 0…2
    float space = 0.4f;       // пространство, 0…1
    float mix = 1.0f;
};

// Для интерфейса: голоса толпы
struct LegionView
{
    std::array<std::atomic<float>, LegionRecipe::kMaxVoices> pan;     // −1…+1 (с учётом ширины)
    std::array<std::atomic<float>, LegionRecipe::kMaxVoices> pitch;   // текущий сдвиг высоты, полутоны
    std::array<std::atomic<float>, LegionRecipe::kMaxVoices> near;    // 1 — ближний, 0 — дальний
    std::array<std::atomic<float>, LegionRecipe::kMaxVoices> level;   // активность, дБ
    std::atomic<float> count { 0.0f };    // сглаженное число голосов
    std::atomic<float> inputHz { 0.0f };  // тон входа (0 — нет тона)
    std::atomic<bool> live { false };
};

class Legion
{
public:
    static constexpr int kControl = 32;   // шаг пересчёта, сэмплов

    void prepare (double sampleRate, uint32_t seed);
    void reset();
    // стерео на месте; R == nullptr — моно
    void process (float* L, float* R, int n, const LegionParams& p, LegionView* view);

    double currentCount() const { return countNow; }
    double detectedHz() const { return voiced ? sr / period : 0.0; }

private:
    struct Grain
    {
        double src = 0.0;     // позиция чтения в истории (абсолютная)
        float speed = 1.0f;   // скорость чтения: сдвиг формант
        float wc = 1.0f, ws = 0.0f;   // окно Ханна: 0.5 − 0.5·cos, косинус — поворотом (wc, ws)
        float rc = 1.0f, rs = 0.0f;   // поворот на один сэмпл
        int left = 0;                 // сколько сэмплов осталось
        float gain = 0.0f;
        int start = 0;        // с какого сэмпла блока звучит
    };
    struct Voice
    {
        // постоянные голоса (из seed)
        float uPitch = 0, uFormant = 0, uDelay = 0, dist = 0, pan = 0, vibRate = 1, vibDepth = 1;
        Lfo drift, wobble;
        // состояние
        double vibPhase = 0.0;
        double mark = 0.0;
        bool hasMark = false;
        double untilNext = 0.0;
        double delayNow = 0.0;     // опоздание, сэмплов (сглаженное)
        float lp = 0.0f;           // срез верха (дальность)
        std::array<Grain, LegionRecipe::kMaxGrains> grains {};
        int nGrains = 0;
        bool on = false;
        float peak = 0.0f, shownDb = -100.0f;
        // на этот блок
        float semis = 0.0f, ratio = 1.0f, formant = 1.0f, gain = 0.0f, gainPrev = 0.0f, send = 0.0f, sendPrev = 0.0f,
              lpCoef = 1.0f, gl = 0.0f, gr = 0.0f;
    };
    struct Allpass
    {
        std::vector<float> buf;
        int pos = 0, len = 1;
        float g = 0.6f;
        void setup (int length) { len = length; buf.assign ((size_t) length, 0.0f); pos = 0; }
        void clear() { std::fill (buf.begin(), buf.end(), 0.0f); pos = 0; }
        inline float tick (float x)
        {
            const float d = buf[(size_t) pos];
            const float v = x + g * d;
            buf[(size_t) pos] = v;
            if (++pos >= len) pos = 0;
            return d - g * v;
        }
    };

    void control (const LegionParams& p, int len);
    void detect();
    void spawn (Voice& v, int offset, int64_t t0);
    void renderVoice (Voice& v, int len);
    float nextRand();

    double sr = 48000.0;
    uint32_t rng = 1;

    // вход: история (моно после «крика») и детектор тона
    std::vector<float> hist;
    int histMask = 0, histSize = 0;
    int64_t written = 0;              // сколько сэмплов записано в историю
    Svf preHp, prePresence, decLp1, decLp2;
    int decim = 4, decCount = 0;
    std::vector<float> decRing;       // прорежённый вход для YIN
    int decPos = 0, decMask = 0, decSinceDetect = 0, decHop = 48;
    std::vector<float> yinBuf, yinDiff;
    int yinW = 300, tauMin = 12, tauMax = 172;
    double period = 200.0;            // период тона, сэмплов
    bool voiced = false;
    double voicedness = 0.0;

    // голоса
    std::array<Voice, LegionRecipe::kMaxVoices> voices;
    std::array<float, 1025> hann {};
    std::array<float, kControl> vbuf {}, busL {}, busR {}, sendL {}, sendR {};

    // пространство: предзадержка и эхо от трибун, диффузия, FDN на 8 линий
    std::vector<float> preL, preR;
    int preMask = 0, prePos = 0, preSize = 0;
    Allpass difL[2], difR[2];
    static constexpr int kLines = 8;
    std::array<std::vector<float>, kLines> line;
    std::array<int, kLines> lineMask {};
    std::array<float, kLines> lineLp {}, lineLen {}, lineGain {};
    int linePos = 0;
    float echoLpL = 0.0f, echoLpR = 0.0f;

    // сглаженные значения
    LegionRecipe::Recipe rec {};
    double countNow = 16.0, mixNow = 1.0, normNow = 1.0, spaceNow = 0.4, pitchNow = 0.0, spreadNow = 0.3, looseNow = 1.0;
    bool first = true;
};
} // namespace chroma
