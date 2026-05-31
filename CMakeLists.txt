cmake_minimum_required(VERSION 3.20)
project(F1FFB VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Release by default ────────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ── Download Dear ImGui via FetchContent ──────────────────────────────────────
include(FetchContent)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# ── SDL2 for force-feedback output (static, single-exe) ──────────────────────
# We drive the wheel through SDL's haptic API instead of raw DirectInput, which
# is required for direct-drive bases like the SimuCube 2.
set(SDL_SHARED        OFF CACHE BOOL "" FORCE)
set(SDL_STATIC        ON  CACHE BOOL "" FORCE)
set(SDL_TEST          OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL2)

# ── ImGui sources (we pick the D3D11 + Win32 backend) ────────────────────────
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

# ── App sources ───────────────────────────────────────────────────────────────
set(APP_SOURCES
    src/main.cpp
    src/udp_receiver.cpp
    src/ffb_engine.cpp
    src/settings_io.cpp
    src/output/wheel_output.cpp
    src/gui/imgui_ui.cpp
    app.rc                 # application icon
)

add_executable(F1FFB WIN32 ${APP_SOURCES} ${IMGUI_SOURCES})

target_include_directories(F1FFB PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# ── Windows system libs ───────────────────────────────────────────────────────
target_link_libraries(F1FFB PRIVATE
    SDL2::SDL2-static   # FFB output (haptic); pulls in dinput8/dxguid itself
    d3d11
    dxgi
    ws2_32       # Winsock for UDP
    winmm        # timeBeginPeriod (1ms timer resolution)
    dwmapi
)

# ── Compiler options ──────────────────────────────────────────────────────────
target_compile_options(F1FFB PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /MP /utf-8>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
)

# ── Copy exe to project root after build ─────────────────────────────────────
add_custom_command(TARGET F1FFB POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:F1FFB> ${CMAKE_SOURCE_DIR}/F1FFB.exe
    COMMENT "Copying F1FFB.exe to project root"
)
