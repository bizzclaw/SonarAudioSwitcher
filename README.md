# SonarAudioSwitcher

A lightweight Windows system tray application that automatically switches [SteelSeries Sonar](https://steelseries.com/gg/sonar) audio devices based on which application is currently running.

I made this because I like using Steelseries Sonar with VR, but I hated having to manually switch my audio output every time I swapped between VR and Desktop, but it can be used for any application, not just VR. I also wanted to use Backup Device Switch, but it had a habbit of being buggy if device IDs or Names change and its arbitrarily disabled with streamer mode - which I prefer to use in general even though I'm not a stremer.

Define rules like "when `vrserver.exe` is running, switch output to my VR headset" and SonarAudioSwitcher will monitor your running processes and call the Sonar API to change your audio routing automatically. When the application closes, it switches back to your configured default.

## Features

- **Automatic switching** — polls running processes and connected audio devices, then applies the first matching rule.
- **Application rules** — trigger a switch when a specific executable (e.g. `vrserver.exe`) is running.
- **Device rules** — trigger a switch when a specific audio device (e.g. a Bluetooth headset or USB dongle) is connected and visible to Sonar.
- **Partial name matching** — all device fields use partial, case-insensitive matching so rules survive device ID changes, driver reinstalls, and Windows renaming things randomly.
- **Classic & Streamer mode** — works with both Sonar modes. In streamer mode, only the monitoring output and mic input are changed; the stream mix is never touched.
- **System tray** — runs quietly in the background with a tray icon for quick access.
- **Pause / Resume** — temporarily disable rule matching without closing the app.
- **Reset to Default** — instantly apply your default audio devices and auto-pause.

## Usage
- Build locally or Download and run the Installer from the Releases page.
- Open the settings window from the system tray icon.
- Set your **default** output and input devices — these are applied whenever no rule is active. Both fields use partial name matching (e.g. `"Arctis"` will match `"Headphones (Arctis Nova Pro Wireless)"`).
- Add rules to define when a different audio profile should be active. Each rule has:
  - **Rule Type** — choose one of:
    - **Application** — activates while a specific executable is running. Enter the exact process name in the *Application Executable Name Exact Match* field (e.g. `vrserver.exe`).
    - **Device** — activates while a specific audio device is connected and visible to Sonar. Enter a partial device name in the *Device Name Partial Match* field (e.g. `P10 Dongle` or `Quest`).
  - **Output Device Partial Match** — the Sonar output device to switch to when this rule is active.
  - **Input Device Partial Match** — the Sonar input device to switch to when this rule is active.
- Rules are evaluated top-to-bottom; the **first matching rule wins**. Use the Up/Down buttons to set priority order.
- Click **Save** — the switcher applies the new config immediately from the system tray.

<img width="612" height="744" alt="SonarAudioSwitcher_novfv5rfdq" src="https://github.com/user-attachments/assets/9c1dfae5-978e-41ec-aea0-87a8b6a51059" />

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

The installer will be output to `Release/SonarAudioSwitcher_Setup.exe`.

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


