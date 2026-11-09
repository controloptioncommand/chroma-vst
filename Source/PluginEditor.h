#pragma once
#include "PluginProcessor.h"
#include "UI/LookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/Views.h"
#include "UI/Rack.h"
#include "UI/OphanPanel.h"
#include "UI/About.h"
#include "UI/Credits.h"
#include "Randomizer.h"

namespace chroma
{
class ChromaEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::DarkModeSettingListener
{
public:
    explicit ChromaEditor (ChromaProcessor&);
    ~ChromaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int kBaseW = 1120, kBaseH = 800;

    // для тестов
    void setThemeForTest (ThemeId id) { setThemeMode (id == ThemeId::Rubber ? ThemeMode::Rubber : ThemeMode::Oxygen); }
    AboutPage& getAboutForTest() { return about; }
    void showAboutForTest (bool show, bool game) { showAbout (show); if (show) about.showGame (game); }
    void setViewForTest (int mode) { mainView.setMode ((MainView::Mode) mode); }
    void selectSlotForTest (int slot) { selectSlot (slot); }
    void setAdvancedForTest (bool adv) { panel.setAdvancedForTest (adv); }
    void rollDiceForTest() { dice.triggerClick(); }
    void showCreditsForTest (bool show) { showCredits (show); }
    CreditsPage& getCreditsForTest() { return creditsPage; }
    RackStrip& getRackForTest() { return rack; }
    void showSamplesForTest (bool show) { if (show) samplesPopup.open (rack.getSelected(), panel.getBounds()); else samplesPopup.close(); }
    void pumpForTest() { timerCallback(); }

private:
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void showAbout (bool show);
    void showCredits (bool show);
    void showPresetMenu();
    void showSettingsMenu();
    void savePreset();
    void setThemeMode (ThemeMode m);
    void applyThemeMode();
    void selectSlot (int slot);
    void loadRandomizerOptions();
    void setRandomizerGroup (RandomizerOptions::Group g, bool allowed);

    ChromaProcessor& proc;
    ChromaLookAndFeel lnf;
    ThemeMode themeMode = ThemeMode::System;
    juce::Component content;   // базовый размер, масштабируется трансформом

    // тулбар
    LogoButton logo;
    IconButton prevPreset { IconButton::Prev, "Previous preset" }, nextPreset { IconButton::Next, "Next preset" },
               settingsButton { IconButton::Settings, "Settings" }, dice { IconButton::Dice, "Randomize" };
    Segmented modeSwitch;   // Serial / Parallel
    TextLinkButton presetName;
    MiniSpectrogram mini;
    InMeter inMeter;
    OutMeter outMeter;
    juce::Rectangle<int> toolbarArea;

    RackStrip rack;
    MainView mainView;
    SlotPanel panel;
    std::array<OphanSnapshot, kSlots> snaps;
    std::array<OttSnapshot, kSlots> ottSnaps;
    std::array<LegionSnapshot, kSlots> legionSnaps;
    std::array<PsalterSnapshot, kSlots> psalterSnaps;
    std::array<int, kSlots> shownTypes {};
    std::array<int, kSlots> shownOrder {};
    int frame = 0;

    RandomizerOptions diceOptions;
    juce::Random rng;

    AboutPage about;
    CreditsPage creditsPage;
    PsalterSamplesPopup samplesPopup { proc };
    std::unique_ptr<juce::AlertWindow> saveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChromaEditor)
};
} // namespace chroma
