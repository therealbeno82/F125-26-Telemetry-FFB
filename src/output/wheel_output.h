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
    std::string lastError() const;                 // thread-safe copy
    unsigned long sendErrors() const { return m_sendErrors.load(); }
    unsigned long sendOk() const { return m_sendOk.load(); }

private:
    void releaseEffects();

    void* m_haptic = nullptr;    // SDL_Haptic* (opaque to keep SDL out of header)
    int   m_effConstant = -1;    // SDL effect ids (-1 = not created)
    int   m_effDamper   = -1;
    int   m_effSine     = -1;

    std::vector<DeviceInfo> m_devices;
    int                 m_activeIdx = -1;
    mutable std::mutex  m_lock;
    std::string         m_error;
    bool                m_inited = false;

    // Diagnostics for the GUI: counts of successful / failed SDL effect updates
    std::atomic<unsigned long> m_sendOk{0};
    std::atomic<unsigned long> m_sendErrors{0};
};
