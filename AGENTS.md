## Requirements
- Route source audio device playback to another target audio device playback.
- Mainly used for "CABLE Input" from VB-Audio as the source audio device playback and internal/external DAC/Sound Card as the target audio device playback.
- Support APO because users may use this with HeSuVi.
- The source audio device and the target audio device may have many channels. For example, when using HeSuVi, you need to set the "CABLE input" to 7.1 while HeSuVi processes them using HRTF HRIR and downmixes them to stereo again. We need to handle cases where audio devices have different numbers of available sound channels. I think it's good to just map them with original position and ignore unavailable channels. For example, 7.1 source device and stereo target device, then only front left and front right channels from source device are mapped to the target device front left and front right too while ignoring other channels.
- Low latency and avoid crackling/stuttering.
- Handle peak clipping correctly.
- Automatically set output format and sample rate in the target audio device same as the source audio device on WASAPI Exclusive mode, if not supported then use resampler.
- Use resampler if the target audio device sample rate is not same as the source audio device on WASAPI Shared mode.
- The source audio device may be detected as 32 bit format even if we set it to 24 bit in Windows sound setting. The target audio device may only support up to 24 bit, so we must handle this case too.
- A clean, simple, modern-looking GUI for the app.

## Setup
- Use WASAPI Loopback for the source audio device playback and use WASAPI Shared mode or WASAPI Exclusive mode for the target audio device playback.
- Use ring buffer.
- Use miniaudio library.
- Use C or C++.
- Use CMake and Ninja build system. Use `-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64` since my CMake is detected as msys2/cygwin instead of MinGW.
- Good project structure.

## File-editing rules
- Never rewrite a file > 200 lines whole. Use replace_in_file with one small SEARCH/REPLACE block at a time, even if you need many calls.
- Each replace_in_file call: maximum 3 SEARCH/REPLACE blocks, max ~80 lines of replacement content total. If you need more, split across multiple calls.
- Before any file write, state the file size. If unknown, read_file first. If > 200 lines, plan replace_in_file. Always.
- If a tool call fails with "without value for required parameter," do NOT retry the same call. Cut the payload in half and try again.

## Reasoning rules
- Keep "thinking" between tool calls under ~200 words. Long planning belongs in plan_mode_respond, not before tool calls.
- Don't restate the full code being edited inside reasoning. The diff block already contains it.

## Task structure
- Break large refactors into a step-list of independent files. Touch one file per turn, build after every 2-3 file changes.
- Verify with execute_command `cmake --build` at every checkpoint.
- Stop and ask for direction if 2 successive build attempts fail.
