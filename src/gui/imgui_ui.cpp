#include "imgui_ui.h"
#include "../settings_io.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <chrono>
#include <cmath>
#include <string>
#include <shellapi.h>                 // ShellExecute — open the donate link in a browser
#pragma comment(lib, "shell32.lib")

// Support / donation link, opened by the footer button.
static const char* KOFI_URL = "https://ko-fi.com/rapidbeno";

// ── Colours — "Amber / Carbon" theme ──────────────────────────────────────────
#define COL_ACCENT    ImVec4(0.961f,0.620f,0.043f,1.f)  // amber  #F59E0B
#define COL_ACCENT_HI ImVec4(0.984f,0.749f,0.141f,1.f)  // amber hover #FBBF24
#define COL_RED       ImVec4(0.937f,0.267f,0.267f,1.f)  // danger/clip #EF4444
#define COL_YEL       ImVec4(0.980f,0.800f,0.082f,1.f)  // warn   #FACC15
#define COL_GREEN     ImVec4(0.518f,0.800f,0.086f,1.f)  // good   #84CC16
#define COL_BLUE      ImVec4(0.360f,0.620f,0.950f,1.f)  // info accent (gauges)
#define COL_BG        ImVec4(0.078f,0.071f,0.059f,1.f)  // carbon #14120F
#define COL_PANEL     ImVec4(0.118f,0.106f,0.086f,1.f)  // panel  #1E1B16
#define COL_BORDER    ImVec4(0.200f,0.180f,0.145f,1.f)  // warm border
#define COL_TEXT      ImVec4(0.925f,0.910f,0.882f,1.f)  // text   #ECE8E1
#define COL_TEXT_SEC  ImVec4(0.604f,0.565f,0.471f,1.f)  // muted  #9A9078

static ImVec4 lerpCol(ImVec4 a, ImVec4 b, float t) {
    return { a.x + (b.x-a.x)*t, a.y + (b.y-a.y)*t,
             a.z + (b.z-a.z)*t, a.w + (b.w-a.w)*t };
}

// Vertical space the footer (separator + setup hint + Ko-fi button) needs.
// The two main columns reserve this much at their bottom so the footer is
// always pinned in view rather than overflowing into a scroll region.
// Computed from the live style/font so it scales with DPI / font size.
static float footerReserveH() {
    const ImGuiStyle& st = ImGui::GetStyle();
    return ImGui::GetFrameHeightWithSpacing()        // Ko-fi button row
         + ImGui::GetTextLineHeightWithSpacing()     // telemetry setup hint line
         + st.ItemSpacing.y * 3.f                     // separator + two Spacing()
         + 6.f;                                       // small safety margin
}

ImGuiUI::ImGuiUI(TelemetryState& state, FFBSettings& settings,
                 FFBSignals& signals, AppStats& stats,
                 WheelOutput& output)
    : m_state(state), m_settings(settings), m_signals(signals),
      m_stats(stats), m_output(output) {}

bool ImGuiUI::init(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* ctx) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "f1ffb_ui.ini";

    applyStyleF1();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(device, ctx);

    refreshProfiles();

    // Count this launch; show an occasional, dismissable donation reminder
    // every 5th time the app starts (5th, 10th, 15th, …). m_nagNumber is which
    // reminder this is (1, 2, 3, …) — the opt-out checkbox appears from the 2nd
    // onward. A stored opt-out flag suppresses it permanently.
    int launches = bumpLaunchCount();
    bool everyFifth = (launches > 0 && launches % 5 == 0);
    m_nagNumber = launches / 5;
    m_showNag   = everyFifth && !isDonationNagDisabled();

    return true;
}

void ImGuiUI::refreshProfiles() {
    m_profiles = profiles::list();
    std::string active = profiles::readActive();
    m_activeProfile = -1;
    for (int i = 0; i < (int)m_profiles.size(); i++)
        if (m_profiles[i] == active) { m_activeProfile = i; break; }
}

