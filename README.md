# ARCO

A live looping station for instruments: 4 scenes, 8 tracks per scene, 4 live inputs, 18 effects, up to 32 speakers.

Private project. Every push to `main` builds a universal Mac app (Apple Silicon + Intel) in GitHub Actions;
download it from the run's **Artifacts** (ARCO-mac).

## First open on the Mac
1. Unzip `ARCO-mac.zip`, drag **ARCO** to Applications.
2. Open it once; macOS says it can't check the developer. Go to **System Settings → Privacy & Security**, scroll down, press **Open Anyway**.
   (Or in Terminal: `xattr -cr /Applications/ARCO.app`.)
3. Allow the microphone when asked - that is your sound card input.
4. Press **SETUP**, choose your sound card, inputs and headphone outputs.

## Build locally
    git clone --depth 1 --branch 8.0.4 https://github.com/juce-framework/JUCE.git JUCE
    cmake -B build -G Xcode && cmake --build build --config Release
