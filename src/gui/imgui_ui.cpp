#include "imgui_ui.h"
#include "../settings_io.h"
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
    // every 5th time the app starts (5th, 10th, 15th, …).
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
    float rightW = 470.f;
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
    bool ttHold   = m_stats.ttHolding.load();   // Time Trial start (AI driving away)

    const char* statusText; ImVec4 statCol;
    if (lastPkt == 0) {
        statusText = "WAITING..."; statCol = COL_TEXT_SEC;
    } else if (!frozen && !rearming && !paused && !aiDrive && !ttHold) {
        statusText = "CONNECTED";  statCol = COL_GREEN;
    } else {
        statusText = paused  ? "PAUSED · FFB RELEASED"
                   : aiDrive ? "AI DRIVING · FFB RELEASED"
                   : ttHold  ? "TT START · FFB HELD"
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
    const float telemH   = 256.f;
    float scopeH = colH - torqueH - signalsH - telemH - gap * 3.f;
    if (scopeH < 100.f) scopeH = 100.f;

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
        if (ImGui::BeginTable("telem", 3, ImGuiTableFlags_None)) {
            char b[32];
            snprintf(b, sizeof(b), "%.0f km/h", m_state.speedKmh);                 cell("SPEED", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.1f G", std::fabs(m_state.lateralG));          cell("LAT G", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.0f N", std::fabs(m_state.frontLatForce));     cell("FRONT", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.0f N", m_state.frontVertForce);               cell("VERT LOAD", b, COL_GREEN);
            snprintf(b, sizeof(b), "%.1f\xC2\xB0", m_state.sideslip * 57.2958f);    cell("SIDESLIP", b, COL_YEL);
            snprintf(b, sizeof(b), "%.2f", m_signals.friction);                     cell("FRICTION", b, COL_BLUE);
            snprintf(b, sizeof(b), "%.2f", m_signals.rumble);                       cell("RUMBLE", b, COL_BLUE);
            snprintf(b, sizeof(b), "%llu", (unsigned long long)m_stats.udpPackets.load());
            cell("UDP PKT", b, COL_TEXT_SEC);
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
    beginPanel("p_device", {0, 212});
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
                m_selectedDevice = i;
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

    if (ImGui::Checkbox("Send test force (25%)", &m_testForce))
        m_settings.testForce = m_testForce ? 0.25f : 0.f;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Sends a constant force so you can confirm the wheel "
                          "responds, independent of telemetry.");
    ImGui::SameLine();
    ImGui::TextDisabled("out: %+.2f", m_signals.torque);

    if (m_output.isActive()) {
        unsigned long ok = m_output.sendOk(), err = m_output.sendErrors();
        ImGui::PushStyleColor(ImGuiCol_Text, err ? COL_RED : COL_TEXT_SEC);
        ImGui::Text("FFB sent: %lu   errors: %lu", ok, err);
        ImGui::PopStyleColor();
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
    beginPanel("p_profile", {0, 132});
    panelTitle("PROFILE", m_engineDirty ? "MODIFIED" : nullptr, COL_YEL);

    int n = (int)m_profiles.size();
    bool hasActive = (m_activeProfile >= 0 && m_activeProfile < n);

    // Horizontal profile pills (click switches + loads live).
    if (n > 0) {
        float gap = 6.f;
        float bw = (ImGui::GetContentRegionAvail().x - (n - 1) * gap) / n;
        if (bw < 52.f) bw = 52.f;
        for (int i = 0; i < n; i++) {
            bool on = (i == m_activeProfile);
            if (on) { ImGui::PushStyleColor(ImGuiCol_Button, COL_ACCENT);
                      ImGui::PushStyleColor(ImGuiCol_Text, COL_BG); }
            else    { ImGui::PushStyleColor(ImGuiCol_Button, COL_PANEL_DARK);
                      ImGui::PushStyleColor(ImGuiCol_Text, COL_TEXT_SEC); }
            if (ImGui::Button(m_profiles[i].c_str(), {bw, 30})) {
                m_activeProfile = i;
                profiles::load(m_profiles[i], m_settings);
                profiles::writeActive(m_profiles[i]);
                m_engineDirty = false;
            }
            ImGui::PopStyleColor(2);
            if (i < n - 1) ImGui::SameLine(0, gap);
        }
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
    pct("Lockup Judder",     m_settings.lockupStrength,     0.f, 1.f,
        "Front-wheel lockup vibration under braking.");
    pct("Wheelspin Rumble",  m_settings.wheelspinStrength,  0.f, 1.f,
        "Rear-wheel spin vibration on power.");
    pct("Braking Weight",    m_settings.brakingStrength,    0.f, 1.f,
        "Adds damping under braking - the wheel gets heavier/thicker to TURN while "
        "you're on the brakes. Felt as you steer (trail-braking); nothing when the "
        "wheel is held still. If it feels wrong/oscillates, tick Invert Braking "
        "Weight below.");

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
    if (ImGui::SliderFloat("TT Start Hold (s)", &m_settings.ttStartHoldSec, 0.f, 10.f, "%.1f s"))
        m_engineDirty = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("SAFETY (Time Trial only). When a TT lap starts or you restart "
                          "from the menu, the game's AI drives the car for a few seconds "
                          "before handing control over. Force is held OFF for this long "
                          "after the restart so unexpected forces can't catch your hands. "
                          "0 = off. Only affects Time Trial restarts (not a stop-and-go on "
                          "track, and not a mid-lap unpause).");
    ImGui::SetNextItemWidth(-150.f);
    int hz = m_settings.ffbUpdateHz;
    if (ImGui::SliderInt("Update Rate (Hz)", &hz, FFB_MIN_HZ, FFB_MAX_HZ))
        m_settings.ffbUpdateHz = hz;
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
    ImGui::TextUnformatted("UDP port 20777   \xC2\xB7   F1 25/26: Telemetry On, 60 Hz, "
                           "Unrestricted   \xC2\xB7   set in-game FFB to 0%");
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
