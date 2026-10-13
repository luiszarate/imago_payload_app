#pragma once

namespace imago {

enum class ThreadRole {
    Network, // MAVLink receive and send timers
    Video,   // RTSP/RTP receive and frame assembly
};

// Names the calling thread and raises its scheduling priority where the OS allows it.
// Failures are ignored: priority is a hint, not a requirement.
void configureCurrentThread(const char* name, ThreadRole role);

} // namespace imago
