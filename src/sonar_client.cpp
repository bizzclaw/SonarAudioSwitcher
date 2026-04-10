#include "sonar_client.h"
#include "logger.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#define CPPHTTPLIB_NO_EXCEPTIONS
#include <httplib.h>
#include <json.hpp>

#include <fstream>
#include <sstream>

using json = nlohmann::json;

static constexpr const char* CORE_PROPS_PATH_ENGINE3 =
    "C:\\ProgramData\\SteelSeries\\SteelSeries Engine 3\\coreProps.json";
static constexpr const char* CORE_PROPS_PATH_GG =
    "C:\\ProgramData\\SteelSeries\\GG\\coreProps.json";

// --- Discovery helpers ---

std::optional<std::string> SonarClient::readCorePropsAddress()
{
    auto tryPath = [](const char* filePath) -> std::optional<std::string>
    {
        std::ifstream file(filePath);
        if (!file.is_open())
        {
            return std::nullopt;
        }

        try
        {
            json coreProps{};
            file >> coreProps;
            if (coreProps.contains("ggEncryptedAddress"))
            {
                return coreProps["ggEncryptedAddress"].get<std::string>();
            }
        }
        catch (const json::exception&)
        {
        }

        return std::nullopt;
    };

    auto address = tryPath(CORE_PROPS_PATH_ENGINE3);
    if (address.has_value())
    {
        return address;
    }

    return tryPath(CORE_PROPS_PATH_GG);
}

std::optional<std::string> SonarClient::querySonarWebServerAddress(const std::string& ggEncryptedAddress)
{
    // Parse host:port from ggEncryptedAddress
    auto colonPos = ggEncryptedAddress.find_last_of(':');
    if (colonPos == std::string::npos)
    {
        logMsg("SonarClient: invalid ggEncryptedAddress format");
        return std::nullopt;
    }

    std::string host = ggEncryptedAddress.substr(0, colonPos);
    int port = std::stoi(ggEncryptedAddress.substr(colonPos + 1));

    // Convert host to wide string for WinHTTP
    std::wstring wideHost(host.begin(), host.end());

    // Use WinHTTP for HTTPS with self-signed cert
    HINTERNET session = WinHttpOpen(L"SonarAudioSwitcher/1.0",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
    {
        logMsg("SonarClient: WinHttpOpen failed");
        return std::nullopt;
    }

    HINTERNET connection = WinHttpConnect(session, wideHost.c_str(), static_cast<INTERNET_PORT>(port), 0);
    if (!connection)
    {
        logMsg("SonarClient: WinHttpConnect failed");
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    HINTERNET request = WinHttpOpenRequest(connection, L"GET", L"/subApps",
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request)
    {
        logMsg("SonarClient: WinHttpOpenRequest failed");
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    // Disable SSL certificate verification (self-signed cert on localhost)
    DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
        SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
        SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
        SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));

    BOOL sendResult = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                         WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sendResult || !WinHttpReceiveResponse(request, nullptr))
    {
        logMsg("SonarClient: HTTPS request to /subApps failed");
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return std::nullopt;
    }

    // Read response body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(request, &bytesAvailable) && bytesAvailable > 0)
    {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(request, buffer.data(), bytesAvailable, &bytesRead))
        {
            responseBody.append(buffer.data(), bytesRead);
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);

    // Parse the response to extract Sonar's webServerAddress
    try
    {
        json subApps = json::parse(responseBody);
        if (subApps.contains("subApps") &&
            subApps["subApps"].contains("sonar") &&
            subApps["subApps"]["sonar"].contains("metadata") &&
            subApps["subApps"]["sonar"]["metadata"].contains("webServerAddress"))
        {
            return subApps["subApps"]["sonar"]["metadata"]["webServerAddress"].get<std::string>();
        }
    }
    catch (const json::exception&)
    {
    }

    logMsg("SonarClient: could not extract webServerAddress from /subApps");
    return std::nullopt;
}

std::string SonarClient::urlEncodeDeviceId(const std::string& deviceId)
{
    std::string encoded;
    encoded.reserve(deviceId.size() * 3);

    for (char character : deviceId)
    {
        if (character == '{')
        {
            encoded += "%7B";
        }
        else if (character == '}')
        {
            encoded += "%7D";
        }
        else
        {
            encoded += character;
        }
    }

    return encoded;
}

// --- Public API ---

