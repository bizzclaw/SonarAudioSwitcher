#include "process_monitor.h"
#include "logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <memory>

struct SnapshotDeleter
{
    void operator()(HANDLE handle) const
    {
        if (handle == INVALID_HANDLE_VALUE)
        {
            return;
        }

        CloseHandle(handle);
    }
};

using SnapshotHandle = std::unique_ptr<void, SnapshotDeleter>;

static std::string toLower(const std::string& input)
{
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

std::set<std::string> getRunningProcesses()
{
    std::set<std::string> processes{};

    SnapshotHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.get() == INVALID_HANDLE_VALUE)
    {
        logMsg("ProcessMonitor: CreateToolhelp32Snapshot failed");
        return processes;
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (!Process32FirstW(snapshot.get(), &entry))
    {
        logMsg("ProcessMonitor: Process32FirstW failed");
        return processes;
    }

    do
    {
        int length = WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 0)
        {
            continue;
        }

        std::string exeName(length - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, entry.szExeFile, -1, exeName.data(), length, nullptr, nullptr);
        processes.insert(toLower(exeName));
    }
    while (Process32NextW(snapshot.get(), &entry));

    return processes;
}
