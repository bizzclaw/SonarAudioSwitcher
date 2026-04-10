#include "switcher.h"
#include "process_monitor.h"
#include "device_matcher.h"
#include "logger.h"
#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>

Switcher::~Switcher()
{
    stop();
}

void Switcher::start(const Config& config)
{
    if (running_.load())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
    }

    running_ = true;
    thread_ = std::thread(&Switcher::run, this);
}

void Switcher::stop()
{
    if (!running_.load())
    {
        return;
    }

    running_ = false;
    cv_.notify_all();

    if (thread_.joinable())
    {
        thread_.join();
    }
}

void Switcher::reloadConfig(const Config& config)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = config;
        configDirty_ = true;
    }
    cv_.notify_all();
}

void Switcher::setPaused(bool paused)
{
    bool wasPaused = paused_.exchange(paused);
    logMsg("Switcher: %s", paused ? "paused" : "resumed");

    // On unpause, trigger an immediate refresh so rules are re-evaluated now
    if (wasPaused && !paused)
    {
        forceRefresh();
    }

    // Always wake the loop so it can act on the new state
    cv_.notify_all();
}

bool Switcher::isPaused() const
{
    return paused_.load();
}

std::string Switcher::getActiveRule() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return activeRule_;
}

SonarStatus Switcher::getSonarStatus() const
{
    return sonarStatus_.load();
}

void Switcher::setNotifyWindow(HWND hwnd)
{
    std::lock_guard<std::mutex> lock(mutex_);
    notifyHwnd_ = hwnd;
}

void Switcher::forceRefresh()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastAppliedExe_.clear();
        lastAppliedOutput_.clear();
        lastAppliedInput_.clear();
        refreshRequested_ = true;
    }
    cv_.notify_all();
    logMsg("Switcher: refresh requested");
}

void Switcher::forceDefault()
{
    logMsg("Switcher: reset to default requested — applying immediately");

    // Grab the default rule under the lock
    Rule defaultRule;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        defaultRule = config_.defaultRule;
    }

    // Apply the default rule right now (this is called from the UI thread,
    // but applyRule only talks to Sonar via HTTP which is thread-safe).
    auto mode = client_.getMode();
    if (!mode.has_value())
    {
        logMsg("Switcher: forceDefault — could not reach Sonar, skipping apply");
        sonarStatus_ = SonarStatus::Disconnected;
        if (notifyHwnd_)
        {
            PostMessageW(notifyHwnd_, WM_RULE_CHANGED, 0, 0);
        }
    }
    else
    {
        applyRule(defaultRule, mode.value());
    }

    // Update bookkeeping so the switcher loop knows what's active
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastAppliedExe_.clear();
        lastAppliedOutput_ = defaultRule.outputDevice;
        lastAppliedInput_ = defaultRule.inputDevice;
        activeRule_ = "Default";

        if (notifyHwnd_)
        {
            PostMessageW(notifyHwnd_, WM_RULE_CHANGED, 0, 0);
        }
    }

    // Auto-pause so rule matching doesn't immediately override the default
    setPaused(true);
}

