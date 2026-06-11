#include "MainComponent.h"
#include "UI/Look.h"

namespace dg
{

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow() : DocumentWindow (names::appName, col::bg, DocumentWindow::allButtons)
    {
        mc = new MainComponent();
        setContentOwned (mc, true);
        setUsingNativeTitleBar (true);
        setResizable (true, true);

       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (mc);
       #else
        setMenuBar (mc, 26);
       #endif
        addKeyListener (mc);

        mc->updateTitle = [this] (const String& t) { setName (t); };
        centreWithSize (juce::jmin (1500, getParentMonitorArea().getWidth() - 60),
                        juce::jmin (920, getParentMonitorArea().getHeight() - 80));
        setVisible (true);
    }

    ~MainWindow() override
    {
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu (nullptr);
       #else
        setMenuBar (nullptr);
       #endif
        removeKeyListener (mc);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    MainComponent* mc = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class DawGlitchApplication : public juce::JUCEApplication
{
public:
    const String getApplicationName() override    { return names::appName; }
    const String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    void initialise (const String&) override
    {
        look = std::make_unique<Look>();
        juce::LookAndFeel::setDefaultLookAndFeel (look.get());
        window = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        look.reset();
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<Look> look;
    std::unique_ptr<MainWindow> window;
};

} // namespace dg

START_JUCE_APPLICATION (dg::DawGlitchApplication)
