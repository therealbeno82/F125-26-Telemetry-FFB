#pragma once
#include "types.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>

class UdpReceiver {
public:
    explicit UdpReceiver(TelemetryState& state, AppStats& stats);
    ~UdpReceiver();

    bool start(uint16_t port = UDP_PORT);
    void stop();
    bool isRunning() const { return m_running; }

    // Switch to a different port without dropping the app: stops the receive
    // thread, rebinds, and restarts. If the new port won't bind (in use), the
    // previous port is restored so telemetry keeps flowing. Returns success.
    bool rebind(uint16_t port);
    uint16_t boundPort() const { return m_boundPort; }

private:
    void loop();
    void parseMotion(const uint8_t* payload, uint8_t playerIdx);
    void parseMotionEx(const uint8_t* payload);
    void parseCarTelemetry(const uint8_t* payload, uint8_t playerIdx);

    TelemetryState& m_state;
    AppStats&       m_stats;
    std::mutex      m_mutex;
    std::thread     m_thread;
    std::atomic<bool> m_running{false};
    uintptr_t       m_sock{0};   // SOCKET (avoid winsock header here)
    bool            m_wsaInited{false};   // WSAStartup succeeded; cleanup exactly once
    uint16_t        m_boundPort{0};       // port the socket is currently bound to (0 = none)

    // Pause detection: track overallFrameIdentifier (continuous, never resets mid-session)
    uint32_t        m_lastFrame{0xFFFFFFFFu};
    int64_t         m_streakStartMs{0};
    uint64_t        m_lastSessionUID{0};   // resets the Auto Max Force peak on session change
};
