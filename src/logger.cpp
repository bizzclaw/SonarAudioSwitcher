#include "logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <string>
#include <mutex>

static FILE* g_logFile = nullptr;
static bool g_consoleMode = false;
static std::mutex g_logMutex;

// Max size before the log file is truncated on startup (~1 MB)
static constexpr long MAX_LOG_SIZE = 1024 * 1024;

static std::wstring getLogFilePath()
{
    // Use %APPDATA%/SonarAudioSwitcher/ — same directory as config.json
    wchar_t* appDataRaw = nullptr;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataRaw);
    if (SUCCEEDED(result) && appDataRaw)
    {
        std::wstring logDir = std::wstring(appDataRaw) + L"\\SonarAudioSwitcher";
        CoTaskMemFree(appDataRaw);
        CreateDirectoryW(logDir.c_str(), nullptr);
        return logDir + L"\\sonar_audio_switcher.log";
    }

    if (appDataRaw)
    {
        CoTaskMemFree(appDataRaw);
    }

    // Fallback: next to the executable
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring path(exePath);
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        path = path.substr(0, pos + 1);
    }

    return path + L"sonar_audio_switcher.log";
}

void logInit(bool consoleMode)
{
    g_consoleMode = consoleMode;

    std::wstring logPath = getLogFilePath();

    // If the log file exceeds MAX_LOG_SIZE, truncate it
    FILE* probe = _wfopen(logPath.c_str(), L"rb");
    if (probe)
    {
        fseek(probe, 0, SEEK_END);
        long size = ftell(probe);
        fclose(probe);
        if (size > MAX_LOG_SIZE)
        {
            // Truncate by opening in write mode
            FILE* trunc = _wfopen(logPath.c_str(), L"w");
            if (trunc)
            {
                fprintf(trunc, "--- log truncated (was %ld bytes) ---\n", size);
                fclose(trunc);
            }
        }
    }

    g_logFile = _wfopen(logPath.c_str(), L"a");
    if (g_logFile)
    {
        fprintf(g_logFile, "\n=== SonarAudioSwitcher started ===\n");
        fflush(g_logFile);
    }
}

void logShutdown()
{
    if (g_logFile)
    {
        fprintf(g_logFile, "=== SonarAudioSwitcher stopped ===\n");
        fclose(g_logFile);
        g_logFile = nullptr;
    }
}

void logMsg(const char* fmt, ...)
{
    // Format the user's message
    char userBuf[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(userBuf, sizeof(userBuf), fmt, args);
    va_end(args);

    // Build a timestamped line
    char timeBuf[64];
    {
        time_t now = time(nullptr);
        struct tm local = {};
        localtime_s(&local, &now);
        strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &local);
    }

    char line[2200];
    snprintf(line, sizeof(line), "[%s] %s\n", timeBuf, userBuf);

    std::lock_guard<std::mutex> lock(g_logMutex);

    // Always send to OutputDebugString
    OutputDebugStringA(line);

    // Write to log file
    if (g_logFile)
    {
        fputs(line, g_logFile);
        fflush(g_logFile);
    }

    // Console output
    if (g_consoleMode)
    {
        fputs(line, stdout);
    }
}
