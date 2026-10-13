#include "imago/imago_core.h"
#include "payload/command_manager.h"

#include <doctest.h>

#include <cstring>

using namespace imago;

namespace {

constexpr int64_t kTimeout = 300'000'000;

mavlink_command_long_t cmd(uint16_t command) {
    mavlink_command_long_t m{};
    m.command = command;
    return m;
}

mavlink_command_ack_t ack(uint16_t command, uint8_t result) {
    mavlink_command_ack_t a{};
    a.command = command;
    a.result = result;
    return a;
}

} // namespace

TEST_CASE("an ACK completes the oldest matching command and reports the round trip") {
    CommandManager m(kTimeout, 3);
    CommandManager::Message a = cmd(2000);
    CommandManager::Message b = cmd(2000);
    const auto idA = m.add(a, 0);
    const auto idB = m.add(b, 10'000'000);
    CHECK(idA != idB);
    CHECK(m.pending() == 2);

    auto r = m.onCommandAck(ack(2000, MAV_RESULT_ACCEPTED), 50'000'000);
    REQUIRE(r);
    CHECK(r->requestId == idA);
    CHECK(r->roundTripMs == doctest::Approx(50.0f));
    CHECK(r->result == IMAGO_RESULT_ACCEPTED);
    CHECK_FALSE(m.onCommandAck(ack(531, MAV_RESULT_ACCEPTED), 60'000'000).has_value());
    CHECK(m.pending() == 1);
}

TEST_CASE("unacknowledged commands are resent with increasing confirmation, then time out") {
    CommandManager m(kTimeout, 2);
    CommandManager::Message c = cmd(531);
    const auto id = m.add(c, 0);
    std::vector<CommandManager::Message> resend;
    std::vector<CommandManager::Result> expired;

    m.tick(kTimeout - 1, resend, expired);
    CHECK(resend.empty());

    m.tick(kTimeout, resend, expired);
    REQUIRE(resend.size() == 1);
    CHECK(std::get<mavlink_command_long_t>(resend[0]).confirmation == 1);
    resend.clear();
    m.tick(kTimeout * 2, resend, expired);
    REQUIRE(resend.size() == 1);
    CHECK(std::get<mavlink_command_long_t>(resend[0]).confirmation == 2);
    resend.clear();
    m.tick(kTimeout * 3, resend, expired);
    CHECK(resend.empty());
    REQUIRE(expired.size() == 1);
    CHECK(expired[0].requestId == id);
    CHECK(expired[0].result == IMAGO_RESULT_TIMEOUT);
    CHECK(m.pending() == 0);
}

TEST_CASE("commands registered with zero retries are never resent") {
    CommandManager m(kTimeout, 3);
    CommandManager::Message photo = cmd(2000);
    m.add(photo, 0, 0);
    std::vector<CommandManager::Message> resend;
    std::vector<CommandManager::Result> expired;
    m.tick(kTimeout, resend, expired);
    CHECK(resend.empty());
    CHECK(expired.size() == 1);
}

TEST_CASE("IN_PROGRESS stops resending but keeps waiting for the final result") {
    CommandManager m(kTimeout, 3);
    CommandManager::Message c = cmd(2500);
    m.add(c, 0);
    CHECK_FALSE(m.onCommandAck(ack(2500, MAV_RESULT_IN_PROGRESS), 1).has_value());
    std::vector<CommandManager::Message> resend;
    std::vector<CommandManager::Result> expired;
    m.tick(kTimeout * 2, resend, expired);
    CHECK(resend.empty());
    CHECK(expired.empty());
    auto r = m.onCommandAck(ack(2500, MAV_RESULT_ACCEPTED), kTimeout * 3);
    REQUIRE(r);
    CHECK(r->result == MAV_RESULT_ACCEPTED);
}

TEST_CASE("PARAM_EXT_ACK matches by parameter id") {
    CommandManager m(kTimeout, 3);
    mavlink_param_ext_set_t set{};
    std::memcpy(set.param_id, "GB_MODE", 7);
    CommandManager::Message msg = set;
    const auto id = m.add(msg, 0);
    mavlink_param_ext_ack_t a{};
    std::memcpy(a.param_id, "C_SOURCE", 8);
    CHECK_FALSE(m.onParamExtAck(a, 1).has_value());
    std::memset(a.param_id, 0, sizeof(a.param_id));
    std::memcpy(a.param_id, "GB_MODE", 7);
    a.param_result = PARAM_ACK_ACCEPTED;
    auto r = m.onParamExtAck(a, 2);
    REQUIRE(r);
    CHECK(r->requestId == id);
    CHECK(r->isParam);
    CHECK(r->paramId == "GB_MODE");
}
