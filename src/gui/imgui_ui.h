#pragma once
#include "../types.h"
#include "../output/wheel_output.h"
#include <d3d11.h>
#include <string>
#include <vector>

class UdpReceiver;   // reference member; full definition pulled in by the .cpp

class ImGuiUI {
public:
    ImGuiUI(TelemetryState& state, FFBSettings& settings,
            FFBSignals& signals, AppStats& stats,
            WheelOutput& output, UdpReceiver& udp);

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
    void drawGuidePopup();    // Quick Start Guide (every launch until opted out)
    void applyStyleF1();
    void refreshProfiles();   // reload list + active index from disk

    TelemetryState&     m_state;
    FFBSettings&        m_settings;
    FFBSignals&         m_signals;
    AppStats&           m_stats;
    WheelOutput&        m_output;
    UdpReceiver&        m_udp;

    bool m_quit           = false;
    bool m_engineDirty    = false;
    std::string m_deviceError;

    int         m_udpPort = 20777;   // port shown/edited in the device panel
    std::string m_portError;         // last rebind failure message (empty = ok)

    // Profiles
    std::vector<std::string> m_profiles;
    int  m_activeProfile = -1;        // index into m_profiles, or -1
    char m_newProfile[64] = "";       // text buffer for "Save As"

    bool m_testForce  = false;        // diagnostic steady-force toggle
    bool m_testRumble = false;        // diagnostic steady-rumble toggle
    bool m_showNag   = false;         // queue the donation reminder this session
    int  m_nagNumber = 0;             // which reminder this is (1st, 2nd, …)
    bool m_nagOptOut = false;         // "don't remind me again" checkbox state
    bool m_showGuide   = false;       // queue the Quick Start Guide this session
    bool m_guideOptOut = false;       // "don't show this again" checkbox state

    // Output history for the scope (channel-switchable: torque/friction/rumble)
    static constexpr int HISTORY = 200;
    float m_torqueHistory[HISTORY]{};
    float m_frictionHistory[HISTORY]{};
    float m_rumbleHistory[HISTORY]{};
    int   m_historyIdx   = 0;
    int   m_scopeChannel = 0;        // 0=torque, 1=friction, 2=rumble
};
