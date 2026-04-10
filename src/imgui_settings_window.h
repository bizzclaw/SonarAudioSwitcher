#pragma once

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "config.h"
#include <functional>

// Callback invoked when the user clicks Save. The caller should persist
// the config and notify the switcher to reload.
using SettingsSaveCallback = std::function<void(const Config&)>;

void showSettingsDialog(HINSTANCE hInstance, HWND parent, Config& config,
                        SettingsSaveCallback onSave = nullptr);

