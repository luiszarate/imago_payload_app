#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace imago::web {

// The Orus L's web settings pages (firmware 3.0.5, read 2026-10-16; see
// docs/protocol/orus-l.md, "Web settings"). Plain HTML forms: the current values are in the
// page, and each section's Apply posts its own fields to the same URL.
inline constexpr uint16_t kOrusWebPort = 8000;
inline constexpr const char* kIrCameraPath = "/setup-ir-camera";
inline constexpr const char* kSystemsPath = "/setup-systems";

// Field name -> current value: each <input>'s value and each <select>'s selected option.
// Tolerant of the page's broken markup (repeated ids, a <head> inside <body>).
std::map<std::string, std::string> formValues(const std::string& html);

// Body of the "Isotherms Regions" Apply. measurementRange 0: -20..150 °C, 1: -20..550 °C.
std::string isothermForm(bool enabled, int thresholdC, int measurementRange);

// Body that restarts the payload's VIO service (fullReboot false) or the whole system.
std::string restartForm(bool fullReboot);

} // namespace imago::web
