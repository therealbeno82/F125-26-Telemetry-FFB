#include "imgui_ui.h"
#include "../settings_io.h"
#include "../update_check.h"
#include "../udp_receiver.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <chrono>
#include <cmath>
#include <string>
#include <shellapi.h>                 // ShellExecute - open the donate link in a browser
#pragma comment(lib, "shell32.lib")

// Support / donation link, opened by the footer button.
static const char* KOFI_URL = "https://ko-fi.com/rapidbeno";

// ── Colours - "Amber / Carbon" theme ──────────────────────────────────────────
#define COL_ACCENT     ImVec4(0.961f,0.620f,0.043f,1.f)  // amber  #F59E0B
#define COL_ACCENT_HI  ImVec4(0.984f,0.749f,0.141f,1.f)  // amber hover #FBBF24
#define COL_RED        ImVec4(0.937f,0.267f,0.267f,1.f)  // danger/clip #EF4444
#define COL_YEL        ImVec4(0.980f,0.800f,0.082f,1.f)  // warn   #FACC15
#define COL_GREEN      ImVec4(0.518f,0.800f,0.086f,1.f)  // good   #84CC16
#define COL_BLUE       ImVec4(0.360f,0.620f,0.950f,1.f)  // info accent (gauges)
#define COL_BG         ImVec4(0.078f,0.071f,0.059f,1.f)  // carbon #14120F
#define COL_PANEL      ImVec4(0.118f,0.106f,0.086f,1.f)  // panel  #1E1B16
#define COL_PANEL_DARK ImVec4(0.059f,0.051f,0.039f,1.f)  // right column bg
#define COL_BORDER     ImVec4(0.200f,0.180f,0.145f,1.f)  // warm border
#define COL_TEXT       ImVec4(0.925f,0.910f,0.882f,1.f)  // text   #ECE8E1
#define COL_TEXT_SEC   ImVec4(0.604f,0.565f,0.471f,1.f)  // muted  #9A9078
#define COL_TEXT_FAINT ImVec4(0.420f,0.388f,0.329f,1.f)  // faint  #6B6354

// ── Small drawing helpers ─────────────────────────────────────────────────────

// Pentagon logo mark.
static void drawPentagon(ImVec2 pos, float size, ImU32 col) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float cx = pos.x + size * 0.5f, cy = pos.y + size * 0.5f, r = size * 0.5f;
    ImVec2 pts[5];
    for (int i = 0; i < 5; i++) {
        float a = -1.5708f + i * 1.25664f;            // -90° + i*72°
        pts[i] = { cx + cosf(a) * r, cy + sinf(a) * r };
    }
    dl->AddConvexPolyFilled(pts, 5, col);
}

// Begin / end a bordered "panel" child (rounded, panel-coloured background).
static bool beginPanel(const char* id, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL);
    return ImGui::BeginChild(id, size, true);
}
static void endPanel() {
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// A panel header: muted label on the left, optional right-aligned text, divider.
static void panelTitle(const char* label, const char* right = nullptr,
                       ImVec4 rcol = COL_TEXT_SEC) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    if (right && right[0]) {
        float tw = ImGui::CalcTextSize(right).x;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - tw);
        ImGui::PushStyleColor(ImGuiCol_Text, rcol);
        ImGui::TextUnformatted(right);
        ImGui::PopStyleColor();
    }
    ImGui::Separator();
    ImGui::Spacing();
}

