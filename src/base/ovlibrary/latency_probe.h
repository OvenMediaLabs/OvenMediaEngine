//==============================================================================
//
//  OvenMediaEngine
//
//  Copyright (c) 2024 AirenSoft. All rights reserved.
//
//==============================================================================
#pragma once

// Serving-path latency/stall instrumentation, compiled only when the OME_LATENCY_PROBE build
// option is enabled (-DOME_LATENCY_PROBE=ON). Every subsystem appends to a single file resolved
// once at first use:
//   - OME_LATENCY_PROBE_DIR environment variable, if set
//   - otherwise /dev/shm (tmpfs, so the log I/O does not perturb the latency being measured)
// The directory is created if missing, and the resolved path is announced once on stderr.
#ifdef OME_LATENCY_PROBE

#include "format_string.h"

namespace ov
{
	// Appends one record to "<dir>/latency_probe.log". The current wall-clock time and the tag
	// are prepended automatically, so records from every subsystem form one time-sorted stream:
	//   "<epoch_ms> <TAG> <fields...>"
	// Filter a single subsystem with e.g. `grep RECV_LATE`.
	void LatencyProbeLog(const char *tag, const char *fmt, ...) OV_PRINTF_FORMAT(2, 3);
}  // namespace ov

#endif	// OME_LATENCY_PROBE
