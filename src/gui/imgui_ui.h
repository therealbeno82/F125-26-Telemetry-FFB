#pragma once
#include "../types.h"
#include "../output/wheel_output.h"
#include <d3d11.h>
#include <string>
#include <vector>

class ImGuiUI {
public:
    ImGuiUI(TelemetryState& state, FFBSettings& settings,
            FFBSignals& signals, AppStats& stats,
            WheelOutput& output);

    bool init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx);
    void render();
    void shutdown();

    bool wantsQuit() const { return m_quit; }

private:
    void drawHeader();
    void drawGauges();
    void drawSettings();
    void drawDeviceSelector();
    void drawProfiles();
    void drawFooter();
    void drawNagPopup();      // occasional donation reminder (every 5th launch)
    void applyStyleF1();
    void refreshProfiles();   // reload list + active index from disk

    TelemetryState&     m_state;
    FFBSettings&        m_settings;
    FFBSignals&         m_signals;
    AppStats&           m_stats;
    WheelOutput&        m_output;

    bool m_quit           = false;
    bool m_engineDirty    = false;
    int  m_selectedDevice = 0;
    std::string m_deviceError;

    // Profiles
    std::vector<std::string> m_profiles;
    int  m_activeProfile = -1;        // index into m_profiles, or -1
    char m_newProfile[64] = "";       // text buffer for "Save As"

    bool m_testForce = false;         // diagnostic steady-force toggle
    bool m_showNag   = false;         // queue the donation reminder this session
    int  m_nagNumber = 0;             // which reminder this is (1st, 2nd, …)
    bool m_nagOptOut = false;         // "don't remind me again" checkbox state

    // Torque history for mini-graph
    static constexpr int HISTORY = 200;
    float m_torqueHistory[HISTORY]{};
    int   m_historyIdx = 0;
};
