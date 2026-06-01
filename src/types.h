#pragma once
#include <cstdint>
#include <atomic>
#include <cmath>

// Application version (shown in the title bar and header)
#define APP_VERSION "v1.1 Beta"

// ── F1 25/26 UDP packet IDs ───────────────────────────────────────────────────
constexpr uint8_t PKT_MOTION    = 0;
constexpr uint8_t PKT_CAR_TELEM = 6;
constexpr uint8_t PKT_CAR_STATUS= 7;
constexpr uint8_t PKT_MOTION_EX = 13;
constexpr uint16_t UDP_PORT      = 20777;

// FFB output-rate bounds. The default rate is conservative; the maximum is left
// generous so capable wheels aren't limited. Note that some DirectInput drivers
// can stop applying force — or fault — when effect updates come too fast, and
// that ceiling varies per wheel, so the rate is a user-tuned slider.
constexpr int FFB_MIN_HZ = 30;
constexpr int FFB_MAX_HZ = 360;

// Pause/safety thresholds. The game freezes the physics frame counter when
// paused or in menus while still sending packets, so we gate FFB on the frame
// actually advancing rather than on packets merely arriving.
constexpr int64_t FFB_PAUSE_FREEZE_MS = 200;  // no new frame this long → released
constexpr int64_t FFB_REARM_MS        = 150;  // frames must advance this long to re-arm

// ── Raw telemetry from UDP (written by UdpReceiver, read by FFBEngine) ────────
struct TelemetryState {
    // Motion packet
    float lateralG        = 0.f;   // g-force lateral
    float longitudinalG   = 0.f;
    float yaw             = 0.f;   // rad/s
    float roll            = 0.f;

    // Motion Ex packet (player car)
    float frontSlipAngle  = 0.f;   // avg front tyre slip angle (rad)
    float rearSlipAngle   = 0.f;   // avg rear  tyre slip angle (rad)
    float suspFL          = 0.f;   // suspension deflection FL (m)
    float suspFR          = 0.f;   // suspension deflection FR (m)
    float frontLatForce   = 0.f;   // FL+FR tyre lateral force (N) — true SAT source
    float frontLonForce   = 0.f;   // FL+FR tyre longitudinal force (N) — braking load
    float frontVertForce  = 0.f;   // FL+FR tyre vertical load (N) — weight + downforce
    float suspVelFL       = 0.f;   // suspension velocity FL (m/s) — kerb impacts
    float suspVelFR       = 0.f;   // suspension velocity FR (m/s)
    float sideslip        = 0.f;   // vehicle sideslip angle (rad) — oversteer cue
    float frontSlipRatio  = 0.f;   // avg front slip ratio (-=lockup, +=spin)
    float rearSlipRatio   = 0.f;   // avg rear  slip ratio (+=wheelspin)

    // Car Telemetry packet
    float speedKmh        = 0.f;
    float steer           = 0.f;   // -1..1
    float throttle        = 0.f;   // 0..1
    float brake           = 0.f;   // 0..1

    // Derived (written by engine)
    float understeer      = 0.f;   // 0..1
    float oversteer       = 0.f;   // 0..1
    float frontSlipNorm   = 0.f;   // 0..1
    float rearSlipNorm    = 0.f;   // 0..1
};

// ── FFB output signals (written by engine, read by output + GUI) ──────────────
struct FFBSignals {
    float torque   = 0.f;   // -1..1   main wheel force
    float friction = 0.f;   //  0..1   damping
    float rumble   = 0.f;   //  0..1   kerb vibration
};

// ── User-tunable settings ─────────────────────────────────────────────────────
struct FFBSettings {
    float overallStrength   = 0.85f;
    float gripLossStrength  = 0.70f;
    float understeerStrength= 0.80f;
    float oversteerStrength = 0.90f;
    float smoothing         = 0.15f;   // IIR alpha (0=none, →1=heavy)
    float minSpeedKmh       = 10.f;
    int   ffbUpdateHz       = 90;      // FFB/output rate. Some DirectInput wheels
                                       // drop force when updated too fast; the
                                       // safe ceiling varies, so the user tunes it.
    float maxForceN         = 12000.f; // front lateral force mapped to full torque
    bool  invertForce       = false;   // flip wheel direction if it pulls the wrong way
    float lockupStrength    = 0.60f;   // front lockup judder under braking
    float wheelspinStrength = 0.40f;   // rear wheelspin rumble on power
    float brakingStrength   = 0.50f;   // wheel firms up under braking load
    float maxOutput         = 1.00f;   // hard cap on final output (0..1) — safety/kids
    float softStartSec      = 0.50f;   // ramp force in over this long after re-arm/connect
    float minForce          = 0.00f;   // lift small forces past a belt/gear deadzone (0..0.3)
    float loadSensitivity   = 0.00f;   // weight steering by front vertical load (0=off..1)
    float loadRefN          = 8000.f;  // front vert load (FL+FR) treated as full weight
    float testForce         = 0.f;     // diagnostic: steady torque bypassing physics (not saved)
};

// ── App-wide statistics ───────────────────────────────────────────────────────
struct AppStats {
    std::atomic<uint64_t> udpPackets{0};
    std::atomic<uint64_t> ffbUpdates{0};
    std::atomic<int64_t>  lastUdpTimeMs{0};       // ms since epoch, any packet
    std::atomic<int64_t>  lastFrameAdvanceMs{0};  // ms when frameIdentifier last changed
    std::atomic<int64_t>  frameStreakStartMs{0};  // ms when current run of advancing began
    std::atomic<float>    clipLevel{0.f};         // 0..1 EMA of torque saturation (clipping)
    std::atomic<bool>     deviceConnected{false};
    char                  deviceName[128] = "None";
};

// ── Inline helpers ────────────────────────────────────────────────────────────
inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float lerpf(float a, float b, float t) {
    return a + t * (b - a);
}
inline float signf(float v) {
    return v >= 0.f ? 1.f : -1.f;
}
