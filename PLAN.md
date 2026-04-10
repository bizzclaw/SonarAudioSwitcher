# SonarAudioSwitcher — Implementation Plan

A Windows system tray application that automatically switches SteelSeries Sonar audio devices based on which application is currently running.

---

## 1. Technology Choices

### Language: C++17 (MSVC)

### UI Framework: Dear ImGui (DirectX 11 + Win32 backend) + raw Win32 for tray

We use two different UI approaches depending on the surface:

**Tray icon + context menu — raw Win32.** This is the minimal, always-resident UI.
- `Shell_NotifyIcon` + `NOTIFYICONDATA` for the system tray icon
- `TrackPopupMenu` for the right-click context menu (Open Settings / Pause / Refresh / Reset to Default / Exit)
- No ImGui required for this surface — it's pure Win32 API calls from `main.cpp`.

**Settings window — Dear ImGui.** The rules list needs rich editing: add/remove rows, enable toggles, per-cell dropdowns of detected devices, drag-to-reorder, etc. Raw Win32 `ListView` + `Edit` controls got ugly fast, so we switched to [Dear ImGui](https://github.com/ocornut/imgui) with the Win32 + DirectX 11 backend.
- Vendored into `vendor/imgui/` — the main library plus `imgui_impl_win32.cpp`, `imgui_impl_dx11.cpp`, and `imgui_stdlib.cpp` (for `std::string` input helpers).
- The window is created on demand from the tray menu and destroyed on close — no ongoing render cost while hidden.
- Links against `d3d11`, `d3dcompiler`, `dxgi`, `dwmapi`, `ole32`.
- Final binary is larger than a pure-Win32 build would be (ImGui + D3D11 runtime), but well under a megabyte and with zero external runtime dependencies.

### Process Monitoring: Polling with `CreateToolhelp32Snapshot`

| Approach | Admin Required | Complexity | Latency |
|---|---|---|---|
| WMI (Win32_ProcessStartTrace) | Yes | Moderate (COM boilerplate) | 100-500ms |
| ETW (Kernel-Process provider) | Yes | High (raw) / Low (krabsetw) | Sub-10ms |
| **Polling (CreateToolhelp32Snapshot)** | **No** | **Very Low** | **1-2s** |
| Kernel driver | Yes (driver signing) | Very High | Synchronous |

**Polling wins** for this use case:
- No admin elevation required — the app can run as a normal user
- 1-2 second latency is more than acceptable for switching audio devices
- ~1-5ms CPU per snapshot on a system with 200-400 processes — negligible
- Under 100 lines of code vs. 200+ for WMI COM boilerplate
- The processes we care about (games, apps) run for minutes/hours, so we won't miss them

### HTTP Client: WinHTTP (built into Windows)

Used to call the Sonar REST API. Zero external dependencies — `WinHttpOpen`, `WinHttpConnect`, `WinHttpOpenRequest`, etc. Alternatively, we could use `wininet` or bundle `cpp-httplib` (header-only) for a cleaner API.

**Recommendation:** Use [`cpp-httplib`](https://github.com/yhirose/cpp-httplib) — it's a single header file, dramatically simpler than raw WinHTTP, and supports the plain HTTP calls we need. We vendor the single header into the project.

### Configuration Storage: JSON file

Use [`nlohmann/json`](https://github.com/nlohmann/json) (single header, vendored into the project) for reading/writing a `config.json` file next to the executable.

### Build System: CMake

CMake is the standard for cross-IDE C++ projects. It works natively with:
- **JetBrains Rider** (supports CMake C++ projects directly)
- **CLion** (JetBrains' dedicated C++ IDE, same engine as Rider's C++ support)
- **Visual Studio** (File > Open > CMake)
- **VS Code** (with CMake Tools extension)

---

## 2. SteelSeries Sonar API Reference

### Discovery (performed on app startup and periodically)

1. Read `C:\ProgramData\SteelSeries\SteelSeries Engine 3\coreProps.json`
   ```json
   {
     "address": "127.0.0.1:XXXXX",
     "ggEncryptedAddress": "127.0.0.1:YYYYY"
   }
   ```

2. Query `https://{ggEncryptedAddress}/subApps` (disable SSL verification — self-signed cert)
   ```json
   {
     "subApps": {
       "sonar": {
         "isEnabled": true,
         "isReady": true,
         "isRunning": true,
         "metadata": {
           "webServerAddress": "http://localhost:12268"
         }
       }
     }
   }
   ```

3. Use `webServerAddress` as the base URL for all subsequent calls.

### Key Endpoints

| Action | Method | Endpoint |
|---|---|---|
| Get current mode | `GET` | `/mode` → `"classic"` or `"stream"` |
| List audio devices | `GET` | `/audioDevices` |
| Get classic routing | `GET` | `/classicRedirections` |
| Set classic output device | `PUT` | `/classicRedirections/{channel}/deviceId/{url_encoded_device_id}` |
| Get stream routing | `GET` | `/streamRedirections` |
| Set monitoring output device | `PUT` | `/streamRedirections/monitoring/deviceId/{url_encoded_device_id}` |

- `{channel}` = `master`, `game`, `chatRender`, `media`, `aux`, or `chatCapture`
- Device IDs are Windows audio endpoint IDs: `{0.0.0.00000000}.{GUID}` — curly braces must be URL-encoded (`%7B` / `%7D`)
- All PUT requests have `Content-Length: 0` (parameters are in the URL path)
- No authentication required

### Streamer Mode Behavior

In streamer mode, audio is split into two mixes:
- **Monitoring** — what the user hears (we change this)
- **Streaming** — what the stream captures (we do NOT change this)

When the user is in streamer mode, we only call:
- `PUT /streamRedirections/monitoring/deviceId/{id}` for output
- The appropriate input device endpoint for chatCapture

We **never** touch the streaming device routing.

### Device Name Matching

The `/audioDevices` endpoint returns device objects with human-readable names. We fetch this list, then find the first device whose name contains the user's partial match string (case-insensitive). This resolves the device ID instability problem — names like "Arctis Nova Pro" stay consistent even as GUIDs change.

---

## 3. Project Structure

```
F:\OpenSource\SonarAudioSwitcher\
├── CMakeLists.txt                  # Build configuration
├── PLAN.md                         # This file
├── README.md                       # Usage instructions (later)
├── resources/
│   ├── app.rc                      # Win32 resource script (dialog, icon, manifest)
│   ├── app.ico                     # Tray icon
│   ├── resource.h                  # Resource IDs
│   └── app.manifest                # DPI awareness, common controls v6
├── vendor/
│   ├── httplib.h                   # cpp-httplib (single header)
│   ├── json.hpp                    # nlohmann/json (single header)
│   └── imgui/                      # Dear ImGui core + Win32/DX11 backends + stdlib helpers
└── src/
    ├── main.cpp                    # Entry point (WinMain), message loop, tray icon
    ├── config.h / config.cpp       # Rule definitions, JSON load/save
    ├── process_monitor.h / .cpp    # CreateToolhelp32Snapshot polling
    ├── sonar_client.h / .cpp       # Sonar API discovery + device switching
    ├── device_matcher.h / .cpp     # Partial name → device ID resolution
    ├── switcher.h / .cpp           # Core logic: monitor → match rule → apply
    ├── imgui_settings_window.h / .cpp  # Dear ImGui settings window (rules editor)
    ├── startup.h / .cpp            # "Start with Windows" registry toggle
    ├── logger.h / .cpp             # File + debug output logging
    └── diag.cpp                    # Standalone SonarDiag console tool
```

### Component Breakdown

#### `main.cpp` — Entry Point & System Tray
- `WinMain` entry point
- Registers a hidden message-only window (`HWND_MESSAGE`)
- Creates the system tray icon via `Shell_NotifyIcon`
- Handles `WM_APP+1` for tray icon interactions (right-click → context menu)
- Menu items: "Open Settings", separator, "Exit"
- Starts the `Switcher` on a background thread
- Registers with Task Scheduler or `HKCU\...\Run` for startup

#### `config.h / config.cpp` — Configuration
```cpp
enum class RuleType {
    Application = 0,  // match by running exe name
    Device      = 1,  // match by audio device presence (partial name)
};

struct Rule {
    bool enabled = true;
    RuleType type = RuleType::Application;
    std::string exeName;            // used when type == Application
    std::string deviceNameMatch;    // used when type == Device
    std::string outputDevice;       // partial name, e.g. "Arctis Nova"
    std::string inputDevice;        // partial name, e.g. "Arctis Nova"
};

struct Config {
    int pollIntervalMs = 2000;
    Rule defaultRule{};             // applied when no rule matches
    std::vector<Rule> rules;
};
```
- Loads/saves `config.json` next to the executable
- JSON format via nlohmann/json

#### `process_monitor.h / .cpp` — Process Detection
- `std::set<std::string> getRunningProcesses()` — snapshots all running exe names (lowercased)
- Called by the switcher on each tick

#### `sonar_client.h / .cpp` — Sonar API
- `discoverSonarAddress()` — reads coreProps.json, queries /subApps, returns base URL
- `getMode()` — returns `"classic"` or `"stream"`
- `getAudioDevices()` — returns list of `{id, name, type}` structs
- `setClassicDevice(channel, deviceId)` — PUT to classicRedirections
- `setMonitoringDevice(deviceId)` — PUT to streamRedirections/monitoring
- Handles URL-encoding of device IDs
- Reconnects if the Sonar port changes (GG restart)

#### `device_matcher.h / .cpp` — Name Resolution
- `std::optional<std::string> findDeviceId(devices, partialName, type)` — case-insensitive substring match
- `type` distinguishes input vs. output devices

#### `switcher.h / .cpp` — Core Logic
This is the brain of the application. Runs on a dedicated thread.

```
loop every 1-2 seconds:
    1. Get running processes (CreateToolhelp32Snapshot)
    2. Find the highest-priority matching rule (first match in the rules list)
       - If no rule matches, use the default rule
    3. If the matched rule is the same as the currently applied rule → skip
    4. Fetch audio devices from Sonar API
    5. Resolve the rule's partial device names to device IDs
    6. Check current mode (classic vs stream)
    7. Apply changes:
       - Classic mode: set output via classicRedirections, set input via chatCapture
       - Stream mode: set output via streamRedirections/monitoring (NOT streaming), set input via chatCapture
    8. Remember the currently applied rule to avoid redundant API calls
```

Key behaviors:
- **Only acts on changes:** Tracks which rule is currently applied. Does nothing if the same rule is still active.
- **Priority by list order:** Rules are checked top-to-bottom; first match wins. This lets users order rules by priority.
- **Streamer mode safety:** Never touches the streaming output device — only monitoring + input.
- **Graceful degradation:** If Sonar is unreachable (GG not running), log the error and retry on next tick. If a partial name matches nothing, log a warning and skip that device change.

#### `imgui_settings_window.h / .cpp` — Settings UI
- A standalone Dear ImGui window launched on demand from the tray menu, rendered with the Win32 + DirectX 11 backends
- A table of rules with columns: Enabled | Exe Name | Output Device | Input Device | Actions
- The "Default" rule sits above the table and is not removable
- Device cells are combo boxes populated from the live Sonar device list (re-fetched when the window opens)
- Add / Remove / Move Up / Move Down buttons per row
- "Save" writes to config.json and calls `Switcher::reloadConfig()` to hot-apply
- "Cancel" discards changes and closes the window
- The window is created on demand and fully torn down on close — no render cost while hidden

---

## 4. IDE Setup (JetBrains Rider)

Rider supports CMake-based C++ projects. To open and work on this project:

1. **Open Project:** File → Open → select `F:\OpenSource\SonarAudioSwitcher\CMakeLists.txt` (or the directory)
2. **Toolchain:** Rider will detect Visual Studio (MSVC) automatically. Ensure you have:
   - Visual Studio 2022 (or Build Tools) with the "Desktop development with C++" workload
   - Windows SDK (comes with the VS workload)
3. **CMake Profile:** Rider auto-creates a Debug profile. You can add Release via Settings → Build → CMake.
4. **Build:** Build → Build Project (or Ctrl+F9)
5. **Run/Debug:** Add a Run Configuration pointing to the built executable. For debugging the tray app, you may want to add a `--no-tray` flag that runs the switcher in console mode for easier debugging.

### Alternative IDEs
- **Visual Studio:** File → Open → CMake → select CMakeLists.txt
- **VS Code:** Install "CMake Tools" extension, open the folder
- **CLion:** Open the folder directly (native CMake support)

---

## 5. Build Steps

### Prerequisites
- Visual Studio 2022 Build Tools (or full VS) with C++ workload
- CMake 3.20+ (included with VS, or install separately)

### Build Commands
```bash
cd F:\OpenSource\SonarAudioSwitcher
cmake -B . -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Output: `Release/SonarAudioSwitcher.exe` (plus a companion `SonarDiag.exe` console tool)

The CMake project generates the `.sln` into the project root rather than a subdirectory so IDEs (and GitHub Copilot) don't complain about files outside the solution scope.

### CMakeLists.txt Outline
```cmake
cmake_minimum_required(VERSION 3.20)
project(SonarAudioSwitcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SonarAudioSwitcher WIN32
    src/main.cpp
    src/config.cpp
    src/process_monitor.cpp
    src/sonar_client.cpp
    src/device_matcher.cpp
    src/switcher.cpp
    src/startup.cpp
    src/logger.cpp
    src/imgui_settings_window.cpp
    resources/app.rc

    # Dear ImGui (vendored)
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/imgui_impl_win32.cpp
    vendor/imgui/imgui_impl_dx11.cpp
    vendor/imgui/imgui_stdlib.cpp
)

target_include_directories(SonarAudioSwitcher PRIVATE vendor vendor/imgui src resources)
target_link_libraries(SonarAudioSwitcher PRIVATE
    comctl32          # common controls
    shell32           # Shell_NotifyIcon
    winhttp           # HTTPS discovery call
    advapi32          # Registry (startup entry)
    d3d11             # Dear ImGui DX11 backend
    d3dcompiler
    dxgi
    dwmapi
    ole32
)

# Diagnostic tool — standalone console app for testing Sonar API connectivity
add_executable(SonarDiag src/diag.cpp)
target_include_directories(SonarDiag PRIVATE vendor src resources)
target_link_libraries(SonarDiag PRIVATE winhttp)
```

---

## 6. Configuration File Format

`config.json` (stored next to the executable):

```json
{
    "pollIntervalMs": 2000,
    "default": {
        "outputDevice": "Speakers",
        "inputDevice": "Microphone"
    },
    "rules": [
        {
            "exe": "cs2.exe",
            "outputDevice": "Arctis Nova",
            "inputDevice": "Arctis Nova"
        },
        {
            "exe": "Discord.exe",
            "outputDevice": "Arctis Nova",
            "inputDevice": "Arctis Nova"
        }
    ]
}
```

---

## 7. Testing Strategy

### Unit Tests (GoogleTest or Catch2)

Testable components in isolation:

| Component | What to test |
|---|---|
| `config` | Round-trip load/save of JSON, handling of missing/malformed fields, empty rules list |
| `device_matcher` | Partial name matching: exact match, substring, case-insensitivity, no match returns nullopt, multiple matches returns first |
| `switcher` (logic only) | Rule priority resolution: first match wins, default fallback, no redundant switches when same rule active |

These components are pure logic with no Win32 dependencies, making them straightforward to test.

### Integration / Manual Testing

| Scenario | How to verify |
|---|---|
| Sonar discovery | Run with Sonar open, check that the correct port is found |
| Device switching | Launch a target exe, verify Sonar UI shows the device changed |
| Streamer mode | Enable streamer mode in Sonar, verify only monitoring device changes |
| Sonar restart | Restart SteelSeries GG, verify the app recovers and finds the new port |
| Config reload | Edit config.json while running, click "Save" in settings, verify new rules take effect |
| Startup | Reboot, verify the app appears in the tray |

### Debug Mode

Add a `--console` flag that:
- Allocates a console window (`AllocConsole`)
- Prints each poll tick: running processes matched, rule selected, API calls made
- Makes it easy to verify behavior without inspecting Sonar's UI

---

## 8. Implementation Order

| Phase | What | Milestone | Status |
|---|---|---|---|
| **1** | Project scaffold: CMakeLists.txt, vendor headers, empty source files | Builds successfully | ✅ Done |
| **2** | `config` — JSON load/save with hardcoded test file | Can read/write config.json | ✅ Done |
| **3** | `process_monitor` — snapshot polling in a console app | Prints running processes to console | ✅ Done |
| **4** | `sonar_client` — discovery + device listing in a console app | Connects to Sonar, prints devices | ✅ Done |
| **5** | `device_matcher` — partial name resolution | Resolves "Arctis" → full device ID | ✅ Done |
| **6** | `switcher` — core loop tying it all together + `main.cpp` tray icon wiring | Switches Sonar devices when exe starts/stops; tray icon appears with context menu | ✅ Done |
| **7** | `imgui_settings_window` — rules management UI via Dear ImGui (Win32 + DX11) | Can add/edit/remove/reorder rules; save writes config.json and reloads the switcher | ✅ Done |
| **8** | Startup registration (registry or Task Scheduler) | App starts on boot | ✅ Done |
| **9** | Polish: error handling, logging, icon design | Production-ready | ✅ Done |

Phases 2-5 were developed and tested independently before being wired together in phase 6.

---

## 9. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Sonar API is undocumented and could change | Pin to known working GG version in docs; the API has been stable across versions per community reports |
| Device IDs change across reboots | Already handled — we match by partial name, not ID |
| GG restarts change the Sonar port | Re-run discovery on HTTP connection failure |
| `coreProps.json` path varies by GG version | Check both known paths: `SteelSeries Engine 3` and `GG` subdirectories |
| Self-signed cert for GG discovery endpoint | Disable SSL verification for localhost-only calls (acceptable security tradeoff) |
| Multiple rules match simultaneously | First match wins (list order = priority) — documented clearly in UI |
