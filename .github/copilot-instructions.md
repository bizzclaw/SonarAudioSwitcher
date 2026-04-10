# SonarAudioSwitcher — Copilot Agent Instructions

You are working on **SonarAudioSwitcher**, a lightweight C++17 Windows system tray application that automatically switches SteelSeries Sonar audio devices based on which application is currently running.

Read `PLAN.md` at the project root for the full architecture, API reference, project structure, and implementation phases.

---

## Project Context

- **Language:** C++17, compiled with MSVC
- **Build system:** CMake 3.20+ (generates the `.sln` into the project root, not a subdirectory)
- **UI:**
  - **Tray icon + context menu:** raw Win32 (`Shell_NotifyIcon`, `TrackPopupMenu`) — handled in `main.cpp`
  - **Settings window:** Dear ImGui with the Win32 + DirectX 11 backend, vendored under `vendor/imgui/` and implemented in `imgui_settings_window.cpp`. Created on demand when the user opens settings; fully destroyed on close (no render cost while hidden).
- **HTTP:** `cpp-httplib` (vendored single header in `vendor/`) for plain HTTP Sonar calls; **WinHTTP** for the single HTTPS discovery call (to avoid an OpenSSL dependency on self-signed certs)
- **JSON:** `nlohmann/json` (vendored single header in `vendor/`)
- **Process monitoring:** `CreateToolhelp32Snapshot` polling (no WMI, no ETW)
- **Logging:** `logger.h` — file log under `%APPDATA%\SonarAudioSwitcher\` plus `OutputDebugStringA`; use `logMsg("fmt %s", ...)` rather than raw `OutputDebugStringA` so output shows up in both sinks
- **Target:** Windows 10/11 only, x64, no admin elevation required
- **Secondary target:** `SonarDiag.exe` — a small console tool built from `src/diag.cpp` for testing Sonar API connectivity without launching the tray app

---

## Code Style Rules

### Control Flow

- **Minimize nesting.** Never nest `if` statements more than two levels deep. Prefer early returns and guard clauses to flatten logic.
- **Always use braces** for `if`, `else`, `for`, and `while` blocks — even single-line bodies. This improves readability and prevents bugs when adding lines later.

```cpp
// BAD — deeply nested
void process(const Rule& rule) {
    if (rule.isValid()) {
        if (auto device = findDevice(rule.outputDevice)) {
            if (client.isConnected()) {
                client.setDevice(*device);
            }
        }
    }
}

// BAD — no braces on single-line body
if (!rule.isValid())
    return;

// GOOD — guard clauses with early return, always braced
void process(const Rule& rule) {
    if (!rule.isValid()) {
        return;
    }

    auto device = findDevice(rule.outputDevice);
    if (!device) {
        return;
    }

    if (!client.isConnected()) {
        return;
    }

    client.setDevice(*device);
}
```

### Functions and Decomposition

- **Break logic into small, reusable pieces.** Each function should do one thing. If a function exceeds ~40 lines, look for extractable helpers.
- **Name functions as verbs** that describe what they do: `discoverSonarAddress()`, `findDeviceByPartialName()`, `applyRule()`.
- **Prefer free functions over methods** when the function doesn't need access to object state.
- **Extract predicates and transforms** into named functions rather than inlining complex lambda bodies.

```cpp
// BAD — inline logic blob
for (auto& proc : processes) {
    std::string lower = proc;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == rule.exeName) { ... }
}

// GOOD — extracted helper
bool matchesExe(const std::string& processName, const std::string& target);
```

### Error Handling

- **Return `std::optional` or `std::expected`** for operations that can fail. Reserve exceptions for truly exceptional/unrecoverable situations.
- **Never silently swallow errors.** Log failures (at minimum to debug output via `OutputDebugStringA`), then degrade gracefully.
- **Fail fast on startup, degrade gracefully at runtime.** If config.json is malformed on load, report the error clearly. If a Sonar API call fails mid-operation, log it and retry on the next poll tick.

### Resource Management

- **RAII everywhere.** Wrap Win32 handles (`HANDLE`, `HKEY`, snapshots) in small RAII wrappers or `std::unique_ptr` with custom deleters. Never rely on manual `CloseHandle` calls.

```cpp
// Wrap CreateToolhelp32Snapshot
struct SnapshotDeleter {
    void operator()(HANDLE h) const {
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
        }
    }
};
using SnapshotHandle = std::unique_ptr<void, SnapshotDeleter>;
```

- **No raw `new`/`delete`.** Use `std::unique_ptr`, `std::shared_ptr`, stack allocation, or containers.
- **Always value-initialize variables** with `{}` when declaring structs, classes, or POD types. Uninitialized locals are compiler-dependent behavior.

```cpp
// BAD — uninitialized
Rule rule;
POINT pt;
MSG msg;

