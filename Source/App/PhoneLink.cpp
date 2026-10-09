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
#include <set>
#if JUCE_MAC || JUCE_IOS
 #include <dns_sd.h>
 #define ARCO_BONJOUR 1
#else
 #define ARCO_BONJOUR 0
#endif

namespace arco {

static void closeFd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }
static void noDelay(int fd) { int one = 1; ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#ifdef SO_NOSIGPIPE
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}
static int waitFd(int fd, bool forRead, int ms) { pollfd p { fd, (short) (forRead ? POLLIN : POLLOUT), 0 }; return ::poll(&p, 1, ms); }
static bool sendAll(int fd, const void* d, size_t n) {
    auto* p = static_cast<const char*>(d);
    while (n > 0) {
        if (waitFd(fd, false, 800) <= 0) return false;
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
static bool sendFrame(int fd, int type, const void* payload, size_t len) {
    uint8_t h[4] = { (uint8_t) type, (uint8_t) (len & 0xff), (uint8_t) ((len >> 8) & 0xff), (uint8_t) ((len >> 16) & 0xff) };
    return sendAll(fd, h, 4) && (len == 0 || sendAll(fd, payload, len));
}
// takes complete frames out of a receive buffer
template <typename F> static void takeFrames(std::vector<char>& buf, size_t& have, F&& onFrame) {
    size_t at = 0;
    while (have - at >= 4) {
        auto* h = reinterpret_cast<const uint8_t*>(buf.data() + at);
        size_t len = (size_t) h[1] | ((size_t) h[2] << 8) | ((size_t) h[3] << 16);
        if (have - at - 4 < len) { if (len + 4 > buf.size()) buf.resize(len + 4 + 4096); break; }
        onFrame((int) h[0], buf.data() + at + 4, len);
        at += 4 + len;
    }
    if (at > 0) { std::memmove(buf.data(), buf.data() + at, have - at); have -= at; }
}

// ================================================================== the phone
PhoneServer::PhoneServer() : juce::Thread("ARCO phone link") { data.resize((size_t) fifo.getTotalSize()); }
PhoneServer::~PhoneServer() { stop(); }

void PhoneServer::start(const juce::String& n) {
    { const juce::ScopedLock sl(lock); myName = n.substring(0, 47); }
    if (!isThreadRunning()) startThread(juce::Thread::Priority::high);
}
void PhoneServer::stop() {
    signalThreadShouldExit(); stopThread(3000);
    int c = clientFd.exchange(-1); closeFd(c); closeFd(serverFd);
#if ARCO_BONJOUR
    if (dnsRef != nullptr) { DNSServiceRefDeallocate((DNSServiceRef) dnsRef); dnsRef = nullptr; }
#endif
}

void PhoneServer::push(const float* x, int n, double sr) {
    float pk = 0; for (int i = 0; i < n; ++i) pk = juce::jmax(pk, std::abs(x[i]));
    level.store(juce::jmax(pk, level.load() * 0.9f));
    rate.store(sr);
    if (clientFd.load() < 0 || !micOn.load()) return;
    int s1, n1, s2, n2; fifo.prepareToWrite(n, s1, n1, s2, n2);
    if (n1 > 0) std::memcpy(data.data() + s1, x, sizeof(float) * (size_t) n1);
    if (n2 > 0) std::memcpy(data.data() + s2, x + n1, sizeof(float) * (size_t) n2);
    fifo.finishedWrite(n1 + n2);
}

void PhoneServer::send(const juce::String& line) { const juce::ScopedLock sl(lock); outText << line << "\n"; }

void PhoneServer::handleAsyncUpdate() {
    juce::StringArray lines; bool changed, conn;
    { const juce::ScopedLock sl(lock); lines.swapWith(inLines); changed = connChanged; connChanged = false; conn = lastConn; }
    if (changed && onConnection) onConnection(conn);
    if (!lines.isEmpty() && onState) onState(lines);
}

juce::StringArray PhoneServer::localAddresses() {
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

#if ARCO_BONJOUR
static void DNSSD_API regReply(DNSServiceRef, DNSServiceFlags, DNSServiceErrorType, const char*, const char*, const char*, void*) {}
#endif

void PhoneServer::run() {
    auto header = [this](int c, double sr) {
        struct { char magic[4]; uint32_t sr; char name[48]; } h {};
        std::memcpy(h.magic, "ARC2", 4); h.sr = (uint32_t) std::lround(sr);
        auto nm = name().toStdString(); std::strncpy(h.name, nm.c_str(), sizeof(h.name) - 1);
        return sendAll(c, &h, sizeof(h));
    };
    auto setConn = [this](bool on) { { const juce::ScopedLock sl(lock); lastConn = on; connChanged = true; } triggerAsyncUpdate(); };
    std::vector<char> rbuf(65536); size_t have = 0;
    while (!threadShouldExit()) {
        if (serverFd < 0) {
            serverFd = ::socket(AF_INET, SOCK_STREAM, 0);
            int one = 1; ::setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
            sockaddr_in a {}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t) kPhonePort); a.sin_addr.s_addr = htonl(INADDR_ANY);
            if (serverFd < 0 || ::bind(serverFd, (sockaddr*) &a, sizeof(a)) != 0 || ::listen(serverFd, 4) != 0) { closeFd(serverFd); wait(1000); continue; }
#if ARCO_BONJOUR
            if (dnsRef == nullptr) {
                DNSServiceRef ref = nullptr;
                auto nm = name().toStdString();
                if (DNSServiceRegister(&ref, 0, 0, nm.c_str(), "_arco._tcp", nullptr, nullptr, htons((uint16_t) kPhonePort), 0, nullptr, regReply, nullptr) == kDNSServiceErr_NoError) dnsRef = ref;
            }
#endif
        }
#if ARCO_BONJOUR
        if (dnsRef != nullptr) { int dfd = DNSServiceRefSockFD((DNSServiceRef) dnsRef); if (dfd >= 0 && waitFd(dfd, true, 0) > 0) DNSServiceProcessResult((DNSServiceRef) dnsRef); }
#endif
        if (waitFd(serverFd, true, 200) <= 0) continue;
        int c = ::accept(serverFd, nullptr, nullptr);
        if (c < 0) continue;
        noDelay(c);
        for (int w = 0; rate.load() <= 0 && w < 100 && !threadShouldExit(); ++w) wait(10);
        double sentRate = rate.load() > 0 ? rate.load() : 48000.0;
        fifo.finishedRead(fifo.getNumReady());
        if (!header(c, sentRate)) { closeFd(c); continue; }
        clientFd.store(c); have = 0;
        setConn(true);
        double lastSend = juce::Time::getMillisecondCounterHiRes();
        while (!threadShouldExit()) {
            bool did = false;
            // another station (or the same one another way) knocks: say we are busy
            if (waitFd(serverFd, true, 0) > 0) { int c2 = ::accept(serverFd, nullptr, nullptr); if (c2 >= 0) { header(c2, sentRate); sendFrame(c2, FrBusy, nullptr, 0); ::close(c2); } }
            if (rate.load() > 0 && rate.load() != sentRate) {     // the microphone changed its rate
                sentRate = rate.load();
                auto t = ("#rate " + juce::String((int) std::lround(sentRate)) + "\n").toStdString();
                if (!sendFrame(c, FrCommand, t.data(), t.size())) break;
            }
            int ready = fifo.getNumReady();
            if (ready > 0) {
                int s1, n1, s2, n2; fifo.prepareToRead(juce::jmin(ready, 4096), s1, n1, s2, n2);
                bool ok = true;
                if (micOn.load()) {
                    std::vector<float> tmp((size_t) (n1 + n2));
                    if (n1) std::memcpy(tmp.data(), data.data() + s1, sizeof(float) * (size_t) n1);
                    if (n2) std::memcpy(tmp.data() + n1, data.data() + s2, sizeof(float) * (size_t) n2);
                    ok = sendFrame(c, FrAudio, tmp.data(), tmp.size() * sizeof(float));
                }
                fifo.finishedRead(n1 + n2);
                if (!ok) break;
                did = true; lastSend = juce::Time::getMillisecondCounterHiRes();
            }
            juce::String out; { const juce::ScopedLock sl(lock); out.swapWith(outText); }
            if (out.isNotEmpty()) { auto t = out.toStdString(); if (!sendFrame(c, FrCommand, t.data(), t.size())) break; did = true; lastSend = juce::Time::getMillisecondCounterHiRes(); }
            if (juce::Time::getMillisecondCounterHiRes() - lastSend > 700) { if (!sendFrame(c, FrCommand, nullptr, 0)) break; lastSend = juce::Time::getMillisecondCounterHiRes(); }
            // what the station says
            if (waitFd(c, true, did ? 0 : 2) > 0) {
                if (have + 65536 > rbuf.size()) rbuf.resize(have + 65536);
                auto r = ::recv(c, rbuf.data() + have, rbuf.size() - have, 0);
                if (r <= 0) break;
                have += (size_t) r;
                juce::StringArray got;
                takeFrames(rbuf, have, [&](int type, const char* p, size_t len) {
                    if (type == FrState && len > 0) got.addLines(juce::String::fromUTF8(p, (int) len));
                    else if (type == FrListen && len > 4) {
                        uint32_t r; std::memcpy(&r, p, 4);
                        std::vector<int16_t> pcm((len - 4) / 2); std::memcpy(pcm.data(), p + 4, pcm.size() * 2);
                        listen.write(pcm.data(), (int) pcm.size() / 2, (double) r);
                    } });
                if (!got.isEmpty()) { { const juce::ScopedLock sl(lock); inLines.addArray(got); } triggerAsyncUpdate(); }
            }
        }
        clientFd.store(-1);
        closeFd(c);
        listen.reset();
        setConn(false);
    }
    closeFd(serverFd);
}

// ================================================================== one phone, seen from the station
PhoneConn::PhoneConn(Mode m, const juce::String& h, int p, int id) : juce::Thread("ARCO phone"), mode(m), host(h), port(p), usbId(id) {
    data.resize((size_t) fifo.getTotalSize());
    ldata.resize((size_t) lfifo.getTotalSize() * 2);
    setStatus(m == Usb ? "on the cable, connecting ..." : "connecting to " + h + " ...");
}
PhoneConn::~PhoneConn() { signalThreadShouldExit(); stopThread(4000); }

void PhoneConn::sendState(const juce::String& lines) { const juce::ScopedLock sl(lock); outText << lines; }
juce::StringArray PhoneConn::takeCommands() { juce::StringArray c; const juce::ScopedLock sl(lock); c.swapWith(commands); return c; }

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
static juce::Array<int> usbDevices() {
    juce::Array<int> ids;
    int fd = muxOpen();
    if (fd < 0) return ids;
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
    return ids;
}

int PhoneConn::openUsb(juce::String& why) {
    int fd = muxOpen();
    if (fd < 0) { why = "the cable service (usbmuxd) is not there"; return -1; }
    int p = ((port & 0xff) << 8) | ((port >> 8) & 0xff);
    muxSend(fd, plist("<key>MessageType</key><string>Connect</string><key>DeviceID</key><integer>" + juce::String(usbId)
                      + "</integer><key>PortNumber</key><integer>" + juce::String(p) + "</integer>"), 2);
    auto x = muxRecv(fd);
    auto* num = x ? dictGet(x->getChildByName("dict"), "Number") : nullptr;
    if (num != nullptr && num->getAllSubText().getIntValue() == 0) return fd;
    ::close(fd);
    why = "on the cable - open ARCO on it";
    return -1;
}

int PhoneConn::openWiFi(juce::String& why) {
    addrinfo hints {}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    if (::getaddrinfo(host.toRawUTF8(), juce::String(port).toRawUTF8(), &hints, &res) != 0 || res == nullptr) { why = "unknown address " + host; return -1; }
    int fd = ::socket(res->ai_family, res->ai_socktype, 0);
    int fl = ::fcntl(fd, F_GETFL, 0); ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    int r = ::connect(fd, res->ai_addr, res->ai_addrlen);
    ::freeaddrinfo(res);
    if (r != 0 && errno != EINPROGRESS) { ::close(fd); why = "no answer from " + host; return -1; }
    int err = 0; socklen_t el = sizeof(err);
    if (waitFd(fd, false, 2500) <= 0 || ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0) { ::close(fd); why = "no answer from " + host; return -1; }
    ::fcntl(fd, F_SETFL, fl);
    return fd;
}

void PhoneConn::writeAudio(const float* f, int nf) {
    int n = juce::jmin(fifo.getFreeSpace(), nf);
    int s1, n1, s2, n2; fifo.prepareToWrite(n, s1, n1, s2, n2);
    if (n1 > 0) std::memcpy(data.data() + s1, f, sizeof(float) * (size_t) n1);
    if (n2 > 0) std::memcpy(data.data() + s2, f + n1, sizeof(float) * (size_t) n2);
    fifo.finishedWrite(n1 + n2);
}

void PhoneConn::run() {
    std::vector<char> buf(65536);
    int failures = 0;
    while (!threadShouldExit()) {
        juce::String why;
        int fd = mode == Usb ? openUsb(why) : openWiFi(why);
        if (fd >= 0) {
            char magic[4];
            if (!recvAll(fd, magic, 4, 3000)) { ::close(fd); fd = -1; why = "the other side is not ARCO"; }
            else if (std::memcmp(magic, "ARC2", 4) == 0) {
                struct { uint32_t sr; char name[48]; } h {};
                if (!recvAll(fd, &h, sizeof(h), 3000)) { ::close(fd); fd = -1; why = "no answer"; }
                else { inRate.store(juce::jlimit(8000.0, 192000.0, (double) h.sr)); h.name[47] = 0; const juce::ScopedLock sl(lock); phoneName = juce::String::fromUTF8(h.name); isLegacy = false; }
            } else if (std::memcmp(magic, "ARCM", 4) == 0) {
                uint32_t sr = 0;
                if (!recvAll(fd, &sr, 4, 3000)) { ::close(fd); fd = -1; why = "no answer"; }
                else { inRate.store(juce::jlimit(8000.0, 192000.0, (double) sr)); const juce::ScopedLock sl(lock); if (phoneName.isEmpty()) phoneName = "phone (ARCO 1.1)"; isLegacy = true; }
            } else { ::close(fd); fd = -1; why = "the other side is not ARCO"; }
        }
        if (fd < 0) { setStatus(why); isConnected.store(false); wait(juce::jmin(5000, 1200 + 400 * failures++)); continue; }
        failures = 0;
        noDelay(fd);
        resetReq.store(true);
        busyElsewhere.store(false);
        { const juce::ScopedLock sl(lock); commands.clear(); outText.clear(); }
        needFull = true;
        isConnected.store(true);
        setStatus(juce::String(mode == Usb ? "connected by cable" : "connected by Wi-Fi"));
        size_t have = 0;
        double lastIn = juce::Time::getMillisecondCounterHiRes();
        while (!threadShouldExit()) {
            // send the state (the phone's screen)
            juce::String out; { const juce::ScopedLock sl(lock); out.swapWith(outText); }
            if (out.isNotEmpty() && !isLegacy.load()) { auto t = out.toStdString(); if (!sendFrame(fd, FrState, t.data(), t.size())) break; }
            // the sound this phone listens to
            if (listenMix.load() > 0 && !isLegacy.load()) {
                int ready = lfifo.getNumReady();
                if (ready >= 128) {
                    int s1, n1, s2, n2; lfifo.prepareToRead(juce::jmin(ready, 2048), s1, n1, s2, n2);
                    std::vector<char> pl(4 + (size_t) (n1 + n2) * 4);
                    uint32_t r = (uint32_t) std::lround(listenRate.load()); std::memcpy(pl.data(), &r, 4);
                    if (n1) std::memcpy(pl.data() + 4, ldata.data() + (size_t) s1 * 2, (size_t) n1 * 4);
                    if (n2) std::memcpy(pl.data() + 4 + (size_t) n1 * 4, ldata.data() + (size_t) s2 * 2, (size_t) n2 * 4);
                    lfifo.finishedRead(n1 + n2);
                    if (!sendFrame(fd, FrListen, pl.data(), pl.size())) break;
                }
            } else lfifo.finishedRead(lfifo.getNumReady());
            if (waitFd(fd, true, listenMix.load() > 0 ? 4 : 20) <= 0) {
                if (juce::Time::getMillisecondCounterHiRes() - lastIn > 2500) break;     // the phone went quiet
                continue;
            }
            if (have + 65536 > buf.size()) buf.resize(have + 65536);
            auto r = ::recv(fd, buf.data() + have, buf.size() - have, 0);
            if (r <= 0) break;
            have += (size_t) r; lastIn = juce::Time::getMillisecondCounterHiRes();
            if (isLegacy.load()) {                                        // 1.1: just floats
                size_t nf = have / sizeof(float);
                if (nf > 0) { writeAudio(reinterpret_cast<const float*>(buf.data()), (int) nf); size_t used = nf * sizeof(float); std::memmove(buf.data(), buf.data() + used, have - used); have -= used; }
                continue;
            }
            bool busy = false;
            takeFrames(buf, have, [&](int type, const char* p, size_t len) {
                if (type == FrAudio) writeAudio(reinterpret_cast<const float*>(p), (int) (len / sizeof(float)));
                else if (type == FrCommand && len > 0) {
                    juce::StringArray lines; lines.addLines(juce::String::fromUTF8(p, (int) len));
                    const juce::ScopedLock sl(lock);
                    for (auto& l : lines) {
                        if (l.startsWith("#rate ")) { inRate.store(juce::jlimit(8000.0, 192000.0, (double) l.substring(6).getIntValue())); resetReq.store(true); }
                        else if (l.trim().isNotEmpty()) commands.add(l.trim());
                    }
                } else if (type == FrBusy) busy = true;
            });
            if (busy) { busyElsewhere.store(true); break; }
        }
        ::close(fd);
        isConnected.store(false);
        if (threadShouldExit()) break;
        if (busyElsewhere.load()) { setStatus("already connected another way"); wait(3000); }
        else { setStatus("the phone went away - trying again ..."); wait(800); }
    }
}

void PhoneConn::pull(float* out, int n, double outRate) {
    if (!isConnected.load()) { std::fill(out, out + n, 0.0f); level.store(level.load() * 0.9f); primed = false; return; }
    if (resetReq.exchange(false)) { pos = 0; adj = 0; primed = false; for (auto& h : hist) h = 0; hpX1 = hpX2 = hpY1 = hpY2 = 0; }
    if (outRate != hpRate) {   // 2nd-order high-pass at 28 Hz (below the cello's low C, 65 Hz): only handling noise and rumble go
        hpRate = outRate; double w = 2 * juce::MathConstants<double>::pi * 28.0 / outRate, cw = std::cos(w), al = std::sin(w) / (2 * 0.7071);
        double a0 = 1 + al; hb0 = (1 + cw) / 2 / a0; hb1 = -(1 + cw) / a0; hb2 = hb0; ha1 = -2 * cw / a0; ha2 = (1 - al) / a0; }
    const double ir = inRate.load();
    double targetMs = juce::jlimit(10.0, 40.0, 10.0 + 5.0 * dropouts.load());
    int target = (int) (ir * targetMs / 1000.0) + (int) (n * ir / outRate);
    int fill = fifo.getNumReady();
    if (!primed) {
        if (fill < target) { std::fill(out, out + n, 0.0f); delayMs.store((float) (fill * 1000.0 / ir)); return; }
        primed = true;
    }
    if (fill > target * 4 + 2048) { fifo.finishedRead(fill - target); fill = target; }
    double err = (double) (fill - target) / (double) juce::jmax(1, target);
    adj = juce::jlimit(-0.004, 0.004, adj * 0.995 + 0.00002 * err);
    double step = ir / outRate * (1.0 + adj + 0.002 * juce::jlimit(-1.0, 1.0, err));
    int need = (int) std::ceil(n * step) + 2;
    int s1, n1, s2, n2; fifo.prepareToRead(juce::jmin(need, fill), s1, n1, s2, n2);
    int got = n1 + n2, k = 0;
    auto next = [&](float& v) { if (k >= got) return false; v = k < n1 ? data[(size_t) (s1 + k)] : data[(size_t) (s2 + k - n1)]; ++k; return true; };
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        while (pos >= 1.0) {
            float v; if (!next(v)) { std::fill(out + i, out + n, 0.0f); primed = false; dropouts.fetch_add(1); fifo.finishedRead(k); return; }
            hist[0] = hist[1]; hist[1] = hist[2]; hist[2] = hist[3]; hist[3] = v; pos -= 1.0;
        }
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

void PhoneConn::pushListen(const float* l, const float* r, int n, double rate) {
    if (listenMix.load() <= 0 || !isConnected.load()) return;
    listenRate.store(rate);
    if (lfifo.getFreeSpace() < n) return;                          // the network is behind: skip (the phone catches up)
    int s1, n1, s2, n2; lfifo.prepareToWrite(n, s1, n1, s2, n2);
    auto put = [&](int start, int count, int from) {
        for (int i = 0; i < count; ++i) {
            ldata[(size_t) (start + i) * 2] = (int16_t) std::lround(juce::jlimit(-1.0f, 1.0f, l[from + i]) * 32767.0f);
            ldata[(size_t) (start + i) * 2 + 1] = (int16_t) std::lround(juce::jlimit(-1.0f, 1.0f, r[from + i]) * 32767.0f);
        } };
    put(s1, n1, 0); put(s2, n2, n1);
    lfifo.finishedWrite(n1 + n2);
}

// ================================================================== a stereo stream from the network
void StreamPlayer::write(const int16_t* lr, int frames, double rate) {
    if (rate >= 8000 && rate <= 192000 && rate != inRate.load()) { inRate.store(rate); resetReq.store(true); }
    int n = juce::jmin(frames, fifo.getFreeSpace());
    int s1, n1, s2, n2; fifo.prepareToWrite(n, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) { data[(size_t) (s1 + i) * 2] = lr[i * 2] / 32768.0f; data[(size_t) (s1 + i) * 2 + 1] = lr[i * 2 + 1] / 32768.0f; }
    for (int i = 0; i < n2; ++i) { data[(size_t) (s2 + i) * 2] = lr[(n1 + i) * 2] / 32768.0f; data[(size_t) (s2 + i) * 2 + 1] = lr[(n1 + i) * 2 + 1] / 32768.0f; }
    fifo.finishedWrite(n1 + n2);
}

void StreamPlayer::pull(float* L, float* R, int n, double outRate) {
    auto silence = [&] { std::fill(L, L + n, 0.0f); std::fill(R, R + n, 0.0f); level.store(level.load() * 0.9f); };
    if (resetReq.exchange(false)) { primed = false; pos = 0; adj = 0; for (int i = 0; i < 4; ++i) hl[i] = hr[i] = 0; fifo.finishedRead(fifo.getNumReady()); }
    const double ir = inRate.load();
    // Wi-Fi is bumpy: keep 40-120 ms in hand (more after every hiccup)
    double targetMs = juce::jlimit(40.0, 120.0, 40.0 + 10.0 * underruns);
    int target = (int) (ir * targetMs / 1000.0) + (int) (n * ir / outRate);
    int fill = fifo.getNumReady();
    if (!primed) { if (fill < target) { silence(); return; } primed = true; }
    if (fill > target * 3 + 4096) { fifo.finishedRead(fill - target); fill = target; }
    double err = (double) (fill - target) / (double) juce::jmax(1, target);
    adj = juce::jlimit(-0.004, 0.004, adj * 0.995 + 0.00002 * err);
    double step = ir / outRate * (1.0 + adj + 0.002 * juce::jlimit(-1.0, 1.0, err));
    int need = (int) std::ceil(n * step) + 2;
    int s1, n1, s2, n2; fifo.prepareToRead(juce::jmin(need, fill), s1, n1, s2, n2);
    int got = n1 + n2, k = 0;
    float pk = 0;
    for (int i = 0; i < n; ++i) {
        while (pos >= 1.0) {
            if (k >= got) { std::fill(L + i, L + n, 0.0f); std::fill(R + i, R + n, 0.0f); primed = false; ++underruns; fifo.finishedRead(k); return; }
            int at = k < n1 ? s1 + k : s2 + k - n1; ++k;
            for (int j = 0; j < 3; ++j) { hl[j] = hl[j + 1]; hr[j] = hr[j + 1]; }
            hl[3] = data[(size_t) at * 2]; hr[3] = data[(size_t) at * 2 + 1]; pos -= 1.0;
        }
        float t = (float) pos;
        auto cub = [t](const float* h) { float c1 = 0.5f * (h[2] - h[0]), c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3], c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
                                         return ((c3 * t + c2) * t + c1) * t + h[1]; };
        L[i] = cub(hl); R[i] = cub(hr);
        pk = juce::jmax(pk, std::abs(L[i]), std::abs(R[i]));
        pos += step;
    }
    fifo.finishedRead(k);
    level.store(juce::jmax(pk, level.load() * 0.9f));
    delayMs.store((float) (fifo.getNumReady() * 1000.0 / ir));
}

// ================================================================== the station
PhoneHub::PhoneHub() : juce::Thread("ARCO phone finder") {}
PhoneHub::~PhoneHub() {
    signalThreadShouldExit(); stopThread(4000);
    juce::OwnedArray<PhoneConn> old;
    { const juce::ScopedLock sl(audioLock); old.swapWith(conns); }
}
void PhoneHub::setEnabled(bool on) { if (on && !isThreadRunning()) startThread(); else if (!on && isThreadRunning()) { signalThreadShouldExit(); stopThread(4000); } }
void PhoneHub::addAddress(const juce::String& hp) {
    auto h = hp.trim(); if (h.isEmpty()) return;
    if (!h.containsChar(':')) h << ":" << kPhonePort;
    manual.addIfNotAlreadyThere(h);
}

PhoneConn* PhoneHub::forLive(int live) const {
    for (auto* c : conns) if (c->live.load() == live && c->connected()) return c;
    return nullptr;
}
void PhoneHub::setLive(PhoneConn* c, int live) {
    if (live > 0) for (auto* o : conns) if (o != c && o->live.load() == live) { o->live.store(0); if (o->name().isNotEmpty()) remembered[o->name()] = 0; }
    c->live.store(live);
    if (c->name().isNotEmpty()) remembered[c->name()] = live;
}

#if ARCO_BONJOUR
struct Browse { struct E { std::string name, type, domain; uint32_t ifIndex; }; std::vector<E> adds; std::set<std::string> gone; };
static void DNSSD_API browseReply(DNSServiceRef, DNSServiceFlags flags, uint32_t ifIndex, DNSServiceErrorType err, const char* name, const char* type, const char* domain, void* ctx) {
    if (err != kDNSServiceErr_NoError) return;
    auto* b = static_cast<Browse*>(ctx);
    if (flags & kDNSServiceFlagsAdd) b->adds.push_back({ name, type, domain, ifIndex }); else b->gone.insert(name);
}
struct Resolved { std::string host; int port = 0; bool done = false; };
static void DNSSD_API resolveReply(DNSServiceRef, DNSServiceFlags, uint32_t, DNSServiceErrorType err, const char*, const char* target, uint16_t portNet, uint16_t, const unsigned char*, void* ctx) {
    auto* r = static_cast<Resolved*>(ctx); r->done = true;
    if (err == kDNSServiceErr_NoError) { r->host = target; r->port = ntohs(portNet); }
}
#endif

void PhoneHub::run() {
#if ARCO_BONJOUR
    Browse browse; DNSServiceRef bref = nullptr;
    DNSServiceBrowse(&bref, 0, 0, "_arco._tcp", nullptr, browseReply, &browse);
    std::map<std::string, std::pair<juce::String, int>> resolved;    // service name -> host, port
    std::map<std::string, Browse::E> known;
#endif
    double lastUsb = 0;
    while (!threadShouldExit()) {
        double now = juce::Time::getMillisecondCounterHiRes();
#if ARCO_BONJOUR
        if (bref != nullptr) {
            int fd = DNSServiceRefSockFD(bref);
            while (fd >= 0 && waitFd(fd, true, 0) > 0) DNSServiceProcessResult(bref);
            for (auto& e : browse.adds) known[e.name] = e;
            for (auto& g : browse.gone) { known.erase(g); resolved.erase(g); }
            browse.adds.clear(); browse.gone.clear();
            for (auto& kv : known) {
                if (resolved.count(kv.first)) continue;
                Resolved r; DNSServiceRef rref = nullptr;
                if (DNSServiceResolve(&rref, 0, kv.second.ifIndex, kv.second.name.c_str(), kv.second.type.c_str(), kv.second.domain.c_str(), resolveReply, &r) == kDNSServiceErr_NoError) {
                    int rfd = DNSServiceRefSockFD(rref);
                    if (rfd >= 0 && waitFd(rfd, true, 2000) > 0) DNSServiceProcessResult(rref);
                    DNSServiceRefDeallocate(rref);
                }
                if (!r.done || r.host.empty()) continue;
                // the IPv4 address of the phone
                addrinfo hints {}, *res = nullptr; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
                if (::getaddrinfo(r.host.c_str(), nullptr, &hints, &res) == 0 && res != nullptr) {
                    char b[64]; ::inet_ntop(AF_INET, &((sockaddr_in*) res->ai_addr)->sin_addr, b, sizeof(b));
                    resolved[kv.first] = { juce::String(b), r.port };
                    ::freeaddrinfo(res);
                }
            }
            const juce::ScopedLock sl(foundLock);
            for (auto& kv : resolved) {
                bool have = false;
                for (auto& f : found) if (f.mode == PhoneConn::WiFi && f.host == kv.second.first && f.port == kv.second.second) { f.lastSeen = now; have = true; }
                if (!have) found.push_back({ PhoneConn::WiFi, kv.second.first, kv.second.second, 0, now });
            }
        }
#endif
#if JUCE_MAC || JUCE_LINUX
        if (now - lastUsb > 2000) {
            lastUsb = now;
            auto ids = usbDevices();
            const juce::ScopedLock sl(foundLock);
            for (int id : ids) {
                bool have = false;
                for (auto& f : found) if (f.mode == PhoneConn::Usb && f.usbId == id) { f.lastSeen = now; have = true; }
                if (!have) found.push_back({ PhoneConn::Usb, {}, kPhonePort, id, now });
            }
        }
#endif
        wait(500);
    }
#if ARCO_BONJOUR
    if (bref != nullptr) DNSServiceRefDeallocate(bref);
#endif
}

void PhoneHub::tick() {
    double now = juce::Time::getMillisecondCounterHiRes();
    std::vector<Found> f;
    { const juce::ScopedLock sl(foundLock);
      found.erase(std::remove_if(found.begin(), found.end(), [&](const Found& x) { return now - x.lastSeen > 15000; }), found.end());
      f = found; }
    for (auto& m : manual) { auto host = m.upToLastOccurrenceOf(":", false, false); int port = m.fromLastOccurrenceOf(":", false, false).getIntValue();
        f.push_back({ PhoneConn::WiFi, host, port > 0 ? port : kPhonePort, 0, now }); }
    auto keyOf = [](const Found& x) { return x.mode == PhoneConn::Usb ? "usb:" + juce::String(x.usbId) : x.host + ":" + juce::String(x.port); };
    // new phones: connect
    for (auto& x : f) {
        auto k = keyOf(x);
        if (ignoreUntil.count(k) && ignoreUntil[k] > now) continue;
        bool have = false; for (auto* c : conns) if (c->key() == k) have = true;
        if (have) continue;
        auto* c = new PhoneConn(x.mode, x.host, x.port, x.usbId);
        { const juce::ScopedLock sl(audioLock); conns.add(c); }
        c->begin();
    }
    // the same phone twice (cable and Wi-Fi), phones gone, remembered LIVE inputs
    juce::Array<PhoneConn*> drop;
    for (auto* c : conns) {
        bool stillFound = false; for (auto& x : f) if (keyOf(x) == c->key()) stillFound = true;
        if (!stillFound && !c->connected()) { drop.add(c); continue; }
        if (c->busyElsewhere.load()) { bool other = false; for (auto* o : conns) if (o != c && o->connected() && o->name() == c->name()) other = true;
            if (other) { drop.add(c); ignoreUntil[c->key()] = now + 60000; continue; } }
        if (c->connected() && c->live.load() == 0 && c->name().isNotEmpty()) {
            auto it = remembered.find(c->name());
            if (it != remembered.end() && it->second > 0 && forLive(it->second) == nullptr) c->live.store(it->second);
        }
    }
    for (auto* c : drop) {
        std::unique_ptr<PhoneConn> gone;
        { const juce::ScopedLock sl(audioLock); gone.reset(conns.removeAndReturn(conns.indexOf(c))); }
    }
}

} // namespace arco
