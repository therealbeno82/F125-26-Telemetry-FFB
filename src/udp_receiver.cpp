#include "udp_receiver.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>
#include <chrono>
#include <cmath>

#pragma comment(lib, "ws2_32.lib")

// ── F1 UDP packet header (48 bytes total) ────────────────────────────────────
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t packetFormat;       // 2025 or 2026
    uint8_t  gameYear;
    uint8_t  gameMajorVersion;
    uint8_t  gameMinorVersion;
    uint8_t  packetVersion;
    uint8_t  packetId;
    uint64_t sessionUID;
    float    sessionTime;
    uint32_t frameIdentifier;
    uint32_t overallFrameIdentifier;
    uint8_t  playerCarIndex;
    uint8_t  secondaryPlayerCarIndex;
};

// CarMotionData (60 bytes per car)
struct CarMotionData {
    float worldPositionX, worldPositionY, worldPositionZ;
    float worldVelocityX, worldVelocityY, worldVelocityZ;
    int16_t worldForwardDirX, worldForwardDirY, worldForwardDirZ;
    int16_t worldRightDirX,   worldRightDirY,   worldRightDirZ;
    float gForceLateral, gForceLongitudinal, gForceVertical;
    float yaw, pitch, roll;
};

// Motion Ex (player car only) - wheel order: RL, RR, FL, FR
struct PacketMotionExData {
    float suspensionPosition[4];
    float suspensionVelocity[4];
    float suspensionAcceleration[4];
    float wheelSpeed[4];
    float wheelSlipRatio[4];
    float wheelSlipAngle[4];   // ← this is what we want
    float wheelLatForce[4];
    float wheelLonForce[4];
    float heightOfCOGAboveGround;
    float localVelocityX, localVelocityY, localVelocityZ;
    float angularVelocityX, angularVelocityY, angularVelocityZ;
    float angularAccelerationX, angularAccelerationY, angularAccelerationZ;
    float frontWheelsAngle;
    float wheelVertForce[4];
};

// Car telemetry (per car, simplified to fields we use)
struct CarTelemetryData {
    uint16_t speed;           // km/h
    float    throttle;
    float    steer;           // -1..1
    float    brake;
    float    clutch;
    int8_t   gear;
    uint16_t engineRPM;
    uint8_t  drs;
    uint8_t  revLightsPercent;
    uint16_t revLightsBitValue;
    uint16_t brakesTemperature[4];
    uint8_t  tyresSurfaceTemperature[4];
    uint8_t  tyresInnerTemperature[4];
    uint16_t engineTemperature;
    float    tyresPressure[4];
    uint8_t  surfaceType[4];
};
#pragma pack(pop)

// ── UdpReceiver ───────────────────────────────────────────────────────────────

UdpReceiver::UdpReceiver(TelemetryState& state, AppStats& stats)
    : m_state(state), m_stats(stats) {}

UdpReceiver::~UdpReceiver() { stop(); }

bool UdpReceiver::start(uint16_t port) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return false;

    // Allow port reuse
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, sizeof(yes));

    // 200ms recv timeout so thread can check m_running
    DWORD tv = 200;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&tv, sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        return false;
    }

    m_sock    = (uintptr_t)sock;
    m_running = true;
    m_thread  = std::thread(&UdpReceiver::loop, this);
    return true;
}

void UdpReceiver::stop() {
    m_running = false;
    if (m_sock) {
        closesocket((SOCKET)m_sock);
        m_sock = 0;
    }
    if (m_thread.joinable()) m_thread.join();
    WSACleanup();
}

