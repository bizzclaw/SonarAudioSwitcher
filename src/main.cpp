#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include "resource.h"
#include "config.h"
#include "switcher.h"
#include "startup.h"
#include "logger.h"
#include "imgui_settings_window.h"

#include <cstdio>
#include <cstring>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

static constexpr const wchar_t* WND_CLASS_NAME = L"SonarAudioSwitcherMain";

static HINSTANCE g_hInstance = nullptr;
static HWND g_hwnd = nullptr;
static NOTIFYICONDATAW g_nid = {};
static Config g_config = {};
static Switcher g_switcher;

static void updateTrayIcon()
{
    WORD iconId = IDI_APP_ICON; // blue — paused (default)

    if (g_switcher.getSonarStatus() != SonarStatus::Connected)
    {
        iconId = IDI_APP_ICON_RED;
    }
    else if (!g_switcher.isPaused())
    {
        iconId = IDI_APP_ICON_GREEN;
    }

    HICON newIcon = LoadIconW(g_hInstance, MAKEINTRESOURCEW(iconId));
    if (newIcon && newIcon != g_nid.hIcon)
    {
        g_nid.hIcon = newIcon;
        Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    }
}

static void updateTrayTooltip()
{
    std::string activeRule = g_switcher.getActiveRule();
    if (activeRule.empty())
    {
        activeRule = "Default";
    }

    std::wstring tip = L"Active Rule: ";
    // Convert UTF-8 rule name to wide string
    int len = MultiByteToWideChar(CP_UTF8, 0, activeRule.c_str(), -1, nullptr, 0);
    if (len > 0)
    {
        std::wstring wRule(len - 1, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, activeRule.c_str(), -1, wRule.data(), len);
        tip += wRule;
    }

    switch (g_switcher.getSonarStatus())
    {
    case SonarStatus::Connected:
        tip += L"\nSonar: Connected";
        break;
    case SonarStatus::Connecting:
        tip += L"\nSonar: Connecting...";
        break;
    case SonarStatus::Disconnected:
        tip += L"\nSonar: Disconnected";
        break;
    }

    // szTip is 128 wchars max
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);

    updateTrayIcon();
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAY_ICON:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_OPEN_SETTINGS, L"Open Settings");

            // "Start with Windows" — checked if currently registered
            UINT startupFlags = MF_STRING | (isStartupEnabled() ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(menu, startupFlags, IDM_TOGGLE_STARTUP, L"Start with Windows");

            // "Paused" — checked if currently paused
            UINT pausedFlags = MF_STRING | (g_switcher.isPaused() ? MF_CHECKED : MF_UNCHECKED);
            AppendMenuW(menu, pausedFlags, IDM_TOGGLE_PAUSED, L"Paused");

            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_REFRESH, L"Refresh");
            AppendMenuW(menu, MF_STRING, IDM_RESET_DEFAULT, L"Reset to Default");

            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

            POINT pt{};
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_OPEN_SETTINGS:
            showSettingsDialog(g_hInstance, hwnd, g_config, [](const Config& newConfig)
            {
                saveConfig(newConfig, getDefaultConfigPath());
                g_switcher.reloadConfig(newConfig);
            });
            break;
        case IDM_TOGGLE_STARTUP:
            {
                bool currentlyEnabled = isStartupEnabled();
                if (setStartupEnabled(!currentlyEnabled))
                    logMsg("Startup registration %s", currentlyEnabled ? "disabled" : "enabled");
                else
                    logMsg("Failed to change startup registration");
                break;
            }
        case IDM_TOGGLE_PAUSED:
            {
                bool nowPaused = !g_switcher.isPaused();
                g_switcher.setPaused(nowPaused);
                updateTrayTooltip();
                break;
            }
        case IDM_EXIT:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        case IDM_REFRESH:
            g_switcher.forceRefresh();
            break;
        case IDM_RESET_DEFAULT:
            g_switcher.forceDefault();
            break;
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;

    case WM_RULE_CHANGED:
        // Switcher rule changed — always update the tray tooltip, even while paused
        updateTrayTooltip();
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool createTrayIcon(HWND hwnd, HINSTANCE hInstance)
{
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON));
    wcscpy_s(g_nid.szTip, L"SonarAudioSwitcher");
    return Shell_NotifyIconW(NIM_ADD, &g_nid) != FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int)
{
    g_hInstance = hInstance;

    // ---- Check for --console flag ----
    bool consoleMode = false;
    if (lpCmdLine && wcsstr(lpCmdLine, L"--console"))
    {
        consoleMode = true;
        AllocConsole();
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }

    logInit(consoleMode);
    logMsg("SonarAudioSwitcher starting%s", consoleMode ? " (console mode)" : "");

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WND_CLASS_NAME;
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(0, WND_CLASS_NAME, L"SonarAudioSwitcher",
                             0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!g_hwnd)
    {
        logMsg("Failed to create message window");
        logShutdown();
        return 1;
    }

    createTrayIcon(g_hwnd, hInstance);

    // Load config and start switcher
    auto configPath = getDefaultConfigPath();
    auto loadedConfig = loadConfig(configPath);
    if (loadedConfig.has_value())
    {
        g_config = loadedConfig.value();
        logMsg("Config loaded successfully (%d rules)", static_cast<int>(g_config.rules.size()));
    }
    else
    {
        logMsg("No config found, using defaults");
        // Save a default config so the user has something to edit
        saveConfig(g_config, configPath);
    }
    g_switcher.start(g_config);
    g_switcher.setNotifyWindow(g_hwnd);

    // Set initial tooltip
    updateTrayTooltip();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_switcher.stop();
    logMsg("SonarAudioSwitcher exiting");
    logShutdown();

    if (consoleMode)
        FreeConsole();

    return static_cast<int>(msg.wParam);
}
