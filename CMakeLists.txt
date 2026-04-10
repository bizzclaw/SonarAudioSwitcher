cmake_minimum_required(VERSION 3.20)
project(SonarAudioSwitcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(SonarAudioSwitcher WIN32
    src/main.cpp
    src/config.cpp
    src/process_monitor.cpp
    src/sonar_client.cpp
    src/device_matcher.cpp
    src/switcher.cpp
    src/startup.cpp
    src/logger.cpp
    src/imgui_settings_window.cpp
    resources/app.rc

    # Dear ImGui
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    vendor/imgui/imgui_impl_win32.cpp
    vendor/imgui/imgui_impl_dx11.cpp
    vendor/imgui/imgui_stdlib.cpp
)

target_include_directories(SonarAudioSwitcher PRIVATE vendor vendor/imgui src resources)
target_link_libraries(SonarAudioSwitcher PRIVATE
    comctl32
    shell32
    winhttp
    advapi32
    d3d11
    d3dcompiler
    dxgi
    dwmapi
)

# Embed the application manifest for common controls v6 and DPI awareness
set_target_properties(SonarAudioSwitcher PROPERTIES
    LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/resources/app.manifest"
)

# Diagnostic tool (console app for testing Sonar API connectivity)
add_executable(SonarDiag src/diag.cpp)
target_include_directories(SonarDiag PRIVATE vendor src resources)
target_link_libraries(SonarDiag PRIVATE winhttp)
