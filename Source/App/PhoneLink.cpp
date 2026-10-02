#include "PhoneLink.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cmath>

namespace arco {

static void closeFd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }
static void noDelay(int fd) { int one = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}
static int waitFd(int fd, bool forRead, int ms) {
    pollfd p { fd, (short) (forRead ? POLLIN : POLLOUT), 0 };
    return ::poll(&p, 1, ms);
}
static bool sendAll(int fd, const void* d, size_t n) {
    auto* p = static_cast<const char*>(d);
    while (n > 0) {
        if (waitFd(fd, false, 500) <= 0) return false;
#ifdef MSG_NOSIGNAL
        auto w = ::send(fd, p, n, MSG_NOSIGNAL);
#else
        auto w = ::send(fd, p, n, 0);
#endif
        if (w <= 0) return false;
        p += w; n -= (size_t) w;
    }
    return true;
}
static bool recvAll(int fd, void* d, size_t n, int timeoutMs) {
    auto* p = static_cast<char*>(d);
    while (n > 0) {
        if (waitFd(fd, true, timeoutMs) <= 0) return false;
        auto r = ::recv(fd, p, n, 0);
        if (r <= 0) return false;
        p += r; n -= (size_t) r;
    }
    return true;
}

// ================================================================== phone side
MicSender::MicSender() : juce::Thread("ARCO mic sender") { data.resize((size_t) fifo.getTotalSize()); }
MicSender::~MicSender() { stop(); }

void MicSender::start() { if (!isThreadRunning()) startThread(juce::Thread::Priority::high); }
void MicSender::stop() { signalThreadShouldExit(); stopThread(2000); int c = clientFd.exchange(-1); closeFd(c); closeFd(serverFd); }

void MicSender::push(const float* x, int n, double sr) {
    float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(x[i]));
    level.store(juce::jmax(pk, level.load() * 0.9f));
    rate.store(sr);
    if (clientFd.load() < 0) return;
    int s1, n1, s2, n2; fifo.prepareToWrite(n, s1, n1, s2, n2);
    if (n1 > 0) std::memcpy(data.data() + s1, x, sizeof(float) * (size_t) n1);
    if (n2 > 0) std::memcpy(data.data() + s2, x + n1, sizeof(float) * (size_t) n2);
    fifo.finishedWrite(n1 + n2);
}

juce::StringArray MicSender::localAddresses() {
    juce::StringArray out;
    ifaddrs* ifs = nullptr;
    if (::getifaddrs(&ifs) != 0) return out;
    for (auto* i = ifs; i != nullptr; i = i->ifa_next) {
        if (i->ifa_addr == nullptr || i->ifa_addr->sa_family != AF_INET) continue;
        juce::String name(i->ifa_name);
        if (name.startsWith("lo") || name.startsWith("pdp") || name.startsWith("utun")) continue;
        char buf[64]; ::inet_ntop(AF_INET, &((sockaddr_in*) i->ifa_addr)->sin_addr, buf, sizeof(buf));
        out.addIfNotAlreadyThere(buf);
    }
    ::freeifaddrs(ifs);
    return out;
}

