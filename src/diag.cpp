#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// Vendor headers
#define CPPHTTPLIB_NO_EXCEPTIONS
#include <httplib.h>
#include <json.hpp>

using json = nlohmann::json;

static const char* CORE_PROPS_PATH_ENGINE3 = "C:\\ProgramData\\SteelSeries\\SteelSeries Engine 3\\coreProps.json";
static const char* CORE_PROPS_PATH_GG      = "C:\\ProgramData\\SteelSeries\\GG\\coreProps.json";

std::string readFile(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return content;
}

std::string httpsGet(const std::string& host, int port, const wchar_t* path) {
    std::wstring wHost(host.begin(), host.end());
    HINTERNET session = WinHttpOpen(L"Diag/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!session) { printf("  WinHttpOpen failed: %lu\n", GetLastError()); return {}; }

    HINTERNET conn = WinHttpConnect(session, wHost.c_str(), (INTERNET_PORT)port, 0);
    if (!conn) { printf("  WinHttpConnect failed: %lu\n", GetLastError()); WinHttpCloseHandle(session); return {}; }

    HINTERNET req = WinHttpOpenRequest(conn, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!req) { printf("  WinHttpOpenRequest failed: %lu\n", GetLastError()); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return {}; }

    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE |
                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

    BOOL ok = WinHttpSendRequest(req, NULL, 0, NULL, 0, 0, 0);
    if (!ok) { printf("  WinHttpSendRequest failed: %lu\n", GetLastError()); WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return {}; }
    if (!WinHttpReceiveResponse(req, NULL)) { printf("  WinHttpReceiveResponse failed: %lu\n", GetLastError()); WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session); return {}; }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &statusCode, &statusSize, NULL);
    printf("  HTTPS status: %lu\n", statusCode);

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        std::vector<char> buf(avail);
        DWORD read = 0;
        WinHttpReadData(req, buf.data(), avail, &read);
        body.append(buf.data(), read);
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(conn); WinHttpCloseHandle(session);
    return body;
}

