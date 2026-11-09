#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Params.h"
#include "Presets.h"
#include "EffectPresets.h"
#include "DSP/Ophan.h"
#include "DSP/OttStack.h"
#include "DSP/Legion.h"
#include "DSP/Psalter.h"

namespace chroma
{
class ChromaProcessor : public juce::AudioProcessor
{
public:
    ChromaProcessor();
    ~ChromaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "chroma"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }   // хвост пространства Legion, ноты Psalter до 6 с

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- rack (поток сообщений)
    EffectType getSlotType (int slot) const;
    void setSlotType (int slot, EffectType t);
    // порядок обработки: order[k] — номер слота на месте k
    std::array<int, kSlots> getOrder() const;
    void setOrder (std::array<int, kSlots> o);
    void moveSlot (int fromPos, int toPos);
    void clearSlot (int slot);
    void copySlot (int from, int to);   // параметры и расширенные настройки

    // режим обработки: Serial — слоты по очереди (порядок важен), Parallel — каждый слот получает
    // сухой вход, выходы усредняются по включённым слотам. Не параметр хоста: хранится в состоянии, как порядок.
    ChainMode getChainMode() const { return (ChainMode) chainMode.load(); }
    void setChainMode (ChainMode m);

    // пресеты эффекта в слоте (поток сообщений). Имя хранится в состоянии; пустое — «Init».
    juce::String getEffectPresetName (int slot) const;
    void setEffectPresetName (int slot, const juce::String& name);
    void loadEffectFactory (int slot, int index);
    bool loadEffectUser (int slot, const juce::File& f);
    bool saveEffectPreset (int slot, const juce::String& name);
    void stepEffectPreset (int slot, int dir);
    juce::ValueTree captureEffect (int slot);              // ручки, Mix и Advanced слота
    bool applyEffect (int slot, const juce::ValueTree& t);   // false — пресет другого эффекта

    // расширенные настройки Ophan
    OphanAdvanced& advanced (int slot) { return adv[(size_t) juce::jlimit (0, kSlots - 1, slot)]; }
    void resetAdvanced (int slot);

    // свои сэмплы нот Psalter (поток сообщений). Сэмпл заменяет синтезированную ноту; хранится в проекте и в пресете эффекта.
    static constexpr double kMaxSampleSeconds = 8.0;
    bool loadPsalterSample (int slot, int note, const juce::File& f);   // false — не аудио или не прочитался
    bool setPsalterSample (int slot, int note, const juce::AudioBuffer<float>& audio, double rate, const juce::String& name);
    void clearPsalterSample (int slot, int note);
    bool hasPsalterSample (int slot, int note) const;
    juce::String getPsalterSampleName (int slot, int note) const;
    // для теста и prepareToPlay: собрать IR нот прямо сейчас (иначе их строит фоновый поток за ≈ 0.1–0.3 с)
    void buildPsalterNow();

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    // ---- для интерфейса
    std::array<OphanView, kSlots> views;
    std::array<OttView, kSlots> ottViews;
    std::array<LegionView, kSlots> legionViews;
    std::array<PsalterView, kSlots> psalterViews;
    static constexpr int kScopeSize = 1 << 15;
    juce::AbstractFifo scopeFifo { kScopeSize };
    std::vector<float> scopeBuf = std::vector<float> (kScopeSize, 0.0f);   // моно-сумма выхода
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, cpuLoad { 0.0f };

private:
    void pushRackState();   // атомики → дерево (перед сохранением)
    void pullRackState();   // дерево → атомики (после загрузки)
    void onPresetLoaded (const FactoryPreset* fp);
    void ensureScratch (int samples);   // только вне аудиопотока
    bool slotActive (int slot) const;   // не пустой и включён
    // Psalter: IR нот строит фоновый поток; sync — сразу (вне аудиопотока, до prepare)
    void servicePsalter (bool sync);
    PsalterIrSpec psalterSpec (int slot, int note) const;
    juce::AudioBuffer<float> buildPsalterIr (int slot, int note, const PsalterIrSpec& spec);
    void writePsalterSamples (juce::ValueTree& t, int slot);
    void readPsalterSamples (const juce::ValueTree& t, int slot);
    void clearAllPsalterSamples();
    void runSlot (int slot, float* L, float* R, int n);
    void processParallel (float* L, float* R, int n);

