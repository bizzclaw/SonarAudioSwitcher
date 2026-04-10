#pragma once

#include <string>
#include <vector>
#include <optional>

enum class RuleType {
    Application = 0,
    Device = 1
};

struct Rule {
    bool enabled = true;
    RuleType type = RuleType::Application;
    std::string exeName;
    std::string deviceNameMatch;
    std::string outputDevice;
    std::string inputDevice;
};

struct Config {
    int pollIntervalMs = 2000;
    Rule defaultRule{};
    std::vector<Rule> rules;
};

std::wstring getDefaultConfigPath();
std::optional<Config> loadConfig(const std::wstring& path);
bool saveConfig(const Config& config, const std::wstring& path);
