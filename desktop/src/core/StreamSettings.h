#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// The payload's own settings with no place in its PARAM_EXT catalog's generic rows: the
/// encoder of the stream the Orus L serves itself (one per camera) and the range finder's rate.
namespace StreamSettings {

/// imago_stream_camera: 1 EO, 2 IR, as Gremsy's examples number them (the core sends nothing
/// for 0, which the apps sent before).
QList<int> cameras();
/// "EO", "IR"; empty for another number.
QString cameraTitle(int camera);
bool isCamera(int camera);

/// "1920×1080" for each resolution level of a camera's stream, from the core; empty for an
/// unknown camera.
QStringList sizeTitles(int camera);

/// The range finder's rates in the order of the camera's LRF_MODE setting (imago_lrf_set_mode).
QStringList rangeFinderRates();
/// After this the rate shown as asked is read back from the payload: through Herelink the
/// acknowledgement often never comes though the setting applied (2026-11-13).
constexpr int kRangeFinderReadBackMs = 1500;

} // namespace StreamSettings
