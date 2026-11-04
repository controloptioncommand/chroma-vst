#include "PluginEditor.h"

namespace chroma
{
using LnF = ChromaLookAndFeel;

namespace
{
std::unique_ptr<juce::PropertiesFile> openPrefs()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "chroma";
    o.filenameSuffix = "settings";
    o.folderName = "chroma";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}
} // namespace

ChromaEditor::ChromaEditor (ChromaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), outMeter (p.apvts), rack (p), panel (p)
{
    Accessibility::refresh();
    auto prefs = openPrefs();
    themeMode = (ThemeMode) juce::jlimit (0, 2, prefs->getIntValue ("themeMode", 0));
    applyThemeMode();
    setLookAndFeel (&lnf);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    setTitle (productName());
    addAndMakeVisible (content);

    // тулбар
    for (juce::Component* c : std::initializer_list<juce::Component*> { &logo, &prevPreset, &presetName, &nextPreset, &mini, &inMeter, &outMeter, &settingsButton })
        content.addAndMakeVisible (c);
    logo.onClick = [this] { showAbout (true); };
    presetName.setTitle ("Preset");
    presetName.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { proc.presets.step (-1); };
    nextPreset.onClick = [this] { proc.presets.step (1); };
    settingsButton.onClick = [this] { showSettingsMenu(); };

    content.addAndMakeVisible (rack);
    content.addAndMakeVisible (mainView);
    content.addAndMakeVisible (panel);
    rack.onSelect = [this] (int s) { selectSlot (s); };
    panel.onSlotChanged = [this] { rack.repaint(); };

    content.addChildComponent (about);
    about.onClose = [this] { showAbout (false); };
    disableClickFocus (*this);

    selectSlot (juce::jlimit (0, kSlots - 1, (int) proc.apvts.state.getProperty ("selectedSlot", 0)));

    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / kBaseH);
    const int scale = juce::jlimit (50, 200, prefs->getIntValue ("scale", 100));
    setSize (kBaseW * scale / 100, kBaseH * scale / 100);
    timerCallback();
    startTimerHz (30);
}

ChromaEditor::~ChromaEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void ChromaEditor::applyThemeMode()
{
    ThemeId id = ThemeId::Oxygen;
    if (themeMode == ThemeMode::Rubber) id = ThemeId::Rubber;
    else if (themeMode == ThemeMode::System) id = juce::Desktop::getInstance().isDarkModeActive() ? ThemeId::Rubber : ThemeId::Oxygen;
    applyTheme (id);
    lnf.refreshColours();
    sendLookAndFeelChange();
    repaintTree (*this);
}

void ChromaEditor::setThemeMode (ThemeMode m)
{
    themeMode = m;
    if (auto prefs = openPrefs()) { prefs->setValue ("themeMode", (int) m); prefs->saveIfNeeded(); }
    applyThemeMode();
}

void ChromaEditor::darkModeSettingChanged()
{
    if (themeMode == ThemeMode::System) applyThemeMode();
}

void ChromaEditor::showAbout (bool show)
{
    if (show == about.isVisible()) return;
    if (show)
    {
        about.setBounds (0, 0, kBaseW, kBaseH);
        about.setBackLogoBounds (logo.getBounds());
        about.setVisible (true);
        about.toFront (true);
        about.grabKeyboardFocus();
    }
    else
    {
        about.showGame (false);
        about.setVisible (false);
    }
}

void ChromaEditor::selectSlot (int slot)
{
    rack.setSelected (slot);
    panel.setSlot (slot);
    proc.apvts.state.setProperty ("selectedSlot", slot, nullptr);
}

// ---------------------------------------------------------------------------
void ChromaEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Factory");
    const auto& fp = factoryPresets();
    const auto cur = proc.presets.getCurrentName();
    for (int i = 0; i < (int) fp.size(); ++i) m.addItem (1 + i, fp[(size_t) i].name, true, cur == fp[(size_t) i].name);
    const auto users = proc.presets.userPresets();
    if (! users.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("User");
        for (int i = 0; i < users.size(); ++i)
            m.addItem (1000 + i, users[i].getFileNameWithoutExtension(), true, cur == users[i].getFileNameWithoutExtension());
    }
    m.addSeparator();
    m.addItem (2000, "Save preset...");
    m.addItem (2001, "Show presets folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [this, users] (int r)
    {
        if (r >= 1 && r < 1000) proc.presets.loadFactory (r - 1);
        else if (r >= 1000 && r < 2000 && r - 1000 < users.size()) proc.presets.loadUser (users[r - 1000]);
        else if (r == 2000) savePreset();
        else if (r == 2001)
        {
            auto dir = PresetManager::userFolder();
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

void ChromaEditor::savePreset()
{
    saveWindow = std::make_unique<juce::AlertWindow> ("Save preset", "The whole rack is saved: slots, order and advanced settings.", juce::MessageBoxIconType::NoIcon);
    saveWindow->addTextEditor ("name", proc.presets.getCurrentName(), "Name");
    saveWindow->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveWindow->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && saveWindow != nullptr)
            if (! proc.presets.save (saveWindow->getTextEditorContents ("name")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, productName(), "Could not save preset.");
        saveWindow.reset();
    }), false);
}

