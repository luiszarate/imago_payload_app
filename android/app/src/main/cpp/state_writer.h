#pragma once

#include <imago/imago_core.h>

#include <cstddef>
#include <cstdint>

namespace imago::android {

/// Written last, so the reader can tell that both sides agree on the layout.
constexpr int64_t kStateEndMarker = 0x494D4147; // "IMAG"

/// Largest snapshot writeState can produce.
constexpr size_t kStateBufferSize = 4096;

/// Serializes the state snapshot for Kotlin, little-endian: every number as 4 bytes (int32 or
/// float32, booleans as 0/1), every string as an int32 byte count followed by UTF-8 bytes.
/// The order is the contract with PayloadSnapshot.read() in Kotlin: change both together.
/// Returns the number of bytes written, or 0 if `capacity` is too small.
size_t writeState(const imago_state& state, uint8_t* out, size_t capacity);

} // namespace imago::android
