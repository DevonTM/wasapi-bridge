## Requirement
- Route source audio device playback to another target audio device playback.
- Mainly used for "CABLE Input" from VB-Audio as the source audio device playback and internal/external DAC/Sound Card as the target audio device playback.
- Support APO because user may use this with HeSuVi.
- The source audio device and the target audio device may have many channels. For example when using HeSuVi need to set the "CABLE input" to 7.1 while the HeSuVi processed them using HRTF HRIR and downmix them to stereo again. We need to handle case where audio device have different number of available sound channel. I think it good to just map them with original position and ignore unavailable channel. For example 7.1 source device and stereo target device, then only front left and front right channel from source device mapped to the target device front left and front right too while ignoring other channels.
- Low latency and avoid crackling/stuttering.
- Handle peak clipping correctly.
- Automatically set output format and sample rate in the target audio device same as the source audio device on WASAPI Exclusive mode, if not supported then use resampler.
- Use resampler if the target audio device sample rate not same as the source audio device on WASAPI Shared mode.
- The source audio device may detected as 32 bit format even if we set it to 24 bit in Windows sound setting. The target audio device may only support up to 24 bit, so we must handle this case too.
- Prompt user to select the source and target device. Also filter out unsupported audio device.
- Prompt user to select the WASAPI mode.
- Prompt user to adjust the latency, the app must set good default latency for WASAPI Shared mode or WASAPI Exclusive mode.
- The prompt is simple, no need color or arrow selection.

## Setup
- Use WASAPI Loopback for the source audio device playback and use WASAPI Shared mode or WASAPI Exclusive mode for the target audio device playback.
- Use ring buffer.
- Use miniaudio library.
- Use C or C++.
- Use CMake and Ninja build system. Use -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 since my CMake detected as msys2/cygwin instead of MingW.
- Good project structure.
