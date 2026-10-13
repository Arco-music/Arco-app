// iPhone / iPad: the microphone and the output.
//  - "auto": the best built-in microphone (the bottom one, all-around pattern); a cable / USB interface that is plugged in wins.
//  - or a chosen input: built-in front / back / bottom, the headset cable, a USB interface, a Bluetooth headset.
//  - the output: whatever iOS routes to (cable, Bluetooth, AirPlay), chosen with the system picker; or the speaker forced.
// (Apple's headers first: after JuceHeader.h's "using namespace juce", their Point / AudioBuffer would clash with JUCE's)
#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVKit.h>
#import <UIKit/UIKit.h>
#include "IOSAudio.h"

namespace arco::ios {

static juce::String S(NSString* s) { return s != nil ? juce::String::fromUTF8(s.UTF8String) : juce::String(); }
static juce::String& chosen() { static juce::String c = "auto"; return c; }
static bool& speaker() { static bool s = false; return s; }

static juce::String kindOf(NSString* t) {
    if ([t isEqualToString: AVAudioSessionPortBuiltInMic]) return "built-in";
    if ([t isEqualToString: AVAudioSessionPortHeadsetMic] || [t isEqualToString: AVAudioSessionPortLineIn]) return "cable";
    if ([t isEqualToString: AVAudioSessionPortUSBAudio]) return "usb";
    if ([t isEqualToString: AVAudioSessionPortBluetoothHFP]) return "bluetooth";
    return "other";
}
static juce::String sideName(AVAudioSessionDataSourceDescription* d) {
    if (d.orientation == nil) return S(d.dataSourceName);
    if ([d.orientation isEqualToString: AVAudioSessionOrientationBottom]) return "bottom";
    if ([d.orientation isEqualToString: AVAudioSessionOrientationFront]) return "front";
    if ([d.orientation isEqualToString: AVAudioSessionOrientationBack]) return "back";
    if ([d.orientation isEqualToString: AVAudioSessionOrientationTop]) return "top";
    return S(d.dataSourceName);
}

juce::Array<Port> inputs() {
    juce::Array<Port> out;
    out.add({ "auto", "AUTOMATIC - what is plugged in, else the best built-in mic", "auto" });
    AVAudioSession* s = [AVAudioSession sharedInstance];
    for (AVAudioSessionPortDescription* p in s.availableInputs) {
        auto kind = kindOf(p.portType);
        auto name = S(p.portName);
        if (kind == "built-in" && p.dataSources.count > 1) {
            for (AVAudioSessionDataSourceDescription* d in p.dataSources)
                out.add({ S(p.UID) + "|" + juce::String([d.dataSourceID intValue]), "built-in mic (" + sideName(d) + ")", kind });
        } else {
            if (kind == "bluetooth") name << " (Bluetooth - phone-call quality)";
            out.add({ S(p.UID), name, kind });
        }
    }
    return out;
}

juce::String chosenInput() { return chosen(); }

static void preferBest() {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    // something plugged in (cable, USB): iOS already uses it - leave it
    for (AVAudioSessionPortDescription* p in session.availableInputs) {
        auto k = kindOf(p.portType);
        if (k == "cable" || k == "usb") { [session setPreferredInput: p error: nil]; return; }
    }
    for (AVAudioSessionPortDescription* port in session.availableInputs) {
        if (! [port.portType isEqualToString: AVAudioSessionPortBuiltInMic]) continue;
        AVAudioSessionDataSourceDescription* best = nil;
        for (AVAudioSessionDataSourceDescription* src in port.dataSources)          // the bottom microphone is the main, fullest one
            if ([src.orientation isEqualToString: AVAudioSessionOrientationBottom]) best = src;
        if (best == nil) best = port.dataSources.firstObject;
        if (best != nil) {
            if ([best.supportedPolarPatterns containsObject: AVAudioSessionPolarPatternOmnidirectional])
                [best setPreferredPolarPattern: AVAudioSessionPolarPatternOmnidirectional error: nil];
            [port setPreferredDataSource: best error: nil];
        }
        [session setPreferredInput: port error: nil];      // a Bluetooth headset does not take the microphone
    }
}

// The session JUCE opens allows Bluetooth "hands-free" (HFP) too: with a headset connected iOS then prefers the phone-call
// connection (low quality, or the sound stays on the iPad). ARCO asks for high-quality Bluetooth (A2DP) only, unless a
// Bluetooth microphone was chosen on purpose. Then Bluetooth headphones / speakers take the sound as soon as they connect.
void configureSession() {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    if (! [session.category isEqualToString: AVAudioSessionCategoryPlayAndRecord]) return;
    bool btMic = chosen().startsWith("bt") || chosen().containsIgnoreCase("bluetooth");
    for (AVAudioSessionPortDescription* p in session.availableInputs)
        if ([p.portType isEqualToString: AVAudioSessionPortBluetoothHFP] && chosen().upToFirstOccurrenceOf("|", false, false) == S(p.UID)) btMic = true;
    NSUInteger want = AVAudioSessionCategoryOptionMixWithOthers | AVAudioSessionCategoryOptionDefaultToSpeaker
                    | AVAudioSessionCategoryOptionAllowAirPlay | AVAudioSessionCategoryOptionAllowBluetoothA2DP;
    if (btMic) want |= AVAudioSessionCategoryOptionAllowBluetooth;
    if (session.categoryOptions == want) return;
    [session setCategory: AVAudioSessionCategoryPlayAndRecord mode: AVAudioSessionModeDefault options: want error: nil];
}

void showOutputPicker() {
    static AVRoutePickerView* picker = nil;
    UIWindow* win = nil;
    for (UIScene* sc in [UIApplication sharedApplication].connectedScenes)
        if ([sc isKindOfClass: [UIWindowScene class]])
            for (UIWindow* w in ((UIWindowScene*) sc).windows) if (w.isKeyWindow) win = w;
    if (win == nil) win = [UIApplication sharedApplication].windows.firstObject;
    if (win == nil) return;
    if (picker == nil) {
        picker = [[AVRoutePickerView alloc] initWithFrame: CGRectMake(win.bounds.size.width * 0.5 - 20, 60, 40, 40)];
        picker.prioritizesVideoDevices = NO;
        picker.alpha = 0.011f;                       // invisible, but iOS still lets it show its list
    }
    if (picker.superview != win) [win addSubview: picker];
    picker.frame = CGRectMake(win.bounds.size.width * 0.5 - 20, 60, 40, 40);
    for (UIView* v in picker.subviews)
        if ([v isKindOfClass: [UIButton class]]) { [(UIButton*) v sendActionsForControlEvents: UIControlEventTouchUpInside]; return; }
}

void applyInput() {
    AVAudioSession* session = [AVAudioSession sharedInstance];
    configureSession();
    auto id = chosen();
    if (id == "auto" || id.isEmpty()) preferBest();
    else {
        auto uid = id.upToFirstOccurrenceOf("|", false, false);
        int ds = id.contains("|") ? id.fromFirstOccurrenceOf("|", false, false).getIntValue() : -1;
        bool found = false;
        for (AVAudioSessionPortDescription* p in session.availableInputs) {
            if (S(p.UID) != uid) continue;
            found = true;
            if (ds >= 0)
                for (AVAudioSessionDataSourceDescription* d in p.dataSources)
                    if ([d.dataSourceID intValue] == ds) {
                        if ([d.supportedPolarPatterns containsObject: AVAudioSessionPolarPatternOmnidirectional])
                            [d setPreferredPolarPattern: AVAudioSessionPolarPatternOmnidirectional error: nil];
                        [p setPreferredDataSource: d error: nil];
                    }
            [session setPreferredInput: p error: nil];
        }
        if (! found) preferBest();                       // unplugged: back to the best one
    }
    [session overrideOutputAudioPort: (speaker() ? AVAudioSessionPortOverrideSpeaker : AVAudioSessionPortOverrideNone) error: nil];
}

void chooseInput(const juce::String& id) { chosen() = id.isEmpty() ? juce::String("auto") : id; applyInput(); }

juce::String currentInputName() {
    AVAudioSessionPortDescription* p = [AVAudioSession sharedInstance].currentRoute.inputs.firstObject;
    if (p == nil) return "no microphone";
    auto n = S(p.portName);
    if (p.selectedDataSource != nil && [p.portType isEqualToString: AVAudioSessionPortBuiltInMic]) n = "built-in mic (" + sideName(p.selectedDataSource) + ")";
    return n;
}
juce::String currentOutputName() {
    juce::StringArray n;
    for (AVAudioSessionPortDescription* p in [AVAudioSession sharedInstance].currentRoute.outputs) n.add(S(p.portName));
    return n.isEmpty() ? juce::String("no output") : n.joinIntoString(" + ");
}
bool outputIsBluetooth() {
    for (AVAudioSessionPortDescription* p in [AVAudioSession sharedInstance].currentRoute.outputs)
        if ([p.portType isEqualToString: AVAudioSessionPortBluetoothA2DP] || [p.portType isEqualToString: AVAudioSessionPortBluetoothHFP]
            || [p.portType isEqualToString: AVAudioSessionPortBluetoothLE] || [p.portType isEqualToString: AVAudioSessionPortAirPlay]) return true;
    return false;
}
bool speakerForced() { return speaker(); }
void forceSpeaker(bool on) { speaker() = on; applyInput(); }

void* makeRoutePicker() {
    AVRoutePickerView* v = [[AVRoutePickerView alloc] initWithFrame: CGRectMake(0, 0, 40, 40)];
    v.prioritizesVideoDevices = NO;
    v.activeTintColor = [UIColor colorWithRed: 0.30 green: 0.76 blue: 0.48 alpha: 1.0];
    v.tintColor = [UIColor colorWithRed: 0.56 green: 0.58 blue: 0.64 alpha: 1.0];
    v.backgroundColor = [UIColor clearColor];
    return (__bridge_retained void*) v;
}

} // namespace arco::ios
