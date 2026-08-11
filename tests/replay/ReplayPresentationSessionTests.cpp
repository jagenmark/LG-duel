#include "client/ClientGame.hpp"
#include "replay/ReplayPresentationSession.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

static_assert(!std::is_constructible_v<lg::replay::ReplayPresentationSession,
                                       lg::ClientGame &>);

int expect(bool condition, std::string_view message) {
  if (condition)
    return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

lg::replay::ReplayDemo presentationDemo() {
  lg::replay::ReplayDemo demo;
  demo.metadata.initialServerTick = 100U;
  demo.metadata.players[0] = {0U, true, false, lg::Team::None, "alpha"};
  demo.metadata.players[1] = {1U, true, true, lg::Team::None, "bot"};
  for (std::uint32_t tick = 100U; tick < 108U; ++tick) {
    demo.ticks.push_back({tick, {}});
  }
  return demo;
}

} // namespace

int main() {
  int failures = 0;
  const lg::replay::ReplayDemo demo = presentationDemo();
  lg::replay::ReplayPresentationSession session;
  std::string error;
  failures += expect(session.begin(demo, 7U, &error),
                     "presentation should select the first recorded player "
                     "when follow slot is invalid");
  failures += expect(session.state().active && session.state().paused &&
                         session.state().followSlot == 0U &&
                         session.state().currentTick == 100U &&
                         session.state().progress == 0.0F,
                     "new presentation should start paused at the replay start "
                     "without a ClientGame");
  failures += expect(
      session.setSpeed(4.0F) && !session.setSpeed(4.1F) &&
          session.setCameraMode(lg::replay::ReplayCameraMode::Chase) &&
          session.setFollowSlot(1U) && !session.setFollowSlot(2U),
      "controls should bound speed, camera mode, and recorded follow slots");
  failures += expect(!session.advance(1.0),
                     "paused presentation should not advance its replay clock");
  session.setPaused(false);
  failures +=
      expect(!session.advance(0.001) && session.advance(0.002) &&
                 session.state().currentTick == 101U,
             "fractional replay time should advance only whole fixed ticks");
  failures += expect(
      session.step(-1) && session.state().currentTick == 100U &&
          session.seek(105U) && session.state().progress > 0.5F,
      "step and seek should operate within the independent replay clock");
  failures +=
      expect(session.seek(108U) && !session.state().active &&
                 session.state().stopReason ==
                     lg::replay::ReplayPresentationStopReason::Complete,
             "reaching the end should complete the presentation session");

  failures += expect(session.begin(demo, 0U, &error),
                     "presentation should restart without live client state");
  session.abort(lg::replay::ReplayPresentationStopReason::UserSkipped);
  failures +=
      expect(!session.state().active &&
                 session.state().stopReason ==
                     lg::replay::ReplayPresentationStopReason::UserSkipped,
             "user skip should expose a clear abort reason");
  failures += expect(session.begin(demo, 0U, &error),
                     "presentation should restart after a skip");
  session.abort(lg::replay::ReplayPresentationStopReason::TransferFailed);
  failures +=
      expect(session.state().stopReason ==
                 lg::replay::ReplayPresentationStopReason::TransferFailed,
             "failed killcam transfer should not fall back to live state");

  lg::replay::ReplayDemo invalid = demo;
  invalid.ticks[3].tick = invalid.ticks[2].tick;
  failures +=
      expect(!session.begin(invalid, 0U, &error) &&
                 session.state().stopReason ==
                     lg::replay::ReplayPresentationStopReason::InvalidDemo,
             "non-contiguous input should fail before presentation begins");
  return failures == 0 ? 0 : 1;
}
