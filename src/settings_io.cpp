#include "settings_io.h"
#include <fstream>
#include <string>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// ── Save ────────────────────────────────────────────────────────────────────
void saveSettings(const FFBSettings& s, const char* path) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    f << "# F1 FFB settings — edited live by the app, safe to delete to reset\n";
    f << "overallStrength="    << s.overallStrength    << "\n";
    f << "maxForceN="          << s.maxForceN          << "\n";
    f << "gripLossStrength="   << s.gripLossStrength   << "\n";
    f << "understeerStrength=" << s.understeerStrength << "\n";
    f << "oversteerStrength="  << s.oversteerStrength  << "\n";
    f << "smoothing="          << s.smoothing          << "\n";
    f << "minSpeedKmh="        << s.minSpeedKmh        << "\n";
    f << "ffbUpdateHz="        << s.ffbUpdateHz        << "\n";
    f << "invertForce="        << (s.invertForce ? 1 : 0) << "\n";
    f << "lockupStrength="     << s.lockupStrength     << "\n";
    f << "wheelspinStrength="  << s.wheelspinStrength  << "\n";
    f << "brakingStrength="    << s.brakingStrength    << "\n";
    f << "brakingInvert="      << (s.brakingInvert ? 1 : 0) << "\n";
    f << "maxOutput="          << s.maxOutput          << "\n";
    f << "softStartSec="       << s.softStartSec       << "\n";
    f << "ttStartHoldSec="     << s.ttStartHoldSec     << "\n";
    f << "minForce="           << s.minForce           << "\n";
    f << "loadSensitivity="    << s.loadSensitivity    << "\n";
    f << "loadRefN="           << s.loadRefN           << "\n";
}

// ── Validation ────────────────────────────────────────────────────────────────
// Pin every field to the range its GUI slider allows. Ranges mirror the sliders
// in imgui_ui.cpp; keep the two in sync if a slider range ever changes.
void clampSettings(FFBSettings& s) {
    // Clamp to [lo,hi], but first scrub NaN/Inf (which clampf passes through,
    // since NaN compares false both ways) back to the struct default `def`.
    auto fix = [](float v, float lo, float hi, float def) {
        if (!std::isfinite(v)) v = def;
        return clampf(v, lo, hi);
    };
    const FFBSettings d;   // built-in defaults, used as the fallback per field

    s.overallStrength    = fix(s.overallStrength,    0.f, 1.f,   d.overallStrength);
    s.maxOutput          = fix(s.maxOutput,          0.f, 1.f,   d.maxOutput);
    s.softStartSec       = fix(s.softStartSec,       0.f, 2.f,   d.softStartSec);
    s.ttStartHoldSec     = fix(s.ttStartHoldSec,     0.f, 10.f,  d.ttStartHoldSec);
    s.maxForceN          = fix(s.maxForceN,       4000.f, 20000.f, d.maxForceN);
    s.loadSensitivity    = fix(s.loadSensitivity,    0.f, 1.f,   d.loadSensitivity);
    s.loadRefN           = fix(s.loadRefN,        3000.f, 15000.f, d.loadRefN);
    s.gripLossStrength   = fix(s.gripLossStrength,   0.f, 2.f,   d.gripLossStrength);
    s.understeerStrength = fix(s.understeerStrength, 0.f, 2.f,   d.understeerStrength);
    s.oversteerStrength  = fix(s.oversteerStrength,  0.f, 1.f,   d.oversteerStrength);
    s.lockupStrength     = fix(s.lockupStrength,     0.f, 1.f,   d.lockupStrength);
    s.wheelspinStrength  = fix(s.wheelspinStrength,  0.f, 1.f,   d.wheelspinStrength);
    s.brakingStrength    = fix(s.brakingStrength,    0.f, 1.f,   d.brakingStrength);
    s.smoothing          = fix(s.smoothing,          0.f, 0.5f,  d.smoothing);
    s.minForce           = fix(s.minForce,           0.f, 0.3f,  d.minForce);
    s.minSpeedKmh        = fix(s.minSpeedKmh,        0.f, 200.f, d.minSpeedKmh);

    if (s.ffbUpdateHz < FFB_MIN_HZ) s.ffbUpdateHz = FFB_MIN_HZ;
    if (s.ffbUpdateHz > FFB_MAX_HZ) s.ffbUpdateHz = FFB_MAX_HZ;
}

