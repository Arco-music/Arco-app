#!/usr/bin/env python3
"""Writes ARCO.jucer - the project JUCE's Projucer turns into the Xcode project for iPad / iPhone (Builds/iOS).
Run from the repository root:  python3 tools/make_jucer.py   then   Projucer --resave ARCO.jucer
(then empty Builds/iOS/App.entitlements: the Projucer adds inter-app-audio for AU hosting, which ARCO does not need)"""
import os, hashlib, xml.sax.saxutils as su

VERSION = open("VERSION").read().strip() if os.path.exists("VERSION") else "1.1.0"
def uid(s): return hashlib.md5(s.encode()).hexdigest()[:6]
def esc(s): return su.quoteattr(s)

src = []
for root, _, files in os.walk("Source"):
    for f in sorted(files):
        if f.endswith((".cpp", ".h", ".mm")):
            src.append(os.path.join(root, f))
src.sort()
groups = {}
for p in src:
    groups.setdefault(os.path.dirname(p), []).append(p)

modules = ["juce_audio_basics", "juce_audio_devices", "juce_audio_formats", "juce_audio_processors", "juce_audio_utils",
           "juce_core", "juce_data_structures", "juce_events", "juce_graphics", "juce_gui_basics", "juce_gui_extra"]

out = []
w = out.append
w('<?xml version="1.0" encoding="UTF-8"?>')
w(f'<JUCERPROJECT id="arco01" name="ARCO" projectType="guiapp" version="{VERSION}" bundleIdentifier="com.pacello.arco" '
  f'companyName="ARCO" cppLanguageStandard="17" displaySplashScreen="0" jucerFormatVersion="1" '
  f'compilerFlagSchemes="nofastmath" '
  f'defines="JUCE_WEB_BROWSER=0&#10;JUCE_USE_CURL=0&#10;JUCE_DISPLAY_SPLASH_SCREEN=0">')
w('  <MAINGROUP id="grp000" name="ARCO">')
for g, files in sorted(groups.items()):
    w(f'    <GROUP id="{uid(g)}" name={esc(g)}>')
    for p in files:
        comp = "1" if p.endswith((".cpp", ".mm")) and "ThirdParty" not in p else "0"
        extra = ' compilerFlagScheme="nofastmath"' if os.path.basename(p) == "Analysis.cpp" else ''   # Signalsmith Stretch: no -ffast-math
        w(f'      <FILE id="{uid(p)}" name={esc(os.path.basename(p))} compile="{comp}" resource="0" file={esc(p)}{extra}/>')
    w('    </GROUP>')
w('    <GROUP id="grpres" name="Resources">')
for f in ["Inter-Regular.otf", "Inter-SemiBold.otf", "Inter-Bold.otf", "icon.png"]:
    w(f'      <FILE id="{uid(f)}" name="{f}" compile="0" resource="1" file="Resources/{f}"/>')
w(f'      <FILE id="icnios" name="icon-ios.png" compile="0" resource="0" file="Resources/icon-ios.png"/>')
w('    </GROUP>')
w('  </MAINGROUP>')
w('  <MODULES>')
for m in modules:
    w(f'    <MODULE id="{m}" showAllCode="1" useLocalCopy="0" useGlobalPath="0"/>')
w('  </MODULES>')
w('  <JUCEOPTIONS JUCE_STRICT_REFCOUNTEDPOINTER="1" JUCE_PLUGINHOST_AU="1"/>')
w('  <EXPORTFORMATS>')
w('    <XCODE_IPHONE targetFolder="Builds/iOS" iosDeviceFamily="1,2" bigIcon="icnios" smallIcon="icnios" '
  'iPhoneScreenOrientation="UIInterfaceOrientationPortrait,UIInterfaceOrientationLandscapeLeft,UIInterfaceOrientationLandscapeRight" '
  'iPadScreenOrientation="UIInterfaceOrientationPortrait,UIInterfaceOrientationPortraitUpsideDown,UIInterfaceOrientationLandscapeLeft,UIInterfaceOrientationLandscapeRight" '
  'microphonePermissionNeeded="1" microphonePermissionsText="ARCO uses the microphone (or your audio interface) to record your instrument." '
  'iosBackgroundAudio="1" UIFileSharingEnabled="1" UISupportsDocumentBrowser="1" UIStatusBarHidden="1" '
  'customPList="&lt;plist&gt;&lt;dict&gt;&lt;key&gt;NSLocalNetworkUsageDescription&lt;/key&gt;&lt;string&gt;ARCO finds your phones and your station (Mac or iPad) on the Wi-Fi: microphones and remote control.&lt;/string&gt;&lt;key&gt;LSSupportsOpeningDocumentsInPlace&lt;/key&gt;&lt;true/&gt;&lt;key&gt;NSBonjourServices&lt;/key&gt;&lt;array&gt;&lt;string&gt;_arco._tcp&lt;/string&gt;&lt;/array&gt;&lt;key&gt;UIRequiresFullScreen&lt;/key&gt;&lt;false/&gt;&lt;/dict&gt;&lt;/plist&gt;" '
  'extraFrameworks="AVKit,CoreAudioKit" '
  'extraCompilerFlags="-Wno-unused-variable -Wno-unused-parameter" nofastmath="-fno-fast-math">')
w('      <CONFIGURATIONS>')
w('        <CONFIGURATION name="Debug" isDebug="0" iosDeploymentTarget="15.0" optimisation="3" fastMath="1" targetName="ARCO"/>')
w('        <CONFIGURATION name="Release" isDebug="0" iosDeploymentTarget="15.0" optimisation="3" fastMath="1" targetName="ARCO"/>')
w('      </CONFIGURATIONS>')
w('      <MODULEPATHS>')
for m in modules:
    w(f'        <MODULEPATH id="{m}" path="JUCE/modules"/>')
w('      </MODULEPATHS>')
w('    </XCODE_IPHONE>')
w('  </EXPORTFORMATS>')
w('</JUCERPROJECT>')
open("ARCO.jucer", "w").write("\n".join(out) + "\n")
print("ARCO.jucer written:", len(src), "source files, version", VERSION)
