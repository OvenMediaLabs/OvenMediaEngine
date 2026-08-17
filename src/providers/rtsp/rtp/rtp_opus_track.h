#pragma once

#include "../rtsp_library.h"

#include "rtp_passthrough_audio_track.h"

template<typename T>
using RtpOpusTrack = RtpPassthroughAudioTrack<T>;