void ImGuiUI::applyStyleF1() {
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 4.f;
    s.ChildRounding     = 4.f;
    s.FrameRounding     = 3.f;
    s.GrabRounding      = 3.f;
    s.PopupRounding     = 3.f;
    s.ScrollbarRounding = 3.f;
    s.WindowBorderSize  = 1.f;
    s.FramePadding      = { 8.f, 5.f };
    s.ItemSpacing       = { 8.f, 6.f };
    s.WindowPadding     = { 12.f, 12.f };

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = COL_BG;
    c[ImGuiCol_ChildBg]          = COL_PANEL;
    c[ImGuiCol_Border]           = COL_BORDER;
    c[ImGuiCol_FrameBg]          = ImVec4(0.16f,0.145f,0.115f,1.f);
    c[ImGuiCol_FrameBgHovered]   = ImVec4(0.22f,0.20f, 0.16f, 1.f);
    c[ImGuiCol_FrameBgActive]    = ImVec4(0.27f,0.24f, 0.19f, 1.f);
    c[ImGuiCol_SliderGrab]       = COL_ACCENT;
    c[ImGuiCol_SliderGrabActive] = COL_ACCENT_HI;
    c[ImGuiCol_Button]           = COL_ACCENT;
    c[ImGuiCol_ButtonHovered]    = COL_ACCENT_HI;
    c[ImGuiCol_ButtonActive]     = ImVec4(0.78f,0.49f,0.03f,1.f);
    c[ImGuiCol_Header]           = ImVec4(0.961f,0.620f,0.043f,0.28f);
    c[ImGuiCol_HeaderHovered]    = ImVec4(0.961f,0.620f,0.043f,0.45f);
    c[ImGuiCol_HeaderActive]     = ImVec4(0.961f,0.620f,0.043f,0.70f);
    c[ImGuiCol_TitleBg]          = ImVec4(0.063f,0.055f,0.043f,1.f);
    c[ImGuiCol_TitleBgActive]    = ImVec4(0.063f,0.055f,0.043f,1.f);
    c[ImGuiCol_CheckMark]        = COL_ACCENT;
    c[ImGuiCol_PlotLines]        = COL_ACCENT;
    c[ImGuiCol_PlotHistogram]    = COL_GREEN;
    c[ImGuiCol_Separator]        = COL_BORDER;
    c[ImGuiCol_Text]             = COL_TEXT;
    c[ImGuiCol_TextDisabled]     = COL_TEXT_SEC;
}

void ImGuiUI::render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    // Full-screen dockable window
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus);

    drawHeader();
    ImGui::Spacing();

    // Two-column layout
    ImGui::Columns(2, "main_cols", false);
    ImGui::SetColumnWidth(0, io.DisplaySize.x * 0.55f);

    drawGauges();

    ImGui::NextColumn();

    drawDeviceSelector();
    ImGui::Spacing();
    drawProfiles();
    ImGui::Spacing();
    drawSettings();

    ImGui::Columns(1);
    drawFooter();

    // Occasional donation reminder — opened once when queued, then drawn every
    // frame so ImGui keeps the modal alive until the user dismisses it.
    if (m_showNag) {
        ImGui::OpenPopup("Enjoying F1 FFB?");
        m_showNag = false;
    }
    drawNagPopup();

    ImGui::End();
    ImGui::Render();
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

// ── Header ────────────────────────────────────────────────────────────────────

void ImGuiUI::drawHeader() {
    // Amber accent bar at top
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, {p.x + ImGui::GetContentRegionAvail().x, p.y + 3},
                      IM_COL32(245, 158, 11, 255));
    ImGui::Dummy({0, 6});

    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::Text("F1 FFB");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("Force Feedback Enhancer  ·  %s  |  F1 25 / F1 26", APP_VERSION);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180.f);

    // Connection status mirrors the engine's safety gate (FFBEngine::compute):
    // FFB is live only when the physics frame is advancing and past the re-arm
    // delay. Anything else (no data / paused / resuming) means the wheel is off.
    using namespace std::chrono;
    auto now     = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    auto lastPkt = m_stats.lastUdpTimeMs.load();
    auto adv     = m_stats.lastFrameAdvanceMs.load();
    auto streak  = m_stats.frameStreakStartMs.load();

    bool frozen   = (adv == 0) || (now - adv > FFB_PAUSE_FREEZE_MS);
    bool rearming = (now - streak < FFB_REARM_MS);

    if (lastPkt == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
        ImGui::Text("● WAITING...");
    } else if (!frozen && !rearming) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
        ImGui::Text("● CONNECTED");
    } else {
        // Paused / in menu / resuming → wheel released for safety
        ImGui::PushStyleColor(ImGuiCol_Text, COL_YEL);
        ImGui::Text("● PAUSED · FFB RELEASED");
    }
    ImGui::PopStyleColor();
    ImGui::Separator();
}