// Centre-origin (bipolar) torque bar drawn into the draw list.
static void bipolarBar(float t, float w, float h) {
    t = clampf(t, -1.f, 1.f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    dl->AddRectFilled(p, { p.x + w, p.y + h }, IM_COL32(40, 36, 29, 255), 4.f);
    // clip rails at the extremes
    dl->AddRectFilled(p, { p.x + w * 0.04f, p.y + h }, IM_COL32(239, 68, 68, 45), 4.f);
    dl->AddRectFilled({ p.x + w * 0.96f, p.y }, { p.x + w, p.y + h }, IM_COL32(239, 68, 68, 45), 4.f);
    float mid = p.x + w * 0.5f;
    dl->AddLine({ mid, p.y }, { mid, p.y + h }, IM_COL32(236, 232, 225, 60));
    float len = fabsf(t) * (w * 0.5f - 2.f);
    ImU32 col = fabsf(t) > 0.98f ? IM_COL32(239, 68, 68, 255)
              : (t >= 0.f ? IM_COL32(92, 158, 242, 255) : IM_COL32(245, 158, 11, 255));
    if (t >= 0.f) dl->AddRectFilled({ mid, p.y + 3 }, { mid + len, p.y + h - 3 }, col, 2.f);
    else          dl->AddRectFilled({ mid - len, p.y + 3 }, { mid, p.y + h - 3 }, col, 2.f);
    ImGui::Dummy({ w, h });
}

// 270° arc gauge with a coloured label below.
static void drawArcGauge(const char* label, float value, ImVec4 color, float size) {
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2      pos = ImGui::GetCursorScreenPos();
    float       cx  = pos.x + size * 0.5f;
    float       cy  = pos.y + size * 0.52f;
    float       r   = size * 0.40f;
    const float START_ANG = 3.14159f * 0.75f;
    const float SWEEP     = 3.14159f * 1.5f;
    const int   SEGS      = 48;
    const float thick     = size > 110.f ? 6.f : 5.f;

    value = clampf(value, 0.f, 1.f);

    dl->PathClear();
    for (int i = 0; i <= SEGS; i++) {
        float a = START_ANG + SWEEP * ((float)i / SEGS);
        dl->PathLineTo({ cx + cosf(a) * r, cy + sinf(a) * r });
    }
    dl->PathStroke(IM_COL32(48, 43, 34, 255), false, thick);

    if (value > 0.001f) {
        int fillSegs = (int)(SEGS * value);
        dl->PathClear();
        for (int i = 0; i <= fillSegs; i++) {
            float a = START_ANG + SWEEP * ((float)i / SEGS);
            dl->PathLineTo({ cx + cosf(a) * r, cy + sinf(a) * r });
        }
        dl->PathStroke(ImGui::ColorConvertFloat4ToU32(color), false, thick);
    }

    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", (int)(value * 100.f));
    ImVec2 ts = ImGui::CalcTextSize(pct);
    dl->AddText({ cx - ts.x * 0.5f, cy - ts.y * 0.5f }, IM_COL32(236, 232, 225, 255), pct);

    ImVec2 ls = ImGui::CalcTextSize(label);
    dl->AddText({ cx - ls.x * 0.5f, cy + r * 0.62f },
                ImGui::ColorConvertFloat4ToU32(color), label);

    ImGui::Dummy({ size, size });
}

// Throttle / brake / steer input bar.
static void inputBar(const char* label, float value, ImVec4 col, bool bipolar = false) {
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SameLine(42.f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x - 46.f;
    if (w < 20.f) w = 20.f;
    const float h = 9.f;
    p.y += 3.f;
    dl->AddRectFilled(p, { p.x + w, p.y + h }, IM_COL32(40, 36, 29, 255), 3.f);
    ImU32 c = ImGui::ColorConvertFloat4ToU32(col);
    if (bipolar) {
        float mid = p.x + w * 0.5f;
        dl->AddLine({ mid, p.y }, { mid, p.y + h }, IM_COL32(236, 232, 225, 45));
        float len = clampf(fabsf(value), 0.f, 1.f) * (w * 0.5f);
        if (value >= 0.f) dl->AddRectFilled({ mid, p.y }, { mid + len, p.y + h }, c, 3.f);
        else              dl->AddRectFilled({ mid - len, p.y }, { mid, p.y + h }, c, 3.f);
    } else {
        float len = clampf(value, 0.f, 1.f) * w;
        dl->AddRectFilled(p, { p.x + len, p.y + h }, c, 3.f);
    }
    ImGui::Dummy({ w, h + 3.f });
    ImGui::SameLine();
    ImGui::Text("%d", (int)(value * 100.f));
}

// Short display name for the Session packet's m_sessionType. Unknown values
// show the raw number so a new game mode can be identified and supported.
static const char* sessionTypeName(uint8_t t, char* buf, size_t n) {
    switch (t) {
        case 0:  return "--";
        case 1: case 2: case 3: return "PRACTICE";
        case 4:  return "SHORT P";
        case 5:  return "Q1";
        case 6:  return "Q2";
        case 7:  return "Q3";
        case 8:  return "SHORT Q";
        case 9:  return "ONE-SHOT Q";
        case 10: case 11: case 12: return "SHOOTOUT";
        case 13: return "SHORT SO";
        case 14: return "ONE-SHOT SO";
        case 15: case 16: case 17: return "RACE";
        case 18: return "TIME TRIAL";
        default: snprintf(buf, n, "TYPE %u", t); return buf;
    }
}

// Amber section header inside the params list (Strength / Cues / Output).
static void groupHeader(const char* label) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

ImGuiUI::ImGuiUI(TelemetryState& state, FFBSettings& settings,
                 FFBSignals& signals, AppStats& stats,
                 WheelOutput& output, UdpReceiver& udp)
    : m_state(state), m_settings(settings), m_signals(signals),
      m_stats(stats), m_output(output), m_udp(udp) {}

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

    // Seed the editable port field from the bound socket (= the saved port).
    m_udpPort = (int)m_udp.boundPort();
    if (m_udpPort == 0) m_udpPort = (int)loadUdpPort();   // bind failed at startup

    // Fire-and-forget update check; the header shows a notice if one is found.
    updatecheck::start();

    // Quick Start Guide: shown every launch until the user opts out.
    m_showGuide = !isQuickStartGuideDisabled();

    // Count this launch; show an occasional, dismissable donation reminder
    // every 5th time the app starts (5th, 10th, 15th, …). The guide takes
    // priority — never stack two popups on one launch.
    int launches = bumpLaunchCount();
    bool everyFifth = (launches > 0 && launches % 5 == 0);
    m_nagNumber = launches / 5;
    m_showNag   = everyFifth && !isDonationNagDisabled() && !m_showGuide;

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
    s.WindowRounding    = 0.f;     // full-screen root
    s.ChildRounding     = 8.f;     // panels
    s.FrameRounding     = 4.f;
    s.GrabRounding      = 4.f;
    s.PopupRounding     = 6.f;
    s.ScrollbarRounding = 4.f;
    s.WindowBorderSize  = 0.f;
    s.ChildBorderSize   = 1.f;
    s.FramePadding      = { 8.f, 5.f };
    s.ItemSpacing       = { 8.f, 6.f };
    s.WindowPadding     = { 12.f, 12.f };

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]         = COL_BG;
    c[ImGuiCol_ChildBg]          = ImVec4(0,0,0,0);   // transparent; panels set their own
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

// ── Top-level layout ──────────────────────────────────────────────────────────

void ImGuiUI::render() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0,0});
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    // Record the scope history (one sample per frame, all three channels).
    m_torqueHistory[m_historyIdx]   = m_signals.torque;
    m_frictionHistory[m_historyIdx] = m_signals.friction;
    m_rumbleHistory[m_historyIdx]   = m_signals.rumble;
    m_historyIdx = (m_historyIdx + 1) % HISTORY;

    drawHeader();

    // ── Two-column body: flexible left, fixed-ish right ──
    const float footH  = ImGui::GetFrameHeight() + 18.f + ImGui::GetTextLineHeightWithSpacing();
    float bodyH = ImGui::GetContentRegionAvail().y - footH;
    if (bodyH < 120.f) bodyH = 120.f;
    float totalW = ImGui::GetContentRegionAvail().x;
    float rightW = 440.f;
    if (rightW > totalW * 0.5f) rightW = totalW * 0.5f;   // guard narrow windows
    float leftW = totalW - rightW;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10,10});

    ImGui::BeginChild("left_col", {leftW, bodyH}, false);
    drawGauges();
    ImGui::EndChild();

    ImGui::SameLine(0, 0);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, COL_PANEL_DARK);
    ImGui::BeginChild("right_col", {0, bodyH}, false);
    drawDeviceSelector();
    ImGui::Spacing();
    drawProfiles();
    ImGui::Spacing();
    drawSettings();
    ImGui::EndChild();
    ImGui::PopStyleColor();

    ImGui::PopStyleVar();

    drawFooter();

    if (m_showGuide) {
        ImGui::OpenPopup("Quick Start Guide");
        m_showGuide = false;
    }
    drawGuidePopup();

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
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;

    // 3px amber accent rule.
    dl->AddRectFilled(p, {p.x + w, p.y + 3}, IM_COL32(245, 158, 11, 255));
    ImGui::Dummy({0, 9});

    ImGui::SetCursorPosX(16.f);
    ImGui::AlignTextToFramePadding();

    // Pentagon mark.
    ImVec2 mk = ImGui::GetCursorScreenPos();
    mk.y += 1.f;
    drawPentagon(mk, 16.f, IM_COL32(245, 158, 11, 255));
    ImGui::Dummy({16, 16});
    ImGui::SameLine(0, 9);

    // "F1" amber + "FFB" white.
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::TextUnformatted("F1");
    ImGui::PopStyleColor();
    ImGui::SameLine(0, 3);
    ImGui::TextUnformatted("FFB");

    ImGui::SameLine(0, 14);
    ImGui::TextDisabled("Force Feedback Enhancer  ·  %s  ·  F1 25 / F1 26", APP_VERSION);

    // Update notice (startup check against GitHub Releases). Clickable.
    if (updatecheck::available()) {
        ImGui::SameLine(0, 14);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
        ImGui::Text("UPDATE %s AVAILABLE", updatecheck::latestTag());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("A newer version is out - click to open the download page.\n\n"
                              "To upgrade: close this app, then extract the new F1FFB.exe\n"
                              "from the ZIP into THIS folder, replacing the old exe.\n"
                              "Your profiles and settings are stored next to the exe and\n"
                              "carry straight over.");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                ShellExecuteA(nullptr, "open", updatecheck::releasesUrl(),
                              nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    // ── Connection status (right-aligned) - mirrors FFBEngine's safety gate ──
    using namespace std::chrono;
    auto now     = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    auto lastPkt = m_stats.lastUdpTimeMs.load();
    auto adv     = m_stats.lastFrameAdvanceMs.load();
    auto streak  = m_stats.frameStreakStartMs.load();

    bool frozen   = (adv == 0) || (now - adv > FFB_PAUSE_FREEZE_MS);
    bool rearming = (now - streak < FFB_REARM_MS);
    bool paused   = m_stats.gamePaused.load();
    bool aiDrive  = m_stats.aiInControl.load();
    bool ttHold   = m_stats.ttHolding.load();   // AI lap start (TT / one-shot quali)

    const char* statusText; ImVec4 statCol;
    if (lastPkt == 0) {
        statusText = "WAITING..."; statCol = COL_TEXT_SEC;
    } else if (!frozen && !rearming && !paused && !aiDrive && !ttHold) {
        statusText = "CONNECTED";  statCol = COL_GREEN;
    } else {
        statusText = paused  ? "PAUSED · FFB RELEASED"
                   : aiDrive ? "AI DRIVING · FFB RELEASED"
                   : ttHold  ? "AI START · FFB HELD"
                   :           "PAUSED · FFB RELEASED";
        statCol = COL_YEL;
    }

    float pillW = ImGui::CalcTextSize(statusText).x + 18.f;
    ImGui::SameLine(w - pillW - 10.f);
    ImVec2 dot = ImGui::GetCursorScreenPos();
    dot.y += ImGui::GetTextLineHeight() * 0.5f;
    ImU32 dc = ImGui::ColorConvertFloat4ToU32(statCol);
    dl->AddCircleFilled(dot, 4.f, dc);
    dl->AddCircle(dot, 7.f, (dc & 0x00FFFFFF) | 0x59000000, 0, 2.f);   // soft glow
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 13.f);
    ImGui::PushStyleColor(ImGuiCol_Text, statCol);
    ImGui::TextUnformatted(statusText);
    ImGui::PopStyleColor();

    ImGui::Dummy({0, 9});
    ImVec2 bp = ImGui::GetCursorScreenPos();
    dl->AddLine(bp, {bp.x + w, bp.y}, IM_COL32(236, 232, 225, 22));
}

