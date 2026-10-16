#include "mavlink/tlog_filter.h"

#include "platform/clock.h"

namespace imago {

namespace {

constexpr int64_t kStreamPeriodNs = kNsPerSec;

bool isEvent(const mavlink_message_t& m) {
    switch (m.msgid) {
    case MAVLINK_MSG_ID_COMMAND_LONG:
        // The angle-control fallback repeats at the joystick's rate: a stream, not a command.
        return mavlink_msg_command_long_get_command(&m) != MAV_CMD_DO_MOUNT_CONTROL;
    case MAVLINK_MSG_ID_COMMAND_INT:
    case MAVLINK_MSG_ID_COMMAND_ACK:
    case MAVLINK_MSG_ID_PARAM_EXT_REQUEST_READ:
    case MAVLINK_MSG_ID_PARAM_EXT_REQUEST_LIST:
    case MAVLINK_MSG_ID_PARAM_EXT_VALUE:
    case MAVLINK_MSG_ID_PARAM_EXT_SET:
    case MAVLINK_MSG_ID_PARAM_EXT_ACK:
    case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
    case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
    case MAVLINK_MSG_ID_PARAM_SET:
    case MAVLINK_MSG_ID_PARAM_VALUE:
    case MAVLINK_MSG_ID_STATUSTEXT:
    case MAVLINK_MSG_ID_CAMERA_IMAGE_CAPTURED:
    case MAVLINK_MSG_ID_CAMERA_INFORMATION:
    case MAVLINK_MSG_ID_VIDEO_STREAM_INFORMATION:
    case MAVLINK_MSG_ID_COMPONENT_INFORMATION_BASIC:
        return true;
    default:
        return false;
    }
}

// What tells one stream from another within a message: the data value's index, or the
// repeated command.
uint16_t streamOf(const mavlink_message_t& m) {
    switch (m.msgid) {
    case MAVLINK_MSG_ID_DEBUG:
        return mavlink_msg_debug_get_ind(&m);
    case MAVLINK_MSG_ID_COMMAND_LONG:
        return mavlink_msg_command_long_get_command(&m);
    default:
        return 0;
    }
}

} // namespace

bool TlogFilter::keep(const mavlink_message_t& m, bool outgoing, int64_t nowNs) {
    if (!outgoing && !keepSender_(m.compid)) {
        return false;
    }
    if (isEvent(m)) {
        return true;
    }
    const uint64_t key = (static_cast<uint64_t>(outgoing) << 63) |
                         (static_cast<uint64_t>(m.msgid & 0xFFFFFF) << 32) |
                         (static_cast<uint64_t>(m.sysid & 0xFF) << 24) |
                         (static_cast<uint64_t>(m.compid) << 16) | streamOf(m);
    std::lock_guard lock(mutex_);
    auto [it, fresh] = lastKeptNs_.try_emplace(key, nowNs);
    if (fresh) {
        return true;
    }
    if (nowNs - it->second < kStreamPeriodNs) {
        return false;
    }
    it->second = nowNs;
    return true;
}

} // namespace imago