// ── Helper: arc gauge ─────────────────────────────────────────────────────────

static void drawArcGauge(const char* label, float value,
                          ImVec4 color, float size = 70.f) {
    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2      pos  = ImGui::GetCursorScreenPos();
    float       cx   = pos.x + size * 0.5f;
    float       cy   = pos.y + size * 0.55f;
    float       r    = size * 0.42f;
    const float START_ANG = 3.14159f * 0.75f;     // 135°
    const float SWEEP     = 3.14159f * 1.5f;      // 270°
    const int   SEGS      = 48;

    value = clampf(value, 0.f, 1.f);

    // Background arc
    dl->PathClear();
    for (int i = 0; i <= SEGS; i++) {
        float a = START_ANG + SWEEP * ((float)i / SEGS);
        dl->PathLineTo({cx + cosf(a)*r, cy + sinf(a)*r});
    }
    dl->PathStroke(IM_COL32(48,43,34,255), false, 5.f);

    // Value arc
    if (value > 0.001f) {
        int fillSegs = (int)(SEGS * value);
        dl->PathClear();
        for (int i = 0; i <= fillSegs; i++) {
            float a = START_ANG + SWEEP * ((float)i / SEGS);
            dl->PathLineTo({cx + cosf(a)*r, cy + sinf(a)*r});
        }
        ImU32 col = ImGui::ColorConvertFloat4ToU32(color);
        dl->PathStroke(col, false, 5.f);
    }

    // Centre text
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)(value * 100.f));
    ImVec2 ts = ImGui::CalcTextSize(pct);
    dl->AddText({cx - ts.x*0.5f, cy - ts.y*0.55f},
                IM_COL32(236,232,225,255), pct);

    // Label below
    ImVec2 ls = ImGui::CalcTextSize(label);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    dl->AddText({cx - ls.x*0.5f, cy + r*0.6f}, IM_COL32(154,144,120,255), label);
    ImGui::PopStyleColor();

    ImGui::Dummy({size, size});
}

// ── Gauges panel ─────────────────────────────────────────────────────────────

