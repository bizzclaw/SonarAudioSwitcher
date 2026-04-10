# SonarAudioSwitcher

A lightweight Windows system tray application that automatically switches [SteelSeries Sonar](https://steelseries.com/gg/sonar) audio devices based on which application is currently running.

I made this because I like using Steelseries Sonar with VR, but I hated having to manually switch my audio output every time I swapped between VR and Desktop, but it can be used for any application, not just VR.

Define rules like "when `vrserver.exe` is running, switch output to my VR headset" and SonarAudioSwitcher will monitor your running processes and call the Sonar API to change your audio routing automatically. When the application closes, it switches back to your configured default.

## Features

- **Automatic switching** — polls running processes and applies the first matching rule.
- **Classic & Streamer mode** — works with both Sonar modes. In streamer mode, only the monitoring output and mic input are changed; the stream mix is never touched.
- **System tray** — runs quietly in the background with a tray icon for quick access.
- **Pause / Resume** — temporarily disable rule matching without closing the app.
- **Reset to Default** — instantly apply your default audio devices and auto-pause.

## Usage
- Build locally or Download and run the Installer from the Releases page.
- Open the settings window from the system tray icon.
- Set your default output and mic devices (the ones you want when no rules are active) - Note that instead of a dropdown, you enter partial match for the device name, I did this because setting the device ID directly is unreliable as it may change over time or when other decices are added.
- Add a rule and set the exact executable name (e.g. `vrserver.exe`) and the output/mic you want to switch to when that app is running using partial matches for the device names as well.
- Click "Save" and the app will run from your System Tray.

## Building Locally

### Prerequisites

- **Windows 10/11** (x64)
- **Visual Studio 2022** with the "Desktop development with C++" workload
- **CMake 3.20+** (bundled with Visual Studio or installed separately)

### Steps

```bash
# Clone the repository
git clone https://github.com/your-username/SonarAudioSwitcher.git
cd SonarAudioSwitcher

# Configure
cmake -B build -A x64

# Build (Debug)
cmake --build build --config Debug

# Build (Release)
cmake --build build --config Release
```

The executable will be in `build/Debug/SonarAudioSwitcher.exe` or `build/Release/SonarAudioSwitcher.exe`.

### Installer (optional)

An [Inno Setup 6.7](https://jrsoftware.org/isinfo.php) script is provided in `installer/installer.iss`. After building a Release binary, open the script in Inno Setup Compiler or run:

```bash
iscc.exe installer/installer.iss
```

The installer will be output to `build/installer/SonarAudioSwitcher_Setup.exe`.

## Credits

This project uses the following open-source libraries:

- **[Dear ImGui](https://github.com/ocornut/imgui)** — Immediate-mode GUI library by Omar Cornut. Used for the settings window (with the Win32 + DX11 backends). Licensed under the MIT License.
- **[nlohmann/json](https://github.com/nlohmann/json)** — JSON for Modern C++ by Niels Lohmann. Used for config file and Sonar API response parsing. Licensed under the MIT License.
- **[cpp-httplib](https://github.com/yhirose/cpp-httplib)** — Header-only HTTP/HTTPS library by Yuji Hirose. Used for communicating with the Sonar REST API. Licensed under the MIT License.

All vendor libraries are included directly in the `vendor/` directory (single-header or source files) — no package manager required.

## Acknowledgements

Thank you to **SteelSeries** for exposing a local REST API in SteelSeries GG / Sonar. Without that API, automatic audio device switching like this wouldn't be possible. The API has been remarkably stable and well-structured, even though it's undocumented — it made this entire project feasible.

## Disclaimer

This project was developed with the assistance of AI coding agents. All generated code was reviewed, tested, and cleaned up by a human. The architecture, design decisions, and final implementation are human-directed.

This is also my First non unreal based c++ project so there were certain concepts I am still unfamiliar with and may not have corrected on the AI on properly. If you see any issues, kindly let me know. This was partially made out of annoyance, but also as a learning oppurtunity for me.

This project is not affiliated with, endorsed by, or officially supported by SteelSeries. It interacts with an undocumented local API that could change at any time.

## Known Issues

- **Sonar UI does not reflect changes made by this app.** When SonarAudioSwitcher switches your audio device via the API, the audio routing changes correctly at the system level (you will hear audio through the new device), but the SteelSeries GG / Sonar desktop app will still display the previous device in its UI. This is a limitation of the Sonar frontend — it does not poll its own API for external changes.

## License

[MIT](LICENSE)


