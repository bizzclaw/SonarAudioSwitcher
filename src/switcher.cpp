#include "switcher.h"
#include "process_monitor.h"
#include "device_matcher.h"
#include "logger.h"
#include "resource.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>

// ---------------------------------------------------------------------------
//  Checks whether a rule's trigger condition is currently satisfied.
// ---------------------------------------------------------------------------
static bool isRuleActive(const Rule& rule,
                         const std::set<std::string>& processes,
                         const std::vector<AudioDevice>& devices)
{
    switch (rule.type)
    {
    case RuleType::Application:
        {
            std::string lowerExe = rule.exeName;
            std::transform(lowerExe.begin(), lowerExe.end(), lowerExe.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return processes.count(lowerExe) > 0;
        }
    case RuleType::Device:
        {
            return isAudioDevicePresent(devices, rule.deviceNameMatch);
        }
    }
    return false;
}


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
        lastAppliedRuleIdx_ = -2; // sentinel: force re-evaluation (distinct from -1 = default)
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
        lastAppliedRuleIdx_ = -1;
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
                lastAppliedRuleIdx_ = -2;
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

        // Fetch audio devices once per tick (needed for Device-type rules
        // and for applying the matched rule).
        auto audioDevices = client_.getAudioDevices();

        // 3. Find the highest-priority matching rule (first match in list order)
        const Rule* matchedRule = nullptr;
        int matchedRuleIdx = -1;
        for (int ruleIdx = 0; ruleIdx < static_cast<int>(currentConfig.rules.size()); ruleIdx++)
        {
            const auto& rule = currentConfig.rules[ruleIdx];
            if (!rule.enabled)
            {
                continue;
            }

            if (isRuleActive(rule, processes, audioDevices))
            {
                matchedRule = &rule;
                matchedRuleIdx = ruleIdx;
                break;
            }
        }


        // Consume the refresh flag (the last-applied state was already
        // cleared when forceRefresh() was called, so normal evaluation
        // will re-apply the current rule).
        refreshRequested_.exchange(false);

        const Rule& activeRule = matchedRule ? *matchedRule : currentConfig.defaultRule;

        // 3. If the matched rule is the same as currently applied, skip
        if (matchedRuleIdx == lastAppliedRuleIdx_ &&
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

        // Build a human-readable label for logging and the tooltip
        std::string ruleLabel = "Default";
        if (matchedRule)
        {
            switch (matchedRule->type)
            {
            case RuleType::Application:
                ruleLabel = matchedRule->exeName + " (Application)";
                break;
            case RuleType::Device:
                ruleLabel = matchedRule->deviceNameMatch + " (Device)";
                break;
            }
        }

        // Log what we're switching to
        logMsg("Switcher: applying rule [%d] %s - output: \"%s\", input: \"%s\"",
               matchedRuleIdx,
               ruleLabel.c_str(),
               activeRule.outputDevice.c_str(),
               activeRule.inputDevice.c_str());

        // 4. Apply the rule
        applyRule(activeRule, currentMode.value());

        // Remember what we applied
        lastAppliedRuleIdx_ = matchedRuleIdx;
        lastAppliedOutput_ = activeRule.outputDevice;
        lastAppliedInput_ = activeRule.inputDevice;

        // Update active profile name and notify UI
        {
            std::lock_guard<std::mutex> lock(mutex_);
            activeRule_ = ruleLabel;
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
    auto devices = client_.getAudioDevices();
    if (devices.empty())
    {
        logMsg("Switcher: no audio devices returned from Sonar");
        return;
    }

    logMsg("Switcher: mode is '%s', found %d audio devices", mode.c_str(), static_cast<int>(devices.size()));

    applyOutputDevice(devices, rule.outputDevice, mode);
    applyInputDevice(devices, rule.inputDevice, mode);
}

void Switcher::applyOutputDevice(const std::vector<AudioDevice>& devices, const std::string& outputDeviceName,
                                 const std::string& mode)
{
    if (outputDeviceName.empty())
    {
        return;
    }

    auto outputId = findDeviceId(devices, outputDeviceName, "render");
    if (!outputId.has_value())
    {
        logMsg("Switcher: no render device matching \"%s\"", outputDeviceName.c_str());
        return;
    }

    logMsg("Switcher: resolved output \"%s\" -> %s", outputDeviceName.c_str(), outputId.value().c_str());

    if (mode != "classic")
    {
        // Stream mode: only change monitoring device, never touch streaming
        client_.setMonitoringDevice(outputId.value());
        return;
    }

    // Classic mode has separate channels — set them all
    static const char* renderChannels[] = {"master", "game", "chatRender", "media", "aux"};
    for (const auto* channel : renderChannels)
    {
        if (!client_.setClassicDevice(channel, outputId.value()))
        {
            logMsg("Switcher: failed to set classic channel '%s'", channel);
        }
    }
}

void Switcher::applyInputDevice(const std::vector<AudioDevice>& devices, const std::string& inputDeviceName,
                                const std::string& mode)
{
    if (inputDeviceName.empty())
    {
        return;
    }

    auto inputId = findDeviceId(devices, inputDeviceName, "capture");
    if (!inputId.has_value())
    {
        logMsg("Switcher: no capture device matching \"%s\"", inputDeviceName.c_str());
        return;
    }

    if (mode != "classic")
    {
        // Stream mode: use streamRedirections/mic endpoint
        client_.setStreamMicDevice(inputId.value());
        return;
    }

    client_.setChatCaptureDevice(inputId.value());
}
