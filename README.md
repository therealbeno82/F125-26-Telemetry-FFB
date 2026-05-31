# F1 FFB — Telemetry Force Feedback Enhancer
### EA F1 25 / F1 26  ·  Windows  ·  Direct-Drive & belt/gear wheels

A lightweight C++ app that reads F1's UDP telemetry and generates its own force
feedback, sent to your wheel through SDL2's haptic layer. It replaces the game's
built-in FFB with physics-driven forces — base torque from real tyre lateral
force, plus understeer/oversteer cues, lockup and wheelspin rumble, and braking
weight — all tunable live and saved into named profiles.

Works with **any DirectInput force-feedback wheel** (SimuCube, Fanatec, Moza,
Asetek, Simagic, Logitech, Thrustmaster, …).

---

## Quick Start

### 1. Install prerequisites (one-time)
- **CMake** — https://cmake.org/download/ (tick *Add CMake to PATH* during install)
- **Visual Studio 2022 Build Tools** — https://aka.ms/vs/17/release/vs_BuildTools.exe
  (select the **Desktop development with C++** workload)

### 2. Build
```
build.bat
```
The first build downloads Dear ImGui and SDL2 and compiles everything (a few
minutes). Later builds take seconds. `F1FFB.exe` appears in the project root —
it's fully self-contained (SDL2 is linked statically, no DLLs to ship).

### 3. Turn on telemetry in F1 25 / 26
```
Settings → Telemetry Settings
  UDP Telemetry:    On
  UDP Broadcast:    Off
  UDP IP Address:   127.0.0.1      (same PC as the game)
  UDP Port:         20777
  UDP Send Rate:    60 Hz
  UDP Format:       2025  (or 2026)
  Your Telemetry:   Public / Unrestricted
```
Then, in **Settings → Vibration & Force Feedback**, turn the game's own
**FFB Strength to 0%** — this app drives the wheel instead, and you don't want
the two fighting.

### 4. Run
Launch `F1FFB.exe`, click your wheel in the **Wheel Device** list to connect
(the base's auto-centre spring switching off confirms it's connected), then
drive. The header shows **CONNECTED** once telemetry is flowing.

---

## Using the app

**Connect your wheel** — pick it from the Wheel Device list. If it won't open,
close your wheel's tuning software (True Drive, Tuner, etc.) and any game holding
the wheel, then try again. The list shows the exact error if it fails.

**Test without the game** — tick *Send test force (25%)*. The wheel should pull
and hold to one side. Use this to confirm the wheel responds and to check
direction before driving.

**Profiles** — the *Profile* panel lets you save your tuning under a name and
switch instantly (e.g. a strong **F1** profile, a softer **F2**, or a capped
**Kids** profile). Type a name and **Save As** to create one; **Save** overwrites
the active profile; **Delete** removes it. Your last-used profile is restored on
the next launch. Profiles are plain text files in the `profiles/` folder.

**Tune** — adjust the sliders live; changes apply instantly. Watch the
**CLIP** meter by the torque bar while driving:
- grey = healthy headroom
- yellow = occasional clipping
- red = clipping often (you're losing road detail — lower Overall Strength or
  raise Max Force)

---

## FFB Parameters

| Parameter | What it does |
|-----------|--------------|
| **Overall Strength** | Master output scale. |
| **Max Output (Safety)** | Hard ceiling on *all* force, applied last. Set low for a kids profile — nothing can exceed it. |
| **Soft Start (s)** | Eases force in over this long after connecting/unpausing, so it never snaps on. |
| **Max Force (N)** | Front tyre lateral force mapped to full torque. **Lower = stronger** (clips earlier); higher = lighter with more headroom. |
| **Grip Loss Feel** | How strongly tyre slip reduces wheel force (>1 = exaggerated). |
| **Understeer Cue** | Wheel lightening when the front tyres slide (>1 = exaggerated). |
| **Oversteer Cue** | Counter-steer cue when the rear steps out (from vehicle sideslip). |
| **Lockup Judder** | Vibration when the front wheels lock under braking. |
| **Wheelspin Rumble** | Vibration when the rear wheels spin on power. |
| **Braking Weight** | Firms the wheel up under heavy braking load. |
| **Smoothing** | Output smoothing/interpolation. Increase for DD wheels to calm oscillation. |
| **Min Force** | Lifts small forces above a wheel's mechanical deadzone. **Raise for belt/gear wheels** (Logitech, Thrustmaster); leave **0 for direct drive**. |
| **Update Rate (Hz)** | How fast the wheel is updated. The 60 Hz telemetry is interpolated up to this rate. See the note below. |
| **Invert Force Direction** | Flip the wheel's pull direction if forces feel reversed. |

