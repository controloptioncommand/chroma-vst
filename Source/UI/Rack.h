#pragma once
#include "Views.h"
#include "../Params.h"

namespace chroma
{
class ChromaProcessor;

// Полоса rack: слоты цепочки слева направо, между ними — стрелки хода сигнала.
// Мест 8, но пока эффектов меньше четырёх (и все стоят на первых четырёх местах), видно 4.
// На экране помещаются 4 карточки, остальные — прокруткой (колесо, трекпад, стрелки по краям).
// Клик — выбрать слот, переключатель — On, «⋯» или правый клик — меню, перетаскивание — порядок.
// С клавиатуры: ← → — выбор, ⌥← ⌥→ — переставить, пробел — On, Enter — меню.
class RackStrip : public juce::Component, private juce::Timer
{
public:
    explicit RackStrip (ChromaProcessor& p);
    void setSelected (int slot);
    int getSelected() const { return selected; }
    std::function<void (int)> onSelect;
    void update (const std::array<OphanSnapshot, kSlots>& snaps, const std::array<OttSnapshot, kSlots>& ottSnaps,
                 const std::array<LegionSnapshot, kSlots>& legionSnaps, double sampleRate);

    void paint (juce::Graphics& g) override;
    void resized() override {}
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; hoverArrow = 0; repaint(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void showMenuForTest (int slot) { showMenu (slot); }
    int getShownCount() const { return shown; }
    void scrollToEndForTest() { scrollTarget = scrollX = maxScroll(); repaint(); }

private:
    void timerCallback() override;
    void updateShown();
    float cardWidth() const;
    float maxScroll() const;
    void scrollBy (float dx);
    void scrollToPos (int pos);
    juce::Rectangle<float> arrowRect (bool right) const;
    bool arrowVisible (bool right) const;
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
    int shown = kBaseSlots;                   // сколько мест показано: 4 или 8
    float scrollX = 0.0f, scrollTarget = 0.0f;
    int hoverArrow = 0;                       // −1 левая, +1 правая
    std::array<OphanSnapshot, kSlots> snaps;
    std::array<OttSnapshot, kSlots> ottSnaps;
    std::array<LegionSnapshot, kSlots> legionSnaps;
    double sampleRate = 48000.0;
};
} // namespace chroma
