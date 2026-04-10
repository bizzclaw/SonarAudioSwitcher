#pragma once

#include <string>
#include <vector>
#include <optional>

struct AudioDevice
{
    std::string id;
    std::string name;
    std::string direction; // "render" or "capture"
};

class SonarClient
{
public:
    bool discover();
    bool isConnected() const;

    std::optional<std::string> getMode();
    std::vector<AudioDevice> getAudioDevices();

    bool setClassicDevice(const std::string& channel, const std::string& deviceId);
    bool setMonitoringDevice(const std::string& deviceId);
    bool setChatCaptureDevice(const std::string& deviceId);
    bool setStreamMicDevice(const std::string& deviceId);

private:
    std::optional<std::string> readCorePropsAddress();
    std::optional<std::string> querySonarWebServerAddress(const std::string& ggEncryptedAddress);
    std::string urlEncodeDeviceId(const std::string& deviceId);

    std::string baseUrl_;
    std::string host_;
    int port_ = 0;
    bool connected_ = false;
};