void ImGuiUI::drawGauges() {
    // Reserve footer height so the left column doesn't stretch to the very
    // bottom and push the footer off-screen.
    ImGui::BeginChild("gauges", {0, -footerReserveH()}, false);

    // Torque bar
    ImGui::TextDisabled("WHEEL TORQUE");
    // Clipping indicator (right-aligned) — lights up as torque demand saturates
    {
        float clip = m_stats.clipLevel.load();
        char clipbuf[16];
        if (clip < 0.02f) snprintf(clipbuf, sizeof(clipbuf), "CLIP  --");
        else              snprintf(clipbuf, sizeof(clipbuf), "CLIP %3.0f%%", clip * 100.f);
        ImVec4 col = clip < 0.02f ? COL_TEXT_SEC : (clip < 0.15f ? COL_YEL : COL_RED);
        float tw = ImGui::CalcTextSize(clipbuf).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(clipbuf);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How often the wheel is maxed out (force clipping). "
                              "Frequent clipping = you lose road detail; lower "
                              "Overall Strength or raise Max Force.");
    }
    {
        float t = m_signals.torque;
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0.16f,0.145f,0.115f,1.f));
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,  t >= 0.f ? COL_BLUE : COL_RED);

        // Draw torque as bi-directional bar using PlotHistogram trick
        float tNorm = (t + 1.f) * 0.5f;  // remap -1..1 to 0..1
        ImGui::ProgressBar(tNorm, {-1, 22}, "");
        ImGui::SameLine(0, 0);
        char tbuf[32];
        snprintf(tbuf, sizeof(tbuf), " %+.3f", t);
        ImGui::TextDisabled("%s", tbuf);
        ImGui::PopStyleColor(2);
    }

    // Torque history graph
    m_torqueHistory[m_historyIdx % HISTORY] = m_signals.torque;
    m_historyIdx++;
    char overlay[32];
    snprintf(overlay, sizeof(overlay), "t=%+.2f", m_signals.torque);
    ImGui::PlotLines("##tgraph", m_torqueHistory, HISTORY,
                     m_historyIdx % HISTORY, overlay,
                     -1.f, 1.f, {-1, 60});

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("LIVE SIGNALS");
    ImGui::Spacing();

    // 4 arc gauges in a row
    float gaugeSize = (ImGui::GetContentRegionAvail().x - 30.f) / 4.f;
    gaugeSize = gaugeSize < 60.f ? 60.f : gaugeSize;

    drawArcGauge("UNDERSTEER", m_state.understeer,   COL_RED,   gaugeSize);
    ImGui::SameLine(0, 6);
    drawArcGauge("OVERSTEER",  m_state.oversteer,    COL_YEL,   gaugeSize);
    ImGui::SameLine(0, 6);
    drawArcGauge("FRONT SLIP", m_state.frontSlipNorm,COL_BLUE,  gaugeSize);
    ImGui::SameLine(0, 6);
    drawArcGauge("REAR SLIP",  m_state.rearSlipNorm, COL_GREEN, gaugeSize);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("TELEMETRY");
    ImGui::Spacing();

    // Readouts in a 2-col table
    if (ImGui::BeginTable("telem", 4, ImGuiTableFlags_None)) {
        auto row = [&](const char* lbl, float val, const char* unit) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", lbl);
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
            char buf[32]; snprintf(buf, sizeof(buf), "%.1f", val);
            ImGui::Text("%s", buf);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", unit);
            ImGui::TableNextColumn();
        };
        row("SPEED",     m_state.speedKmh,    "km/h");
        row("LAT G",     m_state.lateralG,    "G");
        row("FRONT N",   m_state.frontLatForce, "N");
        row("SIDESLIP",  m_state.sideslip * 57.2958f, "deg");
        row("FRICTION",  m_signals.friction,  "");
        row("RUMBLE",    m_signals.rumble,     "");
        row("FFB Hz",    (float)m_settings.ffbUpdateHz, "Hz");
        {
            auto pkts = m_stats.udpPackets.load();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("UDP PKT");
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
            ImGui::Text("%llu", (unsigned long long)pkts);
            ImGui::PopStyleColor();
            ImGui::TableNextColumn(); ImGui::TableNextColumn();
        }
        ImGui::EndTable();
    }

    ImGui::EndChild();
}

// ── Device selector ───────────────────────────────────────────────────────────

