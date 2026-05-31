# WASAPI Bridge

A low-latency C++ tool to route audio from a WASAPI loopback device (such as "CABLE Input" from VB-Audio) to a target WASAPI playback device (DAC/Sound Card) using the internal capabilities of the [`miniaudio`](https://github.com/mackron/miniaudio) library.

## Features

- **Device Routing**: Route real-time audio from a Source Audio Device to a Target Audio Device.
- **Channel Mapping**: Automatically maps source channels matching target channels by position. Excellent for downmixing highly-channelled APOs (like HeSuVi taking 7.1 and converting it directly down to Stereo).
- **Format Integrity**: Everything runs seamlessly using 32-bit floating point calculations avoiding integer overflow crackles.
- **Peak Handling**: Hard clamping avoids harsh audio clipping distortion and safeguards output drivers.
- **WASAPI Modes**: Supports configurable target output logic using either **Shared Mode** or **Exclusive Mode**.
- **Real-Time Configuration**: Prompts the user smoothly for Source Device, Target Device, Latency offsets, and limits.
- **Optimized**: Compile with native LTO & static linking ensuring blazing-fast execution speeds without overhead.
- **GUI + Terminal-Aware**: Native Win32 GUI with three tabs (Bridge / Log / About). Launching from a terminal attaches to the parent console so logs flow back into the shell that started it; launching from Explorer just shows the GUI.
- **Tray Integration**: Optional minimize-to-tray with right-click Open/Exit menu and live-state tooltip.

## Setup Requirements

- Windows 7 or later
- Visual Studio / GCC / LLVM Toolchain
- CMake

## Building the Project

This repo uses git submodules to gather `miniaudio`. Clone the project recursively:

```bash
git clone --recurse-submodules https://github.com/DevonTM/wasapi-bridge.git
cd wasapi-bridge
```

Then generate and build the executable using CMake and Ninja:

```bash
mkdir build
cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release ..
ninja
```

## Usage

Run `wasapi-bridge.exe`. The GUI opens with three tabs:

- **Bridge** — pick the Source loopback device and the Target playback device, choose Shared or Exclusive mode, set latency in ms (defaults: 10 for Shared, 5 for Exclusive), and click **Start Bridge**. The button toggles to **Stop Bridge** while running and inputs lock until you stop. Use **Rescan** after plugging or unplugging audio devices.
- **Log** — live log mirroring everything the bridge prints. Toggle auto-scroll, export to a UTF-8 file, or clear.
- **About** — version, license link, and a button to open the GitHub repo.

Tick **Minimize to tray** (default on) to send the window to the notification area on minimize. The tray icon shows a tooltip with the current bridge state and offers Open/Exit on right-click.

When launched from a terminal (`.\wasapi-bridge.exe` from cmd or PowerShell), the app attaches to the parent console and mirrors log output there. Launched from Explorer it just shows the GUI without opening a console.

## License

This project is licensed under the [MIT License](LICENSE).

`miniaudio` is dual-licensed under the MIT No Attribution (MIT-0) and Public Domain (Unlicense).