void ChromaEditor::showSettingsMenu()
{
    juce::PopupMenu m, appearance, size;
    appearance.addItem (201, "Match system", true, themeMode == ThemeMode::System);
    appearance.addSeparator();
    appearance.addItem (202, "Oxygen (light)", true, themeMode == ThemeMode::Oxygen);
    appearance.addItem (203, "Rubber (dark)", true, themeMode == ThemeMode::Rubber);
    m.addSubMenu ("Appearance", appearance);
    for (int pct : { 75, 100, 125, 150 })
        size.addItem (100 + pct, juce::String (pct) + "%", true, std::abs (getWidth() - kBaseW * pct / 100) < 4);
    m.addSubMenu ("Size", size);
    m.addSeparator();
    m.addItem (2000, "Save preset...");
    m.addSeparator();
    m.addItem (5, "About " + productName());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton), [this] (int r)
    {
        if (r == 5) showAbout (true);
        else if (r == 2000) savePreset();
        else if (r == 201) setThemeMode (ThemeMode::System);
        else if (r == 202) setThemeMode (ThemeMode::Oxygen);
        else if (r == 203) setThemeMode (ThemeMode::Rubber);
        else if (r > 100 && r < 300)
        {
            const int pct = r - 100;
            if (auto prefs = openPrefs()) { prefs->setValue ("scale", pct); prefs->saveIfNeeded(); }
            setSize (kBaseW * pct / 100, kBaseH * pct / 100);
        }
    });
}

// ---------------------------------------------------------------------------
void ChromaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::window);
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
    LnF::drawPlate (g, toolbarArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawVerticalLine (logo.getRight() + 8, (float) toolbarArea.getY() + 15, (float) toolbarArea.getBottom() - 15);
}

void ChromaEditor::resized()
{
    content.setBounds (0, 0, kBaseW, kBaseH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
    about.setBounds (0, 0, kBaseW, kBaseH);

    auto r = juce::Rectangle<int> (0, 0, kBaseW, kBaseH).reduced (14);
    toolbarArea = r.removeFromTop (52);
    {
        auto h = toolbarArea.reduced (10, 10);
        const auto logoSize = LnF::pixelTextSize (productName(), 1);
        logo.setBounds (h.removeFromLeft (juce::jmax (64, logoSize.x + 8)));
        h.removeFromLeft (18);
        prevPreset.setBounds (h.removeFromLeft (32));
        presetName.setBounds (h.removeFromLeft (170));
        nextPreset.setBounds (h.removeFromLeft (32));
        settingsButton.setBounds (h.removeFromRight (32));
        h.removeFromRight (10);
        outMeter.setBounds (h.removeFromRight (140));
        h.removeFromRight (18);
        inMeter.setBounds (h.removeFromRight (124));
        h.removeFromRight (18);
        mini.setBounds (h.removeFromRight (96).withSizeKeepingCentre (96, 32));
    }
    r.removeFromTop (10);
    rack.setBounds (r.removeFromTop (104));
    r.removeFromTop (10);
    mainView.setBounds (r.removeFromTop (236));
    r.removeFromTop (10);
    panel.setBounds (r);
}

void ChromaEditor::timerCallback()
{
    // выход → главный вид и мини-спектрограмма
    const int ready = proc.scopeFifo.getNumReady();
    if (ready > 0)
    {
        const auto rd = proc.scopeFifo.read (ready);
        if (! about.isVisible())
            for (auto [start, size] : { std::pair<int, int> { rd.startIndex1, rd.blockSize1 }, std::pair<int, int> { rd.startIndex2, rd.blockSize2 } })
                if (size > 0)
                {
                    mainView.push (proc.scopeBuf.data() + start, size);
                    mini.push (proc.scopeBuf.data() + start, size);
                }
    }
    if (about.isVisible()) return;

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    for (int s = 0; s < kSlots; ++s)
        snaps[(size_t) s].read (proc.views[(size_t) s], proc.apvts.getRawParameterValue (slotParam (s, ParamId::mix))->load() / 100.0f);

    // тип слота или порядок сменились (меню, пресет, автоматизация) — обновить панель
    bool changed = false;
    const auto order = proc.getOrder();
    for (int s = 0; s < kSlots; ++s)
    {
        const int t = (int) proc.getSlotType (s);
        if (t != shownTypes[(size_t) s]) { shownTypes[(size_t) s] = t; changed = true; }
        if (order[(size_t) s] != shownOrder[(size_t) s]) { shownOrder[(size_t) s] = order[(size_t) s]; changed = true; }
    }
    if (changed) panel.refresh();

    const int sel = rack.getSelected();
    int pos = 0;
    for (int k = 0; k < kSlots; ++k) if (order[(size_t) k] == sel) pos = k;
    const auto type = proc.getSlotType (sel);
    juce::String caption = "Slot " + juce::String (pos + 1) + midDot();
    if (type == EffectType::Ophan)
    {
        const float rate = proc.apvts.getRawParameterValue (slotParam (sel, ParamId::rate))->load();
        const float drift = proc.apvts.getRawParameterValue (slotParam (sel, ParamId::drift))->load() / 100.0f;
        const float now = rate * std::pow (2.0f, 2.0f * drift * proc.views[(size_t) sel].drift.load());
        caption << "Ophan" << midDot() << (snaps[(size_t) sel].live ? "LFO " + juce::String (now, 2) + " Hz" : juce::String ("off"));
        mainView.setSlot (caption, &snaps[(size_t) sel]);
    }
    else mainView.setSlot (caption + "Empty", nullptr);

    mainView.setSampleRate (sr);
    mainView.tick();
    mini.setSampleRate (sr);
    mini.tick();
    rack.update (snaps, sr);
    if (++frame % 15 == 0) panel.repaint();   // значения Advanced после пресета / копии

    inMeter.setLevel (proc.inPeak.load());
    outMeter.setLevel (proc.outPeak.load());
    presetName.setButtonText (proc.presets.getCurrentName());
    presetName.setDescription ("Preset " + proc.presets.getCurrentName());
    panel.setStatus ("CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + "%");
}
} // namespace chroma