void ImGuiUI::drawDeviceSelector() {
    ImGui::TextDisabled("WHEEL DEVICE");
    ImGui::BeginChild("devices", {-1, 110}, true);

    const auto& devs = m_output.devices();
    if (devs.empty()) {
        ImGui::TextDisabled("No FFB devices found.");
        ImGui::TextDisabled("Check wheel is powered on and drivers installed.");
        ImGui::TextDisabled("Then click Rescan below.");
    } else {
        for (int i = 0; i < (int)devs.size(); i++) {
            const char* nameBuf = devs[i].name.c_str();
            bool active = m_output.isActive() && (m_output.activeDeviceIndex() == i);

            // Status dot, then a full-width selectable showing the whole name.
            // Clicking the row connects — keeps the name readable in the narrow
            // column instead of fighting a Connect button for space.
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
                ImGui::TextUnformatted("\xE2\x97\x8F");   // ●
                ImGui::PopStyleColor();
            } else {
                ImGui::TextDisabled("\xE2\x97\x8B");        // ○
            }
            ImGui::SameLine();

            char label[300];
            snprintf(label, sizeof(label), "%s###dev%d", nameBuf, i);
            if (active) ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
            bool clicked = ImGui::Selectable(label, active);
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(active ? "Connected — click to reconnect"
                                         : "Click to connect this wheel");

            if (clicked) {
                bool ok = m_output.openDevice(i);
                m_deviceError = ok ? "" : m_output.lastError();
                m_selectedDevice = i;
            }
        }
    }

    ImGui::EndChild();
    ImGui::TextDisabled("Click a device to connect");

    // Error message
    if (!m_deviceError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
        ImGui::TextWrapped("⚠ %s", m_deviceError.c_str());
        ImGui::PopStyleColor();
    }

    // Status line
    if (m_output.isActive()) {
        int idx = m_output.activeDeviceIndex();
        const char* nm = (idx >= 0 && idx < (int)devs.size())
                         ? devs[idx].name.c_str() : "wheel";
        ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
        ImGui::Text("● Connected: %s", nm);
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Rescan Devices", {-1, 0})) {
        m_output.enumerate();
        m_deviceError = "";
    }

    // ── Diagnostic: steady test force (no game needed) ────────────────────────
    if (ImGui::Checkbox("Send test force (25%)", &m_testForce))
        m_settings.testForce = m_testForce ? 0.25f : 0.f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sends a constant force so you can confirm the wheel "
                          "responds, independent of telemetry. The wheel should "
                          "pull and hold to one side.");
    ImGui::SameLine();
    ImGui::TextDisabled("torque out: %+.2f", m_signals.torque);

    // Live count of accepted vs rejected SDL effect updates. If the wheel feels
    // dead while "errors" climbs, SDL is rejecting the force (see message).
    if (m_output.isActive()) {
        unsigned long ok = m_output.sendOk(), err = m_output.sendErrors();
        ImGui::PushStyleColor(ImGuiCol_Text, err ? COL_RED : COL_TEXT_SEC);
        ImGui::Text("FFB sent: %lu   errors: %lu", ok, err);
        ImGui::PopStyleColor();
        std::string e = m_output.lastError();
        if (!e.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
            ImGui::TextWrapped("⚠ %s", e.c_str());
            ImGui::PopStyleColor();
        }
    }
}

// ── Profiles ──────────────────────────────────────────────────────────────────

