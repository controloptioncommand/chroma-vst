#pragma once
#include "LookAndFeel.h"

namespace chroma
{
// Кто вдохновил эффекты chrōma: автор → эффект. Новый эффект по чужому рецепту — строчку сюда.
struct CreditLine { const char* author; const char* effect; };
inline const std::vector<CreditLine>& credits()
{
    static const std::vector<CreditLine> list = {
        { "@rosco.moon", "Ophan" },
        { "Ableton", "OTT Stack" },
        { "@nest.acoustics", "Psalter" },
    };
    return list;
}

// Страница Credits: на всё окно, как About — фон с точечной сеткой, маленький логотип в точке логотипа тулбара (назад),
// выше центра крупное «Credits» (Medeo Pixel), под ним строки «автор — эффект». Escape — назад.
class CreditsPage : public juce::Component
{
public:
    CreditsPage();
    bool keyPressed (const juce::KeyPress& k) override;
    void paint (juce::Graphics& g) override;
    void resized() override { back.setBounds (backBounds); }
    void setBackLogoBounds (juce::Rectangle<int> r) { backBounds = r; resized(); }
    std::function<void()> onClose;

private:
    struct BackLogo : juce::Button
    {
        BackLogo() : juce::Button ("chroma") { setTitle ("Back to " + productName()); setWantsKeyboardFocus (false); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };
    BackLogo back;
    juce::Rectangle<int> backBounds { 24, 24, 64, 32 };
};
} // namespace chroma