int main() {
    printf("=== SonarAudioSwitcher Diagnostics ===\n\n");

    // Step 1: Read coreProps.json
    printf("[1] Reading coreProps.json...\n");
    std::string corePropsContent;
    const char* usedPath = nullptr;
    for (auto p : {CORE_PROPS_PATH_ENGINE3, CORE_PROPS_PATH_GG}) {
        corePropsContent = readFile(p);
        if (!corePropsContent.empty()) { usedPath = p; break; }
    }
    if (corePropsContent.empty()) {
        printf("  FAIL: Could not read coreProps.json from either path.\n");
        return 1;
    }
    printf("  OK: Read from %s\n", usedPath);
    printf("  Content: %s\n", corePropsContent.c_str());

    // Step 2: Parse ggEncryptedAddress
    printf("\n[2] Parsing ggEncryptedAddress...\n");
    json coreProps;
    try { coreProps = json::parse(corePropsContent); } catch (const json::exception& e) {
        printf("  FAIL: JSON parse error: %s\n", e.what()); return 1;
    }
    if (!coreProps.contains("ggEncryptedAddress")) {
        printf("  FAIL: No 'ggEncryptedAddress' key found. Keys present:");
        for (auto& [k, v] : coreProps.items()) printf(" %s", k.c_str());
        printf("\n");
        return 1;
    }
    std::string ggAddr = coreProps["ggEncryptedAddress"].get<std::string>();
    printf("  OK: ggEncryptedAddress = %s\n", ggAddr.c_str());

    // Step 3: Query /subApps via HTTPS
    printf("\n[3] Querying https://%s/subApps ...\n", ggAddr.c_str());
    auto colon = ggAddr.find_last_of(':');
    std::string ggHost = ggAddr.substr(0, colon);
    int ggPort = std::stoi(ggAddr.substr(colon + 1));
    std::string subAppsBody = httpsGet(ggHost, ggPort, L"/subApps");
    if (subAppsBody.empty()) {
        printf("  FAIL: Empty response from /subApps\n");
        return 1;
    }
    printf("  Response (first 500 chars): %.500s\n", subAppsBody.c_str());

    // Step 4: Extract Sonar webServerAddress
    printf("\n[4] Extracting Sonar webServerAddress...\n");
    std::string sonarUrl;
    try {
        json sa = json::parse(subAppsBody);
        if (sa.contains("subApps") && sa["subApps"].contains("sonar")) {
            auto& sonar = sa["subApps"]["sonar"];
            printf("  sonar.isEnabled  = %s\n", sonar.value("isEnabled", false) ? "true" : "false");
            printf("  sonar.isReady    = %s\n", sonar.value("isReady", false) ? "true" : "false");
            printf("  sonar.isRunning  = %s\n", sonar.value("isRunning", false) ? "true" : "false");
            if (sonar.contains("metadata") && sonar["metadata"].contains("webServerAddress")) {
                sonarUrl = sonar["metadata"]["webServerAddress"].get<std::string>();
            }
        }
    } catch (const json::exception& e) {
        printf("  FAIL: JSON parse error: %s\n", e.what()); return 1;
    }
    if (sonarUrl.empty()) {
        printf("  FAIL: Could not extract webServerAddress\n");
        return 1;
    }
    printf("  OK: webServerAddress = %s\n", sonarUrl.c_str());

    // Parse host:port from sonarUrl
    std::string urlNoScheme = sonarUrl;
    auto schemeEnd = urlNoScheme.find("://");
    if (schemeEnd != std::string::npos) urlNoScheme = urlNoScheme.substr(schemeEnd + 3);
    auto portSep = urlNoScheme.find_last_of(':');
    std::string sonarHost = urlNoScheme.substr(0, portSep);
    int sonarPort = std::stoi(urlNoScheme.substr(portSep + 1));
    printf("  Parsed: host=%s port=%d\n", sonarHost.c_str(), sonarPort);

    // Step 5: GET /mode
    printf("\n[5] GET /mode ...\n");
    {
        httplib::Client cli(sonarHost, sonarPort);
        cli.set_connection_timeout(5);
        auto res = cli.Get("/mode");
        if (!res) {
            printf("  FAIL: No response (error: %s)\n", httplib::to_string(res.error()).c_str());
        } else {
            printf("  Status: %d  Body: %s\n", res->status, res->body.c_str());
        }
    }

    // Step 6: GET /audioDevices
    printf("\n[6] GET /audioDevices ...\n");
    {
        httplib::Client cli(sonarHost, sonarPort);
        cli.set_connection_timeout(5);
        auto res = cli.Get("/audioDevices");
        if (!res) {
            printf("  FAIL: No response (error: %s)\n", httplib::to_string(res.error()).c_str());
        } else {
            printf("  Status: %d\n", res->status);
            printf("  Raw body (first 2000 chars): %.2000s\n", res->body.c_str());
            try {
                json devJson = json::parse(res->body);
                printf("  JSON type: %s\n", devJson.is_array() ? "array" : devJson.is_object() ? "object" : "other");
                if (devJson.is_array()) {
                    for (auto& d : devJson) {
                        printf("  device: %s\n", d.dump().c_str());
                    }
                } else if (devJson.is_object()) {
                    for (auto& [direction, list] : devJson.items()) {
                        printf("  key='%s' type=%s\n", direction.c_str(), 
                               list.is_array() ? "array" : list.is_object() ? "object" : "other");
                        if (list.is_array()) {
                            for (auto& d : list) {
                                printf("    [%s] id=%s  name=%s\n",
                                       direction.c_str(),
                                       d.value("id", "?").c_str(),
                                       d.value("name", "?").c_str());
                            }
                        }
                    }
                }
            } catch (const json::exception& e) {
                printf("  Parse error: %s\n", e.what());
            }
        }
    }

    // Step 7: GET /classicRedirections (to see current routing)
    printf("\n[7] GET /classicRedirections ...\n");
    {
        httplib::Client cli(sonarHost, sonarPort);
        cli.set_connection_timeout(5);
        auto res = cli.Get("/classicRedirections");
        if (!res) {
            printf("  FAIL: No response (error: %s)\n", httplib::to_string(res.error()).c_str());
        } else {
            printf("  Status: %d\n  Body (first 1000 chars): %.1000s\n", res->status, res->body.c_str());
        }
    }

    // Step 8: GET /streamRedirections
    printf("\n[8] GET /streamRedirections ...\n");
    {
        httplib::Client cli(sonarHost, sonarPort);
        cli.set_connection_timeout(5);
        auto res = cli.Get("/streamRedirections");
        if (!res) {
            printf("  FAIL: No response (error: %s)\n", httplib::to_string(res.error()).c_str());
        } else {
            printf("  Status: %d\n  Body (first 1000 chars): %.1000s\n", res->status, res->body.c_str());
        }
    }

    // Step 9: Try a PUT to classicRedirections/master to test if PUT works
    printf("\n[9] Testing PUT (dry run — will read current master device and PUT it back)...\n");
    {
        httplib::Client cli(sonarHost, sonarPort);
        cli.set_connection_timeout(5);
        auto res = cli.Get("/classicRedirections");
        if (res && res->status == 200) {
            try {
                json redirects = json::parse(res->body);
                // Try to find the current master device ID
                std::string currentMasterDeviceId;
                for (auto& [channel, info] : redirects.items()) {
                    if (channel == "master" || channel == "game") {
                        if (info.contains("deviceId")) {
                            currentMasterDeviceId = info["deviceId"].get<std::string>();
                            break;
                        }
                        // Try nested structure
                        if (info.contains("classic") && info["classic"].contains("deviceId")) {
                            currentMasterDeviceId = info["classic"]["deviceId"].get<std::string>();
                            break;
                        }
                        if (info.is_object()) {
                            printf("  Channel '%s' structure: %s\n", channel.c_str(), info.dump(2).c_str());
                        }
                    }
                }
                if (currentMasterDeviceId.empty()) {
                    printf("  Could not determine current master device ID from redirections.\n");
                    printf("  Full redirections JSON:\n%s\n", redirects.dump(2).c_str());
                } else {
                    printf("  Current master deviceId: %s\n", currentMasterDeviceId.c_str());
                    // URL-encode the device ID
                    std::string encoded;
                    for (char c : currentMasterDeviceId) {
                        if (c == '{') encoded += "%7B";
                        else if (c == '}') encoded += "%7D";
                        else encoded += c;
                    }
                    std::string putPath = "/classicRedirections/master/deviceId/" + encoded;
                    printf("  PUT %s\n", putPath.c_str());
                    auto putRes = cli.Put(putPath, "", "application/json");
                    if (!putRes) {
                        printf("  FAIL: No response (error: %s)\n", httplib::to_string(putRes.error()).c_str());
                    } else {
                        printf("  Status: %d  Body: %s\n", putRes->status, putRes->body.c_str());
                    }
                }
            } catch (const json::exception& e) {
                printf("  JSON error: %s\n", e.what());
            }
        }
    }

    // Step 10: If stream mode, test PUT on streamRedirections/monitoring
    printf("\n[10] Testing stream mode PUT (re-set current monitoring device)...\n");
    {
        httplib::Client cli(sonarHost, sonarPort);
        cli.set_connection_timeout(5);
        auto res = cli.Get("/streamRedirections");
        if (res && res->status == 200) {
            try {
                json sr = json::parse(res->body);
                std::string monDeviceId;
                if (sr.is_array()) {
                    for (auto& entry : sr) {
                        if (entry.value("streamRedirectionId", "") == "monitoring") {
                            monDeviceId = entry.value("deviceId", "");
                            break;
                        }
                    }
                }
                if (monDeviceId.empty()) {
                    printf("  Could not find monitoring device ID\n");
                } else {
                    printf("  Current monitoring deviceId: %s\n", monDeviceId.c_str());
                    std::string encoded;
                    for (char c : monDeviceId) {
                        if (c == '{') encoded += "%7B";
                        else if (c == '}') encoded += "%7D";
                        else encoded += c;
                    }
                    std::string putPath = "/streamRedirections/monitoring/deviceId/" + encoded;
                    printf("  PUT %s\n", putPath.c_str());
                    auto putRes = cli.Put(putPath, "", "application/json");
                    if (!putRes) {
                        printf("  FAIL: No response (error: %s)\n", httplib::to_string(putRes.error()).c_str());
                    } else {
                        printf("  Status: %d  Body: %.500s\n", putRes->status, putRes->body.c_str());
                    }
                }
            } catch (const json::exception& e) {
                printf("  JSON error: %s\n", e.what());
            }
        }
    }

    printf("\n=== Done ===\n");
    return 0;
}



