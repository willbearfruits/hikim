#include "MainComponent.h"
#include "UI/Look.h"

#if DG_USE_MIMALLOC
 #include <mimalloc-new-delete.h>   // route global new/delete through mimalloc (one TU only)
#endif

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
    const String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override    { return true; }

    void initialise (const String&) override
    {
        // crashes write a backtrace so bugs come home with evidence
        juce::SystemStats::setApplicationCrashHandler ([] (void*)
        {
            const auto trace = juce::SystemStats::getStackBacktrace();
            juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile (juce::String (names::appName).toLowerCase() + "-crash.log")
                .replaceWithText (juce::Time::getCurrentTime().toString (true, true) + "\n" + trace);
            std::cerr << names::appName << " crashed:\n" << trace << std::endl;
        });

        juce::LookAndFeel::setDefaultLookAndFeel (&Look::get());

        // the whole point of the splash screen
        {
            juce::Image img (juce::Image::ARGB, 720, 400, true);
            juce::Graphics g (img);
            g.fillAll (juce::Colour (0xff0d0d0d));
            g.setColour (juce::Colour (0xff333333));
            g.drawRect (img.getBounds(), 2);
            g.setColour (juce::Colour (0xffe04040));
            g.setFont (juce::Font (juce::FontOptions (110.0f, juce::Font::bold)));
            g.drawText ("HI KIM!", img.getBounds().withTrimmedBottom (60), juce::Justification::centred);
            g.setColour (juce::Colour (0xff808080));
            g.setFont (juce::Font (juce::FontOptions (16.0f)));
            g.drawText ("this is HIKIM - break things beautifully  (press ? inside)",
                        img.getBounds().removeFromBottom (110), juce::Justification::centredTop);
            auto* splash = new juce::SplashScreen (names::appName, img, true);
            splash->deleteAfterDelay (juce::RelativeTime::seconds (2.5), true);
        }

        window = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        window.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};

} // namespace dg

START_JUCE_APPLICATION (dg::DawGlitchApplication)
