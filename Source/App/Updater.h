#pragma once
// UPDATE: looks for a newer ARCO in the private GitHub repository and installs it (Mac).
// It needs a read-only GitHub key (fine-grained token: this repository, Contents: read), kept on this computer only.
#include <JuceHeader.h>
#include "../UI/Theme.h"
#if JUCE_MAC || JUCE_LINUX || JUCE_IOS
 #include <sys/stat.h>
 #include <unistd.h>
#endif

#ifndef ARCO_BUILD
 #define ARCO_BUILD 0
#endif

namespace arco {

class Updater : private juce::Thread {
public:
    Updater() : juce::Thread("ARCO update") {}
    ~Updater() override { stopThread(4000); }
    static constexpr const char* repo = "pacello1103-png/ARCO";
    static int build() { return ARCO_BUILD; }
    static juce::String releasesPage() { return "https://github.com/" + juce::String(repo) + "/releases/latest"; }

    juce::String key() const {
        auto env = juce::SystemStats::getEnvironmentVariable("ARCO_GITHUB_KEY", {});   // for testing
        return env.isNotEmpty() ? env.trim() : keyFile().loadFileAsString().trim(); }
    void setKey(const juce::String& k) {
        keyFile().getParentDirectory().createDirectory();
        keyFile().replaceWithText(k.trim());
       #if JUCE_MAC || JUCE_LINUX
        ::chmod(keyFile().getFullPathName().toRawUTF8(), 0600);
       #endif
    }

    // check (and, when asked, install) in the background; the callbacks come on the message thread
    std::function<void(const juce::String& message, int newBuild, const juce::String& notes)> onChecked;
    std::function<void(const juce::String& message)> onProgress;
    void check() { if (!isThreadRunning()) { installAfter = false; startThread(); } }
    void install() { if (!isThreadRunning()) { installAfter = true; startThread(); } }
    juce::String latestName;

private:
    bool installAfter = false;
    juce::File keyFile() const {
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("ARCO").getChildFile("github-key"); }
    void post(std::function<void()> f) { juce::MessageManager::callAsync(std::move(f)); }
    juce::String headers(const juce::String& accept) const {
        return "Authorization: Bearer " + key() + "\r\nAccept: " + accept + "\r\nUser-Agent: ARCO\r\nX-GitHub-Api-Version: 2022-11-28"; }

    void run() override {
        auto say = [this](const juce::String& m, int b = 0, const juce::String& n = {}) { post([this, m, b, n] { if (onChecked) onChecked(m, b, n); }); };
        if (key().isEmpty()) { say("no key"); return; }
        int code = 0;
        juce::URL api("https://api.github.com/repos/" + juce::String(repo) + "/releases/latest");
        auto in = api.createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                            .withExtraHeaders(headers("application/vnd.github+json")).withConnectionTimeoutMs(10000).withStatusCode(&code));
        if (in == nullptr || code == 401 || code == 403 || code == 404) {
            say(code == 401 || code == 403 || code == 404 ? "the GitHub key does not open ARCO's repository (check it allows Contents: read)" : "no internet connection");
            return; }
        auto json = juce::JSON::parse(in->readEntireStreamAsString());
        auto tag = json["tag_name"].toString();                       // e.g. v1.1.0-b42
        int newBuild = tag.fromLastOccurrenceOf("-b", false, false).getIntValue();
        latestName = json["name"].toString();
        if (!installAfter) { say(newBuild > build() ? "new" : "up to date", newBuild, json["body"].toString()); return; }
        // find the Mac zip
        juce::String assetUrl;
        if (auto* a = json["assets"].getArray()) for (auto& x : *a) if (x["name"].toString() == "ARCO-mac.zip") assetUrl = x["url"].toString();
        if (assetUrl.isEmpty()) { say("this version has no Mac download"); return; }
        post([this] { if (onProgress) onProgress(U("downloading …")); });
        // GitHub answers with a redirect to the file itself (which must be fetched without the key)
        juce::StringPairArray resp; code = 0;
        auto r1 = juce::URL(assetUrl).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                                            .withExtraHeaders(headers("application/octet-stream")).withNumRedirectsToFollow(0)
                                                            .withConnectionTimeoutMs(15000).withResponseHeaders(&resp).withStatusCode(&code));
        juce::String loc = resp["Location"]; if (loc.isEmpty()) loc = resp["location"];
        std::unique_ptr<juce::InputStream> data;
        if (loc.isNotEmpty()) { r1.reset(); data = juce::URL(loc).createInputStream(juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(15000)); }
        else data = std::move(r1);                                   // some answers come directly
        if (data == nullptr) { say("the download did not start"); return; }
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ARCO-update");
        tmp.deleteRecursively(); tmp.createDirectory();
        auto zip = tmp.getChildFile("ARCO-mac.zip");
        {
            juce::FileOutputStream os(zip);
            if (!os.openedOk()) { say("could not write the download"); return; }
            char buf[65536]; juce::int64 total = 0;
            for (;;) {
                if (threadShouldExit()) return;
                auto n = data->read(buf, sizeof(buf));
                if (n <= 0) break;
                os.write(buf, (size_t) n); total += n;
                if ((total & 0xfffff) < 65536) { auto mb = total / 1048576.0; post([this, mb] { if (onProgress) onProgress("downloading " + juce::String(mb, 1) + " MB"); }); }
            }
        }
        if (zip.getSize() < 100000) { say("the download was incomplete"); return; }
       #if JUCE_MAC
        // replace this app once it has quit, then start the new one
        auto app = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
        auto script = tmp.getChildFile("install.sh");
        script.replaceWithText("#!/bin/bash\n"
            "while kill -0 " + juce::String((int) ::getpid()) + " 2>/dev/null; do sleep 0.3; done\n"
            "cd \"" + tmp.getFullPathName() + "\" && /usr/bin/ditto -x -k ARCO-mac.zip new || exit 1\n"
            "[ -d new/ARCO.app ] || exit 1\n"
            "rm -rf \"" + app.getFullPathName() + ".old\"\n"
            "mv \"" + app.getFullPathName() + "\" \"" + app.getFullPathName() + ".old\" && /usr/bin/ditto new/ARCO.app \"" + app.getFullPathName() + "\" && rm -rf \"" + app.getFullPathName() + ".old\"\n"
            "/usr/bin/xattr -cr \"" + app.getFullPathName() + "\"\n"
            "open \"" + app.getFullPathName() + "\"\n");
        std::system(("nohup /bin/bash \"" + script.getFullPathName() + "\" >/dev/null 2>&1 &").toRawUTF8());
        post([] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); });
       #else
        say("downloaded - install it by hand");
       #endif
    }
};

} // namespace arco
