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

class Switcher {
public:
    ~Switcher();

    void start(const Config& config);
    void stop();
    void reloadConfig(const Config& config);

    // Pause / resume switching (no-op while paused; does not persist)
    void setPaused(bool paused);
    bool isPaused() const;

    // Returns the human-readable active profile name.
    // Empty string means "Default".
    std::string getActiveProfile() const;

    // Force re-apply the current matching rule immediately.
    void forceRefresh();

    // Force-apply the default rule right now and auto-pause.
    // Normal rule matching resumes when the user unpauses.
    void forceDefault();

    // When set, the switcher posts WM_PROFILE_CHANGED to this HWND
    // whenever the active profile changes.
    void setNotifyWindow(HWND hwnd);

private:
    void run();
    void applyRule(const Rule& rule);

    Config config_;
    SonarClient client_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> running_{false};
    std::atomic<bool> configDirty_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> refreshRequested_{false};

    HWND notifyHwnd_ = nullptr;

    std::thread thread_;
    std::string lastAppliedExe_;       // exe name of the last applied rule ("" = default)
    std::string lastAppliedOutput_;
    std::string lastAppliedInput_;
    std::string activeProfile_;        // current profile label shown in tooltip
};
