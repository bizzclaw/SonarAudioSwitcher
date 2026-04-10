#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "config.h"
#include "sonar_client.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <string>

enum class SonarStatus
{
    Disconnected,
    Connecting,
    Connected,
};

class Switcher {
public:
    ~Switcher();

    void start(const Config& config);
    void stop();
    void reloadConfig(const Config& config);

    // Pause / resume switching (no-op while paused; does not persist)
    void setPaused(bool paused);
    bool isPaused() const;

    // Returns the human-readable active rule name (exe name, or "Default").
    std::string getActiveRule() const;

    // Returns the current Sonar API connection status.
    SonarStatus getSonarStatus() const;

    // Force re-apply the current matching rule immediately.
    void forceRefresh();

    // Force-apply the default rule right now and auto-pause.
    // Normal rule matching resumes when the user unpauses.
    void forceDefault();

    // When set, the switcher posts WM_RULE_CHANGED to this HWND
    // whenever the active rule changes.
    void setNotifyWindow(HWND hwnd);

private:
    void run();
    void applyRule(const Rule& rule, const std::string& mode);

    Config config_;
    SonarClient client_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> configDirty_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> refreshRequested_{false};
    std::atomic<SonarStatus> sonarStatus_{SonarStatus::Disconnected};

    HWND notifyHwnd_ = nullptr;

    std::thread thread_;
    std::string lastAppliedExe_;       // exe name of the last applied rule ("" = default)
    std::string lastAppliedOutput_;
    std::string lastAppliedInput_;
    std::string activeRule_;           // current rule label shown in tooltip
};
