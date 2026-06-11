#pragma once
#include "../types.h"
#include <vector>
#include <string>
#include <mutex>
#include <atomic>

// FFB output via SDL2's haptic subsystem. We use SDL rather than raw
// DirectInput because many direct-drive bases refuse the DirectInput
// exclusive-acquire path, yet work through SDL_HapticOpen — which also
// enumerates them by haptic index instead of the joystick-haptic flag some of
// them report as 0. Works with any DirectInput force-feedback wheel.

struct DeviceInfo {
    int         hapticIndex;   // index into SDL's haptic device list
    std::string name;          // UTF-8 device name
};

class WheelOutput {
public:
    WheelOutput();
    ~WheelOutput();

    bool init();                 // SDL_Init(JOYSTICK|HAPTIC) + first enumerate
    void enumerate();            // (re)scan haptic devices
    bool openDevice(int index);  // open device at m_devices[index]
    void closeDevice();

    // Called from the FFB thread at the engine update rate.
    void send(const FFBSignals& sig);

    const std::vector<DeviceInfo>& devices() const { return m_devices; }
    int  activeDeviceIndex() const { return m_activeIdx; }
    bool isActive() const { return m_haptic != nullptr; }
    // Which optional effects the open device accepted (diagnostics for the GUI).
    bool sineSupported()   const { return m_effSine   >= 0; }
    bool damperSupported() const { return m_effDamper >= 0; }
    std::string lastError() const;                 // thread-safe copy
    unsigned long sendErrors() const { return m_sendErrors.load(); }
    unsigned long sendOk() const { return m_sendOk.load(); }

private:
    void releaseEffects();
    void closeDeviceLocked();    // body of closeDevice; caller must hold m_lock

    void* m_haptic = nullptr;    // SDL_Haptic* (opaque to keep SDL out of header)
    int   m_effConstant = -1;    // SDL effect ids (-1 = not created)
    int   m_effDamper   = -1;
    int   m_effSine     = -1;

    // Last values pushed to the damper/sine effects. These change less often than
    // the constant force, so we skip the driver round-trip when they're unchanged.
    // Sentinel = "nothing sent yet" (no valid level), reset on each openDevice().
    static constexpr int LVL_UNSET = -100000;   // outside the Sint16 level range
    int   m_lastDamperLvl  = LVL_UNSET;
    int   m_lastSineLvl    = LVL_UNSET;
    int   m_lastSinePeriod = LVL_UNSET;         // ms; tracked so a pitch change updates too

    std::vector<DeviceInfo> m_devices;
    int                 m_activeIdx = -1;
    mutable std::mutex  m_lock;
    std::string         m_error;
    bool                m_inited = false;

    // Diagnostics for the GUI: counts of successful / failed SDL effect updates
    std::atomic<unsigned long> m_sendOk{0};
    std::atomic<unsigned long> m_sendErrors{0};
};