void Switcher::run()
{
    logMsg("Switcher: thread started");

    while (running_.load())
    {
        // Grab a snapshot of the config
        Config currentConfig;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            currentConfig = config_;

            if (configDirty_.load())
            {
                configDirty_ = false;
                // Force re-evaluation by clearing last applied state
                lastAppliedExe_.clear();
                lastAppliedOutput_.clear();
                lastAppliedInput_.clear();
                logMsg("Switcher: config reloaded");
            }
        }

        // If paused and no refresh requested, just wait and skip everything
        if (paused_.load() && !refreshRequested_.load())
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(currentConfig.pollIntervalMs),
                         [this]
                         {
                             return !running_.load() || configDirty_.load()
                                 || !paused_.load() || refreshRequested_.load();
                         });
            continue;
        }

        // Ensure we're connected to Sonar
        if (!client_.isConnected())
        {
            sonarStatus_ = SonarStatus::Connecting;
            logMsg("Switcher: attempting Sonar discovery...");
            if (!client_.discover())
            {
                sonarStatus_ = SonarStatus::Disconnected;
                logMsg("Switcher: Sonar not available, will retry");
                // Wake the UI so the tooltip reflects Disconnected immediately
                if (notifyHwnd_)
                {
                    PostMessageW(notifyHwnd_, WM_RULE_CHANGED, 0, 0);
                }
                // Wait before retrying discovery
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(5000),
                             [this] { return !running_.load() || configDirty_.load(); });
                continue;
            }
            sonarStatus_ = SonarStatus::Connected;
            if (notifyHwnd_)
            {
                PostMessageW(notifyHwnd_, WM_RULE_CHANGED, 0, 0);
            }
        }

        // 1. Get running processes
        auto processes = getRunningProcesses();

        // 2. Verify Sonar is still reachable — getMode() sets connected_ = false
        //    internally if the call fails, so the next iteration will rediscover.
        auto currentMode = client_.getMode();
        if (!currentMode.has_value())
        {
            sonarStatus_ = SonarStatus::Disconnected;
            logMsg("Switcher: lost connection to Sonar, will rediscover");
            if (notifyHwnd_)
            {
                PostMessageW(notifyHwnd_, WM_RULE_CHANGED, 0, 0);
            }
            continue;
        }

        // 3. Find the highest-priority matching rule (first match in list order)
        const Rule* matchedRule = nullptr;
        for (const auto& rule : currentConfig.rules)
        {
            // Skip disabled rules
            if (!rule.enabled)
                continue;

            // Convert exe name to lower for comparison (process list is already lowered)
            std::string lowerExe = rule.exeName;
            std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (processes.count(lowerExe) > 0)
            {
                matchedRule = &rule;
                break;
            }
        }


        // Consume the refresh flag (the last-applied state was already
        // cleared when forceRefresh() was called, so normal evaluation
        // will re-apply the current rule).
        refreshRequested_.exchange(false);

        const Rule& activeRule = matchedRule ? *matchedRule : currentConfig.defaultRule;
        std::string activeExe = matchedRule ? matchedRule->exeName : std::string{};

        // 3. If the matched rule is the same as currently applied, skip
        if (activeExe == lastAppliedExe_ &&
            activeRule.outputDevice == lastAppliedOutput_ &&
            activeRule.inputDevice == lastAppliedInput_)
        {
            // No change needed — wait for next tick
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(currentConfig.pollIntervalMs),
                         [this]
                         {
                             return !running_.load() || configDirty_.load()
                                 || refreshRequested_.load();
                         });
            continue;
        }

        // Log what we're switching to
        logMsg("Switcher: applying rule for %s - output: \"%s\", input: \"%s\"",
               activeExe.empty() ? "(default)" : activeExe.c_str(),
               activeRule.outputDevice.c_str(),
               activeRule.inputDevice.c_str());

        // 4. Apply the rule
        applyRule(activeRule, currentMode.value());

        // Remember what we applied
        lastAppliedExe_ = activeExe;
        lastAppliedOutput_ = activeRule.outputDevice;
        lastAppliedInput_ = activeRule.inputDevice;

        // Update active profile name and notify UI
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeRule_ = activeExe.empty() ? "Default" : activeExe;
            if (notifyHwnd_)
                PostMessageW(notifyHwnd_, WM_RULE_CHANGED, 0, 0);
        }

        // Wait for next poll interval
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(currentConfig.pollIntervalMs),
                     [this]
                     {
                         return !running_.load() || configDirty_.load()
                             || refreshRequested_.load();
                     });
    }

    logMsg("Switcher: thread stopped");
}

void Switcher::applyRule(const Rule& rule, const std::string& mode)
{
    // Fetch audio devices from Sonar
    auto devices = client_.getAudioDevices();
    if (devices.empty())
    {
        logMsg("Switcher: no audio devices returned from Sonar");
        return;
    }

    logMsg("Switcher: mode is '%s', found %d audio devices", mode.c_str(), static_cast<int>(devices.size()));

    // Resolve output device
    if (!rule.outputDevice.empty())
    {
        auto outputId = findDeviceId(devices, rule.outputDevice, "render");
        if (outputId.has_value())
        {
            logMsg("Switcher: resolved output \"%s\" -> %s", rule.outputDevice.c_str(), outputId.value().c_str());
            if (mode == "classic")
            {
                // Classic mode has separate channels — set them all
                static const char* renderChannels[] = {
                    "master", "game", "chatRender", "media", "aux"
                };
                for (const auto* channel : renderChannels)
                {
                    if (!client_.setClassicDevice(channel, outputId.value()))
                    {
                        logMsg("Switcher: failed to set classic channel '%s'", channel);
                    }
                }
            }
            else
            {
                // Stream mode: only change monitoring device, never touch streaming
                client_.setMonitoringDevice(outputId.value());
            }
        }
        else
        {
            logMsg("Switcher: no render device matching \"%s\"", rule.outputDevice.c_str());
        }
    }

    // Resolve input device
    if (!rule.inputDevice.empty())
    {
        auto inputId = findDeviceId(devices, rule.inputDevice, "capture");
        if (inputId.has_value())
        {
            if (mode == "classic")
            {
                client_.setChatCaptureDevice(inputId.value());
            }
            else
            {
                // Stream mode: use streamRedirections/mic endpoint
                client_.setStreamMicDevice(inputId.value());
            }
        }
        else
        {
            logMsg("Switcher: no capture device matching \"%s\"", rule.inputDevice.c_str());
        }
    }
}
