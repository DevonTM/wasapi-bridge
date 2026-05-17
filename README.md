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

Simply run the executable and follow the command-line prompts. You will be asked to:
1. Select your Source Audio Device (Loopback device that you want to capture).
2. Select your Target Audio Device (Hardware speaker/DAC).
3. Select Shared or Exclusive mode (Exclusive prevents other applications from using the target device).
4. Specify target Latency in milliseconds (defaults to 10ms for Shared, 5ms for Exclusive).

To quit the application gracefully and release audio locks, press `Ctrl+C` or close the console window.

## License

This project is licensed under the [MIT License](LICENSE).

`miniaudio` is dual-licensed under the MIT No Attribution (MIT-0) and Public Domain (Unlicense).
