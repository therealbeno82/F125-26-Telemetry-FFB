#pragma once
#include "types.h"
#include <string>
#include <vector>

// Plain key=value file kept next to the exe (same dir as the ImGui .ini).
constexpr const char* SETTINGS_PATH  = "f1ffb_settings.ini";

// Named profiles live as individual .ini files in this folder; the active one
// is remembered between launches so the user keeps their last selection.
constexpr const char* PROFILE_DIR    = "profiles";
constexpr const char* ACTIVE_PROFILE = "f1ffb_active.txt";

// Persistent count of how many times the app has been launched (used to show
// an occasional donation reminder). Stored next to the exe.
constexpr const char* LAUNCH_COUNT_PATH = "f1ffb_launches.txt";

// Read the stored launch count, increment it, write it back, and return the
// new value. No-throw; returns 1 on the first ever launch.
int bumpLaunchCount();

// Persistent "user opted out of the donation reminder" flag. Stored next to
// the exe; once set, the reminder never appears again.
constexpr const char* NAG_DISABLED_PATH = "f1ffb_nag_off.txt";
bool isDonationNagDisabled();
void setDonationNagDisabled(bool disabled);

// Persistent "user dismissed the Quick Start Guide" flag. The guide pops up on
// every launch until the user ticks "don't show this again".
constexpr const char* GUIDE_DISABLED_PATH = "f1ffb_guide_off.txt";
bool isQuickStartGuideDisabled();
void setQuickStartGuideDisabled(bool disabled);

// User-configurable UDP port the game sends telemetry to. Stored GLOBALLY (next
// to the exe, NOT per-profile): it's a machine/setup value, so switching a
// tuning profile must never change which port receives telemetry. Returns
// UDP_PORT (20777) when the file is absent or holds an out-of-range value.
constexpr const char* UDP_PORT_PATH = "f1ffb_udp_port.txt";
uint16_t loadUdpPort();
void     saveUdpPort(uint16_t port);

// Pin every field to the same valid range its GUI slider enforces. Called
// automatically at the end of loadSettings so a corrupt or hand-edited file
// can never inject out-of-range values into the running app. Safe to call
// directly too. No-throw.
void clampSettings(FFBSettings& s);

// Persist / restore user FFB tuning. Both are no-throw; load leaves any
// missing field at its struct default, so older/partial files stay valid,
// and load runs clampSettings() so every value is in range before use.
void saveSettings(const FFBSettings& s, const char* path = SETTINGS_PATH);
bool loadSettings(FFBSettings& s, const char* path = SETTINGS_PATH);

// ── Named profiles ──────────────────────────────────────────────────────────
// Each profile is one .ini in PROFILE_DIR; its name is the file stem. All
// functions are no-throw and resolve paths relative to the working directory.
namespace profiles {
    // Sorted list of existing profile names (file stems, no extension).
    std::vector<std::string> list();

    // Load a profile's tuning into `out`. False if the profile doesn't exist.
    bool load(const std::string& name, FFBSettings& out);

    // Create or overwrite a profile. Name is sanitised for the filesystem.
    // Returns the stored (sanitised) name, or empty on failure.
    std::string save(const std::string& name, const FFBSettings& s);

    // Delete a profile. False if it didn't exist.
    bool remove(const std::string& name);

    // Strip characters Windows forbids in filenames; never returns empty.
    std::string sanitize(const std::string& name);

    // Remember / recall which profile is active across launches.
    std::string readActive();
    void        writeActive(const std::string& name);
}
