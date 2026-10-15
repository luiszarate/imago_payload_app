#pragma once

namespace imago {

enum class ThreadRole {
    Network, // MAVLink receive and send timers
    Video,   // RTSP/RTP receive and frame assembly
    Background, // disk writes and other work that must never compete with the above
};

// Names the calling thread and raises its scheduling priority where the OS allows it.
// Failures are ignored: priority is a hint, not a requirement.
void configureCurrentThread(const char* name, ThreadRole role);

// Keeps the OS timer at 1 ms resolution while it lives. Windows otherwise rounds every sleep and
// timed wait up to its 15.6 ms tick, and a 30 Hz gimbal rate goes out at about 21 Hz. Since
// Windows 10 2004 this affects only the calling process. Elsewhere it does nothing.
class TimerResolution {
  public:
    TimerResolution();
    ~TimerResolution();
    TimerResolution(const TimerResolution&) = delete;
    TimerResolution& operator=(const TimerResolution&) = delete;
};

} // namespace imago