// ── Left column: live monitor ─────────────────────────────────────────────────

void ImGuiUI::drawGauges() {
    float colH = ImGui::GetContentRegionAvail().y;
    float gap  = ImGui::GetStyle().ItemSpacing.y;
    const float torqueH  = 120.f;
    const float signalsH = 190.f;
    const float telemH   = 280.f;   // fits the telemetry grid (3 rows) + inputs
    float scopeH = colH - torqueH - signalsH - telemH - gap * 3.f;
    if (scopeH < 90.f) scopeH = 90.f;   // output history: compact, no scrollbar

    // ── WHEEL TORQUE ──
    {
        float clip = m_stats.clipLevel.load();
        char clipbuf[16];
        if (clip < 0.02f) snprintf(clipbuf, sizeof(clipbuf), "CLIP  --");
        else              snprintf(clipbuf, sizeof(clipbuf), "CLIP %3.0f%%", clip * 100.f);
        ImVec4 clipcol = clip < 0.02f ? COL_TEXT_SEC : (clip < 0.15f ? COL_YEL : COL_RED);

        beginPanel("p_torque", {0, torqueH});
        panelTitle("WHEEL TORQUE", clipbuf, clipcol);

        float t = m_signals.torque;
        ImGui::BeginGroup();
        ImGui::PushStyleColor(ImGuiCol_Text, fabsf(t) > 0.98f ? COL_RED : COL_ACCENT);
        ImGui::SetWindowFontScale(2.6f);
        ImGui::Text("%+.2f", t);
        ImGui::SetWindowFontScale(1.f);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
        ImGui::TextUnformatted(t >= 0.f ? "RIGHT" : "LEFT");
        ImGui::PopStyleColor();
        ImGui::EndGroup();

        ImGui::SameLine(0, 20);
        ImGui::BeginGroup();
        ImGui::Dummy({0, 10});
        ImVec2 barPos = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        bipolarBar(t, bw, 24.f);
        // Scale labels positioned under the bar via the draw list (SameLine
        // offsets would be relative to the window, not the bar's left edge).
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        ImU32 fc = ImGui::ColorConvertFloat4ToU32(COL_TEXT_FAINT);
        float ly = ImGui::GetCursorScreenPos().y;
        ImVec2 z = ImGui::CalcTextSize("0"), pf = ImGui::CalcTextSize("+FULL");
        tdl->AddText({barPos.x, ly}, fc, "-FULL");
        tdl->AddText({barPos.x + bw * 0.5f - z.x * 0.5f, ly}, fc, "0");
        tdl->AddText({barPos.x + bw - pf.x, ly}, fc, "+FULL");
        ImGui::Dummy({bw, z.y});
        ImGui::EndGroup();

        endPanel();
    }

    // ── OUTPUT HISTORY (channel-switchable scope) ──
    {
        beginPanel("p_scope", {0, scopeH});
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
        ImGui::TextUnformatted("OUTPUT HISTORY");
        ImGui::PopStyleColor();
        ImGui::Separator();

        const char* chans[3] = { "TORQUE", "FRICTION", "RUMBLE" };
        for (int i = 0; i < 3; i++) {
            bool on = (m_scopeChannel == i);
            ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.961f,0.620f,0.043f,0.16f) : ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.961f,0.620f,0.043f,0.10f));
            ImGui::PushStyleColor(ImGuiCol_Text, on ? COL_ACCENT : COL_TEXT_FAINT);
            if (ImGui::SmallButton(chans[i])) m_scopeChannel = i;
            ImGui::PopStyleColor(3);
            if (i < 2) ImGui::SameLine(0, 4);
        }

        float* hist = m_scopeChannel == 1 ? m_frictionHistory
                    : m_scopeChannel == 2 ? m_rumbleHistory
                    :                       m_torqueHistory;
        float lo = m_scopeChannel == 0 ? -1.f : 0.f;
        ImVec4 ccol = m_scopeChannel == 0 ? COL_ACCENT : COL_BLUE;
        ImGui::PushStyleColor(ImGuiCol_PlotLines, ccol);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.055f,0.047f,0.035f,1.f));
        float ph = ImGui::GetContentRegionAvail().y;
        if (ph < 24.f) ph = 24.f;
        ImGui::PlotLines("##scope", hist, HISTORY, m_historyIdx, nullptr, lo, 1.f, {-1, ph});
        ImGui::PopStyleColor(2);
        endPanel();
    }

    // ── LIVE SIGNALS (arc gauges) ──
    {
        beginPanel("p_signals", {0, signalsH});
        panelTitle("LIVE SIGNALS");
        float avail = ImGui::GetContentRegionAvail().x;
        float gsize = (avail - 24.f) / 4.f;
        if (gsize > 130.f) gsize = 130.f;
        if (gsize < 64.f)  gsize = 64.f;
        float pad = (avail - gsize * 4.f - 18.f) * 0.5f;
        if (pad > 0.f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
        drawArcGauge("UNDERSTEER", m_state.understeer,    COL_RED,   gsize); ImGui::SameLine(0, 6);
        drawArcGauge("OVERSTEER",  m_state.oversteer,     COL_YEL,   gsize); ImGui::SameLine(0, 6);
        drawArcGauge("FRONT SLIP", m_state.frontSlipNorm, COL_BLUE,  gsize); ImGui::SameLine(0, 6);
        drawArcGauge("REAR SLIP",  m_state.rearSlipNorm,  COL_GREEN, gsize);
        endPanel();
    }

    // ── TELEMETRY (grid + driver inputs) ──
    {
        char hz[16]; snprintf(hz, sizeof(hz), "%d Hz", m_settings.ffbUpdateHz);
        beginPanel("p_telem", {0, telemH});
        panelTitle("TELEMETRY", hz);

        auto cell = [&](const char* lbl, const char* val, ImVec4 vcol) {
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
            ImGui::TextUnformatted(lbl);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, vcol);
            ImGui::TextUnformatted(val);
            ImGui::PopStyleColor();
        };
        // Four columns so the ten readouts fill the panel width in three rows
        // (three columns needed four rows and forced a scrollbar).
        if (ImGui::BeginTable("telem", 4, ImGuiTableFlags_None)) {
            char b[32];
            snprintf(b, sizeof(b), "%.0f km/h", m_state.speedKmh);                 cell("SPEED", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.1f G", std::fabs(m_state.lateralG));          cell("LAT G", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.0f N", std::fabs(m_state.frontLatForce));     cell("FRONT", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.0f N", m_state.frontVertForce);               cell("VERT LOAD", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.1f\xC2\xB0", m_state.sideslip * 57.2958f);    cell("SIDESLIP", b, COL_YEL);
            snprintf(b, sizeof(b), "%.2f", m_signals.friction);                     cell("FRICTION", b, COL_BLUE);
            if (m_signals.rumble > 0.005f)
                snprintf(b, sizeof(b), "%.2f @ %.0f Hz", m_signals.rumble, m_signals.rumbleHz);
            else
                snprintf(b, sizeof(b), "%.2f", m_signals.rumble);
            cell("RUMBLE", b, COL_BLUE);
            snprintf(b, sizeof(b), "%llu", (unsigned long long)m_stats.udpPackets.load());
            cell("UDP PKT", b, COL_TEXT_SEC);
            char sb[16];
            cell("SESSION", sessionTypeName(m_stats.sessionType.load(), sb, sizeof(sb)),
                 COL_TEXT_SEC);
            // Raw slip ratios (front / rear) — for verifying the lockup/wheelspin
            // gates: braking hard should push FRONT clearly negative (< -0.06),
            // power-oversteer should push REAR positive (> +0.10).
            snprintf(b, sizeof(b), "%+.2f / %+.2f", m_state.frontSlipRatio, m_state.rearSlipRatio);
            cell("SLIP RATIO F/R", b, COL_YEL);
            ImGui::EndTable();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        inputBar("THR", m_state.throttle, COL_GREEN);
        inputBar("BRK", m_state.brake,    COL_RED);
        inputBar("STR", m_state.steer,    COL_BLUE, true);
        endPanel();
    }
}

