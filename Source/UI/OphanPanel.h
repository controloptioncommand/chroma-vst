#pragma once
#include "Widgets.h"
#include "../Params.h"
#include "../DSP/Ophan.h"

namespace chroma
{
class ChromaProcessor;

// Число, которое тянут мышью: вверх — больше, Shift — точнее, двойной клик — значение рецепта, ↑ ↓ с клавиатуры
class DragValue : public juce::Component
{
public:
    enum Kind { Freq, Gain, Q, Rate };
    DragValue (Kind k, const juce::String& title);
    std::function<float()> get;
    std::function<void (float)> set;
    std::function<float()> getDefault;

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent&) override { setMouseCursor (juce::MouseCursor::NormalCursor); }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    juce::String text() const;
    static juce::String format (Kind k, float v);

private:
    float limit (float v) const;
    float stepped (float v0, float delta) const;   // delta — «пиксели» вверх
    Kind kind;
    float startValue = 0;
};

// Панель выбранного слота: Ophan (вкладки Macro / Advanced) или пустой слот (выбор эффекта)
class SlotPanel : public juce::Component
{
public:
    explicit SlotPanel (ChromaProcessor& p);
    void setSlot (int slot);
    int getSlot() const { return slot; }
    void refresh();                  // тип слота мог смениться; перерисовать значения
    void setStatus (const juce::String& s) { status.setText (s, juce::dontSendNotification); }
    void setAdvancedForTest (bool adv) { modeSwitch.setSelectedValue (adv ? 1 : 0); updateVisibility(); }
    std::function<void()> onSlotChanged;   // эффект добавлен

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void attachSlot();
    void updateVisibility();
    void buildGrid();
    OphanAdvanced& adv();

    ChromaProcessor& proc;
    int slot = 0;
    EffectType shownType = EffectType::Empty;
    bool advancedMode = false;

    Segmented modeSwitch, shapeSwitch;
    std::unique_ptr<juce::ParameterAttachment> shapeAtt;
    ParamKnob rate { "Rate" }, drift { "Drift" }, depth { "Depth" }, q { "Q" }, spread { "Spread" }, lowCut { "Low cut" }, mix { "Mix" };
    ParamToggle onToggle { "On" };
    juce::Label status;

    // Advanced: 7 полос × (L: from, to, gain from, gain to, Q | R: то же)
    std::vector<std::unique_ptr<DragValue>> cells;
    DragValue driftRate { DragValue::Rate, "Drift rate" };
    juce::TextButton resetButton { "Reset to recipe" };
    juce::TextButton addButton;

    juce::Rectangle<int> headArea, bodyArea, footArea, gridArea, shapeArea, addArea;
    int gridCol0 = 0, gridCellW = 0, gridGroupGap = 0, gridRowH = 0, gridTop = 0;
};
} // namespace chroma
