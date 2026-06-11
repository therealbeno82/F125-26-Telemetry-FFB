#include "update_check.h"
#include "types.h"          // APP_VERSION

#include <windows.h>
#include <winhttp.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace {

constexpr const wchar_t* API_HOST = L"api.github.com";
constexpr const wchar_t* API_PATH = L"/repos/therealbeno82/F125-26-Telemetry-FFB/releases/latest";
constexpr const char*    REL_URL  = "https://github.com/therealbeno82/F125-26-Telemetry-FFB/releases/latest";

// Result handoff: the worker fills s_tag THEN sets s_available (release order),
// the GUI reads s_available THEN s_tag (acquire order). Fixed buffer — no
// static-destruction-order concerns with a detached thread at process exit.
char              s_tag[32]     = "";
std::atomic<bool> s_available{false};

// Parse "v2.1", "2.1", "v2.1-beta" → {2,1}. Returns false if no digits found.
bool parseVersion(const char* s, int& major, int& minor) {
    while (*s && (*s < '0' || *s > '9')) ++s;
    if (!*s) return false;
    major = 0; minor = 0;
    while (*s >= '0' && *s <= '9') major = major * 10 + (*s++ - '0');
    if (*s == '.') {
        ++s;
        while (*s >= '0' && *s <= '9') minor = minor * 10 + (*s++ - '0');
    }
    return true;
}

// GET the latest-release JSON. Returns empty on any failure. The WinHttpOpen
// agent string doubles as the User-Agent header GitHub's API requires.
std::string fetchLatestReleaseJson() {
    std::string body;
    HINTERNET ses = WinHttpOpen(L"F1FFB-update-check",
                                WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return body;
    WinHttpSetTimeouts(ses, 5000, 5000, 5000, 5000);

    HINTERNET con = WinHttpConnect(ses, API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", API_PATH, nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE)
                        : nullptr;
    if (req &&
        WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(req, nullptr)) {

        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(req,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            char  buf[4096];
            DWORD avail = 0, read = 0;
            // The fields we need sit near the top of the JSON; 64KB is plenty.
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0 &&
                   body.size() < 64 * 1024) {
                DWORD want = avail < sizeof(buf) ? avail : (DWORD)sizeof(buf);
                if (!WinHttpReadData(req, buf, want, &read) || read == 0) break;
                body.append(buf, read);
            }
        }
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return body;
}

void checkWorker() {
    std::string json = fetchLatestReleaseJson();
    if (json.empty()) return;

    // Tiny targeted extraction — no JSON library needed for one string field.
    const char* key = "\"tag_name\":\"";
    size_t pos = json.find(key);
    if (pos == std::string::npos) return;
    pos += std::strlen(key);
    size_t end = json.find('"', pos);
    if (end == std::string::npos || end - pos >= sizeof(s_tag)) return;

    char tag[sizeof(s_tag)];
    std::memcpy(tag, json.c_str() + pos, end - pos);
    tag[end - pos] = '\0';

    int curMaj, curMin, newMaj, newMin;
    if (!parseVersion(APP_VERSION, curMaj, curMin)) return;
    if (!parseVersion(tag,         newMaj, newMin)) return;

    if (newMaj > curMaj || (newMaj == curMaj && newMin > curMin)) {
        std::snprintf(s_tag, sizeof(s_tag), "%s", tag);
        s_available.store(true, std::memory_order_release);
    }
}

} // namespace

namespace updatecheck {

void start() {
    // Detached one-shot worker: bounded by the 5s WinHTTP timeouts, writes only
    // the static buffer + flag above, so there is nothing to join at shutdown.
    std::thread(checkWorker).detach();
}

bool available() {
    return s_available.load(std::memory_order_acquire);
}

const char* latestTag()   { return s_tag; }
const char* releasesUrl() { return REL_URL; }

} // namespace updatecheck
