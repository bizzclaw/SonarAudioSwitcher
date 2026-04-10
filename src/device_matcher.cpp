#include "device_matcher.h"
#include <algorithm>
#include <cctype>

static std::string toLowerStr(const std::string& input)
{
    std::string result = input;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::optional<std::string> findDeviceId(
    const std::vector<AudioDevice>& devices,
    const std::string& partialName,
    const std::string& type)
{
    if (partialName.empty())
    {
        return std::nullopt;
    }

    std::string lowerPartial = toLowerStr(partialName);

    for (const auto& device : devices)
    {
        // Filter by direction type ("render" for output, "capture" for input)
        if (!type.empty() && device.direction != type)
        {
            continue;
        }

        std::string lowerName = toLowerStr(device.name);
        if (lowerName.find(lowerPartial) != std::string::npos)
        {
            return device.id;
        }
    }

    return std::nullopt;
}

bool isAudioDevicePresent(
    const std::vector<AudioDevice>& devices,
    const std::string& partialName)
{
    if (partialName.empty())
    {
        return false;
    }

    std::string lowerPartial = toLowerStr(partialName);

    for (const auto& device : devices)
    {
        std::string lowerName = toLowerStr(device.name);
        if (lowerName.find(lowerPartial) != std::string::npos)
        {
            return true;
        }
    }

    return false;
}
