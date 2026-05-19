/*
 * miniaudio implementation translation unit.
 *
 * We only ever need WASAPI on Windows, and the bridge does not decode,
 * encode, generate, or graph audio - it just shuttles PCM frames from a
 * loopback capture to a playback device. Disabling everything else
 * shrinks the binary, speeds up compilation, and reduces i-cache
 * pressure on the audio thread.
 */

/* Backends: WASAPI only. */
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_WASAPI

/* Decoders / encoders / format readers - unused. */
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3

/* High-level subsystems - unused. */
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