// ── Load ────────────────────────────────────────────────────────────────────
bool loadSettings(FFBSettings& s, const char* path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key.empty() || val.empty()) continue;

        // Each known key parses into its matching field; unknown keys ignored.
        if      (key == "overallStrength")    s.overallStrength    = std::strtof(val.c_str(), nullptr);
        else if (key == "maxForceN")          s.maxForceN          = std::strtof(val.c_str(), nullptr);
        else if (key == "gripLossStrength")   s.gripLossStrength   = std::strtof(val.c_str(), nullptr);
        else if (key == "understeerStrength") s.understeerStrength = std::strtof(val.c_str(), nullptr);
        else if (key == "oversteerStrength")  s.oversteerStrength  = std::strtof(val.c_str(), nullptr);
        else if (key == "smoothing")          s.smoothing          = std::strtof(val.c_str(), nullptr);
        else if (key == "minSpeedKmh")        s.minSpeedKmh        = std::strtof(val.c_str(), nullptr);
        else if (key == "ffbUpdateHz")        s.ffbUpdateHz        = std::atoi(val.c_str());
        else if (key == "invertForce")        s.invertForce        = std::atoi(val.c_str()) != 0;
        else if (key == "lockupStrength")     s.lockupStrength     = std::strtof(val.c_str(), nullptr);
        else if (key == "wheelspinStrength")  s.wheelspinStrength  = std::strtof(val.c_str(), nullptr);
        else if (key == "brakingStrength")    s.brakingStrength    = std::strtof(val.c_str(), nullptr);
        else if (key == "brakingInvert")      s.brakingInvert      = std::atoi(val.c_str()) != 0;
        else if (key == "maxOutput")          s.maxOutput          = std::strtof(val.c_str(), nullptr);
        else if (key == "softStartSec")       s.softStartSec       = std::strtof(val.c_str(), nullptr);
        else if (key == "ttStartHoldSec")     s.ttStartHoldSec     = std::strtof(val.c_str(), nullptr);
        else if (key == "minForce")           s.minForce           = std::strtof(val.c_str(), nullptr);
        else if (key == "loadSensitivity")    s.loadSensitivity    = std::strtof(val.c_str(), nullptr);
        else if (key == "loadRefN")           s.loadRefN           = std::strtof(val.c_str(), nullptr);
    }
    // Validate at the front door: a corrupt or hand-edited file can never push
    // out-of-range values past this point into the running app.
    clampSettings(s);
    return true;
}

// ── Launch counter ────────────────────────────────────────────────────────────
int bumpLaunchCount() {
    int count = 0;
    {
        std::ifstream f(LAUNCH_COUNT_PATH);
        if (f) f >> count;
    }
    if (count < 0) count = 0;
    ++count;
    std::ofstream f(LAUNCH_COUNT_PATH, std::ios::trunc);
    if (f) f << count << "\n";
    return count;
}

bool isDonationNagDisabled() {
    std::ifstream f(NAG_DISABLED_PATH);
    if (!f) return false;
    int v = 0;
    f >> v;
    return v != 0;
}

void setDonationNagDisabled(bool disabled) {
    std::ofstream f(NAG_DISABLED_PATH, std::ios::trunc);
    if (f) f << (disabled ? 1 : 0) << "\n";
}

// ── Named profiles ──────────────────────────────────────────────────────────
namespace profiles {

std::string sanitize(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (char ch : name) {
        // Reject Windows-forbidden filename chars and control chars
        if (ch == '<' || ch == '>' || ch == ':' || ch == '"'  || ch == '/' ||
            ch == '\\'|| ch == '|' || ch == '?' || ch == '*'  || (unsigned char)ch < 0x20)
            out += '_';
        else
            out += ch;
    }
    // Windows dislikes trailing spaces/dots; trim both ends of whitespace too
    auto notspace = [](char c){ return c != ' ' && c != '.'; };
    out.erase(out.begin(), std::find_if(out.begin(), out.end(), notspace));
    out.erase(std::find_if(out.rbegin(), out.rend(), notspace).base(), out.end());
    if (out.empty()) out = "Profile";
    return out;
}

static std::string pathFor(const std::string& name) {
    return std::string(PROFILE_DIR) + "/" + sanitize(name) + ".ini";
}

std::vector<std::string> list() {
    std::vector<std::string> names;
    std::error_code ec;
    if (!fs::exists(PROFILE_DIR, ec)) return names;
    for (const auto& entry : fs::directory_iterator(PROFILE_DIR, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto& p = entry.path();
        if (p.extension() == ".ini")
            names.push_back(p.stem().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool load(const std::string& name, FFBSettings& out) {
    return loadSettings(out, pathFor(name).c_str());
}

std::string save(const std::string& name, const FFBSettings& s) {
    std::error_code ec;
    fs::create_directories(PROFILE_DIR, ec);
    std::string clean = sanitize(name);
    saveSettings(s, pathFor(clean).c_str());
    return clean;
}

bool remove(const std::string& name) {
    std::error_code ec;
    return fs::remove(pathFor(name), ec);
}

std::string readActive() {
    std::ifstream f(ACTIVE_PROFILE);
    if (!f) return "";
    std::string name;
    std::getline(f, name);
    // Trim trailing whitespace/CR the file may carry
    while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' '))
        name.pop_back();
    return name;
}

void writeActive(const std::string& name) {
    std::ofstream f(ACTIVE_PROFILE, std::ios::trunc);
    if (f) f << name << "\n";
}

} // namespace profiles
