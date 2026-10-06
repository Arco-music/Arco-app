#pragma once
// ARCO phones (1.2): iPhones running ARCO are microphones AND remote controls for the station (Mac or iPad).
//   phone (PhoneServer): listens on kPhonePort, announces itself (Bonjour "_arco._tcp"), and over one connection sends
//                        its microphone (audio frames) and what is pressed (command frames); it receives the station's
//                        state (state frames) to show it.
//   station (PhoneHub):  finds the phones (Bonjour on Wi-Fi, usbmuxd on the cable), connects to every one, plays each
//                        phone's microphone on the LIVE input chosen for it (up to 4 phones), and keeps their screens current.
// Frames: 1 byte type + 3 bytes length (little-endian) + payload.  1 = audio (float32 mono), 2 = commands (text lines),
// 3 = state (text lines), 4 = busy (the phone is already connected to a station another way).
#include <JuceHeader.h>
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace arco {

constexpr int kPhonePort = 47123;
enum FrameType { FrAudio = 1, FrCommand = 2, FrState = 3, FrBusy = 4 };

// ------------------------------------------------------------------ the phone
class PhoneServer : private juce::Thread, private juce::AsyncUpdater {
public:
    PhoneServer();
    ~PhoneServer() override;
    void start(const juce::String& myName);
    void stop();
    bool running() const { return isThreadRunning(); }
    bool connected() const { return clientFd.load() >= 0; }
    juce::String name() const { const juce::ScopedLock sl(lock); return myName; }
    // audio thread: the microphone (sent only while micOn)
    void push(const float* x, int n, double sampleRate);
    std::atomic<bool> micOn { true };
    std::atomic<float> level { 0.0f };
    // message thread
    void send(const juce::String& commandLine);                   // e.g. "set vol4 80", "press st_looper playall 1 0", "act 0"
    std::function<void(const juce::StringArray& lines)> onState;  // state lines from the station (message thread)
    std::function<void(bool)> onConnection;                       // connected / disconnected (message thread)
    static juce::StringArray localAddresses();                    // the Wi-Fi addresses (to type on the station if needed)
    void deliverNow() { cancelPendingUpdate(); handleAsyncUpdate(); }   // (tests: no message loop)
private:
    void run() override;
    void handleAsyncUpdate() override;
    juce::AbstractFifo fifo { 1 << 16 };
    std::vector<float> data;
    std::atomic<double> rate { 0.0 };
    std::atomic<int> clientFd { -1 };
    int serverFd = -1;
    juce::CriticalSection lock;
    juce::String myName, outText;
    juce::StringArray inLines;
    bool lastConn = false, connChanged = false;
    void* dnsRef = nullptr;                                         // the Bonjour registration (Apple)
};

// ------------------------------------------------------------------ one phone, seen from the station
class PhoneConn : private juce::Thread {
public:
    enum Mode { Usb = 1, WiFi = 2 };
    PhoneConn(Mode m, const juce::String& hostOrEmpty, int port, int usbDeviceId);
    ~PhoneConn() override;
    void begin() { startThread(juce::Thread::Priority::high); }
    Mode mode;
    juce::String host; int port; int usbId;
    juce::String key() const { return mode == Usb ? "usb:" + juce::String(usbId) : host + ":" + juce::String(port); }
    bool connected() const { return isConnected.load(); }
    bool legacy() const { return isLegacy.load(); }                 // an ARCO 1.1 phone (microphone only)
    juce::String name() const { const juce::ScopedLock sl(lock); return phoneName; }
    juce::String statusText() const { const juce::ScopedLock sl(lock); return status; }
    std::atomic<int> live { 0 };                                    // 0 none, 1-4: plays as that LIVE input
    std::atomic<bool> busyElsewhere { false };
    // audio thread
    void pull(float* out, int n, double outRate);
    std::atomic<float> level { 0.0f }, delayMs { 0.0f };
    std::atomic<int> dropouts { 0 };
    // message thread
    void sendState(const juce::String& lines);
    juce::StringArray takeCommands();
    std::map<std::string, double> sent;                             // what this phone has been told already
    std::map<std::string, juce::String> sentInfo;
    bool needFull = true;
private:
    void run() override;
    int openUsb(juce::String& why);
    int openWiFi(juce::String& why);
    juce::AbstractFifo fifo { 1 << 16 };
    std::vector<float> data;
    std::atomic<bool> isConnected { false }, isLegacy { false };
    std::atomic<double> inRate { 48000.0 };
    mutable juce::CriticalSection lock;
    juce::String phoneName, status, outText;
    juce::StringArray commands;
    void setStatus(const juce::String& s) { const juce::ScopedLock sl(lock); status = s; }
    // resampler state (audio thread)
    double pos = 0.0, adj = 0.0;
    bool primed = false;
    float hist[4] = {};
    double hpX1 = 0, hpX2 = 0, hpY1 = 0, hpY2 = 0, hpRate = 0, hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0;
    std::atomic<bool> resetReq { true };
    void writeAudio(const float* f, int n);
};

// ------------------------------------------------------------------ the station: finds and keeps every phone
class PhoneHub : private juce::Thread {
public:
    PhoneHub();
    ~PhoneHub() override;
    void setEnabled(bool on);                           // finding phones (Bonjour + cable)
    bool enabled() const { return isThreadRunning(); }
    void addAddress(const juce::String& hostPort);      // a phone typed by hand (Wi-Fi without Bonjour)
    void tick();                                        // message thread, ~10x a second: new phones, remembered settings
    juce::OwnedArray<PhoneConn> conns;                  // message thread owns the list; the audio thread reads it under audioLock
    juce::CriticalSection audioLock;
    std::map<juce::String, int> remembered;             // phone name -> LIVE input (saved with this computer's settings)
    void setLive(PhoneConn* c, int live);               // one phone per LIVE input
    PhoneConn* forLive(int live) const;                 // audio thread (under audioLock)
    juce::StringArray manual;                           // typed addresses
private:
    void run() override;                                // discovery
    juce::CriticalSection foundLock;
    struct Found { PhoneConn::Mode mode; juce::String host; int port; int usbId; double lastSeen; };
    std::vector<Found> found;
    std::map<juce::String, double> ignoreUntil;         // endpoints that turned out to be a phone we already have
};

} // namespace arco
