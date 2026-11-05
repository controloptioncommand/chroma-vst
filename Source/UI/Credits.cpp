#include "Credits.h"

namespace chroma
{
using LnF = ChromaLookAndFeel;

void CreditsPage::BackLogo::paintButton (juce::Graphics& g, bool over, bool)
{
    // как на About: маленький логотип всегда сигнальный, клик — назад
    LnF::drawPixelText (g, productName(), getLocalBounds(), 1, over ? Colours::signal.withAlpha (0.7f) : Colours::signal);
}

CreditsPage::CreditsPage()
{
    setWantsKeyboardFocus (true);
    setTitle ("Credits");
    juce::String d;
    for (auto& c : credits()) d << c.author << " inspired " << c.effect << ". ";
    setDescription (d.trim());
    addAndMakeVisible (back);
    back.onClick = [this] { if (onClose) onClose(); };
}

bool CreditsPage::keyPressed (const juce::KeyPress& k)
{
    if (k != juce::KeyPress::escapeKey) return false;
    if (onClose) onClose();
    return true;
}

void CreditsPage::paint (juce::Graphics& g)
{
    g.fillAll (Colours::window);
    // едва заметная точечная сетка — как на About
    g.setColour (Colours::ink.withAlpha (0.07f));
    for (int y = 12; y < getHeight(); y += 16)
        for (int x = 12; x < getWidth(); x += 16)
            g.fillRect (x, y, 1, 1);

    // «Credits» пиксельным шрифтом: клетка — целые пиксели, ширина ≈ 340 px; выше, чем логотип на About
    const juce::String word ("Credits");
    const int cols = juce::jmax (1, LnF::pixelTextSize (word, 1).x);
    const int cell = juce::jmax (1, juce::roundToInt (340.0f / (float) cols));
    const auto size = LnF::pixelTextSize (word, cell);
    const auto title = juce::Rectangle<int> (0, juce::roundToInt ((float) getHeight() * 0.17f), getWidth(), size.y);
    LnF::drawPixelText (g, word, title, cell, Colours::ink);

    // «автор — эффект»: автор Ink, жирный; эффект — Ink 2
    const auto bold = LnF::font (LnF::Type::title3, true), regular = LnF::font (LnF::Type::title3);
    const auto dash = juce::String::fromUTF8 (" \xe2\x80\x94 ");
    int y = title.getBottom() + 56;
    for (auto& c : credits())
    {
        const juce::String a (c.author), rest = dash + c.effect;
        const float wa = (float) juce::GlyphArrangement::getStringWidth (bold, a);
        const float wr = (float) juce::GlyphArrangement::getStringWidth (regular, rest);
        const float x = ((float) getWidth() - wa - wr) * 0.5f;
        g.setColour (Colours::ink);
        g.setFont (bold);
        g.drawText (a, juce::Rectangle<float> (x, (float) y, wa + 2.0f, 24.0f), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.setFont (regular);
        g.drawText (rest, juce::Rectangle<float> (x + wa, (float) y, wr + 2.0f, 24.0f), juce::Justification::centredLeft);
        y += 34;
    }
}
} // namespace chroma
