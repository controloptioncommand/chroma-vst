#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#include "DSP/Ophan.h"
#include "DSP/OttStack.h"

namespace chroma
{
class ChromaProcessor : public juce::AudioProcessor
{
public:
    ChromaProcessor();
    ~ChromaProcessor() override = default;

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
    double getTailLengthSeconds() const override { return 0.1; }

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

    // расширенные настройки Ophan
    OphanAdvanced& advanced (int slot) { return adv[(size_t) juce::jlimit (0, kSlots - 1, slot)]; }
    void resetAdvanced (int slot);

    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;

    // ---- для интерфейса
    std::array<OphanView, kSlots> views;
    std::array<OttView, kSlots> ottViews;
    static constexpr int kScopeSize = 1 << 15;
    juce::AbstractFifo scopeFifo { kScopeSize };
    std::vector<float> scopeBuf = std::vector<float> (kScopeSize, 0.0f);   // моно-сумма выхода
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, cpuLoad { 0.0f };

private:
    void pushRackState();   // атомики → дерево (перед сохранением)
    void pullRackState();   // дерево → атомики (после загрузки)
    void onPresetLoaded (const FactoryPreset* fp);

    std::array<OphanAdvanced, kSlots> adv;
    std::array<Ophan, kSlots> ophan;
    std::array<OttStack, kSlots> ott;
    std::atomic<int> orderPacked { 0x3210 };   // по 4 бита на место

    struct SlotParams
    {
        std::atomic<float>* type = nullptr, * on = nullptr, * mix = nullptr, * rate = nullptr, * drift = nullptr, * depth = nullptr,
                          * q = nullptr, * spread = nullptr, * lowCut = nullptr, * shape = nullptr,
                          * count = nullptr, * ottDepth = nullptr, * time = nullptr, * upward = nullptr, * downward = nullptr, * trim = nullptr;
    };
    std::array<SlotParams, kSlots> sp;
    std::atomic<float>* outParam = nullptr;
    std::array<int, kSlots> lastType {};
    std::array<bool, kSlots> lastOn {};
    float outGainNow = 1.0f;
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaProcessor)
};
} // namespace chroma