bool SonarClient::discover()
{
    connected_ = false;

    auto ggAddress = readCorePropsAddress();
    if (!ggAddress.has_value())
    {
        logMsg("SonarClient: could not read ggEncryptedAddress from coreProps.json");
        return false;
    }

    auto webServerAddress = querySonarWebServerAddress(ggAddress.value());
    if (!webServerAddress.has_value())
    {
        return false;
    }

    baseUrl_ = webServerAddress.value();

    // Parse host and port from webServerAddress (e.g. "http://localhost:12268")
    std::string urlWithoutScheme = baseUrl_;
    auto schemeEnd = urlWithoutScheme.find("://");
    if (schemeEnd != std::string::npos)
    {
        urlWithoutScheme = urlWithoutScheme.substr(schemeEnd + 3);
    }

    auto portSeparator = urlWithoutScheme.find_last_of(':');
    if (portSeparator != std::string::npos)
    {
        host_ = urlWithoutScheme.substr(0, portSeparator);
        port_ = std::stoi(urlWithoutScheme.substr(portSeparator + 1));
    }
    else
    {
        host_ = urlWithoutScheme;
        port_ = 80;
    }

    // Verify connectivity by fetching the mode
    auto mode = getMode();
    if (!mode.has_value())
    {
        logMsg("SonarClient: discovered address but could not reach Sonar API");
        return false;
    }

    connected_ = true;
    logMsg("SonarClient: connected to Sonar at %s (mode: %s)", baseUrl_.c_str(), mode.value().c_str());
    return true;
}

bool SonarClient::isConnected() const
{
    return connected_;
}

std::optional<std::string> SonarClient::getMode()
{
    httplib::Client client(host_, port_);
    auto response = client.Get("/mode");
    if (!response || response->status != 200)
    {
        logMsg("SonarClient: GET /mode failed");
        connected_ = false;
        return std::nullopt;
    }

    // Response body is a quoted JSON string like "classic" or "stream"
    try
    {
        return json::parse(response->body).get<std::string>();
    }
    catch (const json::exception&)
    {
        return response->body;
    }
}

std::vector<AudioDevice> SonarClient::getAudioDevices()
{
    std::vector<AudioDevice> devices;

    httplib::Client client(host_, port_);
    auto response = client.Get("/audioDevices");
    if (!response || response->status != 200)
    {
        logMsg("SonarClient: GET /audioDevices failed");
        connected_ = false;
        return devices;
    }

    try
    {
        json devicesJson = json::parse(response->body);

        if (devicesJson.is_array())
        {
            // Sonar returns a flat array: [{friendlyName, id, dataFlow, ...}, ...]
            for (const auto& deviceJson : devicesJson)
            {
                AudioDevice device{};
                device.id = deviceJson.value("id", "");
                // The API uses "friendlyName" for the display name
                device.name = deviceJson.value("friendlyName", deviceJson.value("name", ""));
                // The API uses "dataFlow" for direction ("render" or "capture")
                device.direction = deviceJson.value("dataFlow", deviceJson.value("direction", ""));
                devices.push_back(device);
            }
        }
        else if (devicesJson.is_object())
        {
            // Fallback: grouped by direction {render: [...], capture: [...]}
            for (const auto& [direction, deviceList] : devicesJson.items())
            {
                if (!deviceList.is_array())
                    continue;

                for (const auto& deviceJson : deviceList)
                {
                    AudioDevice device{};
                    device.id = deviceJson.value("id", "");
                    device.name = deviceJson.value("friendlyName", deviceJson.value("name", ""));
                    device.direction = direction;
                    devices.push_back(device);
                }
            }
        }

        logMsg("SonarClient: found %d audio devices", static_cast<int>(devices.size()));
    }
    catch (const json::exception& parseError)
    {
        logMsg("SonarClient: failed to parse /audioDevices - %s", parseError.what());
    }

    return devices;
}

bool SonarClient::setClassicDevice(const std::string& channel, const std::string& deviceId)
{
    std::string path = "/classicRedirections/" + channel + "/deviceId/" + urlEncodeDeviceId(deviceId);

    httplib::Client client(host_, port_);
    auto response = client.Put(path, "", "application/json");
    if (!response || (response->status != 200 && response->status != 204))
    {
        logMsg("SonarClient: PUT %s failed", path.c_str());
        connected_ = false;
        return false;
    }

    return true;
}

bool SonarClient::setMonitoringDevice(const std::string& deviceId)
{
    std::string path = "/streamRedirections/monitoring/deviceId/" + urlEncodeDeviceId(deviceId);

    httplib::Client client(host_, port_);
    auto response = client.Put(path, "", "application/json");
    if (!response || (response->status != 200 && response->status != 204))
    {
        logMsg("SonarClient: PUT %s failed", path.c_str());
        connected_ = false;
        return false;
    }

    return true;
}

bool SonarClient::setChatCaptureDevice(const std::string& deviceId)
{
    std::string path = "/classicRedirections/chatCapture/deviceId/" + urlEncodeDeviceId(deviceId);

    httplib::Client client(host_, port_);
    auto response = client.Put(path, "", "application/json");
    if (!response || (response->status != 200 && response->status != 204))
    {
        logMsg("SonarClient: PUT %s failed", path.c_str());
        connected_ = false;
        return false;
    }

    return true;
}

bool SonarClient::setStreamMicDevice(const std::string& deviceId)
{
    std::string path = "/streamRedirections/mic/deviceId/" + urlEncodeDeviceId(deviceId);

    httplib::Client client(host_, port_);
    auto response = client.Put(path, "", "application/json");
    if (!response || (response->status != 200 && response->status != 204))
    {
        logMsg("SonarClient: PUT %s failed", path.c_str());
        connected_ = false;
        return false;
    }

    return true;
}