// GOOD — value-initialized
Rule rule{};
POINT pt{};
MSG msg{};
```

### Naming Conventions

- **No single-character or abbreviated variable names.** Every variable name should describe what it holds. Use `configJson` not `j`, `parseError` not `e`, `ruleIndex` not `i`. Longer descriptive names are always preferred over short ambiguous ones.
- **Types:** `PascalCase` — `SonarClient`, `DeviceMatcher`, `AudioDevice`
- **Functions:** `camelCase` — `getRunningProcesses()`, `findDeviceId()`
- **Variables:** `camelCase` — `pollInterval`, `activeRule`, `configJson`
- **Constants/Enums:** `PascalCase` or `UPPER_SNAKE_CASE` — `DefaultPollIntervalMs`, `MAX_RETRIES`
- **File names:** `snake_case.cpp` / `snake_case.h`
- **Win32 resource IDs:** `UPPER_SNAKE_CASE` — `IDC_RULE_LIST`, `IDD_SETTINGS_DIALOG`

### Headers

- Use `#pragma once` (MSVC-only project, no need for include guards).
- Order includes: project headers, vendor headers, standard library, Windows headers.
- Forward-declare where possible to keep headers lightweight.

### Threading

- The switcher runs on a background `std::thread`. The UI runs on the main thread (Win32 message loop).
- Protect shared state (config, active rule) with `std::mutex` and `std::lock_guard`.
- Use `std::condition_variable` or `WaitForSingleObject` for the poll sleep so the thread can be woken for immediate shutdown or config reload.
- **Never block the UI thread** with network calls or long operations.

---

## Win32 API Guidelines

- Use `W` (wide-char) variants of Win32 functions. Store paths and UI strings as `std::wstring`. Convert to/from UTF-8 (`std::string`) at the boundary when interfacing with JSON or the Sonar REST API.
- Register windows with descriptive class names: `L"SonarAudioSwitcherMain"`.
- Use a custom `WM_APP + N` message for tray icon callbacks, not `WM_USER`.
- Keep dialog procedures thin — extract logic into separate functions, use the dialog proc only for message routing.

---

## Sonar API Guidelines

- The Sonar port changes every time SteelSeries GG restarts. **Always re-discover on connection failure.**
- Device IDs contain curly braces that must be URL-encoded (`%7B`, `%7D`) in PUT paths.
- All PUT requests to Sonar use `Content-Length: 0` — parameters are in the URL path, not the body.
- **Never modify the streaming device** in streamer mode. Only change `monitoring` output and `chatCapture` input.
- Cache the device list. Only refresh it when a rule change requires device resolution or when a name lookup fails.

---

## Testing

- Unit-testable components (`config`, `device_matcher`, rule matching in `switcher`) should be pure functions with no Win32 or network dependencies.
- Run `SonarAudioSwitcher.exe --console` to allocate a console window alongside the tray — logs stream to it in real time alongside the file log.
- Run the standalone `SonarDiag.exe` to test Sonar discovery / API connectivity in isolation, without launching the tray app or touching the live switcher state.
- When writing new components, keep the testable logic separate from the Win32/network plumbing so it can be tested independently.

---

## What NOT To Do

- Do not add additional third-party GUI frameworks (Qt, wxWidgets, etc.). The tray surface is raw Win32 and the settings window is Dear ImGui (Win32 + DX11) — don't introduce a third one.
- Do not use WMI or ETW for process monitoring. Polling with `CreateToolhelp32Snapshot` is the deliberate choice.
- Do not store or match devices by Windows device ID — always resolve by partial name match at runtime.
- Do not touch the streaming audio route in streamer mode — only `monitoring` (output) and `chatCapture` / `streamMic` (input).
- Do not add features beyond what PLAN.md specifies without discussion.
- Do not add speculative abstractions, excessive comments, or unnecessary wrapper layers.
- Do not add OpenSSL or link `cpp-httplib`'s HTTPS support — WinHTTP handles the single HTTPS call to Sonar's discovery endpoint.
