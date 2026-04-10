#pragma once

// Query whether the app is registered to start with Windows
// (HKCU\Software\Microsoft\Windows\CurrentVersion\Run)
bool isStartupEnabled();

// Register or unregister the app for startup.
// Uses the current executable's full path as the registry value.
bool setStartupEnabled(bool enable);

