#pragma once

// Startup update check: a detached background thread asks the GitHub Releases
// API for the latest release tag and compares it against APP_VERSION. Entirely
// fire-and-forget — never blocks the GUI, and stays silent on any failure
// (offline, rate-limited, bad response). The GUI polls available() each frame.
namespace updatecheck {

// Kick off the one-shot background check. Safe to call once at startup.
void start();

// True once the check finished AND found a release newer than APP_VERSION.
bool available();

// Tag name of the newer release (e.g. "v2.1"). Valid only when available().
const char* latestTag();

// Releases page to open when the user clicks the update notice.
const char* releasesUrl();

} // namespace updatecheck