// ── Right column: device ──────────────────────────────────────────────────────

void ImGuiUI::drawDeviceSelector() {
    beginPanel("p_device", {0, 248});
    panelTitle("WHEEL DEVICE");

    const auto& devs = m_output.devices();
    if (devs.empty()) {
        ImGui::TextDisabled("No FFB devices found.");
        ImGui::TextDisabled("Power on the wheel + install drivers,");
        ImGui::TextDisabled("then click Rescan below.");
    } else {
        for (int i = 0; i < (int)devs.size(); i++) {
            bool active = m_output.isActive() && (m_output.activeDeviceIndex() == i);

            // Status dot drawn directly (the default font lacks ●/○ glyphs).
            ImDrawList* ddl = ImGui::GetWindowDrawList();
            ImVec2 dp = ImGui::GetCursorScreenPos();
            float dcy = dp.y + ImGui::GetTextLineHeight() * 0.5f;
            if (active) {
                ImU32 g = ImGui::ColorConvertFloat4ToU32(COL_GREEN);
                ddl->AddCircleFilled({dp.x + 5.f, dcy}, 4.5f, g);
                ddl->AddCircle({dp.x + 5.f, dcy}, 7.f, (g & 0x00FFFFFF) | 0x55000000, 0, 2.f);
            } else {
                ddl->AddCircle({dp.x + 5.f, dcy}, 4.f, IM_COL32(154, 144, 120, 180), 0, 1.5f);
            }
            ImGui::Dummy({12.f, ImGui::GetTextLineHeight()});
            ImGui::SameLine(0, 4.f);

            char label[300];
            snprintf(label, sizeof(label), "%s###dev%d", devs[i].name.c_str(), i);
            float selW = active ? ImGui::GetContentRegionAvail().x
                                  - ImGui::CalcTextSize("CONNECTED").x - 8.f
                                : 0.f;
            if (active) ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
            bool clicked = ImGui::Selectable(label, active, 0, {selW, 0});
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(active ? "Connected - click to reconnect"
                                         : "Click to connect this wheel");
            if (active) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, COL_GREEN);
                ImGui::TextUnformatted("CONNECTED");
                ImGui::PopStyleColor();
            }
            if (clicked) {
                bool ok = m_output.openDevice(i);
                m_deviceError = ok ? "" : m_output.lastError();
            }
        }
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
    if (ImGui::Button("Rescan Devices", {-1, 0})) {
        m_output.enumerate();
        m_deviceError = "";
    }
    ImGui::PopStyleColor();

    // ── UDP telemetry port ────────────────────────────────────────────────
    // The game broadcasts to this port; change it only for a telemetry splitter
    // that re-forwards on another port. Applies live (rebinds the socket).
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextUnformatted("UDP PORT");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Port the game sends telemetry to (game default 20777). "
                          "Change it only if you run a telemetry splitter/relay that "
                          "forwards on another port. Click Apply to switch live - it "
                          "must match the UDP Port in the game's telemetry settings.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(78.f);
    int port = m_udpPort;
    if (ImGui::InputInt("##udpport", &port, 0, 0)) m_udpPort = port;  // clamp on Apply
    ImGui::SameLine();
    bool portDirty = (m_udpPort != (int)m_udp.boundPort());
    ImGui::BeginDisabled(!portDirty);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
    if (ImGui::Button("Apply")) {
        int p = m_udpPort < 1024 ? 1024 : (m_udpPort > 65535 ? 65535 : m_udpPort);
        m_udpPort = p;
        if (m_udp.rebind((uint16_t)p)) {
            saveUdpPort((uint16_t)p);
            m_portError.clear();
        } else {
            m_portError = "Port " + std::to_string(p) + " is in use - reverted to "
                        + std::to_string(m_udp.boundPort()) + ".";
            m_udpPort = (int)m_udp.boundPort();
        }
    }
    ImGui::PopStyleColor();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("active %u", m_udp.boundPort());
    if (!m_portError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
        ImGui::TextWrapped("%s", m_portError.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Checkbox("Send test force (25%)", &m_testForce))
        m_settings.testForce = m_testForce ? 0.25f : 0.f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sends a constant force so you can confirm the wheel "
                          "responds, independent of telemetry.");
    ImGui::SameLine();
    ImGui::TextDisabled("out: %+.2f", m_signals.torque);

    if (ImGui::Checkbox("Send test rumble (50%)", &m_testRumble))
        m_settings.testRumble = m_testRumble ? 0.5f : 0.f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Vibrates the wheel at the Lockup Pitch frequency, independent "
                          "of telemetry - confirms the rumble channel works on your base. "
                          "Toggle off/on to apply a new pitch.\n\n"
                          "Feel the test FORCE but not the test RUMBLE? Your wheel's tuning "
                          "software has periodic/sine effects disabled or at 0%% gain - "
                          "enable them there and the rumble will come through.");

    if (m_output.isActive()) {
        unsigned long ok = m_output.sendOk(), err = m_output.sendErrors();
        ImGui::PushStyleColor(ImGuiCol_Text, err ? COL_RED : COL_TEXT_SEC);
        ImGui::Text("FFB sent: %lu   errors: %lu", ok, err);
        ImGui::PopStyleColor();
        if (!m_output.sineSupported()) {
            ImGui::PushStyleColor(ImGuiCol_Text, COL_YEL);
            ImGui::TextWrapped("This wheel exposes no periodic (sine) effect - lockup "
                               "judder and wheelspin rumble can't be rendered. Check "
                               "your wheel's tuning software: DirectInput periodic/sine "
                               "effects may be disabled or at 0%% gain.");
            ImGui::PopStyleColor();
        }
    }
    if (!m_deviceError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_RED);
        ImGui::TextWrapped("%s", m_deviceError.c_str());
        ImGui::PopStyleColor();
    }
    endPanel();
}

