#pragma once

#include <string>
#include <vector>
#include <optional>

struct Rule {
    bool enabled = true;
    std::string exeName;
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
