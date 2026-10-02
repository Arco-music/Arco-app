#pragma once
// ARCO phone microphone: an iPhone / iPad running ARCO in "MIC FOR MAC" mode sends its microphone to ARCO on the Mac,
// through the USB cable (lowest delay) or over Wi-Fi.
//   phone (MicSender): listens on kPhonePort, sends "ARCM" + sample rate, then mono float samples.
//   Mac (MicReceiver): connects through usbmuxd (cable) or to the phone's IP (Wi-Fi), keeps a small buffer,
//                      and follows the phone's clock (the two devices never run at exactly the same speed).
#include <JuceHeader.h>
#include <atomic>
#include <vector>

namespace arco {

constexpr int kPhonePort = 47123;

// ------------------------------------------------------------------ phone side
class MicSender : private juce::Thread {
public:
    MicSender();
    ~MicSender() override;
    void start();
    void stop();
    bool running() const { return isThreadRunning(); }
    bool connected() const { return clientFd.load() >= 0; }
    // audio thread
    void push(const float* x, int n, double sampleRate);
    std::atomic<float> level { 0.0f };
    static juce::StringArray localAddresses();      // the Wi-Fi addresses to type on the Mac
private:
    void run() override;
    juce::AbstractFifo fifo { 1 << 16 };
    std::vector<float> data;
    std::atomic<double> rate { 0.0 };                 // 0 until the microphone has actually started
    std::atomic<int> clientFd { -1 };
    int serverFd = -1;
};

// ------------------------------------------------------------------ Mac side
class MicReceiver : private juce::Thread {
public:
    MicReceiver();
    ~MicReceiver() override;
    enum Mode { Off = 0, Usb = 1, WiFi = 2 };
    void connect(Mode m, const juce::String& host = {});   // message thread
    void disconnect();
    Mode mode() const { return (Mode) modeNow.load(); }
    bool connected() const { return isConnected.load(); }
    juce::String statusText() const;
    // audio thread: n samples at the Mac's rate (silence until the phone is there)
    void pull(float* out, int n, double outRate);
    std::atomic<float> level { 0.0f };
    std::atomic<float> delayMs { 0.0f };               // what the buffer adds right now
    std::atomic<int> dropouts { 0 };
private:
    void run() override;
    int openUsb(juce::String& why);
    int openWiFi(juce::String& why);
    bool readHeader(int fd);
    juce::AbstractFifo fifo { 1 << 16 };
    std::vector<float> data;
    std::atomic<int> modeNow { Off };
    std::atomic<bool> isConnected { false };
    std::atomic<double> inRate { 48000.0 };
    juce::String host;
    juce::CriticalSection statusLock;
    juce::String status;
    void setStatus(const juce::String& s) { const juce::ScopedLock sl(statusLock); status = s; }
    // resampler state (audio thread)
    double pos = 0.0, adj = 0.0;
    bool primed = false;
    float hist[4] = {};                               // 4-point (Hermite) interpolation: clean, no dull top end
    double hpX1 = 0, hpX2 = 0, hpY1 = 0, hpY2 = 0, hpRate = 0, hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0;   // 28 Hz rumble filter
    std::atomic<bool> resetReq { true };
};

} // namespace arco
