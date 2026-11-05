#pragma once
#include "LookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace chroma
{
// Сегментированный переключатель (капсула)
class Segmented : public juce::Component
{
public:
    struct Item { juce::String text; int value; };
    Segmented();
    void setItems (std::vector<Item> it) { items = std::move (it); repaint(); }
    void setSelectedValue (int v) { if (v != selected) { selected = v; repaint(); } }
    int getSelectedValue() const { return selected; }
    std::function<void (int)> onChange;
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void choose (int index);
    std::vector<Item> items;
    int selected = 0;
};

// Логотип chrōma (Medeo Pixel) — открывает About
class LogoButton : public juce::Button
{
public:
    LogoButton() : juce::Button ("chroma") { setTitle ("About " + productName()); setWantsKeyboardFocus (false); }   // Tab пропускает логотип
    void paintButton (juce::Graphics& g, bool over, bool down) override;
};

// Текстовая кнопка без подложки (имя пресета): наведение, нажатие и фокус — сигнальным цветом текста
class TextLinkButton : public juce::Button
{
public:
    TextLinkButton() : juce::Button ({}) {}
    void paintButton (juce::Graphics& g, bool over, bool down) override;
};

class IconButton : public juce::Button
{
public:
    enum Kind { Settings, Pause, Play, Reset, Prev, Next };
    IconButton (Kind k, const juce::String& title) : juce::Button (title), kind (k) { setTitle (title); }
    void setKind (Kind k) { kind = k; repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool down) override;

private:
    Kind kind;
};

using APVTS = juce::AudioProcessorValueTreeState;

// Ручка: круг, под ним жирное имя и значение (как в Mist)
class ParamKnob : public juce::Component
{
public:
    explicit ParamKnob (const juce::String& label);
    void attach (APVTS& state, const juce::String& paramId);
    void setBig (bool b);   // главная ручка эффекта: крупнее круг, дуга 4 px, подписи крупнее
    void resized() override;
    juce::Slider slider;
    juce::Label label, value;

private:
    void updateValue();
    std::unique_ptr<APVTS::SliderAttachment> attachment;
    bool big = false;
};

class ParamToggle : public juce::ToggleButton
{
public:
    explicit ParamToggle (const juce::String& name) : juce::ToggleButton (name) { getProperties().set ("accent", "live"); }
    void attach (APVTS& state, const juce::String& paramId)
    {
        attachment.reset();
        attachment = std::make_unique<APVTS::ButtonAttachment> (state, paramId, *this);
    }

private:
    std::unique_ptr<APVTS::ButtonAttachment> attachment;
};

// Чип «Chip TMS5100 ⌄»: по клику — меню выбора
class PillChoice : public juce::Component
{
public:
    PillChoice (APVTS& s, const juce::String& paramId, const juce::String& caption);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { openMenu(); }
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void openMenu();
    juce::RangedAudioParameter* param;
    juce::String caption;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int index = 0;
};

// «In ▬ −12 dB»: уровень входа, только показ
class InMeter : public juce::Component
{
public:
    InMeter() { setTitle ("Input level"); setInterceptsMouseClicks (false, false); }
    void setLevel (float peak);
    void paint (juce::Graphics& g) override;

private:
    float level = 0.0f;
};

// «Out ▬ −7 dB»: уровень выхода; перетаскивание — громкость
class OutMeter : public juce::Component
{
public:
    explicit OutMeter (APVTS& state);
    void setLevel (float peak);
    void paint (juce::Graphics& g) override;
    void resized() override;
    juce::Slider gain;   // невидимый слайдер поверх шкалы

private:
    float level = 0.0f;
    std::unique_ptr<APVTS::SliderAttachment> attachment;
};

void disableClickFocus (juce::Component& root);
// ряд ручек одинаковой ширины (не шире 96 px), по центру области
void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> items, int gap = 4);
void repaintTree (juce::Component& root);
juce::String minus();                       // типографский минус
juce::String formatDb (float db, int decimals = 1, float silent = -99.0f);   // «−14.2», «−∞»
} // namespace chroma
