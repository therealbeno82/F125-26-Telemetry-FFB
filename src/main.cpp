#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <tchar.h>
#include <cstdio>

#include "types.h"
#include "udp_receiver.h"
#include "ffb_engine.h"
#include "settings_io.h"
#include "output/wheel_output.h"
#include "gui/imgui_ui.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")

// ── Globals ───────────────────────────────────────────────────────────────────
static ID3D11Device*           g_pd3dDevice    = nullptr;
static ID3D11DeviceContext*    g_pd3dCtx       = nullptr;
static IDXGISwapChain*         g_pSwapChain    = nullptr;
static ID3D11RenderTargetView* g_pRTV          = nullptr;
static HWND                    g_hwnd          = nullptr;

static TelemetryState  g_telem;
static FFBSettings     g_settings;
static FFBSignals      g_signals;
static AppStats        g_stats;

// ── D3D11 helpers ─────────────────────────────────────────────────────────────
static bool createD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount                        = 2;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags                              = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;
    sd.SwapEffect                         = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        featureLevels, 2, D3D11_SDK_VERSION,
        &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dCtx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* pBack = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
    g_pd3dDevice->CreateRenderTargetView(pBack, nullptr, &g_pRTV);
    pBack->Release();
    return true;
}

static void cleanupD3D() {
    if (g_pRTV)       { g_pRTV->Release();       g_pRTV = nullptr; }
    if (g_pSwapChain) { g_pSwapChain->Release();  g_pSwapChain = nullptr; }
    if (g_pd3dCtx)    { g_pd3dCtx->Release();     g_pd3dCtx = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release();  g_pd3dDevice = nullptr; }
}

static void resizeRTV() {
    if (g_pRTV)  { g_pRTV->Release(); g_pRTV = nullptr; }
    g_pSwapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    ID3D11Texture2D* pBack = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
    g_pd3dDevice->CreateRenderTargetView(pBack, nullptr, &g_pRTV);
    pBack->Release();
}

// ── Win32 window proc ─────────────────────────────────────────────────────────
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wp != SIZE_MINIMIZED) resizeRTV();
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ── Entry point ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // Register window class
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_CLASSDC;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"F1FFBClass";
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    // App icon from app.rc (resource id 1) — used for title bar & taskbar
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = wc.hIcon;
    RegisterClassExW(&wc);

    // Build the title from APP_VERSION (types.h) so it can never drift out of
    // sync with the version shown in the header. %hs prints the narrow macro
    // into the wide string.
    wchar_t title[160];
    swprintf(title, _countof(title),
             L"F1 FFB  ·  Force Feedback Enhancer  ·  %hs", APP_VERSION);

    g_hwnd = CreateWindowExW(
        0, L"F1FFBClass", title,
        WS_OVERLAPPEDWINDOW,
        100, 100, 1200, 780,
        nullptr, nullptr, hInst, nullptr);

    if (!createD3D(g_hwnd)) {
        MessageBoxW(nullptr, L"Failed to create D3D11 device", L"F1 FFB", MB_OK|MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    // Restore the active profile before any subsystem reads the settings.
    // First run (no profiles yet): seed a "Default" from the built-in values,
    // migrating the legacy single settings file if one exists.
    if (profiles::list().empty()) {
        loadSettings(g_settings);                 // legacy file, if present
        std::string n = profiles::save("Default", g_settings);
        profiles::writeActive(n);
    } else {
        std::string active = profiles::readActive();
        if (active.empty() || !profiles::load(active, g_settings)) {
            // Active record missing/stale — fall back to the first profile
            active = profiles::list().front();
            profiles::load(active, g_settings);
            profiles::writeActive(active);
        }
    }

    // ── Initialise subsystems ─────────────────────────────────────────────────
    WheelOutput wheel;
    if (!wheel.init()) {
        MessageBoxW(nullptr, L"FFB (SDL) init failed", L"F1 FFB", MB_OK|MB_ICONWARNING);
    }
    // Auto-open first device if found
    if (!wheel.devices().empty()) {
        wheel.openDevice(0);
    }

    UdpReceiver udp(g_telem, g_stats);
    if (!udp.start()) {
        MessageBoxW(nullptr,
            L"Could not bind UDP port 20777.\n"
            L"Another app may already be using it.\n"
            L"Close it and restart F1 FFB.",
            L"F1 FFB", MB_OK | MB_ICONWARNING);
    }

    // FFB engine: runs at ffbUpdateHz, calls output directly
    FFBEngine engine(g_telem, g_settings, g_stats,
        [&](const FFBSignals& sig) {
            g_signals = sig;
            wheel.send(sig);
        });
    engine.start();

    ImGuiUI ui(g_telem, g_settings, g_signals, g_stats, wheel);
    ui.init(g_hwnd, g_pd3dDevice, g_pd3dCtx);

    // ── Main loop ─────────────────────────────────────────────────────────────
    const float BG[4] = { 0.078f, 0.071f, 0.059f, 1.f };   // carbon (matches theme)
    MSG msg{};
    while (!ui.wantsQuit()) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) goto done;
        }

        g_pd3dCtx->OMSetRenderTargets(1, &g_pRTV, nullptr);
        g_pd3dCtx->ClearRenderTargetView(g_pRTV, BG);

        ui.render();

        g_pSwapChain->Present(1, 0);   // vsync
    }

done:
    {   // Persist any live tweaks back into the active profile for next launch
        std::string active = profiles::readActive();
        if (!active.empty()) profiles::save(active, g_settings);
    }
    engine.stop();
    udp.stop();
    ui.shutdown();
    wheel.closeDevice();
    cleanupD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(L"F1FFBClass", hInst);
    return 0;
}