// ── Right column: profiles ────────────────────────────────────────────────────

void ImGuiUI::drawProfiles() {
    beginPanel("p_profile", {0, 110});
    panelTitle("PROFILE", m_engineDirty ? "MODIFIED" : nullptr, COL_YEL);

    int n = (int)m_profiles.size();
    bool hasActive = (m_activeProfile >= 0 && m_activeProfile < n);

    // Dropdown selector — picking a profile loads it live. The combo's list
    // scrolls internally, so any number of saved profiles fits without the
    // panel filling up (the old horizontal pills ran out of room).
    const char* preview = hasActive ? m_profiles[m_activeProfile].c_str() : "(no profile)";
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::BeginCombo("##profilesel", preview)) {
        for (int i = 0; i < n; i++) {
            bool on = (i == m_activeProfile);
            if (ImGui::Selectable(m_profiles[i].c_str(), on)) {
                m_activeProfile = i;
                profiles::load(m_profiles[i], m_settings);
                profiles::writeActive(m_profiles[i]);
                m_engineDirty = false;
            }
            if (on) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Spacing();

    // Save / Delete.
    ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
    if (ImGui::Button("Save", {70, 0}) && hasActive) {
        profiles::save(m_profiles[m_activeProfile], m_settings);
        m_engineDirty = false;
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Overwrite the active profile with current sliders");
    ImGui::SameLine();
    bool canDelete = hasActive && n > 1;
    ImGui::BeginDisabled(!canDelete);
    if (ImGui::Button("Delete", {70, 0}) && canDelete) {
        profiles::remove(m_profiles[m_activeProfile]);
        refreshProfiles();
        if (!m_profiles.empty()) {
            m_activeProfile = 0;
            profiles::load(m_profiles[0], m_settings);
            profiles::writeActive(m_profiles[0]);
        }
    }
    ImGui::EndDisabled();

    // New named profile from current tuning.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-78.f);
    bool entered = ImGui::InputTextWithHint("##newprof", "New profile name",
                       m_newProfile, sizeof(m_newProfile),
                       ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Save As", {70, 0}) || entered) && m_newProfile[0] != '\0') {
        std::string nm = profiles::save(m_newProfile, m_settings);
        profiles::writeActive(nm);
        m_newProfile[0] = '\0';
        refreshProfiles();
    }
    endPanel();
}

// ── Right column: parameters ──────────────────────────────────────────────────

void ImGuiUI::drawSettings() {
    float saveRowH = ImGui::GetFrameHeightWithSpacing();
    float h = ImGui::GetContentRegionAvail().y - saveRowH;
    if (h < 140.f) h = 140.f;

    const char* activeName = (m_activeProfile >= 0 && m_activeProfile < (int)m_profiles.size())
                             ? m_profiles[m_activeProfile].c_str() : "";

    beginPanel("p_params", {0, h});
    panelTitle("FFB PARAMETERS", activeName, COL_ACCENT);

    // Percentage slider. Stored value stays a fraction (0..1, or 0..2 for the
    // exaggeratable cues); only the DISPLAY is scaled ×100, so saved profiles
    // are unchanged. `p` is rebuilt from `val` each frame to track profile loads.
    auto pct = [&](const char* label, float& val, float loFrac, float hiFrac,
                   const char* hint = nullptr) {
        float p = val * 100.f;
        ImGui::SetNextItemWidth(-150.f);
        if (ImGui::SliderFloat(label, &p, loFrac * 100.f, hiFrac * 100.f, "%.0f%%")) {
            val = p / 100.f;
            m_engineDirty = true;
        }
        if (hint && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint);
    };

    groupHeader("STRENGTH");
    pct("Overall Strength",  m_settings.overallStrength,    0.f, 1.f,
        "Master GAIN - multiplies all force evenly (scales). Different from Max "
        "Output, which only caps the top.");
    pct("Max Output (Safety)", m_settings.maxOutput,        0.f, 1.f,
        "Hard CEILING applied last - the wheel can NEVER exceed this (great for a "
        "kids profile). Only flattens peaks; doesn't scale everything down.");
    ImGui::SetNextItemWidth(-150.f);
    if (ImGui::SliderFloat("Full-Scale Force (N)", &m_settings.maxForceN, 4000.f, 20000.f, "%.0f"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("In-game front-tyre force that fills the wheel to 100%%. "
                          "HIGHER = lighter (more headroom); LOWER = stronger "
                          "(clips sooner). Scales the force, doesn't cap it.");

    // Auto Max Force: peak sustained front lateral force measured while driving
    // (kerb-spike filtered, reset on session change). One click calibrates the
    // Full-Scale Force slider to it, so the hardest corner just touches 100%.
    {
        const float MIN_PEAK_N = 2000.f;   // below this you haven't really cornered yet
        float peak = m_stats.peakLatForceN.load();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
        if (peak > 1.f) ImGui::Text("Peak this session: %.0f N", peak);
        else            ImGui::TextUnformatted("Peak this session: --");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::BeginDisabled(peak < MIN_PEAK_N);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
        if (ImGui::SmallButton("Auto-Set")) {
            m_settings.maxForceN = clampf(peak, 4000.f, 20000.f);
            m_engineDirty = true;
        }
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Set Full-Scale Force to the peak cornering force seen so "
                              "far, so the hardest corner just reaches 100%% wheel torque. "
                              "Drive 2-3 clean laps first. Lower the slider afterwards if "
                              "you prefer a stronger wheel with some clipping.");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
        if (ImGui::SmallButton("Reset"))
            m_stats.peakLatForceN.store(0.f);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Restart the peak measurement (it also resets automatically "
                              "when a new session starts).");
    }
    pct("Load Sensitivity",  m_settings.loadSensitivity,    0.f, 1.f,
        "EXPERIMENTAL. Weights the wheel by front vertical load - lighter when "
        "unloaded, full under load. 0 = off. Only ever lightens.");
    ImGui::SetNextItemWidth(-150.f);
    if (ImGui::SliderFloat("Load Reference (N)", &m_settings.loadRefN, 3000.f, 15000.f, "%.0f"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Front vertical load treated as fully weighted. Only "
                          "matters when Load Sensitivity > 0.");

    groupHeader("CUES");
    pct("Grip Loss Feel",    m_settings.gripLossStrength,   0.f, 2.f,
        "How strongly tyre slip reduces wheel force (>100% = exaggerated).");
    pct("Understeer Cue",    m_settings.understeerStrength, 0.f, 2.f,
        "Wheel lightening when the front tyres slide (>100% = exaggerated).");
    pct("Oversteer Cue",     m_settings.oversteerStrength,  0.f, 1.f,
        "Counter-kick when the rear breaks away.");
    pct("Braking Weight",    m_settings.brakingStrength,    0.f, 1.f,
        "Adds damping under braking - the wheel gets heavier/thicker to TURN while "
        "you're on the brakes. Felt as you steer (trail-braking); nothing when the "
        "wheel is held still. If it feels wrong/oscillates, tick Invert Braking "
        "Weight below.");

    groupHeader("RUMBLE EFFECTS (SINE)");
    // Lockup judder + wheelspin rumble ride the hardware periodic/sine effect,
    // which not every base renders. The cues above ride the constant-force
    // channel and work on any wheel; these need sine support.
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextWrapped("Vibration cues - need a wheel base that supports the sine "
                       "(periodic) FFB effect. Most direct-drive and belt bases do, but "
                       "some wheel bases may not. Use \"Send test rumble\" in the WHEEL "
                       "DEVICE panel to check yours.");
    ImGui::PopStyleColor();
    if (m_output.isActive() && !m_output.sineSupported()) {
        ImGui::PushStyleColor(ImGuiCol_Text, COL_YEL);
        ImGui::TextWrapped("Your connected wheel reported no sine support - these "
                           "sliders will have no effect on it.");
        ImGui::PopStyleColor();
    }
    ImGui::Spacing();
    pct("Lockup Judder",     m_settings.lockupStrength,     0.f, 1.f,
        "Front-wheel lockup vibration under braking.");
    ImGui::SetNextItemWidth(-150.f);
    if (ImGui::SliderFloat("Lockup Pitch (Hz)", &m_settings.lockupHz, 10.f, 60.f, "%.0f Hz"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Vibration frequency of the lockup judder (severity adds up to "
                          "+30%% on top). Wheel bases render vibration differently - tune "
                          "until a lockup feels like a sharp judder, clearly faster than "
                          "Wheelspin Pitch.");
    pct("Wheelspin Rumble",  m_settings.wheelspinStrength,  0.f, 1.f,
        "Rear-wheel spin vibration on power.");
    ImGui::SetNextItemWidth(-150.f);
    if (ImGui::SliderFloat("Wheelspin Pitch (Hz)", &m_settings.wheelspinHz, 5.f, 40.f, "%.0f Hz"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Vibration frequency of the wheelspin rumble (severity adds up to "
                          "+30%% on top). Keep it well below Lockup Pitch so the two effects "
                          "feel distinct - a slow axle tramp vs a fast brake judder.");

    groupHeader("OUTPUT");
    pct("Smoothing",         m_settings.smoothing,          0.f, 0.5f,
        "Latency-vs-smoothness trade: higher = smoother but laggier (up to ~40ms "
        "at max). Keep low for the crispest feel; raise to calm DD oscillation.");
    pct("Min Force",         m_settings.minForce,           0.f, 0.3f,
        "Lifts small forces past a wheel deadzone. Raise for belt/gear wheels; "
        "0 for direct drive.");
    ImGui::SetNextItemWidth(-150.f);
    if (ImGui::SliderFloat("Soft Start (s)", &m_settings.softStartSec, 0.f, 2.f, "%.2f s"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ease force in over this long after connecting/unpausing.");
    ImGui::SetNextItemWidth(-150.f);
    if (ImGui::SliderFloat("AI Start Hold (s)", &m_settings.ttStartHoldSec, 0.f, 10.f, "%.1f s"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("SAFETY (Time Trial / One-Shot & Hot-Lap Qualifying). When a lap "
                          "starts or you restart from the menu, the game's AI drives the "
                          "car for a few seconds before handing control over. Force is held "
                          "OFF for this long after the start so unexpected forces can't "
                          "catch your hands. 0 = off. Doesn't affect race starts, a "
                          "stop-and-go on track, or a mid-lap unpause.");
    ImGui::SetNextItemWidth(-150.f);
    int hz = m_settings.ffbUpdateHz;
    if (ImGui::SliderInt("Update Rate (Hz)", &hz, FFB_MIN_HZ, FFB_MAX_HZ)) {
        m_settings.ffbUpdateHz = hz;
        m_engineDirty = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Output ceiling. 60-90 Hz suits most wheels; some drop "
                          "force if updated too fast. Raise gradually.");
    if (ImGui::Checkbox("Invert Force Direction", &m_settings.invertForce))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flip wheel pull direction if torque fights your steering.");
    if (ImGui::Checkbox("Invert Braking Weight", &m_settings.brakingInvert))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Flip Braking Weight direction if the wheel pulls AWAY from "
                          "centre under braking instead of firming up.");

    endPanel();

    if (ImGui::Button("Save", {80, 0})) {
        m_engineDirty = false;
        if (m_activeProfile >= 0 && m_activeProfile < (int)m_profiles.size())
            profiles::save(m_profiles[m_activeProfile], m_settings);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save current tuning to the active profile");
    ImGui::SameLine();
    if (ImGui::Button("Quit", {80, 0})) m_quit = true;
}

// ── Footer ────────────────────────────────────────────────────────────────────

void ImGuiUI::drawFooter() {
    ImGui::Separator();

    // Coffee invitation — its own right-aligned line just above the button row.
    // Clickable (it says "click here"), opens the same Ko-fi link as the button.
    const char* coffeeMsg = "If you wish to buy Rapid Beno a coffee, for all the "
                            "late nights coding and Testing click here";
    float msgW = ImGui::CalcTextSize(coffeeMsg).x;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - msgW - 16.f);
    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::TextUnformatted(coffeeMsg);
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            ShellExecuteA(nullptr, "open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::SetCursorPosX(16.f);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::Text("UDP port %u   \xC2\xB7   F1 25/26: Telemetry On, 60 Hz, "
                "Unrestricted   \xC2\xB7   set in-game FFB to 0%%", m_udp.boundPort());
    ImGui::PopStyleColor();

    float bw = ImGui::CalcTextSize("Support on Ko-fi").x + ImGui::GetStyle().FramePadding.x * 2.f;
    ImGui::SameLine(ImGui::GetWindowWidth() - bw - 14.f);
    ImGui::PushStyleColor(ImGuiCol_Button,        COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, COL_ACCENT_HI);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  COL_ACCENT);
    ImGui::PushStyleColor(ImGuiCol_Text,          COL_BG);
    if (ImGui::Button("Support on Ko-fi"))
        ShellExecuteA(nullptr, "open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Enjoying the app? Support development - opens %s", KOFI_URL);
}

// ── Quick Start Guide (every launch until opted out) ──────────────────────────

void ImGuiUI::drawGuidePopup() {
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({560.f, 0.f}, ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Quick Start Guide", nullptr,
                                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
        return;

    ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
    ImGui::TextWrapped("Welcome to F1 FFB!");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextWrapped("Start things in this order so the app can take control "
                       "of your wheel before anything else grabs it:");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // Amber step number + wrapped text, indented past the number column.
    auto step = [](int n, const char* text) {
        char num[8];
        snprintf(num, sizeof(num), "%d.", n);
        ImGui::PushStyleColor(ImGuiCol_Text, COL_ACCENT);
        ImGui::TextUnformatted(num);
        ImGui::PopStyleColor();
        ImGui::SameLine(30.f);
        ImGui::PushTextWrapPos(ImGui::GetContentRegionMax().x - 8.f);
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
    };

    step(1, "Start F1 FFB (this app) - you're here.");
    step(2, "Turn on your wheel base.");
    step(3, "Select your wheel base under WHEEL DEVICE (click Rescan Devices if "
            "it isn't listed). The dot turns green when connected - tick "
            "\"Send test force\" to feel it respond.");
    step(4, "Start your wheel base's own tuning software, if you use one. Do "
            "this AFTER step 3 - if it's running first, it can block the app "
            "from connecting.");
    step(5, "Start F1 25 / F1 26. In game options set: Telemetry ON, 60 Hz, "
            "UDP port 20777, 'Your Telemetry' = Unrestricted, and in-game "
            "Force Feedback strength to 0%.");

    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextWrapped("Drive! The status pill (top right) turns CONNECTED when "
                       "telemetry arrives. Force releases automatically in menus, "
                       "when paused, and while the game's AI drives the car.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, COL_YEL);
    ImGui::TextWrapped("SAFETY");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC);
    ImGui::TextWrapped("Every effort has been made to release the wheel whenever you are "
                       "not driving, but there is still a chance an unwanted force may be "
                       "felt - most likely around pause menus, One-Shot / Hot-Lap "
                       "Qualifying starts, and Time Trial starts. Keep a light but ready "
                       "grip, and avoid resting your hands or body against the wheel while "
                       "in menus or waiting to take control.");
    ImGui::PopStyleColor();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Checkbox("Don't show this again", &m_guideOptOut);
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
    if (ImGui::Button("Got it", {140.f, 0.f})) {
        if (m_guideOptOut) setQuickStartGuideDisabled(true);
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor();

    ImGui::EndPopup();
}

// ── Donation reminder (every 5th launch) ───────────────────────────────────────

void ImGuiUI::drawNagPopup() {
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
        "features, a small tip on Ko-fi goes a long way. No pressure - thank you "
        "for driving with it either way!");
    ImGui::PopTextWrapPos();

    ImGui::Spacing();

    if (m_nagNumber >= 2)
        ImGui::Checkbox("Don't remind me again", &m_nagOptOut);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    auto dismiss = [&]() {
        if (m_nagOptOut) setDonationNagDisabled(true);
        ImGui::CloseCurrentPopup();
    };

    ImGui::PushStyleColor(ImGuiCol_Text, COL_BG);
    if (ImGui::Button("Support on Ko-fi", {190.f, 0.f})) {
        ShellExecuteA(nullptr, "open", KOFI_URL, nullptr, nullptr, SW_SHOWNORMAL);
        dismiss();
    }
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Opens %s", KOFI_URL);

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
