#include "config.h"
#include "logger.h"

#include <json.hpp>
#include <fstream>
#include <sstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

using json = nlohmann::json;

static RuleType parseRuleType(const std::string& typeStr)
{
    return typeStr == "device"
               ? RuleType::Device
               : RuleType::Application; // Default to Application
}

static const char* ruleTypeToString(RuleType type)
{
    switch (type)
    {
    case RuleType::Device:
        return "device";
    case RuleType::Application:
    default:
        return "application";
    }
}

static Rule parseRule(const json& ruleJson)
{
    Rule rule{};
    rule.enabled = ruleJson.value("enabled", true);
    rule.type = parseRuleType(ruleJson.value("type", "application"));
    rule.exeName = ruleJson.value("exe", "");
    rule.deviceNameMatch = ruleJson.value("deviceNameMatch", "");
    rule.outputDevice = ruleJson.value("outputDevice", "");
    rule.inputDevice = ruleJson.value("inputDevice", "");
    return rule;
}

static json ruleToJson(const Rule& rule, bool includeExe)
{
    json ruleJson{};
    if (includeExe)
    {
        ruleJson["enabled"] = rule.enabled;
        ruleJson["type"] = ruleTypeToString(rule.type);
        ruleJson["exe"] = rule.exeName;
        ruleJson["deviceNameMatch"] = rule.deviceNameMatch;
    }
    ruleJson["outputDevice"] = rule.outputDevice;
    ruleJson["inputDevice"] = rule.inputDevice;
    return ruleJson;
}

std::wstring getDefaultConfigPath()
{
    // Use %APPDATA%/SonarAudioSwitcher/config.json — always writable for the
    // current user, unlike the exe directory which may be read-only.
    wchar_t* appDataRaw = nullptr;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataRaw);
    if (SUCCEEDED(result) && appDataRaw)
    {
        std::wstring configDir = std::wstring(appDataRaw) + L"\\SonarAudioSwitcher";
        CoTaskMemFree(appDataRaw);

        // Ensure the directory exists
        CreateDirectoryW(configDir.c_str(), nullptr);

        return configDir + L"\\config.json";
    }

    if (appDataRaw)
    {
        CoTaskMemFree(appDataRaw);
    }

    // Fallback: next to the executable (may fail if directory is read-only)
    logMsg("Config: could not resolve %%APPDATA%%, falling back to exe directory");
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring path(exePath);
    auto pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        path = path.substr(0, pos + 1);
    }

    return path + L"config.json";
}

std::optional<Config> loadConfig(const std::wstring& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        logMsg("Config: could not open config file");
        return std::nullopt;
    }

    json configJson{};
    try
    {
        file >> configJson;
    }
    catch (const json::parse_error& parseError)
    {
        logMsg("Config: parse error - %s", parseError.what());
        return std::nullopt;
    }

    Config config{};
    config.pollIntervalMs = configJson.value("pollIntervalMs", 2000);

    if (configJson.contains("default"))
    {
        config.defaultRule = parseRule(configJson["default"]);
    }

    if (configJson.contains("rules") && configJson["rules"].is_array())
    {
        for (const auto& ruleJson : configJson["rules"])
        {
            config.rules.push_back(parseRule(ruleJson));
        }
    }

    return config;
}

bool saveConfig(const Config& config, const std::wstring& path)
{
    json configJson{};
    configJson["pollIntervalMs"] = config.pollIntervalMs;
    configJson["default"] = ruleToJson(config.defaultRule, false);

    configJson["rules"] = json::array();
    for (const auto& rule : config.rules)
    {
        configJson["rules"].push_back(ruleToJson(rule, true));
    }

    std::ofstream file(path);
    if (!file.is_open())
    {
        logMsg("Config: could not write config file");
        return false;
    }

    file << configJson.dump(4) << std::endl;
    return file.good();
}
