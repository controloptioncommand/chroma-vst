#pragma once
#include "LookAndFeel.h"

namespace chroma
{
// Версия и дата — показываются на странице About
namespace AboutInfo
{
    inline const char* version = "v3";
    inline const char* repoUrl = "https://github.com/controloptioncommand/chroma-vst";
    inline const char* authorUrl = "https://t.me/nothiingnothingnothing";
    inline const char* date = "8th November 2026";
    inline juce::String word() { return productName(); }
    inline const char* author = "made by nnn";
}

// Пиксельная сова-сипуха 12×13 (тело и крылья — сигнальный цвет, лицо-сердечко и грудка светлые)
void drawOwl (juce::Graphics& g, juce::Point<int> origin, int unit, int frame);   // frame: 0,1 бег, 2 прыжок, 3 сидит

// Бегалка: сова прыгает через пни и стог сена
class GameView : public juce::Component, private juce::Timer
{
public:
    GameView();
    void start();
    void stop();
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent&) override { jump(); }
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

private:
    void timerCallback() override;
    void jump();
    void reset();
    void spawnObstacle();

    enum class State { Ready, Running, Over };
    struct Obstacle { float x; int type; };   // 0 — пень, 1 — стог сена

    static constexpr int kUnit = 3, kGridW = 226, kGridH = 73, kGround = 60, kOwlX = 22;
    State state = State::Ready;
    float owlY = 0.0f, velY = 0.0f, speed = 1.6f, score = 0.0f, nextGap = 60.0f, sinceLast = 0.0f, pebbleOffset = 0.0f;
    int hi = 0, frame = 0;
    std::vector<Obstacle> obstacles;
    juce::Random rng;
};

// Большое «chrōma» на странице About с эффектом «Туман»: пиксели букв сдувает движением курсора,
// через ~1 с они возвращаются. В покое рисуется обычный текст — пиксель в пиксель как раньше.
// Пять быстрых кликов по слову открывают игру.
class FogLogo : public juce::Component, private juce::Timer
{
public:
    FogLogo();
    void setWordArea (juce::Rectangle<float> textRectInParent, float targetWidth);
    std::function<void()> onSecretClicks;

    void paint (juce::Graphics& g) override;
    void resized() override { build(); }
    void mouseMove (const juce::MouseEvent& e) override { blow (e.position, e.eventTime); }
    void mouseDrag (const juce::MouseEvent& e) override { blow (e.position, e.eventTime); }
    void mouseEnter (const juce::MouseEvent& e) override { lastPos = e.position; lastTime = e.eventTime; }
    void mouseDown (const juce::MouseEvent& e) override;

    // для тестов
    int getNumParticles() const { return (int) particles.size(); }
    bool isAnimating() const { return isTimerRunning(); }
    void blowForTest (juce::Point<float> from, juce::Point<float> to, int steps);
    void stepForTest (int frames) { for (int i = 0; i < frames; ++i) timerCallback(); }

private:
    struct Particle
    {
        juce::Rectangle<float> home;
        juce::Point<float> off, vel;
        float t = 0, flight = 0.4f;
        bool moving = false, signal = false;
    };
    void build();
    void blow (juce::Point<float> pos, juce::Time time);
    void push (juce::Point<float> pos, juce::Point<float> velocity);
    void timerCallback() override;

    // сетка пикселей слова в Medeo Pixel (шаг шрифта — 38 единиц при 1000 UPM): ширина и высота в клетках
    int kCols = 1, kRows = 1;
    juce::Rectangle<float> textRect;     // где рисуется текст (локально)
    juce::Rectangle<float> wordBounds;   // контур слова в покое — область кликов
    float targetW = 320.0f, cell = 6.4f;
    juce::Font font { juce::FontOptions() };
    std::vector<Particle> particles;
    juce::Point<float> lastPos;
    juce::Time lastTime;
    juce::uint32 lastClick = 0;
    int clicks = 0;
    juce::Random rng;
};

// Страница About: на всё окно. Логотип, версия/дата, Play → игра.
class AboutPage : public juce::Component
{
public:
    AboutPage();
    bool keyPressed (const juce::KeyPress& k) override;   // Escape — назад в плагин (логотип с клавиатуры недоступен)
    void paint (juce::Graphics& g) override;
    void resized() override;
    void showGame (bool show);
    std::function<void()> onClose;
    FogLogo& getFogForTest() { return logoHit; }
    // маленький логотип — ровно там же, где логотип в тулбаре
    void setBackLogoBounds (juce::Rectangle<int> r) { backBounds = r; resized(); }

private:
    struct BackLogo : juce::Button
    {
        BackLogo() : juce::Button ("chroma") { setTitle ("Back to " + productName()); setWantsKeyboardFocus (false); }   // Tab пропускает логотип
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };
    // Невидимая кликабельная область поверх текста: внешне ничем не выделена
    struct HiddenLink : juce::Component
    {
        HiddenLink (const juce::String& title, const juce::String& u) : url (u)
        {
            setTitle (title);
            setDescription (u);
            setWantsKeyboardFocus (true);
            setMouseClickGrabsKeyboardFocus (false);
        }
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked()) open(); }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) { open(); return true; }
            return false;
        }
        void paint (juce::Graphics& g) override
        {
            if (hasKeyboardFocus (false)) ChromaLookAndFeel::drawFocusRing (g, getLocalBounds().toFloat(), 6.0f);
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        void open() { juce::URL (url).launchInDefaultBrowser(); }
        juce::String url;
    };
    struct CapsuleButton : juce::Button
    {
        explicit CapsuleButton (const juce::String& t) : juce::Button (t) { setTitle (t); }
        void paintButton (juce::Graphics&, bool over, bool down) override;
    };

    void drawVersion (juce::Graphics& g, juce::Rectangle<float> r, bool centred = true);

    BackLogo back;
    FogLogo logoHit;
    HiddenLink versionLink { "chroma on GitHub", AboutInfo::repoUrl };
    HiddenLink authorLink { "Author on Telegram", AboutInfo::authorUrl };
    CapsuleButton closeGame { "Close game" };
    GameView game;
    bool gameMode = false;
    juce::Rectangle<int> backBounds { 24, 24, 64, 32 };
    juce::Rectangle<float> bigLogo, versionLine, hintLine;
};
} // namespace chroma
