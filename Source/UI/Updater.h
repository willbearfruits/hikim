#pragma once
#include "../Common.h"

namespace dg
{

// GitHub-releases update check: notify, then hand off to the browser for the
// platform's package. Best-practice shape for an indie app: silent daily check
// on startup + a manual menu item; never blocks, never nags about a version
// the user chose to skip, fails quietly when offline (or while the repo is
// still private - the API 404s until it goes public).
// EXTEND: in-app download + silent install (Windows: run HIKIM-setup.exe /SILENT).
struct Updater
{
    static constexpr const char* kApiLatest =
        "https://api.github.com/repos/willbearfruits/hikim/releases/latest";

    // numeric semver compare: "0.10.2" > "0.9.9"
    static bool isNewer (const String& latest, const String& current)
    {
        auto parse = [] (const String& v)
        {
            auto t = juce::StringArray::fromTokens (v.trimCharactersAtStart ("vV"), ".", "");
            std::array<int, 3> n {};
            for (int i = 0; i < juce::jmin (3, t.size()); ++i) n[(size_t) i] = t[i].getIntValue();
            return n;
        };
        return parse (latest) > parse (current);
    }

    static void checkAsync (juce::PropertiesFile* props, bool manual)
    {
        if (props != nullptr && ! manual)
        {
            const auto today = juce::Time::getCurrentTime().toISO8601 (false).substring (0, 10);
            if (props->getValue ("lastUpdateCheck") == today) return;   // once a day is plenty
            props->setValue ("lastUpdateCheck", today);
        }

        juce::Thread::launch ([props, manual]
        {
            juce::URL api (kApiLatest);
            auto stream = api.createInputStream (
                juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs (8000)
                    .withExtraHeaders ("User-Agent: HIKIM-updater\r\nAccept: application/vnd.github+json"));

            String tag, pageUrl, assetUrl;
            if (stream != nullptr)
            {
                const auto json = juce::JSON::parse (stream->readEntireStreamAsString());
                tag = json.getProperty ("tag_name", "").toString();
                pageUrl = json.getProperty ("html_url", "").toString();

               #if JUCE_WINDOWS
                const char* wanted = "HIKIM-setup.exe";
               #elif JUCE_LINUX
                const char* wanted = "HIKIM.flatpak";
               #else
                const char* wanted = "";
               #endif
                if (auto* assets = json.getProperty ("assets", {}).getArray())
                    for (const auto& a : *assets)
                        if (a.getProperty ("name", "").toString() == wanted)
                            assetUrl = a.getProperty ("browser_download_url", "").toString();
            }

            juce::MessageManager::callAsync ([props, manual, tag, pageUrl, assetUrl]
            {
                const String current = juce::JUCEApplication::getInstance()->getApplicationVersion();

                if (tag.isEmpty())
                {
                    if (manual)
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                            "Updates", "Couldn't reach the release feed (offline?).\nYou're on " + current + ".");
                    return;
                }
                if (! isNewer (tag, current))
                {
                    if (manual)
                        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                            "Updates", "You're on the latest version (" + current + ").");
                    return;
                }
                if (! manual && props != nullptr && props->getValue ("skipVersion") == tag)
                    return;

                const String dest = assetUrl.isNotEmpty() ? assetUrl
                                  : pageUrl.isNotEmpty()  ? pageUrl
                                  : String ("https://github.com/willbearfruits/hikim/releases/latest");

                juce::AlertWindow::showAsync (
                    juce::MessageBoxOptions()
                        .withIconType (juce::MessageBoxIconType::InfoIcon)
                        .withTitle ("Update available")
                        .withMessage ("HIKIM " + tag.trimCharactersAtStart ("v") + " is out - you're on "
                                      + current + ".\nGet the new build?")
                        .withButton ("Download")
                        .withButton ("Skip this version")
                        .withButton ("Later"),
                    [props, tag, dest] (int r)
                    {
                        if (r == 1)
                            juce::URL (dest).launchInDefaultBrowser();
                        else if (r == 2 && props != nullptr)
                            props->setValue ("skipVersion", tag);
                    });
            });
        });
    }
};

} // namespace dg