void MicSender::run() {
    while (!threadShouldExit()) {
        if (serverFd < 0) {
            serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
            int one = 1; ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t) kPhonePort); a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (serverFd < 0 || ::bind(serverFd, (sockaddr*) &a, sizeof(a)) != 0 || ::listen(serverFd, 2) != 0) { closeFd(serverFd); wait(1000); continue; }
        }
        if (waitFd(serverFd, true, 200) <= 0) continue;
        int c = ::accept(serverFd, nullptr, nullptr);
        if (c < 0) continue;
        noDelay(c);
        // what was waiting from before is old: drop it, then say who we are
        for (int w = 0; rate.load() <= 0 && w < 300 && !threadShouldExit(); ++w) wait(10);   // the real rate, not a guess
        if (rate.load() <= 0) { closeFd(c); continue; }
        const double sentRate = rate.load();
        fifo.finishedRead(fifo.getNumReady());
        struct { char magic[4]; uint32_t sr; } head { { 'A', 'R', 'C', 'M' }, (uint32_t) std::lround(sentRate) };
        if (!sendAll(c, &head, sizeof(head))) { closeFd(c); continue; }
        clientFd.store(c);
        int idle = 0;
        while (!threadShouldExit()) {
            if (rate.load() != sentRate) break;          // the phone changed its rate: start again so the Mac knows
            int ready = fifo.getNumReady();
            if (ready == 0) { wait(1); if (++idle > 3000) break; continue; }   // 3 s without sound from the microphone
            idle = 0;
            int s1, n1, s2, n2; fifo.prepareToRead(ready, s1, n1, s2, n2);
            bool ok = (n1 == 0 || sendAll(c, data.data() + s1, sizeof(float) * (size_t) n1))
                   && (n2 == 0 || sendAll(c, data.data() + s2, sizeof(float) * (size_t) n2));
            fifo.finishedRead(n1 + n2);
            if (!ok) break;
            // has the Mac hung up?
            char tmp; if (waitFd(c, true, 0) > 0 && ::recv(c, &tmp, 1, MSG_DONTWAIT) == 0) break;
        }
        clientFd.store(-1);
        closeFd(c);
    }
    closeFd(serverFd);
}

// ================================================================== Mac side
MicReceiver::MicReceiver() : juce::Thread("ARCO phone mic") { data.resize((size_t) fifo.getTotalSize()); }
MicReceiver::~MicReceiver() { disconnect(); }

void MicReceiver::connect(Mode m, const juce::String& h) {
    disconnect();
    host = h.trim();
    modeNow.store(m);
    setStatus(m == Usb ? "looking for the iPhone / iPad on the cable ..." : "connecting to " + host + " ...");
    startThread(juce::Thread::Priority::high);
}
void MicReceiver::disconnect() {
    signalThreadShouldExit(); stopThread(3000);
    modeNow.store(Off); isConnected.store(false); setStatus("off");
}
juce::String MicReceiver::statusText() const { const juce::ScopedLock sl(statusLock); return status; }

// ---- usbmuxd: the service on the Mac that talks to iPhones / iPads on the cable
static juce::String plist(const juce::String& body) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\"><dict>" + body + "<key>ClientVersionString</key><string>arco-1</string><key>ProgName</key><string>ARCO</string></dict></plist>\n";
}
static bool muxSend(int fd, const juce::String& xml, uint32_t tag) {
    auto utf = xml.toStdString();
    uint32_t h[4] = { (uint32_t) (16 + utf.size()), 1u, 8u, tag };
    return sendAll(fd, h, sizeof(h)) && sendAll(fd, utf.data(), utf.size());
}
static std::unique_ptr<juce::XmlElement> muxRecv(int fd) {
    uint32_t h[4];
    if (!recvAll(fd, h, sizeof(h), 1500) || h[0] < 16 || h[0] > (1u << 20)) return nullptr;
    std::string body(h[0] - 16, '\0');
    if (!recvAll(fd, body.data(), body.size(), 1500)) return nullptr;
    return juce::parseXML(juce::String::fromUTF8(body.data(), (int) body.size()));
}
// the value after <key>k</key> in a plist <dict>
static juce::XmlElement* dictGet(juce::XmlElement* dict, const juce::String& k) {
    if (dict == nullptr) return nullptr;
    for (int i = 0; i + 1 < dict->getNumChildElements(); ++i)
        if (dict->getChildElement(i)->hasTagName("key") && dict->getChildElement(i)->getAllSubText() == k) return dict->getChildElement(i + 1);
    return nullptr;
}
static int muxOpen() {
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    sockaddr_un a {}; a.sun_family = AF_UNIX; std::strncpy(a.sun_path, "/var/run/usbmuxd", sizeof(a.sun_path) - 1);
    if (::connect(fd, (sockaddr*) &a, sizeof(a)) != 0) { ::close(fd); return -1; }
    return fd;
}