void ImGuiUI::drawProfiles() {
    ImGui::TextDisabled("PROFILE");
    ImGui::BeginChild("profiles", {-1, 96}, true);

    bool hasActive = (m_activeProfile >= 0 && m_activeProfile < (int)m_profiles.size());
    const char* preview = hasActive ? m_profiles[m_activeProfile].c_str() : "(none)";

    // Selector — switching loads that profile's tuning live
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("##profsel", preview)) {
        for (int i = 0; i < (int)m_profiles.size(); i++) {
            bool sel = (i == m_activeProfile);
            if (ImGui::Selectable(m_profiles[i].c_str(), sel)) {
                m_activeProfile = i;
                profiles::load(m_profiles[i], m_settings);   // applies instantly
                profiles::writeActive(m_profiles[i]);
                m_engineDirty = false;
            }
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // Overwrite the active profile with the current tuning
    if (ImGui::Button("Save", {70, 0}) && hasActive) {
        profiles::save(m_profiles[m_activeProfile], m_settings);
        m_engineDirty = false;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Overwrite the selected profile with current sliders");

    ImGui::SameLine();
    // Delete — disabled when it's the last remaining profile
    bool canDelete = hasActive && m_profiles.size() > 1;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("Delete", {70, 0}) && canDelete) {
        profiles::remove(m_profiles[m_activeProfile]);
        refreshProfiles();
        if (!m_profiles.empty()) {                 // select + load first remaining
            m_activeProfile = 0;
            profiles::load(m_profiles[0], m_settings);
            profiles::writeActive(m_profiles[0]);
        }
    }
    ImGui::EndDisabled();

    // Create a new named profile from the current tuning
    ImGui::SetNextItemWidth(-82.f);
    bool entered = ImGui::InputTextWithHint("##newprof", "New profile name",
                       m_newProfile, sizeof(m_newProfile),
                       ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Save As", {74, 0}) || entered) && m_newProfile[0] != '\0') {
        std::string n = profiles::save(m_newProfile, m_settings);
        profiles::writeActive(n);
        m_newProfile[0] = '\0';
        refreshProfiles();
    }

    ImGui::EndChild();
}

// ── Settings sliders ──────────────────────────────────────────────────────────

void ImGuiUI::drawSettings() {
    ImGui::TextDisabled("FFB PARAMETERS");
    // Reserve room for the Save/Quit row (below the child) AND the window footer
    // so neither the buttons nor the footer get pushed off the bottom.
    ImGui::BeginChild("settings",
                      {-1, -(ImGui::GetFrameHeightWithSpacing() + footerReserveH())}, true);

    auto slider = [&](const char* label, float& val, float lo, float hi,
                      const char* hint = nullptr) {
        ImGui::SetNextItemWidth(-170.f);
        if (ImGui::SliderFloat(label, &val, lo, hi, "%.2f"))
            m_engineDirty = true;
        if (hint && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", hint);
    };

    slider("Overall Strength",  m_settings.overallStrength,    0.f, 1.f,
           "Master FFB output scale");
    slider("Max Output (Safety)", m_settings.maxOutput,        0.f, 1.f,
           "Hard ceiling on ALL force, applied last. Set low for a kids profile "
           "so the wheel can never exceed this no matter the other sliders.");
    slider("Soft Start (s)",    m_settings.softStartSec,       0.f, 2.f,
           "Ease force in over this many seconds after connecting or unpausing, "
           "so the wheel never snaps to full. 0 = instant.");

    // Max force needs integer formatting — own slider rather than the %.2f lambda
    ImGui::SetNextItemWidth(-170.f);
    if (ImGui::SliderFloat("Max Force (N)", &m_settings.maxForceN, 4000.f, 20000.f, "%.0f"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Front lateral force mapped to full wheel torque. "
                          "Lower = stronger / earlier clipping, "
                          "higher = lighter with more headroom");

    slider("Load Sensitivity",  m_settings.loadSensitivity,    0.f, 1.f,
           "EXPERIMENTAL. Weights steering by the front tyres' vertical load so "
           "the wheel lightens when the front is unloaded (low speed, cresting) "
           "and stays full under high load (downforce at speed, trail-braking). "
           "0 = off / original feel. Only ever lightens, never adds clipping.");
    // Reference load needs integer formatting — own slider rather than the lambda
    ImGui::SetNextItemWidth(-170.f);
    if (ImGui::SliderFloat("Load Reference (N)", &m_settings.loadRefN, 3000.f, 15000.f, "%.0f"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Front vertical load (FL+FR) treated as fully weighted. "
                          "Below it the wheel lightens; at/above it stays full. "
                          "Only matters when Load Sensitivity > 0. Lower if the "
                          "wheel feels too light everywhere.");

    slider("Grip Loss Feel",    m_settings.gripLossStrength,   0.f, 2.f,
           "How strongly tyre slip reduces wheel force (>1 = exaggerated)");
    slider("Understeer Cue",    m_settings.understeerStrength, 0.f, 2.f,
           "Wheel lightening when front tyres slide (>1 = exaggerated)");
    slider("Oversteer Cue",     m_settings.oversteerStrength,  0.f, 1.f,
           "Counter-kick when rear breaks away");
    slider("Lockup Judder",     m_settings.lockupStrength,     0.f, 1.f,
           "Front-wheel lockup vibration under braking (slip ratio)");
    slider("Wheelspin Rumble",  m_settings.wheelspinStrength,  0.f, 1.f,
           "Rear-wheel spin vibration on power (slip ratio)");
    slider("Braking Weight",    m_settings.brakingStrength,    0.f, 1.f,
           "How much the wheel firms up under braking load (lon force)");
    slider("Smoothing (DD↑)",   m_settings.smoothing,          0.f, 0.5f,
           "IIR filter — increase for DD wheels to reduce oscillation");
    slider("Min Force",         m_settings.minForce,           0.f, 0.3f,
           "Lifts small forces above the wheel's mechanical deadzone so faint "
           "cues are still felt. Recommended for belt/gear wheels; leave at 0 "
           "for direct-drive.");
    ImGui::TextDisabled("  Min Force: raise for belt/gear wheels (Logitech, "
                        "Thrustmaster); 0 for direct drive");

    ImGui::Spacing();
    ImGui::SetNextItemWidth(-170.f);
    int hz = m_settings.ffbUpdateHz;
    if (ImGui::SliderInt("Update Rate (Hz)", &hz, FFB_MIN_HZ, FFB_MAX_HZ))
        m_settings.ffbUpdateHz = hz;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Output rate. Telemetry is 60Hz; the engine interpolates "
                          "smoothly between frames up to this rate. Higher = "
                          "smoother, but some wheels drop force (or become "
                          "unstable) when updated too fast. Raise gradually and "
                          "watch the FFB counter / feel; back off if force cuts out.");
    ImGui::TextDisabled("  60-90 suits most wheels; raise to find yours");

    ImGui::Spacing();
    if (ImGui::Checkbox("Invert Force Direction", &m_settings.invertForce))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flip wheel pull direction if torque fights your steering");

    ImGui::EndChild();

    if (ImGui::Button("Save", {80, 0})) {
        m_engineDirty = false;
        // Engine already reads settings live by reference; this persists the
        // tuning into the active profile so it survives a restart/force-close.
        if (m_activeProfile >= 0 && m_activeProfile < (int)m_profiles.size())
            profiles::save(m_profiles[m_activeProfile], m_settings);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Save current tuning to the active profile");
    ImGui::SameLine();
    if (ImGui::Button("Quit", {80, 0})) m_quit = true;
}

// ── Footer ────────────────────────────────────────────────────────────────────

void ImGuiUI::drawFooter() {
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled(
        "UDP port 20777  ·  F1 25/26: Settings → Telemetry → On, 60Hz, Unrestricted  "
        "·  Set in-game FFB to 0%%");
    ImGui::Spacing();

    // ── Support / donate button ───────────────────────────────────────────────
    // Amber pill with dark text (matches the theme accent). Opens the Ko-fi page
    // in the user's default browser. Plain text label — the default ImGui font
    // has no ☕/♥ glyphs, so an emoji would render as "?".
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_HI);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_Text,          COL_BG);     // dark text on amber
    if (ImGui::Button("Support on Ko-fi"))
        ShellExecuteA(nullptr, "open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Enjoying the app? Support development — opens %s", KOFI_URL);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextDisabled("Like the app? A coffee keeps development going. Thank you!");
    ImGui::PopStyleColor();
}

// ── Donation reminder (every 5th launch) ───────────────────────────────────────

void ImGuiUI::drawNagPopup() {
    // Centre the modal over the window and give it a sensible fixed width.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({440.f, 0.f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Enjoying F1 FFB?", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::TextWrapped("Thanks for using F1 FFB!");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextWrapped(
        "This app is free and built in my spare time. If it's improved your "
        "driving and you'd like to support ongoing development, testing and new "
        "features, a small tip on Ko-fi goes a long way. No pressure — thank you "
        "for driving with it either way!");
    ImGui::PopTextWrapPos();

    ImGui::Spacing();

    // "Don't remind me again" — only from the 2nd reminder onward, so a brand
    // new user gets a single gentle nudge before being offered the opt-out.
    if (m_nagNumber >= 2)
        ImGui::Checkbox("Don't remind me again", &m_nagOptOut);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Persist the opt-out (if ticked) on whichever button dismisses the popup.
    auto dismiss = [&]() {
        if (m_nagOptOut) setDonationNagDisabled(true);
        ImGui::CloseCurrentPopup();
    };

    // Amber "Support on Ko-fi" — primary action (dark text on accent).
    ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
    if (ImGui::Button("Support on Ko-fi", {190.f, 0.f})) {
        ShellExecuteA(nullptr, "open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
        dismiss();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Opens %s", KOFI_URL);

    // Neutral "Maybe later" — de-emphasised so it doesn't compete with the CTA.
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_PANEL);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_BORDER);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_BORDER);
    ImGui::PushStyleColor(ImGuiCol_Text,          COL_TEXT);
    if (ImGui::Button("Maybe later", {130.f, 0.f}))
        dismiss();
    ImGui::PopStyleColor(4);

    ImGui::EndPopup();
}

void ImGuiUI::shutdown() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}
