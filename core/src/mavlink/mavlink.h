#pragma once

// Single include point for the generated MAVLink C library (common dialect).
// Parsing and encoding always use an explicit mavlink_status_t owned by the caller, never the
// library's global per-channel state, so several sessions and threads can coexist.
#include <common/mavlink.h>
