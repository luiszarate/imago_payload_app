#pragma once

/// What the operating system needs to know about a ground control app.
namespace SystemIntegration {

/// Windows: 1 ms timer resolution for the process, so the core's timers and the video
/// presenter wake on time (the default tick is 15.6 ms). Elsewhere nothing to do.
void configureTimers();

/// Keeps the display on and the computer awake while connected: no screen saver, no sleep.
/// Main thread.
void setKeepAwake(bool on);

} // namespace SystemIntegration