int MicReceiver::openUsb(juce::String& why) {
    int fd = muxOpen();
    if (fd < 0) { why = "the cable service (usbmuxd) is not there"; return -1; }
    juce::Array<int> ids;
    if (muxSend(fd, plist("<key>MessageType</key><string>ListDevices</string>"), 1))
        if (auto x = muxRecv(fd))
            if (auto* list = dictGet(x->getChildByName("dict"), "DeviceList"))
                for (auto* d : list->getChildIterator()) {
                    auto* props = dictGet(d, "Properties");
                    auto* ct = dictGet(props, "ConnectionType");
                    auto* id = dictGet(props, "DeviceID");
                    if (ct != nullptr && ct->getAllSubText() == "USB" && id != nullptr) ids.addIfNotAlreadyThere(id->getAllSubText().getIntValue());
                }
    ::close(fd);
    if (ids.isEmpty()) { why = "no iPhone / iPad on the cable (plug it in, unlock it, press Trust)"; return -1; }
    int port = ((kPhonePort & 0xff) << 8) | ((kPhonePort >> 8) & 0xff);   // network byte order, as usbmuxd wants it
    for (int id : ids) {
        fd = muxOpen();
        if (fd < 0) break;
        muxSend(fd, plist("<key>MessageType</key><string>Connect</string><key>DeviceID</key><integer>" + juce::String(id)
                          + "</integer><key>PortNumber</key><integer>" + juce::String(port) + "</integer>"), 2);
        auto x = muxRecv(fd);
        auto* num = x ? dictGet(x->getChildByName("dict"), "Number") : nullptr;
        if (num != nullptr && num->getAllSubText().getIntValue() == 0) return fd;   // the cable is now a line to the phone
        ::close(fd);
    }
    why = "the iPhone / iPad is on the cable - open ARCO on it and press MIC FOR MAC";
    return -1;
}

int MicReceiver::openWiFi(juce::String& why) {
    if (host.isEmpty()) { why = "type the address the phone shows"; return -1; }
    addrinfo hints {}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(host.toRawUTF8(), juce::String(kPhonePort).toRawUTF8(), &hints, &res) != 0 || res == nullptr) { why = "unknown address " + host; return -1; }
    int fd = ::socket(res->ai_family, res->ai_socktype, 0);
    int fl = ::fcntl(fd, F_GETFL, 0); ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = ::connect(fd, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) { ::close(fd); why = "no answer from " + host; return -1; }
    int err = 0; socklen_t el = sizeof(err);
    if (waitFd(fd, false, 2500) <= 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0) {
        ::close(fd); why = "no answer from " + host + " (same Wi-Fi? MIC FOR MAC on?)"; return -1; }
    ::fcntl(fd, F_SETFL, fl);
    return fd;
}

bool MicReceiver::readHeader(int fd) {
    struct { char magic[4]; uint32_t sr; } head {};
    if (!recvAll(fd, &head, sizeof(head), 3000) || std::memcmp(head.magic, "ARCM", 4) != 0) return false;
    if (head.sr < 8000 || head.sr > 192000) return false;
    inRate.store((double) head.sr);
    return true;
}

void MicReceiver::run() {
    std::vector<char> buf(16384);
    while (!threadShouldExit()) {
        juce::String why;
        int fd = mode() == Usb ? openUsb(why) : openWiFi(why);
        if (fd >= 0 && !readHeader(fd)) { ::close(fd); fd = -1; why = "the other side is not ARCO"; }
        if (fd < 0) { setStatus(why); isConnected.store(false); wait(1200); continue; }
        noDelay(fd);
        resetReq.store(true);
        isConnected.store(true);
        setStatus(juce::String(mode() == Usb ? "connected by cable" : "connected by Wi-Fi") + ", " + juce::String((int) inRate.load()) + " Hz");
        size_t have = 0;
        while (!threadShouldExit()) {
            if (waitFd(fd, true, 300) <= 0) {
                if (waitFd(fd, true, 1700) <= 0) break;          // 2 s silence on the line: try again
                continue;
            }
            auto r = ::recv(fd, buf.data() + have, buf.size() - have, 0);
            if (r <= 0) break;
            have += (size_t) r;
            size_t nf = have / sizeof(float);
            if (nf > 0) {
                int n = (int) juce::jmin((size_t) fifo.getFreeSpace(), nf);
                int s1, n1, s2, n2; fifo.prepareToWrite(n, s1, n1, s2, n2);
                auto* f = reinterpret_cast<const float*>(buf.data());
                if (n1 > 0) std::memcpy(data.data() + s1, f, sizeof(float) * (size_t) n1);
                if (n2 > 0) std::memcpy(data.data() + s2, f + n1, sizeof(float) * (size_t) n2);
                fifo.finishedWrite(n1 + n2);
                size_t used = nf * sizeof(float);
                std::memmove(buf.data(), buf.data() + used, have - used);
                have -= used;
            }
        }
        ::close(fd);
        isConnected.store(false);
        if (!threadShouldExit()) { setStatus("the phone went away - trying again ..."); wait(500); }
    }
}

