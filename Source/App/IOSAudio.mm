// iPhone / iPad: use the best built-in microphone, in its natural (all-around) pattern.
// An external microphone or audio interface that is plugged in is left alone - iOS already uses it.
#import <AVFoundation/AVFoundation.h>

void arcoPreferBestMicrophone()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    AVAudioSessionPortDescription* current = session.currentRoute.inputs.firstObject;
    if (current == nil || ! [current.portType isEqualToString: AVAudioSessionPortBuiltInMic])
        return;

    for (AVAudioSessionPortDescription* port in session.availableInputs)
    {
        if (! [port.portType isEqualToString: AVAudioSessionPortBuiltInMic])
            continue;

        AVAudioSessionDataSourceDescription* best = nil;
        for (AVAudioSessionDataSourceDescription* src in port.dataSources)          // the bottom microphone is the main, fullest one
            if ([src.orientation isEqualToString: AVAudioSessionOrientationBottom])
                best = src;
        if (best == nil)
            best = port.dataSources.firstObject;
        if (best == nil)
            return;

        if ([best.supportedPolarPatterns containsObject: AVAudioSessionPolarPatternOmnidirectional])
            [best setPreferredPolarPattern: AVAudioSessionPolarPatternOmnidirectional error: nil];
        [port setPreferredDataSource: best error: nil];
        [session setPreferredInput: port error: nil];
    }
}
