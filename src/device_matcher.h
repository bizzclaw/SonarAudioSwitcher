#pragma once

#include "sonar_client.h"
#include <optional>
#include <string>
#include <vector>

std::optional<std::string> findDeviceId(
    const std::vector<AudioDevice>& devices,
    const std::string& partialName,
    const std::string& type
);
