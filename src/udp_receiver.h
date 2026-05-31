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

private:
    void loop();
    void parsePacket(const uint8_t* data, int len);
    void parseMotion(const uint8_t* payload, uint8_t playerIdx);
    void parseMotionEx(const uint8_t* payload);
    void parseCarTelemetry(const uint8_t* payload, uint8_t playerIdx);

    TelemetryState& m_state;
    AppStats&       m_stats;
    std::mutex      m_mutex;
    std::thread     m_thread;
    std::atomic<bool> m_running{false};
    uintptr_t       m_sock{0};   // SOCKET (avoid winsock header here)

    // Pause detection: track frameIdentifier so we know when physics is frozen
    uint32_t        m_lastFrame{0xFFFFFFFFu};
    int64_t         m_streakStartMs{0};
};