void UdpReceiver::loop() {
    // Set thread to slightly above normal priority
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    uint8_t buf[4096];
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (m_running) {
        int len = recvfrom((SOCKET)m_sock, (char*)buf, sizeof(buf),
                           0, (sockaddr*)&from, &fromLen);
        if (len <= 0) continue;
        if (len < (int)sizeof(PacketHeader)) continue;

        const auto* hdr = reinterpret_cast<const PacketHeader*>(buf);
        if (hdr->packetFormat != 2025 && hdr->packetFormat != 2026) continue;

        m_stats.udpPackets.fetch_add(1, std::memory_order_relaxed);
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now().time_since_epoch()).count();
        m_stats.lastUdpTimeMs.store(now, std::memory_order_relaxed);

        // Pause detection: the physics frame counter advances while driving and
        // freezes in menus / when paused (even though packets keep arriving).
        // Track when it last changed, and when the current advancing run began
        // so the engine can re-arm only after frames flow steadily again.
        if (hdr->frameIdentifier != m_lastFrame) {
            const int64_t prevChange = m_stats.lastFrameAdvanceMs.load(std::memory_order_relaxed);
            if (now - prevChange > FFB_PAUSE_FREEZE_MS)   // resuming after a freeze
                m_streakStartMs = now;
            m_lastFrame = hdr->frameIdentifier;
            m_stats.lastFrameAdvanceMs.store(now, std::memory_order_relaxed);
            m_stats.frameStreakStartMs.store(m_streakStartMs, std::memory_order_relaxed);
        }

        const uint8_t* payload = buf + sizeof(PacketHeader);
        int            payloadLen = len - (int)sizeof(PacketHeader);

        std::lock_guard<std::mutex> lock(m_mutex);
        switch (hdr->packetId) {
        case PKT_MOTION:
            if (payloadLen >= (int)(sizeof(CarMotionData) * (hdr->playerCarIndex + 1)))
                parseMotion(payload, hdr->playerCarIndex);
            break;
        case PKT_MOTION_EX:
            if (payloadLen >= (int)sizeof(PacketMotionExData))
                parseMotionEx(payload);
            break;
        case PKT_CAR_TELEM:
            if (payloadLen >= (int)(sizeof(CarTelemetryData) * (hdr->playerCarIndex + 1)))
                parseCarTelemetry(payload, hdr->playerCarIndex);
            break;
        }
    }
}

void UdpReceiver::parseMotion(const uint8_t* payload, uint8_t playerIdx) {
    const auto* cars = reinterpret_cast<const CarMotionData*>(payload);
    const auto& c    = cars[playerIdx];
    m_state.lateralG      = c.gForceLateral;
    m_state.longitudinalG = c.gForceLongitudinal;
    m_state.yaw           = c.yaw;
    m_state.roll          = c.roll;
}

void UdpReceiver::parseMotionEx(const uint8_t* payload) {
    const auto* ex = reinterpret_cast<const PacketMotionExData*>(payload);
    // Wheel order: RL=0, RR=1, FL=2, FR=3
    float fl = std::fabs(ex->wheelSlipAngle[2]);
    float fr = std::fabs(ex->wheelSlipAngle[3]);
    float rl = std::fabs(ex->wheelSlipAngle[0]);
    float rr = std::fabs(ex->wheelSlipAngle[1]);
    m_state.frontSlipAngle = (fl + fr) * 0.5f;
    m_state.rearSlipAngle  = (rl + rr) * 0.5f;

    m_state.suspFL = std::fabs(ex->suspensionPosition[2]);
    m_state.suspFR = std::fabs(ex->suspensionPosition[3]);

    // Front tyre lateral forces (FL=2, FR=3) — the real self-aligning torque
    m_state.frontLatForce = ex->wheelLatForce[2] + ex->wheelLatForce[3];

    // Front longitudinal forces — braking/traction load on the steered axle
    m_state.frontLonForce = ex->wheelLonForce[2] + ex->wheelLonForce[3];

    // Front vertical load (FL=2, FR=3) — mechanical weight + aero downforce +
    // braking weight transfer. Drives optional load-sensitive steering weight.
    m_state.frontVertForce = ex->wheelVertForce[2] + ex->wheelVertForce[3];

    // Slip ratios: negative = wheel slower than road (lockup), positive = spin.
    // Front avg drives lockup judder, rear avg drives wheelspin rumble.
    m_state.frontSlipRatio = (ex->wheelSlipRatio[2] + ex->wheelSlipRatio[3]) * 0.5f;
    m_state.rearSlipRatio  = (ex->wheelSlipRatio[0] + ex->wheelSlipRatio[1]) * 0.5f;

    // Suspension velocity (signed) for kerb/bump impact transients
    m_state.suspVelFL = ex->suspensionVelocity[2];
    m_state.suspVelFR = ex->suspensionVelocity[3];

    // Vehicle sideslip angle from local velocity: atan2(lateral, forward).
    // Use |forward| so the sign reflects slide direction, not travel direction;
    // gate at low speed where the ratio is numerically meaningless.
    float vx = ex->localVelocityX;
    float vz = ex->localVelocityZ;
    m_state.sideslip = (std::fabs(vz) > 1.f) ? std::atan2(vx, std::fabs(vz)) : 0.f;
}

void UdpReceiver::parseCarTelemetry(const uint8_t* payload, uint8_t playerIdx) {
    const auto* cars = reinterpret_cast<const CarTelemetryData*>(payload);
    const auto& c    = cars[playerIdx];
    m_state.speedKmh = (float)c.speed;
    m_state.steer    = c.steer;
    m_state.throttle = c.throttle;   // gate wheelspin rumble
    m_state.brake    = c.brake;      // gate lockup judder + braking weight
}
