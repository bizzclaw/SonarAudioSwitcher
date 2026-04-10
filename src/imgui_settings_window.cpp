#include "imgui_settings_window.h"
#include "startup.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "imgui_stdlib.h"

#include <d3d11.h>
#include <dxgi.h>
#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
//  DX11 state
// ---------------------------------------------------------------------------
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations
static bool CreateDeviceD3D(HWND hWnd);
static void CleanupDeviceD3D();
static void CreateRenderTarget();
static void CleanupRenderTarget();
static LRESULT CALLBACK ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Forward declare the ImGui Win32 handler (defined in imgui_impl_win32.cpp)
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ---------------------------------------------------------------------------
//  Helper: label-above InputText (label on its own line, full-width input)
// ---------------------------------------------------------------------------
static void LabeledInput(const char* label, std::string* str)
{
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1.0f); // stretch to full available width
    // Use ## to hide the duplicate label from ImGui's ID but keep it unique
    char id[128];
    snprintf(id, sizeof(id), "##%s", label);
    ImGui::InputText(id, str);
}

// ---------------------------------------------------------------------------
//  Render the settings UI — immediate-mode ImGui calls
// ---------------------------------------------------------------------------
static void RenderSettingsUI(Config& editConfig, bool* startWithWindows, bool* saved, bool* running)
{
    const ImGuiIO& io = ImGui::GetIO();

    // Fill the entire OS window with a single ImGui window
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("SonarAudioSwitcher - Settings", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoTitleBar);

    // ---- General settings ----
    ImGui::SeparatorText("General");
    ImGui::Checkbox("Start with Windows", startWithWindows);

    ImGui::Spacing();

    // Polling interval — stored as ms, displayed as seconds
    {
        float pollSeconds = static_cast<float>(editConfig.pollIntervalMs) / 1000.0f;
        ImGui::TextUnformatted("Polling Interval - Seconds between app detection refresh");
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputFloat("##PollInterval", &pollSeconds, 0.5f, 1.0f, "%.1f"))
        {
            if (pollSeconds < 0.5f)
            {
                pollSeconds = 0.5f;
            }
            if (pollSeconds > 60.0f)
            {
                pollSeconds = 60.0f;
            }
            editConfig.pollIntervalMs = static_cast<int>(pollSeconds * 1000.0f);
        }
    }

    ImGui::Spacing();

    // ---- Default Rule ----
    ImGui::SeparatorText("Default Rule");
    ImGui::PushID("default");
    {
        LabeledInput("Output Device", &editConfig.defaultRule.outputDevice);
        LabeledInput("Input Device", &editConfig.defaultRule.inputDevice);
    }
    ImGui::PopID();

    ImGui::Spacing();

    // ---- Rules list ----
    ImGui::SeparatorText("Rules (higher = higher priority)");

    // Reserve space at the bottom for Add Rule + Save/Cancel buttons
    float bottomHeight = ImGui::GetFrameHeightWithSpacing() * 2 + ImGui::GetStyle().ItemSpacing.y * 2 + 8.0f;
    ImGui::BeginChild("RulesList", ImVec2(0, -bottomHeight), ImGuiChildFlags_Borders);

    int removeIdx = -1;
    int swapA = -1, swapB = -1;
    int ruleCount = static_cast<int>(editConfig.rules.size());

    for (int i = 0; i < ruleCount; i++)
    {
        ImGui::PushID(i);

        // Card header: "Rule N" label + action buttons on the right
        ImGui::Text("Rule %d", i + 1);

        // Calculate right-aligned position for the three buttons
        // SmallButton width = label width + 2 * frame padding
        float framePadX = ImGui::GetStyle().FramePadding.x;
        float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
        float upW      = ImGui::CalcTextSize("Up").x      + framePadX * 2.0f;
        float downW    = ImGui::CalcTextSize("Down").x    + framePadX * 2.0f;
        float removeW  = ImGui::CalcTextSize("Remove").x  + framePadX * 2.0f;
        float totalButtonW = upW + itemSpacing + downW + itemSpacing + removeW;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - totalButtonW);

        // Move Up
        ImGui::BeginDisabled(i == 0);
        if (ImGui::SmallButton("Up"))
        {
            swapA = i;
            swapB = i - 1;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        // Move Down
        ImGui::BeginDisabled(i == ruleCount - 1);
        if (ImGui::SmallButton("Down"))
        {
            swapA = i;
            swapB = i + 1;
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        // Remove
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.1f, 0.1f, 1.0f));
        if (ImGui::SmallButton("Remove"))
        {
            removeIdx = i;
        }
        ImGui::PopStyleColor(3);

        // Card fields — labels above inputs
        ImGui::Checkbox("Enabled", &editConfig.rules[i].enabled);
        LabeledInput("Exe Name", &editConfig.rules[i].exeName);
        LabeledInput("Output Device", &editConfig.rules[i].outputDevice);
        LabeledInput("Input Device", &editConfig.rules[i].inputDevice);

        ImGui::PopID();

        // Separator between cards
        if (i < ruleCount - 1)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
    }

    if (ruleCount == 0)
    {
        ImGui::TextDisabled("No rules defined. Click '+ Add Rule' to create one.");
    }

    ImGui::EndChild();

    // Deferred mutations — safe because we're outside the iteration
    if (swapA >= 0 && swapB >= 0)
        std::swap(editConfig.rules[swapA], editConfig.rules[swapB]);
    if (removeIdx >= 0)
        editConfig.rules.erase(editConfig.rules.begin() + removeIdx);

    // ---- Add Rule button ----
    if (ImGui::Button("+ Add Rule"))
    {
        editConfig.rules.push_back({});
    }

    // ---- Save / Cancel row ----
    ImGui::Separator();
    float avail = ImGui::GetContentRegionAvail().x;
    float buttonW = 80.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(avail - buttonW * 2 - spacing);

    if (ImGui::Button("Save", ImVec2(buttonW, 0)))
    {
        *saved = true;
        *running = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(buttonW, 0)))
    {
        *running = false;
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
//  showSettingsDialog — public API (drop-in replacement)
// ---------------------------------------------------------------------------
void showSettingsDialog(HINSTANCE hInstance, HWND parent, Config& config,
                        SettingsSaveCallback onSave)
{
    // ---- Register a window class for the settings window ----
    static const wchar_t* CLASS_NAME = L"SonarImGuiSettings";
    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = ImGuiWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)); // IDC_ARROW
        RegisterClassExW(&wc);
        classRegistered = true;
    }

    // ---- Determine DPI scale ----
    // Query the monitor DPI for the primary monitor (used for initial window sizing).
    // After window creation we re-query with the actual HWND for precision.
    float dpiScale = 1.0f;
    {
        HMONITOR hMon = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
        if (hMon)
            dpiScale = ImGui_ImplWin32_GetDpiScaleForMonitor(hMon);
        if (dpiScale < 1.0f) dpiScale = 1.0f;
    }

    // ---- Create the settings window ----
    // Scale base window size by DPI
    int baseW = 620, baseH = 520;
    int winW = static_cast<int>(baseW * dpiScale);
    int winH = static_cast<int>(baseH * dpiScale);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    HWND hwnd = CreateWindowExW(
        0, CLASS_NAME, L"SonarAudioSwitcher - Settings",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
        posX, posY, winW, winH,
        parent, nullptr, hInstance, nullptr);

    if (!hwnd)
        return;

    // Refine DPI scale with the actual window handle
    dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (dpiScale < 1.0f) dpiScale = 1.0f;

    // ---- Make it modal by disabling the parent ----
    BOOL parentWasEnabled = TRUE;
    if (parent && IsWindow(parent))
    {
        parentWasEnabled = IsWindowEnabled(parent);
        EnableWindow(parent, FALSE);
    }

    // ---- Initialise DX11 before showing the window ----
    // ShowWindow is deliberately deferred until after DX11 is ready so that
    // any WM_SIZE messages fired by the window becoming visible are handled
    // with a valid swap chain, not a null one.
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        if (parent && IsWindow(parent))
            EnableWindow(parent, parentWasEnabled);
        return;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // ---- Initialise ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't write imgui.ini
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // Scale the ImGui style by DPI
    ImGui::GetStyle().ScaleAllSizes(dpiScale);

    // Load a DPI-scaled font (Segoe UI from Windows)
    {
        float fontSize = 16.0f * dpiScale;
        char fontPath[MAX_PATH] = {};
        if (GetWindowsDirectoryA(fontPath, MAX_PATH))
        {
            std::string fp = std::string(fontPath) + "\\Fonts\\segoeui.ttf";
            if (GetFileAttributesA(fp.c_str()) != INVALID_FILE_ATTRIBUTES)
                io.Fonts->AddFontFromFileTTF(fp.c_str(), fontSize);
        }
    }

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ---- Edit a copy of the config ----
    Config editConfig = config;
    bool startWithWindows = isStartupEnabled();
    bool saved = false;
    bool running = true;

    // ---- Modal render loop ----
    while (running)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
            {
                running = false;
                // Re-post so the outer loop in main.cpp can pick it up
                PostQuitMessage(static_cast<int>(msg.wParam));
            }
        }
        if (!running)
            break;

        // Check if the window was destroyed (e.g. user clicked the X button)
        if (!IsWindow(hwnd))
        {
            running = false;
            break;
        }

        // ---- New frame ----
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderSettingsUI(editConfig, &startWithWindows, &saved, &running);

        // ---- Render ----
        ImGui::Render();
        const float clear_color[4] = {0.10f, 0.10f, 0.10f, 1.00f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present with vsync (1) to avoid burning CPU
        g_pSwapChain->Present(1, 0);
    }

    // ---- Apply config if saved ----
    if (saved)
    {
        config = editConfig;
        setStartupEnabled(startWithWindows);
        if (onSave)
            onSave(config);
    }

    // ---- Cleanup ----
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);

    // Re-enable parent
    if (parent && IsWindow(parent))
    {
        EnableWindow(parent, parentWasEnabled);
        SetForegroundWindow(parent);
    }
}

// ---------------------------------------------------------------------------
//  DX11 helpers
// ---------------------------------------------------------------------------
static bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL obtainedLevel;
    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        &featureLevel, 1, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &obtainedLevel, &g_pd3dDeviceContext);

    if (FAILED(hr))
        return false;

    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain)
    {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext)
    {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice)
    {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

static void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer)
    {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget()
{
    if (g_mainRenderTargetView)
    {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// ---------------------------------------------------------------------------
//  Window procedure for the ImGui settings window
// ---------------------------------------------------------------------------
static LRESULT CALLBACK ImGuiWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice && g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0,
                                        static_cast<UINT>(LOWORD(lParam)),
                                        static_cast<UINT>(HIWORD(lParam)),
                                        DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;

    case WM_CLOSE:
        // Don't call DestroyWindow here — the modal loop checks IsWindow()
        // and will exit cleanly. We just hide the window.
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        return 0;

    default:
        break;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