    std::array<OphanAdvanced, kSlots> adv;
    std::array<Ophan, kSlots> ophan;
    std::array<OttStack, kSlots> ott;
    std::array<Legion, kSlots> legion;
    juce::dsp::ConvolutionMessageQueue convQueue { 512 };   // общий фоновый поток свёрток (до std::array<Psalter>)
    std::array<Psalter, kSlots> psalter;
    juce::CriticalSection psalterLock;   // фоновый поток IR ↔ prepareToPlay

    struct PsalterSample
    {
        juce::String name;
        juce::AudioBuffer<float> audio;   // ≤ 8 с, 1–2 канала
        double rate = 0.0;
        juce::MemoryBlock encoded;        // FLAC (или WAV) для проекта и пресета
        juce::String format;
        uint32_t version = 0;             // 0 — сэмпла нет
    };
    std::array<std::array<PsalterSample, PsalterRecipe::kNotes>, kSlots> samples;
    std::array<std::array<std::atomic<uint32_t>, PsalterRecipe::kNotes>, kSlots> sampleVersion {};
    uint32_t sampleCounter = 0;
    mutable juce::CriticalSection sampleLock;   // поток сообщений ↔ фоновый поток IR ↔ сохранение

    class IrThread : public juce::Thread
    {
    public:
        explicit IrThread (ChromaProcessor& p) : juce::Thread ("chroma Psalter notes"), proc (p) {}
        void run() override { while (! threadShouldExit()) { proc.servicePsalter (false); wait (25); } }
    private:
        ChromaProcessor& proc;
    };
    IrThread irThread { *this };
    std::atomic<int> orderPacked { 0x76543210 };   // по 4 бита на место
    std::atomic<int> chainMode { (int) ChainMode::Serial };

    // параллельный режим: сухая копия, рабочий буфер ветки, сумма и сумма весов (выделены заранее)
    juce::AudioBuffer<float> dryBuf, workBuf, accBuf;
    std::vector<float> normBuf;
    std::array<float, kSlots> branchGain {};   // плавные веса веток (≈ 15 мс)
    float dryGain = 1.0f;                       // вес сухого сигнала, когда ни одной ветки нет
    int modeNow = (int) ChainMode::Serial;      // режим, который сейчас звучит
    float modeFade = 1.0f;                      // смена режима: короткий провал громкости вместо щелчка

    struct SlotParams
    {
        std::atomic<float>* type = nullptr, * on = nullptr, * mix = nullptr, * rate = nullptr, * drift = nullptr, * depth = nullptr,
                          * q = nullptr, * spread = nullptr, * lowCut = nullptr, * shape = nullptr,
                          * count = nullptr, * ottDepth = nullptr, * time = nullptr, * upward = nullptr, * downward = nullptr, * trim = nullptr, * guard = nullptr,
                          * voices = nullptr, * character = nullptr, * pitch = nullptr, * pitchSpread = nullptr, * loose = nullptr, * space = nullptr,
                          * timbre = nullptr, * decay = nullptr, * tone = nullptr, * psRate = nullptr, * psDepth = nullptr, * width = nullptr;
        std::array<std::atomic<float>*, PsalterRecipe::kNotes> note {};
    };
    std::array<SlotParams, kSlots> sp;
    std::atomic<float>* outParam = nullptr;
    std::array<int, kSlots> lastType {};
    std::array<bool, kSlots> lastOn {};
    float outGainNow = 1.0f;
    std::atomic<double> sr { 48000.0 };
    int blockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaProcessor)
};
} // namespace chroma
