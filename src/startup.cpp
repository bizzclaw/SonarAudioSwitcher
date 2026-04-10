#include "startup.h"
#include "logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

static constexpr const wchar_t* STARTUP_REG_KEY  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr const wchar_t* STARTUP_VAL_NAME = L"SonarAudioSwitcher";

// Get the full path of the currently running executable (quoted for safety)
static std::wstring getExePath()
{
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::wstring(L"\"") + buf + L"\"";
}

bool isStartupEnabled()
{
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS)
        return false;

    wchar_t data[MAX_PATH * 2] = {};
    DWORD dataSize = sizeof(data);
    DWORD type = 0;
    rc = RegQueryValueExW(hKey, STARTUP_VAL_NAME, nullptr, &type,
                          reinterpret_cast<BYTE*>(data), &dataSize);
    RegCloseKey(hKey);

    if (rc != ERROR_SUCCESS || type != REG_SZ)
        return false;

    // Compare stored path with current exe path (case-insensitive)
    std::wstring stored(data);
    std::wstring current = getExePath();

    // Simple case-insensitive compare
    return _wcsicmp(stored.c_str(), current.c_str()) == 0;
}

bool setStartupEnabled(bool enable)
{
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, STARTUP_REG_KEY, 0, KEY_WRITE, &hKey);
    if (rc != ERROR_SUCCESS)
    {
        logMsg("Startup: failed to open Run registry key");
        return false;
    }

    bool success = false;
    if (enable)
    {
        std::wstring exePath = getExePath();
        DWORD dataSize = static_cast<DWORD>((exePath.size() + 1) * sizeof(wchar_t));
        rc = RegSetValueExW(hKey, STARTUP_VAL_NAME, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(exePath.c_str()), dataSize);
        success = (rc == ERROR_SUCCESS);
        if (!success)
            logMsg("Startup: failed to write registry value");
    }
    else
    {
        rc = RegDeleteValueW(hKey, STARTUP_VAL_NAME);
        // If the value doesn't exist, that's fine — it's already "disabled"
        success = (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(hKey);
    return success;
}