### A note on Update Rate
The output is interpolated smoothly between the 60 Hz telemetry frames, so a
higher rate is smoother — **but many DirectInput wheels stop applying force, or
even become unstable, when updated too fast**, and that ceiling varies per wheel.
**60–90 Hz suits most wheels.** If you raise it and the force cuts out or the app
misbehaves, lower it again. (On the SimuCube 2 used for development, the limit
was around 245 Hz.)

---

## Safety features

This app is built around the fact that strong wheels can hurt you:

- **Telemetry-loss / pause release** — if the game is paused, exited, in a menu,
  or minimised, the wheel is **released to zero** within a fraction of a second.
  The header shows **PAUSED · FFB RELEASED**. Force returns smoothly when you
  resume (re-arm hysteresis prevents a stray menu frame from spiking the wheel).
- **Soft start** — force always eases in rather than snapping on.
- **Max Output cap** — a hard ceiling you can set per profile (great for kids).
- **Speed fade** — no force below walking pace, so the pits stay calm.

> ⚠ Direct-drive wheels are powerful. Start with a low **Overall Strength** /
> high **Max Force**, keep a hand ready, and raise gradually.

---

## How it works

```
F1 25 / 26  ──UDP 20777 (60 Hz)──►  UdpReceiver  (parses motion, tyre forces,
                                                   slip, pedals, frame counter)
                                          │  shared TelemetryState
                                          ▼
                                     FFBEngine  (own thread, time-based loop)
                                          │   base torque  = front lateral force
                                          │   + understeer / oversteer cues
                                          │   + lockup / wheelspin rumble
                                          │   + braking weight
                                          │   → smoothing/interpolation
                                          │   → min-force, soft-start, max-output
                                          ▼
                                     WheelOutput  (SDL2 haptic: constant force +
                                                   damper + sine effects)
                                          ▼
                                     Your wheel base
```

## File structure
```
F125-Telemetry-FFB/
├── build.bat              ← build & run
├── CMakeLists.txt         ← fetches Dear ImGui + SDL2 (static)
├── app.rc / F1FFB.ico     ← application icon
└── src/
    ├── main.cpp           ← Win32 window, D3D11, wiring, startup/shutdown
    ├── types.h            ← shared structs + tunables (FFBSettings)
    ├── udp_receiver.*     ← F1 UDP telemetry parser
    ├── ffb_engine.*       ← FFB signal processing
    ├── settings_io.*      ← profile save/load
    ├── output/
    │   └── wheel_output.* ← SDL2 haptic FFB output
    └── gui/
        └── imgui_ui.*     ← Dear ImGui UI (Amber / Carbon theme)
```

---

## Troubleshooting

**Wheel won't connect** — close the wheel's tuning software and any game using
the wheel, then click the device again (the error message tells you why). Use
*Rescan Devices* if it isn't listed.

**Connected but no force** — make sure you're actually driving (no force when
paused, stationary, or on a straight, by design). Use *Send test force* to verify
the output path. If the wheel pulls the wrong way, tick **Invert Force Direction**.

**App crashes / force drops at high Update Rate** — lower the **Update Rate**;
your wheel's DirectInput driver can't keep up past a certain rate.

**Wheel feels weak** — lower **Max Force (N)** (lower = stronger) and/or raise
**Overall Strength**. Watch the CLIP meter so you don't over-drive it.

**Belt/gear wheel feels dead around centre** — raise **Min Force** until small
forces come alive (typically 0.05–0.12).

---

## License

Released under the [MIT License](LICENSE) — free to use, modify, and
distribute. Provided **as-is, with no warranty**: direct-drive wheels are
powerful, and you use this software at your own risk.

Built with [Dear ImGui](https://github.com/ocornut/imgui) (MIT) and
[SDL2](https://github.com/libsdl-org/SDL) (zlib).

---

*Built and tuned on a SimuCube 2, but vendor-neutral — it talks to any
DirectInput force-feedback wheel through SDL2.*