void MicReceiver::pull(float* out, int n, double outRate) {
    if (!isConnected.load()) { std::fill(out, out + n, 0.0f); level.store(level.load() * 0.9f); primed = false; return; }
    if (resetReq.exchange(false)) { pos = 0; adj = 0; primed = false; for (auto& h : hist) h = 0; hpX1 = hpX2 = hpY1 = hpY2 = 0; }
    if (outRate != hpRate) {   // 2nd-order high-pass at 28 Hz (below the cello's low C, 65 Hz): only handling noise and rumble go
        hpRate = outRate; double w = 2 * juce::MathConstants<double>::pi * 28.0 / outRate, cw = std::cos(w), al = std::sin(w) / (2 * 0.7071);
        double a0 = 1 + al; hb0 = (1 + cw) / 2 / a0; hb1 = -(1 + cw) / a0; hb2 = hb0; ha1 = -2 * cw / a0; ha2 = (1 - al) / a0; }
    const double ir = inRate.load();
    // keep about 10 ms waiting (more after a dropout), never less than a block
    double targetMs = juce::jlimit(10.0, 40.0, 10.0 + 5.0 * dropouts.load());
    int target = (int) (ir * targetMs / 1000.0) + (int) (n * ir / outRate);
    int fill = fifo.getNumReady();
    if (!primed) {
        if (fill < target) { std::fill(out, out + n, 0.0f); delayMs.store((float) (fill * 1000.0 / ir)); return; }
        primed = true;
    }
    if (fill > target * 4 + 2048) { fifo.finishedRead(fill - target); fill = target; }  // far behind (after a hiccup): jump
    // follow the phone's clock: a tiny speed change keeps the buffer at the target
    double err = (double) (fill - target) / (double) juce::jmax(1, target);
    adj = juce::jlimit(-0.004, 0.004, adj * 0.995 + 0.00002 * err);
    double step = ir / outRate * (1.0 + adj + 0.002 * juce::jlimit(-1.0, 1.0, err));
    int need = (int) std::ceil(n * step) + 2;
    int s1, n1, s2, n2; fifo.prepareToRead(juce::jmin(need, fill), s1, n1, s2, n2);
    int got = n1 + n2, k = 0;
    auto next = [&](float& v) {
        if (k >= got) return false;
        v = k < n1 ? data[(size_t) (s1 + k)] : data[(size_t) (s2 + k - n1)]; ++k; return true; };
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        while (pos >= 1.0) {
            float v; if (!next(v)) { std::fill(out + i, out + n, 0.0f); primed = false; dropouts.fetch_add(1); fifo.finishedRead(k); return; }
            hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hist[3]; hist[3] = v; pos -= 1.0;
        }
        // Hermite between hist[1] and hist[2]
        float t = (float) pos, xm1 = hist[0], x0 = hist[1], x1 = hist[2], x2 = hist[3];
        float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        double xi = ((c3 * t + c2) * t + c1) * t + x0;
        double yv = hb0 * xi + hb1 * hpX1 + hb2 * hpX2 - ha1 * hpY1 - ha2 * hpY2;
        hpX2 = hpX1; hpX1 = xi; hpY2 = hpY1; hpY1 = yv;
        float y = (float) yv;
        out[i] = y; pk = juce::jmax(pk, std::abs(y));
        pos += step;
    }
    fifo.finishedRead(k);
    level.store(juce::jmax(pk, level.load() * 0.9f));
    delayMs.store((float) (fifo.getNumReady() * 1000.0 / ir));
}

} // namespace arco
