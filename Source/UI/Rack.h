#pragma once
#include "Views.h"
#include "../Params.h"

namespace chroma
{
class ChromaProcessor;

// Полоса rack: слоты цепочки слева направо, между ними — стрелки хода сигнала.
// Клик — выбрать слот, переключатель — On, «⋯» или правый клик — меню, перетаскивание — порядок.
// С клавиатуры: ← → — выбор, ⌥← ⌥→ — переставить, пробел — On, Enter — меню.
class RackStrip : public juce::Component
{
public:
    explicit RackStrip (ChromaProcessor& p);
    void setSelected (int slot);
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect;
    void update (const std::array<OphanSnapshot, kSlots>& snaps, const std::array<OttSnapshot, kSlots>& ottSnaps, double sampleRate);

    void paint (juce::Graphics& g) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void showMenuForTest (int slot) { showMenu (slot); }

private:
    juce::Rectangle<float> cardRect (int pos) const;
    juce::Rectangle<float> switchRect (int pos) const;
    juce::Rectangle<float> menuRect (int pos) const;
    int posAt (juce::Point<float> p) const;
    int posOfSlot (int slot) const;
    void toggleOn (int slot);
    void showMenu (int slot);
    void paintCard (juce::Graphics& g, int pos);
    void updateDescription();

    ChromaProcessor& proc;
    int selected = 0, hover = -1;
    int dragFrom = -1, dropPos = -1;
    bool dragging = false;
    std::array<OphanSnapshot, kSlots> snaps;
    std::array<OttSnapshot, kSlots> ottSnaps;
    double sampleRate = 48000.0;
};
} // namespace chroma
